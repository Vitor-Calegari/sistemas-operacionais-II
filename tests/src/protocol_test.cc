#include "protocol.hh"

int main (int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Uso: send? 1 para sim, 0 para nao";
        return 1;
    }
    const int send = atoi(argv[1]);

    NIC<Engine> nic = NIC<Engine>("lo");

    Protocol<NIC<Engine>> * prot = Protocol<NIC<Engine>>::getInstance(&nic);

    if (send) {
        for (int i = 0; i < 10; i++) {
            Protocol<NIC<Engine>>::Address dest = Protocol<NIC<Engine>>::Address(Ethernet::BROADCAST_ADDRESS, 10);
            unsigned char data[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xFF};

            Protocol<NIC<Engine>>::Address from = Protocol<NIC<Engine>>::Address(Ethernet::Address(), 10);
            prot->send(from, dest, data, 5);
        }
    } else {
        while(1) {sleep(10);}
    }
}