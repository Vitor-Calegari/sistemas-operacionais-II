#ifndef UTILS_HH
#define UTILS_HH

#include "buffer.hh"
#include "ethernet.hh"
#include <string>

std::string ethernet_header(struct Ethernet::Frame *buffer);

std::string payload(struct Ethernet::Frame *buffer, int buflen);

std::string pBuflen(int buflen);

void printEthToFile(FILE *log_txt, Buffer<Ethernet::Frame> *buffer);

void printEth(Buffer<Ethernet::Frame> *buffer);

#endif
