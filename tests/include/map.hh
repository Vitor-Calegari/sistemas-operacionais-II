
#include "communicator.hh"
#include "cond.hh"
#include "engine.hh"
#include "message.hh"
#include "navigator.hh"
#include "nic.hh"
#include "rsu_protocol.hh"
#include "shared_engine.hh"
#include "topology.hh"
#include "shared_mem.hh"
#include <csignal>
#include <cstddef>
#include <fcntl.h>
#include <iostream>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

const char *SHM_NAME = "/barrier_shm";

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

class Map {
public:
  using SocketNIC = NIC<Engine<Ethernet>>;
  using SharedMemNIC = NIC<SharedEngine<SharedMem>>;
  using RSU = RSUProtocol<SocketNIC, SharedMemNIC, NavigatorDirected>;
  using Coordinate = NavigatorCommon::Coordinate;
  using Size = Topology::Size;

  static constexpr double RSU_RANGE = 10; // -10 a 0 a 10

  Map(int n_col, int n_line)
      : RSUNum(n_col * n_line), NUM_COLS(n_col), NUM_LINES(n_line),
        _topo({ n_col, n_line }, RSU_RANGE) {

    shouldEnd =
        static_cast<bool *>(mmap(NULL, sizeof(bool), PROT_READ | PROT_WRITE,
                                 MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (shouldEnd == MAP_FAILED) {
      perror("mmap");
      exit(1);
    }
    *shouldEnd = false; // inicializa em false

    mutex = static_cast<pthread_mutex_t *>(
        mmap(NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_ANONYMOUS, -1, 0));

    cond = static_cast<pthread_cond_t *>(
        mmap(NULL, sizeof(pthread_cond_t), PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_ANONYMOUS, -1, 0));
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

    rsu_sem =
        static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                  MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    sem_init(rsu_sem, 1, 0); // Inicialmente bloqueado

    shared_mem_sem =
        static_cast<sem_t *>(mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                  MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    sem_init(shared_mem_sem, 1, 0); // Inicialmente bloqueado

    // Cria e configura a memória compartilhada
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (ftruncate(shm_fd, sizeof(SharedData)) == -1) {
      perror("ftruncate");
      exit(1);
    }
    shared_data =
        (SharedData *)mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE,
                           MAP_SHARED, shm_fd, 0);

    // Inicializa mutex
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared_data->mutex, &mutex_attr);

    // Inicializa barreira
    pthread_barrierattr_t barrier_attr;
    pthread_barrierattr_init(&barrier_attr);
    pthread_barrierattr_setpshared(&barrier_attr, PTHREAD_PROCESS_SHARED);
    pthread_barrier_init(&shared_data->barrier, &barrier_attr, n_col * n_line);

    // Inicializa variaveis de controle para RSU
    shared_data->counter = 0;
    shared_data->entries_size_x = n_col;
    shared_data->entries_size_y = n_line;

    for (int col = 0; col < n_col; col++) {
      for (int line = 0; line < n_line; line++) {
        auto ret_rsu = fork();
        if (ret_rsu < 0) {
          std::cerr << "Erro ao criar processo" << std::endl;
          exit(1);
        }
        if (col == 0 && line == 0) {
          if (!ret_rsu) {
            shared_data->choosen_rsu = getpid();
            sem_post(shared_mem_sem);
            createRSU(col, line); // Processo filho bloqueará aqui, quando for
                                  // desbloqueado, ele morre
          }
        } else {
          if (!ret_rsu) {
            createRSU(col, line);
          }
        }
      }
    }
  }

  ~Map() {
    finalizeRSU();
    int status;
    for (int i = 0; i < RSUNum; i++) {
      wait(&status);
    }
    pthread_mutex_destroy(&shared_data->mutex);
    pthread_barrier_destroy(&shared_data->barrier);
    munmap(shared_data, sizeof(SharedData));
    munmap(shouldEnd, sizeof(bool));
    munmap(mutex, sizeof(pthread_mutex_t));
    munmap(cond, sizeof(pthread_cond_t));
    shm_unlink(SHM_NAME);
  }

  void finalizeRSU() {
    pthread_mutex_lock(mutex);
    *shouldEnd = true;
    pthread_cond_broadcast(cond);
    pthread_mutex_unlock(mutex);
    std::cout << get_timestamp() << " PID " << getpid() << " Should end MAP"
              << std::endl;
  }

  Topology getTopology() {
    return _topo;
  }

private:
  int createRSU(int c, int l) {
    double cell = 2 * RSU_RANGE;

    double x = (c - (NUM_COLS - 1) / 2.0) * cell;
    double y = ((NUM_LINES - 1) / 2.0 - l) * cell;

    Coordinate point(x, y);

    [[maybe_unused]]
    RSU &rsu_p = RSU::getInstance(INTERFACE_NAME, getpid(), shared_data,
                                  std::make_pair(c, l), x + y * NUM_COLS,
                                  { point }, _topo, RSU_RANGE, 0);
    waitCond();
    std::cout << get_timestamp() << " RSU " << getpid() << " ending"
              << std::endl;
    exit(0);
  }

  void waitCond() {
    pthread_mutex_lock(mutex);
    while (!*shouldEnd) {
      pthread_cond_wait(cond, mutex);
    }
    pthread_mutex_unlock(mutex);
  }

private:
  int RSUNum;
  bool *shouldEnd;
  sem_t *rsu_sem;
  sem_t *shared_mem_sem;
  SharedData *shared_data;
  pthread_cond_t *cond;
  pthread_mutex_t *mutex;
  int NUM_COLS;
  int NUM_LINES;
  Topology _topo;
};
