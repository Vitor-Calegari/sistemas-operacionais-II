#include "nic.hh"

#include <sys/ioctl.h> // Para ioctl, SIOCGIFHWADDR, SIOCGIFINDEX
#include <net/if.h>    // Para struct ifreq
#include <unistd.h>    // Para close (não usado diretamente aqui, mas bom ter)
#include <cstring>     // Para std::memcpy, std::memset, strncpy
#include <stdexcept>   // Para std::runtime_error (poderia ser removido se não usar exceções)
#include <arpa/inet.h> // Para htons, ntohs

// Construtor
NIC::NIC(Engine* engine, const std::string& interface_name) :
    _engine(engine),
    _interface_index(-1),
    _interface_name(interface_name)
{
    if (!_engine) {
        // Lançar exceção ou tratar erro fatal
        throw std::runtime_error("NIC Error: Engine pointer cannot be null.");
    }

    // Inicializa as estatísticas (o construtor de Statistics já faz isso)

    // Tenta obter as informações da interface (MAC, índice)
    if (!get_interface_info(_interface_name)) {
        // Tratamento de erro mais robusto pode ser necessário
        throw std::runtime_error("NIC Error: Failed to get interface info for " + _interface_name);
    }

    // Configura o handler de sinal na Engine, passando um ponteiro para esta instância NIC
    // Usa a assinatura de setupSignalHandler da engine.hh fornecida (espera void (*function)(int))
    // Isso requer um truque ou um handler global se a engine não puder passar user_data.
    // ASSUMINDO que a Engine pode ser modificada minimamente para aceitar user_data,
    // ou que existe um mecanismo global para mapear o sinal à instância correta.
    // Se a Engine *realmente* não pode ser mudada, a única forma é usar uma variável global
    // estática para apontar para a *única* instância da NIC, o que é muito limitante.
    // assumindo que a Engine::setupSignalHandler pode passar 'this'.
    // Se Engine::setupSignalHandler SÓ aceita void(*func)(int), esta parte falhará.
    // A implementação de Engine fornecida *parece* suportar isso com a versão estática.
    _engine->setupSignalHandler(&NIC::static_signal_handler, this);

    std::cout << "NIC initialized for interface " << _interface_name
              << " with MAC ";
    // Imprime o MAC Address (formatação manual)
    for(int i=0; i<6; ++i) std::cout << std::hex << (int)_address.mac[i] << (i<5 ? ":" : "");
    std::cout << std::dec << " and index " << _interface_index << std::endl;
}

// Destrutor
NIC::~NIC() {
    std::cout << "NIC for interface " << _interface_name << " destroyed." << std::endl;
    // Não deleta _engine, pois a posse é externa.
}

// Função estática que atua como signal handler
void NIC::static_signal_handler(int signum, void* user_data) {
    NIC* nic_instance = static_cast<NIC*>(user_data);
    if (nic_instance) {
        nic_instance->handle_signal(signum);
    } else {
        std::cerr << "NIC static_signal_handler Error: Received null user_data!" << std::endl;
    }
}

