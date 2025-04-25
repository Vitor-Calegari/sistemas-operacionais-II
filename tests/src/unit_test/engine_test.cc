#include "engine.hh"

#include "buffer.hh"
#include "ethernet.hh"
#include "utils.hh"
#include <csignal>
#include <iostream>
#include <net/if.h>
#include <sys/ioctl.h>

// Arquivo de testes temporario para testar funcionamento da engine

const auto INTERFACE_NAME = "enxf8e43bf0c430";

Engine<Buffer<Ethernet::Frame>> engine = Engine<Buffer<Ethernet::Frame>>(INTERFACE_NAME);

typedef Buffer<Ethernet::Frame> EthFrame;

class Handler {
public:
  Engine<Buffer<Ethernet::Frame>> * engine;

  void handle_signal() {
    int recv_len = 0;
    std::cout << "SIGIO recebido: dados disponíveis no socket\n";
    do {
      std::cout << "Dados recebidos:\n";
    
      EthFrame *buf = new EthFrame(1522);
    
      recv_len = engine->receive(buf);
      
      if (recv_len > 0) {
        printEth(buf);
        fflush(stdout);
      }
    
      delete buf;  
    } while (recv_len > 0);
  }
};


// Funcões para montar o quadro ethernet --------------------------------------

void get_mac(EthFrame *buf, ifreq ifreq_c, int sock_raw) {
  memset(&ifreq_c, 0, sizeof(ifreq_c));
  strncpy(ifreq_c.ifr_name, INTERFACE_NAME, IFNAMSIZ - 1);
  ifreq_c.ifr_name[IFNAMSIZ - 1] = '\0';
  if ((ioctl(sock_raw, SIOCGIFHWADDR, &ifreq_c)) < 0)
    printf("error in SIOCGIFHWADDR ioctl reading");

  memcpy(buf->data()->src.mac, ifreq_c.ifr_hwaddr.sa_data, ETH_ALEN);
  memset(buf->data()->dst.mac, 0xFF, ETH_ALEN);

  printf("Source mac= %.2X-%.2X-%.2X-%.2X-%.2X-%.2X\n",
         (unsigned char)(buf->data()->src.mac[0]),
         (unsigned char)(buf->data()->src.mac[1]),
         (unsigned char)(buf->data()->src.mac[2]),
         (unsigned char)(buf->data()->src.mac[3]),
         (unsigned char)(buf->data()->src.mac[4]),
         (unsigned char)(buf->data()->src.mac[5]));
  printf("Dest mac= %.2X-%.2X-%.2X-%.2X-%.2X-%.2X\n",
         (unsigned char)(buf->data()->dst.mac[0]),
         (unsigned char)(buf->data()->dst.mac[1]),
         (unsigned char)(buf->data()->dst.mac[2]),
         (unsigned char)(buf->data()->dst.mac[3]),
         (unsigned char)(buf->data()->dst.mac[4]),
         (unsigned char)(buf->data()->dst.mac[5]));

  buf->data()->prot = htons(ETH_P_802_EX1); // 0x88B5

  buf->setSize(buf->size() + 14);
}

void get_data(EthFrame *buf) {
  buf->data()->template data<unsigned char>()[0] = 0xAA;
  buf->data()->template data<unsigned char>()[0 + 1] = 0xBB;
  buf->data()->template data<unsigned char>()[0 + 2] = 0xCC;
  buf->data()->template data<unsigned char>()[0 + 3] = 0xDD;
  buf->data()->template data<unsigned char>()[0 + 4] = 0xEE;
  buf->setSize(buf->size() + 5);
}

void get_eth_index(ifreq &ifreq_i, int sock_raw) {
  memset(&ifreq_i, 0, sizeof(ifreq_i));
  strncpy(ifreq_i.ifr_name, INTERFACE_NAME, IFNAMSIZ - 1);
  ifreq_i.ifr_name[IFNAMSIZ - 1] = '\0';

  if ((ioctl(sock_raw, SIOCGIFINDEX, &ifreq_i)) < 0)
    printf("error in index ioctl reading");

  printf("index=%d\n", ifreq_i.ifr_ifindex);
}

// ----------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Uso: send? 1 para sim, 0 para nao";
    return 1;
  }
  const int send = atoi(argv[1]);
  Handler handler = Handler();
  handler.engine = &engine;
  engine.template bind<Handler, &Handler::handle_signal>(&handler);

  if (send) {
    
    for (int i = 0; i < 3; i++) 
    {
      // Setta interface
      struct ifreq ifreq_c, ifreq_i;
  
      EthFrame *buf = new EthFrame();
      buf->setMaxSize(1514);
      get_eth_index(ifreq_i, engine.getSocketFd()); // interface number
      get_mac(buf, ifreq_c,
              engine.getSocketFd()); // Setta macs do quadro de ethernet
      get_data(buf);                 // Setta dados do quadro
  
      engine.send(buf);
      delete buf;
    }
  } else {
    while (1) {
      sleep(10);
    }
  }
}
