#include<sys/ioctl.h>
#include<net/if.h>

#include <iostream>
#include <string>
#include <cstdio>
#include <cstring>
#include <linux/if_ether.h>
#include "ethernet.hh"
#include "buffer.hh"

std::string ethernet_header(struct Ethernet::Frame * buffer);

std::string payload(struct Ethernet::Frame * buffer, int buflen);

std::string pBuflen(int buflen);

void printEthToFile(FILE *log_txt, Buffer<Ethernet::Frame> *buffer);

void printEth(Buffer<Ethernet::Frame> * buffer);