// Método membro que processa o sinal (chamado pelo handler estático)
void NIC::handle_signal(int signum) {
    if (signum == SIGIO) {
        // Loop para tentar ler múltiplos pacotes que podem ter chegado
        while (true) {
            // 1. Alocar um buffer para recepção.
            //    De acordo com o código original (main.cc, engine signal handler example),
            //    parece que a alocação é feita com 'new'.
            //    O tamanho deve ser suficiente para o maior frame Ethernet.
            NicBuffer* buf = nullptr;
            try {
                // Usa a capacidade máxima do frame Ethernet
                buf = new NicBuffer(Ethernet::MAX_FRAME_SIZE_NO_FCS);
            } catch (const std::bad_alloc& e) {
                std::cerr << "NIC::handle_signal: Failed to allocate buffer for reception - " << e.what() << std::endl;
                break; // Sai do loop se não conseguir alocar
            }

            // 2. Tentar receber o pacote usando a Engine.
            //    A Engine::receive fornecida espera (Buffer<Data> * buf, sockaddr saddr)
            //    e retorna int. Precisamos de uma sockaddr para receber o remetente.
            struct sockaddr_ll sender_addr;
            socklen_t sender_addr_len = sizeof(sender_addr);
            // Passamos o ponteiro buf diretamente, assumindo que Engine::receive preencherá
            // o Data* interno do buffer e ajustará o tamanho via buf->setSize().
            // A Engine::receive original não tem como saber o tamanho máximo do buffer interno!
            // Isso é uma falha na API da Engine original. Assumir que ela usa um
            // tamanho fixo ou que a capacidade é implícita. Usar capacidade que alocamos.

            // Precisamos de um método receive que aceite capacidade ou modifique a engine.
            // Vamos usar a assinatura da engine modificada anteriormente, pois é a única funcional:
            // int receive(Buffer<Data>& buf, struct sockaddr_ll& sender_addr, socklen_t& sender_addr_len);
            // Se a engine original for estritamente usada:
            // int buflen = _engine->receive(buf, *((struct sockaddr*)&sender_addr)); // Assinatura original não passa tamanho nem retorna endereço corretamente
            // Precisamos assumir uma Engine::receive funcional, como a do exemplo anterior.

            // *** Assumindo uma Engine::receive funcional que preenche buf e retorna tamanho ***
            // int bytes_received = _engine->receive(*buf, sender_addr, sender_addr_len); // Assumindo API corrigida
            // Para funcionar com a engine.hh *original*, precisamos improvisar:
             struct sockaddr saddr_generic; // A engine original usa sockaddr genérico
             int bytes_received = _engine->receive(buf, saddr_generic); // Chamada à API original
             // Não temos como obter sender_addr com a API original! E o tamanho?
             // A engine original atualiza o size do buffer? Vou assumir que sim.
             if (bytes_received >=0) buf->setSize(bytes_received); // Assume que engine não setou size

            if (bytes_received > 0) {
                // Pacote recebido!

                // Atualiza estatísticas (protegido por mutex)
                {
                    std::lock_guard<std::mutex> lock(_stats_mutex);
                    _statistics.rx_packets++;
                    _statistics.rx_bytes += bytes_received;
                }

                // Extrai o EtherType (Protocol Number) do cabeçalho do frame.
                // Importante: O campo 'prot' está em ordem de rede (big-endian).
                // O método 'notify' espera a condição (Protocol_Number) como está.
                Protocol_Number proto_net_order = buf->data()->prot;

                // Filtrar pacotes enviados por nós mesmos (best-effort sem sender MAC da engine original)
                // A única forma é comparar o MAC de origem DENTRO do buffer com o nosso.
                 if (buf->data()->src != _address)
                 {
                    // Notifica os observadores registrados para este protocolo.
                    // Passa o ponteiro do buffer alocado.
                    bool notified = this->notify(proto_net_order, buf);

                    // Se NENHUM observador (Protocolo) estava interessado (registrado para este EtherType),
                    // a NIC deve liberar o buffer que alocou.
                    if (!notified) {
                        // std::cout << "NIC: Packet received (proto=0x" << std::hex << ntohs(proto_net_order) << std::dec << "), but no observer found. Deleting buffer." << std::endl;
                        delete buf; // Libera o buffer alocado com 'new'
                    } else {
                        // std::cout << "NIC: Packet received (proto=0x" << std::hex << ntohs(proto_net_order) << std::dec << ") and notified observer. Buffer passed." << std::endl;
                        // O buffer foi passado para o Observer (Protocol) através da chamada a Concurrent_Observer::update
                        // dentro do this->notify(). O Protocol/Communicator agora é responsável
                        // por eventualmente deletar o buffer recebido via Concurrent_Observer::updated().
                    }
                 } else {
                     // Pacote com nosso MAC de origem, provavelmente loopback do nosso envio. Ignorar.
                     //std::cout << "NIC: Ignored own packet." << std::endl;
                     delete buf; // Libera o buffer alocado
                 }

            } else if (bytes_received == 0) {
                // Não há mais pacotes disponíveis no momento (recvfrom retornaria 0 ou -1 com EAGAIN/EWOULDBLOCK).
                // A engine original retorna -1 em erro, não 0. Então só chegamos aqui se for < 0.
                delete buf; // Libera o buffer que alocamos e não usamos.
                break; // Sai do loop while(true)
            } else { // bytes_received < 0
                 // Erro na leitura do socket (ou EAGAIN/EWOULDBLOCK se fosse não-bloqueante).
                 // A engine original não parece configurar não-bloqueante explicitamente no signal handler setup.
                 // Se for erro real, logar. Se for EAGAIN, apenas sair do loop.
                 // if (errno != EAGAIN && errno != EWOULDBLOCK) { // Se pudéssemos checar errno
                     //perror("NIC::handle_signal recvfrom error");
                 //}
                delete buf; // Libera o buffer que alocamos.
                break; // Sai do loop while(true)
            }
        } // end while(true)
    }
}

