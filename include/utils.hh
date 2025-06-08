#ifndef UTILS_HH
#define UTILS_HH

#include "buffer.hh"
#include "ethernet.hh"
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

std::string ethernet_header(Ethernet::Frame *buffer);

std::string payload(Ethernet::Frame *buffer, int buflen);

std::string pBuflen(int buflen);

void printEthToFile(FILE *log_txt, Buffer<Ethernet::Frame> *buffer);

void printEth(Buffer<Ethernet::Frame> *buffer);

int randint(int p, int r);

std::string get_timestamp();

void printSyncMsg(bool _needSync, bool _synced);
#endif
