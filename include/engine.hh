#ifndef ENGINE_HH
#define ENGINE_HH

#include<iostream>
#include<cstring>

#include<sys/socket.h>
#include<netinet/in.h>
#include<netinet/if_ether.h>
#include<linux/if_packet.h>
#include<arpa/inet.h>

#include <signal.h>
#include <fcntl.h>

#include "buffer.hh"


class Engine {

public:
    Engine();

    ~Engine();

    int send(Buffer * buf, const sockaddr *sadr_ll);

    // *************TODO**************
    // Ainda está errado os parâmetros
    // A NIC tem 2 receive e 2 send, qual deles é da engine?
    // Porque o receive da engine teria parametros?
    // Será que sempre vai ser na mesma interface de rede?
    // Mensagens transferidas em broadcast são recebidas pela mesma engine que as enviou, como lidar?
    // *******************************
    int receive(Buffer * buf, sockaddr saddr);

    void setupSignalHandler(void (*function)(int));

    // Socket é um inteiro pois seu valor representa um file descriptor
    int socket_raw;
private:

    void confSignalReception();
};

#endif