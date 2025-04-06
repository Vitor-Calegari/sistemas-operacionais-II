#include "engine.hh"
#include "ethernet.hh" // Precisa incluir para usar Ethernet::Frame

#include <cstdlib> // Para exit, EXIT_FAILURE
#include <cstdio>  // Para perror
#include <iostream> // Para std::cerr
#include <new> // Para std::bad_alloc

// Inicializa ponteiro estático (permite apenas uma instância de Engine)
Engine* Engine::_self = nullptr;

Engine::Engine(const char *interface_name) :
    _socket_raw(-1), // Inicializa fd
    _interface_index(-1),
    _interface_name(interface_name) // Armazena o nome da interface
{
    // Validação do ponteiro estático _self
    if (_self != nullptr && _self != this) {
         // Tentativa de criar segunda instância - pode ser um erro ou design desejado
         std::cerr << "Warning: Creating a second Engine instance. The signal handler might behave unexpectedly." << std::endl;
         // Dependendo do design, pode-se lançar exceção ou permitir, mas o handler estático limitará.
    }
     _self = this; // Define a instância atual como a "ativa" para o handler


    // *** IMPORTANTE: MANTER ETH_P_802_EX1 como no original, MAS LEMBRAR QUE O CORRETO PARA NIC É ETH_P_ALL ***
    _socket_raw = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_802_EX1));
    if (_socket_raw == -1) { // Usa getSocketFd() só depois de criado
        perror("Engine: socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Habilita broadcast
    int broadcastEnable = 1;
    if (setsockopt(_socket_raw, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("Engine: setsockopt (SO_BROADCAST) failed");
        close(_socket_raw); // Limpa antes de sair
        exit(EXIT_FAILURE);
    }

    // Obtém informações da interface (MAC, índice)
    if (!get_interface_info()) {
        // get_interface_info imprime o erro específico
        close(_socket_raw);
        exit(EXIT_FAILURE);
    }

    // Configura recepção de sinais
    confSignalReception();

    // Debug print (mantido)
    #ifdef DEBUG
    std::cout << "Engine initialized for interface " << _interface_name
              << " with MAC ";
    for (int i = 0; i < 6; ++i)
      std::cout << std::hex << (int)_address.mac[i] << (i < 5 ? ":" : "");
    std::cout << std::dec << " and index " << _interface_index << std::endl;
    #endif
}

Engine::~Engine() {
    if (_socket_raw != -1) {
        close(_socket_raw);
    }
     // Se esta era a instância ativa, limpa o ponteiro estático
     if (_self == this) {
         _self = nullptr;
     }
    #ifdef DEBUG
    std::cout << "Engine for interface " << _interface_name << " destroyed." << std::endl;
    #endif
}

// --- Implementação dos Métodos de Alocação/Liberação ---

// Aloca memória para um frame. Placeholder com 'new'.
Ethernet::Frame* Engine::allocate_frame_memory() {
    try {
        // Aloca e retorna ponteiro para um Ethernet::Frame.
        // Poderia fazer alinhamento específico ou usar alocadores customizados aqui.
        Ethernet::Frame* frame_ptr = new Ethernet::Frame();
        // Opcional: Limpar a memória alocada
        // std::memset(frame_ptr, 0, sizeof(Ethernet::Frame));
        return frame_ptr;
    } catch (const std::bad_alloc& e) {
         std::cerr << "Engine Error: Failed to allocate frame memory - " << e.what() << std::endl;
         // Lança novamente ou retorna nullptr, dependendo da política de erro.
         // Lançar é geralmente melhor para indicar falha de alocação.
         throw; // Re-lança a exceção std::bad_alloc
    }
}

// Libera memória do frame. Placeholder com 'delete'.
void Engine::free_frame_memory(Ethernet::Frame* frame_ptr) {
    // Libera a memória apontada por frame_ptr.
    // É crucial que frame_ptr tenha sido alocado por allocate_frame_memory.
    delete frame_ptr;
}

// --- Implementação dos Métodos Privados ---

bool Engine::get_interface_info() {
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    // Copia nome da interface, garantindo terminação nula
    strncpy(ifr.ifr_name, _interface_name, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    // Obter MAC Address
    if (ioctl(_socket_raw, SIOCGIFHWADDR, &ifr) == -1) {
      perror("Engine Error: ioctl SIOCGIFHWADDR failed");
      return false;
    }
    _address = Ethernet::Address(reinterpret_cast<const unsigned char *>(ifr.ifr_hwaddr.sa_data));

    // Valida MAC (exceto para 'lo')
    if (strcmp(_interface_name, "lo") != 0 && !_address) {
      std::cerr << "Engine Error: Obtained MAC address is zero for "
                << _interface_name << std::endl;
      return false;
    }

    // Obter Índice da Interface (após obter MAC) - Necessário para sendto
     std::memset(&ifr, 0, sizeof(ifr)); // Re-zera ifr para SIOCGIFINDEX
     strncpy(ifr.ifr_name, _interface_name, IFNAMSIZ - 1);
     ifr.ifr_name[IFNAMSIZ - 1] = '\0';
     if (ioctl(_socket_raw, SIOCGIFINDEX, &ifr) == -1) {
         perror("Engine Error: ioctl SIOCGIFINDEX failed");
         return false;
     }
     _interface_index = ifr.ifr_ifindex;


    return true;
}

void Engine::setupSignalHandler(std::function<void(int)> func) {
    // Armazena a função (std::function) que será chamada pelo handler estático
    signalHandlerFunction = func;

    struct sigaction sigAction;
    sigAction.sa_handler = Engine::signalHandler; // Usa o handler estático C
    sigAction.sa_flags = SA_RESTART; // Reinicia syscalls interrompidas
    sigemptyset(&sigAction.sa_mask); // Não bloqueia outros sinais

    if (sigaction(SIGIO, &sigAction, nullptr) < 0) {
        perror("Engine: sigaction failed");
        exit(EXIT_FAILURE); // Falha crítica
    }
}

void Engine::confSignalReception() {
    // Define processo como dono do socket para receber SIGIO
    if (fcntl(_socket_raw, F_SETOWN, getpid()) < 0) {
        perror("Engine: fcntl F_SETOWN failed");
        exit(EXIT_FAILURE);
    }

    // Obtém flags atuais
    int flags = fcntl(_socket_raw, F_GETFL);
    if (flags < 0) {
        perror("Engine: fcntl F_GETFL failed");
        exit(EXIT_FAILURE);
    }

    // Adiciona O_ASYNC (gera SIGIO) e O_NONBLOCK (recv não bloqueia)
    if (fcntl(_socket_raw, F_SETFL, flags | O_ASYNC | O_NONBLOCK) < 0) {
        perror("Engine: fcntl F_SETFL failed");
        exit(EXIT_FAILURE);
    }
}

// Handler estático C que chama a std::function armazenada na instância _self
void Engine::signalHandler(int signum) {
    // Verifica se a instância _self e a função são válidas
    if (_self && _self->signalHandlerFunction) {
        // Chama a função (ex: std::bind para NIC::handle_signal)
        _self->signalHandlerFunction(signum);
    } else {
        // Situação de erro - handler chamado sem instância ou função configurada
        write(STDERR_FILENO, "Error: Engine::signalHandler called without valid instance/function\n", 66);
        // Evitar perror ou exit dentro de um signal handler real se possível
        // raise(SIGABRT); // Ou outra forma de sinalizar erro fatal
    }
}