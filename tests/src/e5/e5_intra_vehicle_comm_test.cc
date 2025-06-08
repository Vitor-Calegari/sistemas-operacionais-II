#include "car.hh"
#include "component.hh"
#include "message.hh"
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>

int main() {
  using Buffer    = Buffer<Ethernet::Frame>;
  using SocketNIC = NIC<Engine<Buffer>>;
  using SharedNIC = NIC<SharedEngine<Buffer>>;
  using Proto     = Protocol<SocketNIC, SharedNIC>;

  Car car;  

  //Dois componentes em portas diferentes
  auto compA = car.create_component(1);
  auto compB = car.create_component(2);

  //Monta a mensagem de A para B
  auto addrA = compA.addr();
  auto addrB = compB.addr();
  constexpr std::size_t PAYLOAD = 32;
  Message<Proto::Address> msg(addrA, addrB, PAYLOAD);

  for (std::size_t i = 0; i < PAYLOAD; ++i)
    msg.data()[i] = static_cast<std::byte>(i);

  //Envia de A para B
  bool sent = compA.send(&msg);
  assert(sent && "Falha ao enviar intra-veículo");

  //Recebe em B (com timeout curto)
  bool received = false;
  for (int i = 0; i < 100; ++i) {
    if (compB.receive(&msg)) {
      received = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(received && "Deveria receber mensagem intra–veículo");

  //Verifica integridade
  for (std::size_t i = 0; i < PAYLOAD; ++i) {
    assert(msg.data()[i] == static_cast<std::byte>(i)
           && "Payload corrompido intra–veículo");
  }

  std::cout << "[OK] Comunicação intra–veículo otimizada funcionando\n";
  return 0;
}


//g++ -std=c++20 -Iinclude -Iinclude/protocols -Iinclude/data_frames -Iinclude/observers -Itests/include   tests/src/e5/e5_intra_vehicle_comm_test.cc src/utils.cc src/ethernet.cc src/mac.cc -lcrypto -lpthread -o intra_vehicle_test