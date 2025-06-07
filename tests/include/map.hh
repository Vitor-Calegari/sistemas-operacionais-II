
#include "shared_engine.hh"
#include "engine.hh"
#include "nic.hh"
#include "communicator.hh"
#include "rsu_protocol.hh"
#include "cond.hh"
#include "message.hh"
#include <semaphore.h>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

const char* SHM_NAME = "/barrier_shm";

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

class Map {
public:
    using Buffer = Buffer<Ethernet::Frame>;
    using SocketNIC = NIC<Engine<Buffer>>;
    using SharedMemNIC = NIC<SharedEngine<Buffer>>;
    using RSU = RSUProtocol<SocketNIC, SharedMemNIC>;

    Map(int n_col, int n_line) : RSUNum(n_col * n_line), shouldEnd(false) {

        // Inicializa mutex da variavel de condição
        pthread_mutexattr_t mutex_cond_attr;
        pthread_mutexattr_init(&mutex_cond_attr);
        pthread_mutexattr_setpshared(&mutex_cond_attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&mutex, &mutex_cond_attr);
        // Inicializa variavel de condição
        pthread_condattr_t cond_attr;
        pthread_condattr_init(&cond_attr);
        pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
        pthread_cond_init(&cond, &cond_attr);

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
        ftruncate(shm_fd, sizeof(SharedData));
        shared_data = (SharedData*) mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
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
                if (col == 0 && line == 0) {
                    if (!ret_rsu) {
                        sem_wait(shared_mem_sem);
                        createRSU(col, line);  // Processo filho bloqueará aqui, quando for desbloqueado, ele morre
                    } else {
                        shared_data->choosen_rsu = ret_rsu;
                        sem_post(shared_mem_sem);
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
        shm_unlink(SHM_NAME);
    }

private:
    // TODO FAZER COM QUE CADA RSU TENHA SUA PROPRIA LOCALIÇÃO
    int createRSU(int c, int l) {
        [[maybe_unused]] RSU &rsu_p = RSU::getInstance(INTERFACE_NAME, getpid(), shared_data, std::make_pair(c,l), c * (l + 1));
        waitCond();
        exit(0);
    }

    void waitCond() {
        pthread_mutex_lock(&mutex);
        while (!shouldEnd) {
            pthread_cond_wait(&cond, &mutex);
        }
        pthread_mutex_unlock(&mutex);
    }

    void finalizeRSU() {
        pthread_mutex_lock(&mutex);
        shouldEnd = true;
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&mutex);
    }
private:
    int RSUNum;
    bool shouldEnd;
    sem_t *rsu_sem;
    sem_t *shared_mem_sem;
    SharedData* shared_data;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
};
