#include "nic.hh"

#include <sys/ioctl.h> // Para ioctl, SIOCGIFHWADDR, SIOCGIFINDEX
#include <net/if.h>    // Para struct ifreq
#include <unistd.h>    // Para close (não usado diretamente aqui, mas bom ter)
#include <cstring>     // Para std::memcpy, std::memset, strncpy
#include <stdexcept>   // Para std::runtime_error (poderia ser removido se não usar exceções)
#include <arpa/inet.h> // Para htons, ntohs

#include <functional>

// Construtor
NIC::NIC(const std::string& interface_name) :
    _interface_index(-1),
    _interface_name(interface_name) // Acho que a Engine cuida disso e não precisamos lidar com isso na NIC
{
    // Tenta obter as informações da interface (MAC, índice)
    if (!get_interface_info(_interface_name)) {
        // Tratamento de erro mais robusto pode ser necessário
        throw std::runtime_error("NIC Error: Failed to get interface info for " + _interface_name);
    }

    std::function<void(int)> callback = std::bind(&NIC::handle_signal, this, std::placeholders::_1);

    setupSignalHandler(callback);

    std::cout << "NIC initialized for interface " << _interface_name
              << " with MAC ";
    // Imprime o MAC Address (formatação manual)
    for(int i=0; i<6; ++i) std::cout << std::hex << (int)_address.mac[i] << (i<5 ? ":" : "");
    std::cout << std::dec << " and index " << _interface_index << std::endl;
}

// Destrutor
NIC::~NIC() {
    std::cout << "NIC for interface " << _interface_name << " destroyed." << std::endl;
}

#include "utils.cc"

// Método membro que processa o sinal (chamado pelo handler estático)
//  R: Acho que deveriamos testar se essa função funciona sem o While,
//     acho que a Engine da uma interrupção por pacote novo
void NIC::handle_signal(int signum) {
    if (signum == SIGIO) {
        // TODO Print temporário:
        std::cout << "New packet received" << std::endl;
        // Loop para tentar ler múltiplos pacotes que podem ter chegado
        while (true) {
            // 1. Alocar um buffer para recepção.
            //    De acordo com o código original (main.cc, engine signal handler example),
            //    parece que a alocação é feita com 'new'.
            //    O tamanho deve ser suficiente para o maior frame Ethernet.
            Buffer* buf = nullptr;
            try {
                // Usa a capacidade máxima do frame Ethernet
                buf = new Buffer(Ethernet::MAX_FRAME_SIZE_NO_FCS);
            } catch (const std::bad_alloc& e) {
                std::cerr << "NIC::handle_signal: Failed to allocate buffer for reception - " << e.what() << std::endl;
                break; // Sai do loop se não conseguir alocar
            }

            // 2. Tentar receber o pacote usando a Engine.
            //    A Engine::receive fornecida espera (Buffer<Data> * buf, sockaddr saddr)
            //    e retorna int. Precisamos de uma sockaddr para receber o remetente.
            
            struct sockaddr_ll sender_addr; // A engine original usa sockaddr genérico
            socklen_t sender_addr_len;
            int bytes_received = receive(buf, sender_addr, sender_addr_len); // Chamada à API original
            if (bytes_received >= 0) {
                printEth(buf);
            }


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
                //  R: Talvez tenhamos que modificar isso quando a utilizemos a NIC para comunicar entre
                //     Threads e não apenas processos.
                 if (buf->data()->src != _address)
                 {
                    // ************************************************************
                    // TODO Foi comentado pois os observadores ainda não funcionam
                    // Notifica os observadores registrados para este protocolo.
                    // Passa o ponteiro do buffer alocado.
                    // bool notified = notify(proto_net_order, buf);
                    // ************************************************************
                    bool notified = false;
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
int NIC::send(Buffer* buf) {
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
    // std::memset(&sadr_ll, 0, sizeof(sadr_ll));
    // sadr_ll.sll_family = AF_PACKET;
    sadr_ll.sll_ifindex = _interface_index;
    sadr_ll.sll_halen = ETH_ALEN;
    // Copia o MAC de destino DO BUFFER para a estrutura sockaddr_ll
    std::memcpy(sadr_ll.sll_addr, buf->data()->dst.mac, ETH_ALEN);

    // Define o MAC de origem na CÓPIA
    buf->data()->src = _address;

    // Chama o send da Engine com a cópia
    int bytes_sent = Engine::send(buf, (sockaddr*)&sadr_ll); // Ensure the correct namespace or function is used

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

    return bytes_sent;
}

// Envio de alto nível: Aloca, preenche, envia, desaloca.
int NIC::send(Address dst, Protocol_Number prot, void* data, unsigned int size) {
    // 1. Alocar um buffer temporário com 'new'
    Buffer* buf = nullptr;
    try {
        buf = new Buffer(Ethernet::MAX_FRAME_SIZE_NO_FCS);
    } catch (const std::bad_alloc& e) {
        std::cerr << "NIC::send: Failed to allocate buffer - " << e.what() << std::endl;
        return -1;
    }

    // 2. Preencher o buffer
    // Define o endereço de destino como broadcast (ignora o parâmetro dst)
    buf->data()->dst = dst;
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
    //    Este método send(const Buffer*) fará a cópia e chamará a engine.
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
    return _statistics; // Retorna referência direta (cuidado com concorrência)
}

// Função auxiliar privada para obter MAC e índice da interface
bool NIC::get_interface_info(const std::string& interface_name) {
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0'; // Garante terminação nula


    // Obter o índice da interface
    if (ioctl(getSocketFd(), SIOCGIFINDEX, &ifr) == -1) {
        perror(("NIC Error: ioctl SIOCGIFINDEX failed for " + interface_name).c_str());
        return false;
    }
    _interface_index = ifr.ifr_ifindex;

    // Obter o endereço MAC (Hardware Address)
    if (ioctl(getSocketFd(), SIOCGIFHWADDR, &ifr) == -1) {
        perror(("NIC Error: ioctl SIOCGIFHWADDR failed for " + interface_name).c_str());
        return false;
    }

    // Copia o endereço MAC da estrutura ifreq para o membro _address
    // Usa o construtor de Address que recebe unsigned char[6]
    _address = Address(reinterpret_cast<const unsigned char*>(ifr.ifr_hwaddr.sa_data));

    // Caso a interface utilizada seja loopback, geralmente o endereço dela é 00:00:00:00:00:00
    // Verifica se o MAC obtido não é zero
    if (interface_name != "lo" && !_address) { // Usa o operator bool() da Address
         std::cerr << "NIC Error: Obtained MAC address is zero for " << interface_name << std::endl;
         return false;
    }

    return true;
}