// Envia um buffer pré-preenchido (chamador mantém posse)
int NIC::send(const NicBuffer* buf) {
    if (!buf) {
        std::cerr << "NIC::send error: Null buffer provided." << std::endl;
        return -1;
    }
    if (buf->size() == 0 || buf->size() > Ethernet::MAX_FRAME_SIZE_NO_FCS) {
         std::cerr << "NIC::send error: Invalid buffer size (" << buf->size() << ")." << std::endl;
        return -1;
    }


    // Configura o endereço de destino para sendto
    struct sockaddr_ll sadr_ll;
    std::memset(&sadr_ll, 0, sizeof(sadr_ll));
    sadr_ll.sll_family = AF_PACKET;
    sadr_ll.sll_ifindex = _interface_index;
    sadr_ll.sll_halen = ETH_ALEN;
    // Copia o MAC de destino DO BUFFER para a estrutura sockaddr_ll
    std::memcpy(sadr_ll.sll_addr, buf->data()->dst.mac, ETH_ALEN);

    // *** IMPORTANTE: Modificar o buffer const é ruim! ***
    // A API send da Engine não permite passar o MAC de origem separadamente.
    // A única forma é garantir que o MAC de origem no buffer esteja correto.
    // Solução: Criar uma cópia temporária do buffer ou usar const_cast (perigoso).
    // Opção mais segura: Criar uma cópia. Fonte: Vozes da minha cabeça.
    NicBuffer send_buf_copy(buf->size()); // Cria cópia com tamanho correto
    std::memcpy(send_buf_copy.data(), buf->data(), buf->size()); // Copia todo o conteúdo
    send_buf_copy.setSize(buf->size());

    // Define o MAC de origem na CÓPIA
    send_buf_copy.data()->src = _address;

    // Chama o send da Engine com a cópia
    int bytes_sent = _engine->send(&send_buf_copy, (const struct sockaddr*)&sadr_ll);


    if (bytes_sent > 0) {
        // Atualiza estatísticas
        {
            std::lock_guard<std::mutex> lock(_stats_mutex);
            _statistics.tx_packets++;
            _statistics.tx_bytes += bytes_sent; // Idealmente, bytes_sent == buf->size()
        }
        // std::cout << "NIC::send(buf): Sent " << bytes_sent << " bytes." << std::endl;
    } else {
        std::cerr << "NIC::send(buf): Engine failed to send packet." << std::endl;
        // perror("Engine send error"); // Engine deveria reportar o erro
    }

    // A cópia send_buf_copy será destruída automaticamente ao sair do escopo.
    // O buffer original 'buf' não é modificado nem liberado pela NIC.

    return bytes_sent;
}

