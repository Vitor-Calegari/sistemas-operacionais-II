#include "engine.hh"
#include<sys/ioctl.h>
#include<net/if.h>

#include <iostream>
#include <string>
#include <cstdio>
#include <cstring>
#include <linux/if_ether.h>

std::string ethernet_header(const unsigned char* buffer, int buflen) {
    const struct ethhdr *eth = reinterpret_cast<const struct ethhdr*>(buffer);

    // Construir a string diretamente
    std::string header = "\nEthernet Header\n";
    header += "\t|-Source Address      : ";
    for (int i = 0; i < 6; i++) {
        char temp[4];
        snprintf(temp, sizeof(temp), "%.2X", eth->h_source[i]);
        header += temp;
        if (i < 5) header += "-";
    }
    header += "\n";

    header += "\t|-Destination Address : ";
    for (int i = 0; i < 6; i++) {
        char temp[4];
        snprintf(temp, sizeof(temp), "%.2X", eth->h_dest[i]);
        header += temp;
        if (i < 5) header += "-";
    }
    header += "\n";

    header += "\t|-Protocol            : " + std::to_string(eth->h_proto) + "\n";

    return header;
}

std::string payload(const unsigned char* buffer, int buflen) {
    const unsigned char *data = buffer + sizeof(struct ethhdr);
    int remaining_data = buflen - sizeof(struct ethhdr);

    std::string result = "\nData\n";
    char temp[8]; // Buffer temporário para formatação

    for (int i = 0; i < remaining_data; i++) {
        if (i != 0 && i % 16 == 0) {
            result += "\n";
        }
        snprintf(temp, sizeof(temp), " %.2X ", data[i]);
        result += temp;
    }

    result += "\n";
    return result;
}

std::string pBuflen(int buflen) {
    char temp[32]; // Buffer temporário para formatação
    snprintf(temp, sizeof(temp), "buflen: %d\n", buflen);
    return std::string(temp); // Converter para std::string
}

void printEthToFile(FILE *log_txt, const unsigned char *buffer, int buflen) {
    fprintf(log_txt, "\n*************************ETH Packet******************************");
    fprintf(log_txt, "%s", ethernet_header(buffer, buflen).c_str());
    fprintf(log_txt, "%s", payload(buffer, buflen).c_str());
    fprintf(log_txt, "%s", pBuflen(buflen).c_str());
    fprintf(log_txt, "*****************************************************************\n\n\n");
}

void printEth(unsigned char *buffer, int buflen) {

    std::cout << "\n*************************ETH Packet******************************";
    std::cout << ethernet_header(buffer, buflen).c_str();
    std::cout << payload(buffer, buflen).c_str();
    std::cout << pBuflen(buflen).c_str();
    std::cout << "*****************************************************************\n\n\n";
}
