#ifndef NIC_HH
#define NIC_HH

#include <string>
#include <mutex>
#include <atomic> // Para estatísticas, embora mutex seja mais simples aqui
#include <iostream> // Para debug output (opcional)

#include "ethernet.hh"
#include "buffer.hh"
#include "observed.hh"
#include "observer.hh"
#include "engine.hh"



// A classe NIC (Network Interface Controller).
// Ela age como a interface de rede, usando a Engine fornecida para E/S,
// e notifica observadores (Protocolos) sobre frames recebidos.
//
// D (Observed_Data): Buffer<Ethernet::Frame>* - Notifica com ponteiros para buffers recebidos.
// C (Observing_Condition): Ethernet::Protocol - Filtra observadores pelo EtherType.
class NIC : public Ethernet,
            // ******************************************************************************
            // TODO: O OBSERVADOR AQUI ESTÁ ERRADO, DEVERIA SER O CONDITIONAL. Porém ainda não
            // está implementado, concertar depois no resto da NIC.
            public Concurrent_Observed<Buffer<Ethernet::Frame>, Ethernet::Protocol>,
            // ******************************************************************************
            private Engine
{
public:
    typedef Ethernet::Protocol Protocol_Number;
    typedef Concurrent_Observer<Buffer<Ethernet::Frame>, Protocol_Number> Observer;
    typedef Concurrent_Observed<Buffer<Ethernet::Frame>, Protocol_Number> Observed;
    typedef Buffer<Ethernet::Frame> Buffer;

    // Construtor: Recebe um ponteiro para a Engine (cuja vida útil é gerenciada externamente)
    // e o nome da interface de rede.
    // Args:
    //   engine: Ponteiro para a instância da Engine a ser usada.
    //   interface_name: Nome da interface de rede (ex: "eth0", "lo").
    NIC(const std::string& interface_name);

    // Destrutor virtual (boa prática se houver herança, embora não seja estritamente necessário aqui).
    virtual ~NIC();

    // Proibe cópia e atribuição para evitar problemas com ponteiros e estado.
    NIC(const NIC&) = delete;
    NIC& operator=(const NIC&) = delete;

    // --- Funções da API Principal (adaptadas do PDF para o código existente) ---

    // Envia um frame Ethernet contido em um buffer JÁ ALOCADO E PREENCHIDO pelo chamador. 
    // O chamador é responsável pela alocação e desalocação deste buffer.
    // A NIC apenas preenche o MAC de origem e chama a Engine.send.
    // Args:
    //   buf: Ponteiro para o buffer contendo o frame a ser enviado. O chamador mantém a posse.
    // Returns:
    //   Número de bytes enviados pela Engine ou -1 em caso de erro.
    int send(Buffer* buf); // Recebe const pointer, não toma posse
    // R: Acho que o buffer não deveria ser const, conforme o pdf

    // Método de envio de alto nível (conforme PDF, adaptado).
    // Este método aloca temporariamente um buffer, preenche-o e o envia.
    // Args:
    //   dst: Endereço MAC de destino (NOTA: será IGNORADO, sempre envia broadcast).
    //   prot: O número do protocolo (EtherType) a ser colocado no cabeçalho (em ordem de host).
    //   data: Ponteiro para os dados a serem enviados no payload.
    //   size: Tamanho dos dados em bytes.
    // Returns:
    //   Número de bytes enviados no total ou -1 em caso de erro.
    int send(Address dst, Protocol_Number prot, void* data, unsigned int size);

    // --- Métodos de Gerenciamento e Informação ---

    // Retorna o endereço MAC desta NIC.
    const Address& address() const;

    // Retorna as estatísticas de rede acumuladas.
    const Statistics& statistics() const;

private:

    // Método chamado internamente pelo signal handler estático quando dados chegam (SIGIO).
    void handle_signal(int signum);

    // Obtém informações da interface (MAC, índice) usando ioctl.
    bool get_interface_info(const std::string& interface_name);

    // --- Membros ---
    Engine* _engine;           // Ponteiro para a Engine (vida útil gerenciada externamente)
    Address _address;          // Endereço MAC desta NIC (obtido via ioctl)
    int _interface_index;      // Índice da interface de rede (obtido via ioctl)
    std::mutex _stats_mutex;   // Mutex para proteger o acesso às estatísticas
    std::string _interface_name; // Guarda o nome da interface

    Statistics _statistics;    // Estatísticas de rede (requer proteção se acessada de múltiplas threads)
};

#endif // NIC_HH