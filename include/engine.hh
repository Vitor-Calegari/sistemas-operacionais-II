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
#include <functional>

#include "buffer.hh"


class Engine {

public:

    Engine();

    ~Engine();

    template<typename Data>
    int send(Buffer<Data> * buf, const sockaddr *sadr_ll) {
        int send_len = sendto(_self->socket_raw, buf->data(), buf->size(), 0, sadr_ll, sizeof(struct sockaddr_ll));
        if (send_len < 0) {
            printf("error in sending....sendlen=%d....errno=%d\n", send_len, errno);
            return -1;
        }
        return send_len;
    }
    
    // *************TODO**************
    // Ainda está errado os parâmetros
    // A NIC tem 2 receive e 2 send, qual deles é da engine?
    // Porque o receive da engine teria parametros?
    // Será que sempre vai ser na mesma interface de rede?
    // Mensagens transferidas em broadcast são recebidas pela mesma engine que as enviou, como lidar?
    // *******************************
    template<typename Data>
    int receive(Buffer<Data> * buf, sockaddr saddr) {
        int saddr_len = sizeof(saddr);
        int buflen = recvfrom(_self->socket_raw, buf->data(), buf->size(), 0, &saddr, (socklen_t *)&saddr_len);

        if (buflen < 0) {
            printf("error in reading recvfrom function\n");
            return -1;
        }
        return buflen;
    }

    void setupSignalHandler(std::function<void(int)> func);

    // protected:
    // Socket é um inteiro pois seu valor representa um file descriptor
    int socket_raw;
    private:
    static void signalHandler(int signum);
    void confSignalReception();
    std::function<void(int)> signalHandlerFunction;
    static Engine* _self;
};

#endif