#ifndef MAC_STRUCTS_HH
#define MAC_STRUCTS_HH

#include <pthread.h>

static const int MAX_ENTRIES = 10;

struct MacKeyEntry {
    int id;
    char bytes[32];  // TODO ajustar numero de bytes
  };
  
struct SharedData {
    pthread_mutex_t mutex;
    pthread_barrier_t barrier;
    int counter;
    MacKeyEntry entries[MAX_ENTRIES];
    int entries_size_x;
    int entries_size_y;
    pid_t choosen_rsu;
};

#endif