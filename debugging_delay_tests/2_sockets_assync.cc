#include <arpa/inet.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <cstdio>

#include <linux/filter.h>
#include <mutex>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <semaphore.h>
#include <thread>

#include <fcntl.h>

using namespace std::chrono;

constexpr size_t ETH_HDR_LEN = 14;
constexpr size_t PAYLOAD_LEN = 64;
constexpr uint16_t CUSTOM_ETHER_TYPE = 0x88B5;
constexpr int SENDER_COUNT = 15;
constexpr int MESSAGES_PER_SENDER = 100;

class EthernetHeader {
public:
  uint8_t dst_mac[6];
  uint8_t src_mac[6];
  uint16_t ethertype;
} __attribute__((packed));

class Packet : public EthernetHeader {
public:
  uint64_t send_time_us;
  uint32_t sender_id;
  int msg_id;
} __attribute__((packed));

int get_interface_index(int sockfd, const char *iface) {
  struct ifreq ifr{};
  std::strncpy(ifr.ifr_name, iface, IFNAMSIZ);
  if (ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0) {
    perror("SIOCGIFINDEX");
    exit(1);
  }
  return ifr.ifr_ifindex;
}

void get_mac_address(int sockfd, const char *iface, uint8_t *mac) {
  struct ifreq ifr{};
  std::strncpy(ifr.ifr_name, iface, IFNAMSIZ);
  if (ioctl(sockfd, SIOCGIFHWADDR, &ifr) < 0) {
    perror("SIOCGIFHWADDR");
    exit(1);
  }
  memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
}

sem_t sem;

void signal_handler(int sig) {
  sem_post(&sem);
}

void bind_socket(int sockfd, const char *iface) {
  struct sockaddr_ll sll{};
  sll.sll_family = AF_PACKET;
  sll.sll_protocol = htons(ETH_P_ALL);
  sll.sll_ifindex = get_interface_index(sockfd, iface);

  if (bind(sockfd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
    perror("receiver bind");
    exit(1);
  }
}

void set_recv_buf_size(int sockfd) {
  int tam = 50 * 1024 * 1024;

  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUFFORCE, &tam, sizeof(tam)) < 0) {
    perror("setsockopt SO_RCVBUF");
  }
}

void set_bpf_filter(int sockfd) {
  // Para adicionar mais protocolos, verificar documentação do BPF:
  // https://www.kernel.org/doc/Documentation/networking/filter.txt
  struct sock_filter bpf_code[] = {
    // Verifica o ethertype (ex: 0x88B5)
    { 0x28, 0, 0, 0x0000000c }, // ldh [12] (Load half word into A)
    { 0x15, 0, 1, 0x000088b5 }, // jeq 0x88B5? Se não, pula 1 instrução
    { 0x06, 0, 0, 0x0000ffff }, // Retorna o quadro
    { 0x06, 0, 0, 0x00000000 }, // Descarta quadro
  };

  struct sock_fprog bpf_prog = {
    .len = sizeof(bpf_code) / sizeof(bpf_code[0]),
    .filter = bpf_code,
  };

  // Tenta aplicar filtro
  if (setsockopt(sockfd, SOL_SOCKET, SO_ATTACH_FILTER, &bpf_prog,
                 sizeof(bpf_prog))) {
    perror("setsockopt");
    exit(1);
  }
}

void set_socket_opt(int sockfd, const char *iface) {
  // Configura processo como ´dono´ do socket para poder receber
  // sinais, como o SIGIO
  if (fcntl(sockfd, F_SETOWN, getpid()) < 0) {
    perror("fcntl F_SETOWN");
    exit(EXIT_FAILURE);
  }

  // Set interfacace index -------------------------------------
  struct ifreq ifr;
  strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
  if (ioctl(sockfd, SIOCGIFINDEX, &ifr) == -1) {
    perror("ioctl SIOCGIFINDEX");
    exit(EXIT_FAILURE);
  }

  // Obtem flags do socket para não sobrescreve-las posteriormente
  int flags = fcntl(sockfd, F_GETFL);
  if (flags < 0) {
    perror("fcntl F_GETFL");
    exit(EXIT_FAILURE);
  }

  // O_ASYNC faz com que o socket levante o sinal SIGIO quando operacoes de
  // I/O acontecerem
  // O_NONBLOCK faz com que operações normalmente bloqueantes
  // não bloqueiem
  if (fcntl(sockfd, F_SETFL, flags | O_ASYNC | O_NONBLOCK) < 0) {
    perror("fcntl F_SETFL");
    exit(EXIT_FAILURE);
  }
}

void set_sigaction(int sockfd) {
  // Armazena a função de callback
  struct sigaction sigAction;
  memset(&sigAction, 0, sizeof(sigAction));
  // Limpa possiveis sinais existentes antes da configuracao
  sigemptyset(&sigAction.sa_mask);
  sigAction.sa_handler = signal_handler;
  sigAction.sa_flags = SA_RESTART ;

  // Configura sigaction
  // nullptr indica que nao queremos salvar a sigaction anterior
  if (sigaction(SIGIO, &sigAction, nullptr) < 0) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }
}

