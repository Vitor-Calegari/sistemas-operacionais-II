#include "engine.hh"

Engine* Engine::_self = nullptr;

Engine::Engine() {
    _self = this;

    // AF_PACKET para receber pacotes incluindo cabeçalhos da camada de enlace
    // SOCK_RAW para criar um raw socket
    // ETH_P_802_EX1 protocolo experimental
    socket_raw = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_802_EX1));
    if (socket_raw == -1)
        perror("socket creation");

    int broadcastEnable = 1;
    if (setsockopt(socket_raw, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("setsockopt (SO_BROADCAST)");
    }

    confSignalReception();
}

Engine::~Engine()
{
    close(socket_raw);
}

void Engine::setupSignalHandler(std::function<void(int)> func) {\
    // Armazena a função de callback
    signalHandlerFunction = func;
    struct sigaction sigAction;
    sigAction.sa_handler = Engine::signalHandler;
    sigAction.sa_flags = 0;

    // Limpa possiveis sinais existentes antes da configuracao
    sigemptyset(&sigAction.sa_mask);

    // Condigura sigaction de forma que, quando SIGIO for recebido,
    // ´function´ sera chamada
    // nullptr indica que nao queremos salvar a sigaction anterior
    if (sigaction(SIGIO, &sigAction, nullptr) < 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

void Engine::confSignalReception() {
    // Configura processo como ´dono´ do socket para poder receber
    // sinais, como o SIGIO
    if (fcntl(socket_raw, F_SETOWN, getpid()) < 0) {
        perror("fcntl F_SETOWN");
        exit(EXIT_FAILURE);
    }

    // Obtem flags do socket para não sobrescreve-las posteriormente
    int flags = fcntl(socket_raw, F_GETFL);
    if (flags < 0) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }

    // O_ASYNC faz com que o socket levante o sinal SIGIO quando operacoes de I/O acontecerem
    // O_NONBLOCK faz com que operações normalmente bloqueantes não bloqueiem
    if (fcntl(socket_raw, F_SETFL, flags | O_ASYNC | O_NONBLOCK) < 0) {
        perror("fcntl F_SETFL");
        exit(EXIT_FAILURE);
    }
}

// Função estática para envelopar a função que tratará a interrupção
void Engine::signalHandler(int signum) {
    if (_self && _self->signalHandlerFunction) {
        _self->signalHandlerFunction(signum);
    } else {
        perror("Signal handler failed");
        exit(EXIT_FAILURE);
    }
}
