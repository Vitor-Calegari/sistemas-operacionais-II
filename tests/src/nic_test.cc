#include "nic.hh"

int main (int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Uso: send? 1 para sim, 0 para nao";
        return 1;
    }
    const int send = atoi(argv[1]);

    NIC nic = NIC("lo");

    if (send) {
        Ethernet::Address dest = Ethernet::Address(Ethernet::BROADCAST_ADDRESS);
        unsigned char data[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xFF};
        nic.send(dest, ETH_P_802_EX1, data, 5);
    } else {
        while(1) {sleep(10);}
    }
}