void receiver(const char *iface) {
  sem_init(&sem, 0, 0);

  int sockfd = socket(AF_PACKET, SOCK_RAW, ETH_P_ALL);
  if (sockfd < 0) {
    perror("receiver socket");
    exit(1);
  }

  bind_socket(sockfd, iface);

  int promisc = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &promisc, sizeof(promisc));

  set_recv_buf_size(sockfd);

  set_bpf_filter(sockfd);

  set_socket_opt(sockfd, iface);

  set_sigaction(sockfd);

  std::cout << "[Receiver] Listening on interface: " << iface << "\n";

  uint8_t buffer[1500];
  int msg_id[SENDER_COUNT];
  memset(msg_id, 0, sizeof(msg_id));

  uint64_t delta_wait_post[SENDER_COUNT * MESSAGES_PER_SENDER];
  memset(delta_wait_post, 0, sizeof(delta_wait_post));

  uint64_t delta_send_recv[SENDER_COUNT * MESSAGES_PER_SENDER];
  memset(delta_send_recv, 0, sizeof(delta_send_recv));

  int j = 0;
  while (j < SENDER_COUNT * MESSAGES_PER_SENDER) {
    struct sockaddr_ll addr;
    socklen_t addr_len = sizeof(addr);

    sem_wait(&sem);
    while (true) {
        uint64_t pre_recv_t = duration_cast<microseconds>(
                                  high_resolution_clock::now().time_since_epoch())
                                  .count();
        ssize_t len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                               (struct sockaddr *)&addr, &addr_len);
        uint64_t pos_recv_t = duration_cast<microseconds>(
                                  high_resolution_clock::now().time_since_epoch())
                                  .count();
        uint64_t delta = pos_recv_t - pre_recv_t;
    
        if (len < 0) break;
    
        Packet *eth = (Packet *)buffer;
        if (ntohs(eth->ethertype) != CUSTOM_ETHER_TYPE)
          continue;
    
        j++;
        delta_wait_post[j] = delta;
        msg_id[eth->sender_id] = eth->msg_id;
    
        uint64_t now_us = duration_cast<microseconds>(
                              high_resolution_clock::now().time_since_epoch())
                              .count();
        uint64_t latency_us = now_us - eth->send_time_us;
    
        delta_send_recv[j] = latency_us;
        // std::cout << "("<< j << ") " << latency_us << std::endl;
    }
  }

  std::cout << "Tempos de delay para sair do recvfrom:" << std::endl;
  for (int i = 0; i < SENDER_COUNT * MESSAGES_PER_SENDER; i++) {
    std::cout << delta_wait_post[i] << ", ";
  }
  std::cout << std::endl;

  std::cout << "Tempos de delay entre envio e recebimento:" << std::endl;
  for (int i = 0; i < SENDER_COUNT * MESSAGES_PER_SENDER; i++) {
    std::cout << delta_send_recv[i] << ", ";
  }
  std::cout << std::endl;
  sem_destroy(&sem);
  close(sockfd);
}

void sender(const char *iface, int sender_id) {
  int sockfd = socket(AF_PACKET, SOCK_RAW, ETH_P_ALL);
  if (sockfd < 0) {
    perror("sender socket");
    exit(1);
  }

    // set_recv_buf_size(sockfd);
    // set_bpf_filter(sockfd);

  uint8_t src_mac[6];
  get_mac_address(sockfd, iface, src_mac);

  uint8_t dst_mac[6];
  memset(dst_mac, 0xff, 6); // broadcast

  int ifindex = get_interface_index(sockfd, iface);

  struct sockaddr_ll sll{};
  sll.sll_family = AF_PACKET;
  sll.sll_ifindex = ifindex;
  sll.sll_halen = 6;
  memcpy(sll.sll_addr, dst_mac, 6);

  auto now = std::chrono::steady_clock::now();

  for (int i = 1; i <= MESSAGES_PER_SENDER * 2; ++i) {
    uint8_t frame[sizeof(Packet)] = {};
    Packet *eth = (Packet *)frame;

    // Preenche cabeçalho ethernet
    memcpy(eth->dst_mac, dst_mac, 6);
    memcpy(eth->src_mac, src_mac, 6);
    eth->ethertype = htons(CUSTOM_ETHER_TYPE);

    // tempo de envio
    eth->send_time_us = duration_cast<microseconds>(
                            high_resolution_clock::now().time_since_epoch())
                            .count();
    // ID de quem ta enviando
    eth->sender_id = sender_id;
    // ID da mensagem que está sendo enviada
    eth->msg_id = i;

    ssize_t sent = sendto(sockfd, frame, sizeof(frame), 0,
                          (struct sockaddr *)&sll, sizeof(sll));

    if (sent < 0) {
      perror("sendto");
    }

    auto next_wakeup_t = now +
                              std::chrono::microseconds(100000 * (i - 1));
    std::this_thread::sleep_until(next_wakeup_t);
  }

  close(sockfd);
  exit(0);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Uso: sudo ./raw_latency_eth <interface>\n";
    return 1;
  }

  const char *iface = argv[1];

  pid_t pid = fork();
  if (pid == 0) {
    receiver(iface);
    return 0;
  }

  for (int i = 0; i < SENDER_COUNT; ++i) {
    pid_t sender_pid = fork();
    if (sender_pid == 0) {
      sender(iface, i + 1);
    }
  }

  for (int i = 0; i < SENDER_COUNT; ++i) {
    wait(nullptr);
  }

  return 0;
}
