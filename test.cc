#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

int main() {
    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    char buffer[2048];

    while (true) {
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (len > 0) {
            // Cabeçalho Ethernet = 14 bytes
            struct ether_header* eth = (struct ether_header*)buffer;

            if (ntohs(eth->ether_type) == ETHERTYPE_IP) {
                // Cabeçalho IP vem após Ethernet
                struct ip* ip_hdr = (struct ip*)(buffer + sizeof(struct ether_header));

                if (ip_hdr->ip_p == IPPROTO_ICMP) {
                    // Cabeçalho ICMP vem após o cabeçalho IP
                    size_t ip_header_len = ip_hdr->ip_hl * 4;
                    struct icmphdr* icmp_hdr = (struct icmphdr*)(buffer + sizeof(struct ether_header) + ip_header_len);

                    if (icmp_hdr->type == ICMP_ECHO) {
                        std::cout << "[ICMP Request] " << len << " bytes" << std::endl;
                    } else if (icmp_hdr->type == ICMP_ECHOREPLY) {
                        std::cout << "[ICMP Reply] " << len << " bytes" << std::endl;
                    }
                }
            }
        }
    }

    close(sock);
    return 0;
}