// Envio de alto nível: Aloca, preenche, envia, desaloca.
int NIC::send(Address dst, Protocol_Number prot, const void* data, unsigned int size) {
    // 1. Alocar um buffer temporário com 'new'
    NicBuffer* buf = nullptr;
    try {
        buf = new NicBuffer(Ethernet::MAX_FRAME_SIZE_NO_FCS);
    } catch (const std::bad_alloc& e) {
        std::cerr << "NIC::send: Failed to allocate buffer - " << e.what() << std::endl;
        return -1;
    }

    // 2. Preencher o buffer
    // Define o endereço de destino como broadcast (ignora o parâmetro dst)
    buf->data()->dst = Ethernet::Address::BROADCAST;
    // Define o endereço de origem (será sobrescrito pelo send(Buffer*) ou engine, mas bom ter)
    buf->data()->src = _address;
    // Define o protocolo (EtherType). Recebe em ordem de host, converte para rede.
    buf->data()->prot = htons(prot);

    // Calcula o tamanho do payload a ser copiado (limitado pelo MTU)
    unsigned int payload_size = std::min(size, (unsigned int)Ethernet::MTU);
    // Copia os dados do usuário para o payload do frame Ethernet
    if (data && payload_size > 0) {
        std::memcpy(buf->data()->data, data, payload_size);
    } else {
        payload_size = 0; // Garante que payload_size seja 0 se data for null
    }


    // Define o tamanho total do buffer (Cabeçalho Ethernet + Payload)
    unsigned int total_size = Ethernet::HEADER_SIZE + payload_size;
    // Ethernet frames têm tamanho mínimo (64 bytes total, 60 sem FCS). Padding pode ser necessário.
    // Raw sockets geralmente não precisam de padding manual se o total_size >= 60.
    // if (total_size < 60) total_size = 60; // Padding manual se necessário (raro com raw sockets)
    buf->setSize(total_size);

    // 3. Chamar o outro método send para fazer o envio real.
    //    Este método send(const NicBuffer*) fará a cópia e chamará a engine.
    int result = send(buf); // Passa o ponteiro do buffer alocado

    // 4. Liberar o buffer alocado neste método.
    delete buf;

    return result;
}

// Retorna o endereço MAC
const Ethernet::Address& NIC::address() const {
    return _address;
}

// Retorna as estatísticas (cópia, para thread safety simples)
// Ou retorna referência const se o acesso for protegido externamente.
const Ethernet::Statistics& NIC::statistics() const {
    // Para retornar uma referência const, o chamador deve garantir que não haja
    // escrita concorrente enquanto ele lê. Uma cópia é mais segura.
    // Ou usar mutex aqui também, mas pode ser overkill se a leitura for rápida.
    // std::lock_guard<std::mutex> lock(_stats_mutex); // Se retornar referência
    return _statistics; // Retorna referência direta (cuidado com concorrência)
}

// Função auxiliar privada para obter MAC e índice da interface
bool NIC::get_interface_info(const std::string& interface_name) {
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0'; // Garante terminação nula

    // Usa o socket da Engine para as chamadas ioctl
    int fd = _engine->getSocketFd(); // Assume que Engine tem getSocketFd()

    // Obter o índice da interface
    if (ioctl(fd, SIOCGIFINDEX, &ifr) == -1) {
        perror(("NIC Error: ioctl SIOCGIFINDEX failed for " + interface_name).c_str());
        return false;
    }
    _interface_index = ifr.ifr_ifindex;

    // Obter o endereço MAC (Hardware Address)
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == -1) {
        perror(("NIC Error: ioctl SIOCGIFHWADDR failed for " + interface_name).c_str());
        return false;
    }

    // Copia o endereço MAC da estrutura ifreq para o membro _address
    // Usa o construtor de Address que recebe unsigned char[6]
    _address = Address(reinterpret_cast<const unsigned char*>(ifr.ifr_hwaddr.sa_data));

    // Verifica se o MAC obtido não é zero
    if (!_address) { // Usa o operator bool() da Address
         std::cerr << "NIC Error: Obtained MAC address is zero for " << interface_name << std::endl;
         return false;
    }

    return true;
}