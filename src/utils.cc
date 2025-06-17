#include "utils.hh"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <unistd.h>

std::string ethernet_header(Ethernet::Frame *buffer) {
  // Construir a string diretamente
  std::string header = "\nEthernet Header\n";
  header += "\t|-Source Address      : ";
  for (int i = 0; i < 6; i++) {
    char temp[4];
    snprintf(temp, sizeof(temp), "%.2X", buffer->src.mac[i]);
    header += temp;
    if (i < 5)
      header += "-";
  }
  header += "\n";

  header += "\t|-Destination Address : ";
  for (int i = 0; i < 6; i++) {
    char temp[4];
    snprintf(temp, sizeof(temp), "%.2X", buffer->dst.mac[i]);
    header += temp;
    if (i < 5)
      header += "-";
  }
  header += "\n";

  header += "\t|-Protocol            : " + std::to_string(buffer->prot) + "\n";

  return header;
}

std::string payload(Ethernet::Frame *buffer, int buflen) {
  int remaining_data = buflen - 14; // mac + mac + prot

  std::string result = "\nData\n";
  char temp[8]; // Buffer temporário para formatação

  for (int i = 0; i < remaining_data; i++) {
    if (i != 0 && i % 16 == 0) {
      result += "\n";
    }
    snprintf(temp, sizeof(temp), " %.2X ",
             buffer->template data<unsigned char>()[i]);
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

void printEthToFile(FILE *log_txt, Buffer *buffer) {
  fprintf(
      log_txt,
      "\n*************************ETH Packet******************************");
  fprintf(log_txt, "%s",
          ethernet_header(buffer->data<Ethernet::Frame>()).c_str());
  fprintf(log_txt, "%s",
          payload(buffer->data<Ethernet::Frame>(), buffer->size()).c_str());
  fprintf(log_txt, "%s", pBuflen(buffer->size()).c_str());
  fprintf(log_txt, "***********************************************************"
                   "******\n\n\n");
}

void printEth(Buffer *buffer) {
  std::cout
      << "\n*************************ETH Packet******************************";
  std::cout << ethernet_header(buffer->data<Ethernet::Frame>()).c_str();
  std::cout << payload(buffer->data<Ethernet::Frame>(), buffer->size()).c_str();
  std::cout << pBuflen(buffer->size()).c_str();
  std::cout << "***************************************************************"
               "**\n\n\n";
}

int randint(int p, int r) {
  std::random_device rd;
  std::mt19937 rng(rd());
  std::uniform_int_distribution<int> uni(p, r);

  return uni(rng);
}

std::string get_timestamp() {
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::system_clock::to_time_t(now);
  auto tm = std::localtime(&timestamp);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;

  std::stringstream ss;
  ss << "[" << std::put_time(tm, "%H:%M:%S") << "." << std::setfill('0')
     << std::setw(3) << ms.count() << "]";
  return ss.str();
}

void printSyncMsg(bool _needSync, bool _synced, int _announce_iteration) {
  if (_needSync) {
    std::cout << get_timestamp() << " I’m Car " << getpid()
              << " I need SYNC, my iteration is " << _announce_iteration
              << " and I'am ";
    if (_synced) {
      std::cout << " synced. " << std::endl;
    } else {
      std::cout << " not synced. " << std::endl;
    }
  } else {
    std::cout << get_timestamp() << " I’m Car " << getpid()
              << " I need SYNC, my iteration is " << _announce_iteration
              << " and I'am ";
    if (_synced) {
      std::cout << " synced. " << std::endl;
    } else {
      std::cout << " not synced. " << std::endl;
    }
  }
}
