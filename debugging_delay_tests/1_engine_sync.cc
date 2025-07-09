#include "engine.hh"
#include "buffer.hh"
#include "ethernet.hh"
#include <chrono>
#include <cstring>
#include <iostream>
#include <signal.h>
#include <unistd.h>

using timepoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

#define INTERFACE "enxf8e43bf0c430"

int MSG_NUM = 1000;
int PROC_NUM = 15;

class Packet {
public:
    int64_t send_time;
    int msg_id;
    int sender_id;
} __attribute__((packed));

int main() {
    // Cria engine para interface

    // Buffers de envio e recebimento
    Buffer sendBuf{};
    Buffer recvBuf{};

    pid_t pid;

    int k = 0;

    for (auto i = 0; i < PROC_NUM; ++i) {
        k = i;
        pid = fork();
        if (pid == 0) {
          std::cout << "Veículo criado: PID = " << getpid() << std::endl;
          break;
        }
    }

    if (pid == 0) {
        Engine<Ethernet> engine(INTERFACE);
        for (int i = 1; i <= MSG_NUM; i++) {
            auto frame = sendBuf.data<Ethernet::Frame>();
            
            // 1) Preenche MAC de destino
            frame->dst = Ethernet::BROADCAST_ADDRESS;
            // 2) Preenche MAC de origem
            frame->src = engine.getAddress().mac;
            // 3) Preenche o EtherType
            frame->prot = htons(0x88B5);

            Packet *pkt = frame->template data<Packet>();

            // Timestamp no payload
            auto now = std::chrono::high_resolution_clock::now();
            int64_t micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
            pkt->send_time = micros;
            pkt->msg_id = i;
            pkt->sender_id = k;
            sendBuf.setSize(sizeof(Ethernet::Header) + sizeof(Packet));

            if (engine.send(&sendBuf) < 0) {
                std::cout << "Falha no envio" << std::endl;
            }
            usleep(100000);
        }
    } else {
        // Processo receptor
        Engine<Ethernet> engine(INTERFACE);
        int msg_id[PROC_NUM];
        for (int i = 0; i < PROC_NUM; i++) {
            msg_id[i] = 0;
        }
        int j = 0;
        // Processo receptor
        while (j < MSG_NUM * PROC_NUM) {
            int len = engine.receive(&recvBuf);
            if (len <= 0) continue;
            j++;
            // Extrai timestamp enviado pelo emissor (primeiros bytes do payload)
            auto frame = recvBuf.data<Ethernet::Frame>();
            Packet *pkt = frame->template data<Packet>();
            
            int64_t micros_sent = pkt->send_time;
            int proc_id = pkt->sender_id;
            int msg_idd = pkt->msg_id;

            if (msg_id[proc_id] == msg_idd) {
                std::cout << "Received again" << std::endl;
                continue;
            }
            msg_id[proc_id] = msg_idd;

            auto now = std::chrono::high_resolution_clock::now();
            int64_t micros_now = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
            int64_t delta = micros_now - micros_sent;
            std::cout << "Tempo de ida: " << delta << " us" << std::endl;
        }
        kill(pid, SIGKILL);
    }

    return 0;
}
