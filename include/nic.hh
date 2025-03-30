#ifndef NIC_HH
#define NIC_HH

#include <string>
#include <mutex>
#include <atomic> // Para estatísticas, embora mutex seja mais simples aqui
#include <iostream> // Para debug output (opcional)

#include "ethernet.hh" // Definições de Ethernet (Frame, Address, Protocol, Statistics)
#include "buffer.hh"   // Definição de Buffer COMO ESTÁ NO CÓDIGO FORNECIDO
#include "observed.hh" // Definição de Concurrent_Observed COMO ESTÁ NO CÓDIGO FORNECIDO
#include "observer.hh" // Definição de Concurrent_Observer COMO ESTÁ NO CÓDIGO FORNECIDO
#include "engine.hh"   // Definição de Engine COMO ESTÁ NO CÓDIGO FORNECIDO

// Define o tipo de Buffer específico usado pela NIC, usando o Buffer fornecido
// Nota: O Buffer fornecido parece gerenciar o tamanho, mas não a alocação/capacidade explicitamente.
// Assume-se que o ponteiro Data* interno seja alocado externamente ou no construtor se modificado.
// Para compatibilidade estrita, vamos assumir que a Engine::receive espera um buffer pré-alocado.
typedef Buffer<Ethernet::Frame> NicBuffer;

// A classe NIC (Network Interface Controller) adaptada para o código existente.
// Ela age como a interface de rede, usando a Engine fornecida para E/S raw,
// e notifica observadores (Protocolos) sobre frames recebidos.
//
// Herda de Ethernet para ter acesso fácil aos tipos e constantes.
// Herda de Concurrent_Observed para implementar o padrão Observer.
// D (Observed_Data): NicBuffer* - Notifica com ponteiros para buffers recebidos.
// C (Observing_Condition): Ethernet::Protocol - Filtra observadores pelo EtherType.
class NIC : public Ethernet,
            public Concurrent_Observed<NicBuffer*, Ethernet::Protocol>
{
public:
    // Typedefs públicos conforme o estilo do PDF/código
    // (Ethernet::Address, etc., já estão acessíveis via herança de Ethernet)
    typedef Ethernet::Protocol Protocol_Number;
    // Observer: Tipo do observador que se registrará na NIC (classe Protocol)
    // Deve corresponder ao Concurrent_Observer fornecido.
    typedef ::Concurrent_Observer<NicBuffer*, Protocol_Number> Observer; // Usa o Observer global fornecido
    // Observed: O próprio tipo da NIC (já que ela é Concurrent_Observed)
    typedef ::Concurrent_Observed<NicBuffer*, Protocol_Number> Observed; // Usa o Observed global fornecido

    // Construtor: Recebe um ponteiro para a Engine (cuja vida útil é gerenciada externamente)
    // e o nome da interface de rede.
    // Args:
    //   engine: Ponteiro para a instância da Engine a ser usada.
    //   interface_name: Nome da interface de rede (ex: "eth0", "lo").
    NIC(Engine* engine, const std::string& interface_name);

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
    int send(const NicBuffer* buf); // Recebe const pointer, não toma posse

    // Método de envio de alto nível (conforme PDF, adaptado).
    // Este método aloca temporariamente um buffer, preenche-o e o envia.
    // Args:
    //   dst: Endereço MAC de destino (NOTA: será IGNORADO, sempre envia broadcast).
    //   prot: O número do protocolo (EtherType) a ser colocado no cabeçalho (em ordem de host).
    //   data: Ponteiro para os dados a serem enviados no payload.
    //   size: Tamanho dos dados em bytes.
    // Returns:
    //   Número de bytes enviados no total ou -1 em caso de erro.
    int send(Address dst, Protocol_Number prot, const void* data, unsigned int size);


    // --- Métodos de Gerenciamento e Informação ---

    // Retorna o endereço MAC desta NIC.
    const Address& address() const;

    // Retorna as estatísticas de rede acumuladas.
    const Statistics& statistics() const;

    // --- Métodos do Padrão Observer (herdado de Concurrent_Observed) ---
    // void attach(Observer* obs, Protocol_Number prot); // Herdado
    // void detach(Observer* obs, Protocol_Number prot); // Herdado
    // bool notify(Protocol_Number prot, NicBuffer* buf); // Herdado

private:
    // Método chamado internamente pelo signal handler estático quando dados chegam (SIGIO).
    void handle_signal(int signum);

    // Função estática usada como signal handler pela Engine.
    // Chama o método handle_signal da instância específica da NIC.
    static void static_signal_handler(int signum, void* user_data);

    // Obtém informações da interface (MAC, índice) usando ioctl.
    bool get_interface_info(const std::string& interface_name);

    // --- Membros ---
    Engine* _engine;           // Ponteiro para a Engine (vida útil gerenciada externamente)
    Address _address;          // Endereço MAC desta NIC (obtido via ioctl)
    int _interface_index;      // Índice da interface de rede (obtido via ioctl)
    Statistics _statistics;    // Estatísticas de rede (requer proteção se acessada de múltiplas threads)
    std::mutex _stats_mutex;   // Mutex para proteger o acesso às estatísticas
    std::string _interface_name; // Guarda o nome da interface
};

#endif // NIC_HH