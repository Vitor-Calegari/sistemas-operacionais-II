#include "engine.hh"
#include "buffer.hh"
#include "ethernet.hh"
#include <semaphore.h>
#include <thread>
#include <sys/wait.h>
#include <sys/mman.h>

using namespace std::chrono;

constexpr size_t ETH_HDR_LEN = 14;
constexpr size_t PAYLOAD_LEN = 64;
constexpr uint16_t CUSTOM_ETHER_TYPE = 0x88B5;
constexpr int SENDER_COUNT = 15;
constexpr int MESSAGES_PER_SENDER = 100;

int *senders_finished;
pthread_mutex_t *mutex;
pthread_cond_t *cond;

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

class MiniNIC {
  public:
    MiniNIC (const char *iface) : recv_buf(), engine(iface), j(0) {
      memset(msg_id, 0, sizeof(msg_id));
      memset(delta_wait_post, 0, sizeof(delta_wait_post));
      memset(delta_send_recv, 0, sizeof(delta_send_recv));
      engine.bind<MiniNIC, &MiniNIC::handle_signal>(this);
    }
    void handle_signal() {
      while (true) {
        uint64_t pre_recv_t = duration_cast<microseconds>(
                                  high_resolution_clock::now().time_since_epoch())
                                  .count();
        ssize_t len = engine.receive(&recv_buf);
        uint64_t pos_recv_t = duration_cast<microseconds>(
                                  high_resolution_clock::now().time_since_epoch())
                                  .count();
        uint64_t delta = pos_recv_t - pre_recv_t;
    
        if (len < 0) {
          break;
        }

        Packet *eth = recv_buf.data<Packet>();
        if (ntohs(eth->ethertype) != CUSTOM_ETHER_TYPE)
          continue;
    
        if (msg_id[eth->sender_id] == eth->msg_id) {
          break;
        }
        delta_wait_post[j] = delta;
        msg_id[eth->sender_id] = eth->msg_id;
    
        uint64_t now_us = duration_cast<microseconds>(
                              high_resolution_clock::now().time_since_epoch())
                              .count();
        uint64_t latency_us = now_us - eth->send_time_us;
    
        delta_send_recv[j] = latency_us;
        j++;
        // std::cout << "("<< j << ") " << latency_us << std::endl;
      }
    }
  public:
    Buffer recv_buf;
    Engine<Ethernet> engine;
    int j;
    int msg_id[SENDER_COUNT];
    uint64_t delta_wait_post[SENDER_COUNT * MESSAGES_PER_SENDER];
    uint64_t delta_send_recv[SENDER_COUNT * MESSAGES_PER_SENDER];
};

void receiver(const char *iface) {
  // Inicializar engine
  std::cout << "[Receiver] Listening on interface: " << iface << "\n";

  MiniNIC mini_nic(iface);

  std::cout << "Receiver parando na variavel de condição" << std::endl;
  pthread_mutex_lock(mutex);
  while (*senders_finished < SENDER_COUNT) {
    pthread_cond_wait(cond, mutex);
  }
  pthread_mutex_unlock(mutex);

  std::cout << "Tempos de delay para sair do recvfrom:" << std::endl;
  for (int i = 0; i < SENDER_COUNT * MESSAGES_PER_SENDER; i++) {
    std::cout << mini_nic.delta_wait_post[i] << ", ";
  }
  std::cout << std::endl;

  std::cout << "Tempos de delay entre envio e recebimento:" << std::endl;
  for (int i = 0; i < SENDER_COUNT * MESSAGES_PER_SENDER; i++) {
    std::cout << mini_nic.delta_send_recv[i] << ", ";
  }
  std::cout << std::endl;
}

void sender(const char *iface, int sender_id) {
  Engine<Ethernet> engine(iface);

  Ethernet::Address src = engine.getAddress();

  uint8_t dst_mac[6];
  memset(dst_mac, 0xff, 6); // broadcast

  // // Aguarda receptor se preparar
  // usleep(500000);

  Buffer send_buf{};

  auto now = std::chrono::steady_clock::now();

  for (int i = 1; i <= MESSAGES_PER_SENDER; ++i) {
    Packet *eth = send_buf.data<Packet>();

    // Preenche cabeçalho ethernet
    memcpy(eth->dst_mac, dst_mac, 6);
    memcpy(eth->src_mac, src.mac, 6);
    eth->ethertype = htons(CUSTOM_ETHER_TYPE);

    // tempo de envio
    eth->send_time_us = duration_cast<microseconds>(
                            high_resolution_clock::now().time_since_epoch())
                            .count();
    // ID de quem ta enviando
    eth->sender_id = sender_id;
    // ID da mensagem que está sendo enviada
    eth->msg_id = i;

    send_buf.setSize(sizeof(Packet));

    int sent = engine.send(&send_buf);

    if (sent < 0) {
      perror("sendto");
    }

    auto next_wakeup_t = now +
                              std::chrono::microseconds(100000 * (i - 1));
    std::this_thread::sleep_until(next_wakeup_t);
  }

  std::cout << "Sender tentando adicionar valor ao mutex" << std::endl;
  pthread_mutex_lock(mutex);
  (*senders_finished)++;
  pthread_cond_broadcast(cond);
  pthread_mutex_unlock(mutex);

  std::cout << "Terminou sender" << "(" << sender_id << ")" << std::endl;
  exit(0);
}

void init_shared_vars() {
  senders_finished =
  static_cast<int *>(mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  if (senders_finished == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
  *senders_finished = 0;

  mutex = static_cast<pthread_mutex_t *>(
  mmap(NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0));

  cond = static_cast<pthread_cond_t *>(
  mmap(NULL, sizeof(pthread_cond_t), PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0));
        memset(mutex, 0, sizeof(pthread_mutex_t));
        memset(cond, 0, sizeof(pthread_cond_t));
  // Inicializa mutex da variavel de condição
  pthread_mutexattr_t mutex_cond_attr;
  pthread_mutexattr_init(&mutex_cond_attr);
  pthread_mutexattr_setpshared(&mutex_cond_attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(mutex, &mutex_cond_attr);
  // Inicializa variavel de condição
  pthread_condattr_t cond_attr;
  pthread_condattr_init(&cond_attr);
  pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
  pthread_cond_init(cond, &cond_attr);

  pthread_mutexattr_destroy(&mutex_cond_attr);
  pthread_condattr_destroy(&cond_attr);
}

void delete_shared_vars() {
  pthread_mutex_destroy(mutex);
  pthread_cond_destroy(cond);
  
  munmap(senders_finished, sizeof(int));
  munmap(mutex, sizeof(pthread_mutex_t));
  munmap(cond, sizeof(pthread_cond_t));
}

int main(int argc, char *argv[]) {
  // if (argc < 2) {
  //   std::cerr << "Uso: sudo ./raw_latency_eth <interface>\n";
  //   return 1;
  // }

  init_shared_vars();

  const char *iface = "enxf8e43bf0c430";//argv[1];

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

  for (int i = 0; i < SENDER_COUNT + 1; ++i) {
    wait(nullptr);
  }
  
  delete_shared_vars();
  return 0;
}
