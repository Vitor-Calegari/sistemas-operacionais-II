#include "car.hh"


#define NUM_MSGS 100000
#define MSG_SIZE 5

#ifndef INTERFACE_NAME
#define INTERFACE_NAME "lo"
#endif

int main() {
  using Buffer = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<Buffer>>;
  using SharedMemNIC = NIC<SharedEngine<Buffer>>;
  using Protocol = Protocol<SocketNIC, SharedMemNIC>;
  using Message = Message<Protocol::Address>;

    Car car = Car();
    Component comp = car.create_component(1);
    int i = 0;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    while (i < NUM_MSGS) {
        Message message = Message(
            comp.addr(), Protocol::Address(Ethernet::BROADCAST_ADDRESS, Protocol::BROADCAST_SID, Protocol::BROADCAST),
            MSG_SIZE);
        if (comp.send(&message)) {
            i++;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    double avg_time = static_cast<double>(duration.count()) / NUM_MSGS;
    
    std::cout << "Average sending time: " << avg_time << " microseconds" << std::endl;

  return 0;
}
