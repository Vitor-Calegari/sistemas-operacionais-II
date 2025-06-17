#ifndef UTILS_HH
#define UTILS_HH

#include "buffer.hh"
#include "ethernet.hh"
#include <string>

std::string ethernet_header(Ethernet::Frame *buffer);

std::string payload(Ethernet::Frame *buffer, int buflen);

std::string pBuflen(int buflen);

void printEthToFile(FILE *log_txt, Buffer *buffer);

void printEth(Buffer *buffer);

int randint(int p, int r);

std::string get_timestamp();

void printSyncMsg(bool _needSync, bool _synced, int _announce_iteration);
#endif
