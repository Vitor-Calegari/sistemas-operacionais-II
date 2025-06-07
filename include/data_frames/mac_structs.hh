#ifndef MAC_STRUCTS_HH
#define MAC_STRUCTS_HH

#include "mac.hh"
#include <pthread.h>

#ifndef MAX_ENTRIES
#define MAX_ENTRIES 1024
#endif

struct MacKeyEntry {
  int id;
  MAC::Key key;
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
