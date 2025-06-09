#ifndef RSU_ENGINE_HH
#define RSU_ENGINE_HH

#include "control.hh"
#include "mac.hh"
#include "mac_structs.hh"
#include <atomic>
#include <bit>
#include <chrono>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#ifdef DEBUG_MAC
#include "utils.hh"
#endif

template <typename Protocol>
class RSUEngine {
public:
  typedef pid_t SysID;
  typedef typename Protocol::Address Address;
  typedef std::pair<int, int> Coord;

  enum Action { DO_NOTHING = 0, REPLY = 1 };

  static constexpr int PERIOD = 1e6;
  static constexpr auto MAC = Control::Type::MAC;
  static constexpr int renew_mac_int = 3;

public:
  RSUEngine(Protocol *protocol, SharedData *shared_data, Coord coord, int id)
      : _protocol(protocol), _coord(coord), _id(id), _shared_data(shared_data) {
    startKeySenderThread();
  }

  ~RSUEngine() {
    stopKeySenderThread();
  }

  void startKeySenderThread() {
    _key_sender_thread_running = true;
    _key_sender_thread = std::thread([this]() {
      while (_key_sender_thread_running) {
        pthread_mutex_lock(&_shared_data->mutex);
        // Se for igual a zero, gera uma nova chave mac e armazena em
        // memória compartilhada
        if (_shared_data->counter == 0) {
          // Gerar mac e inserir na minha posição do map
          int indx =
              _coord.first + (_coord.second * _shared_data->entries_size_x);
          _shared_data->entries[indx] =
              MacKeyEntry(_id, MAC::generate_random_key());
        }
        // Espera todos os processos RSU chegarem aqui
        pthread_barrier_wait(&_shared_data->barrier);
        // Se for a thread escolhida, adiciona no contador de iterações
        if (_shared_data->choosen_rsu == _protocol->getSysID()) {
          _shared_data->counter = (_shared_data->counter + 1) % renew_mac_int;
        }
        pthread_mutex_unlock(&_shared_data->mutex);

        // Esperar ----------------------
        std::chrono::_V2::steady_clock::time_point next_wakeup_t =
            std::chrono::steady_clock::now() +
            std::chrono::microseconds(PERIOD);
        std::this_thread::sleep_until(next_wakeup_t);
        if (!_key_sender_thread_running)
          break;
        // ------------------------------

        // Espera todos os processos RSU chegarem aqui
        pthread_barrier_wait(&_shared_data->barrier);

        // Enviar mensagem --------------
        Address myaddr = _protocol->getAddr();
        Address broadcast = _protocol->getBroadcastAddr();
        Control ctrl(MAC);

        // Obtem as chaves MAC das RSUs adjacentes e envia broadcast
        std::vector<MacKeyEntry> neighborhood_keys = getNeighborhood(
            _shared_data->entries, _shared_data->entries_size_x,
            _shared_data->entries_size_y, _coord.first, _coord.second);

        std::byte data[9 * sizeof(MacKeyEntry)];
        std::memset(data, 0, sizeof(data));
        for (std::size_t i = 0; i < neighborhood_keys.size(); ++i) {
          auto key = neighborhood_keys[i];

          auto key_bytes =
              std::bit_cast<std::array<std::byte, sizeof(MacKeyEntry)>>(key);

          for (std::size_t j = 0; j < key_bytes.size(); ++j) {
            data[sizeof(MacKeyEntry) * i + j] = key_bytes[j];
          }
        }

        _protocol->send(myaddr, broadcast, ctrl, data, 9 * sizeof(MacKeyEntry));
      }
    });
  }

  void stopKeySenderThread() {
    if (_key_sender_thread_running) {
      _key_sender_thread_running = false;
      if (_key_sender_thread.joinable()) {
        _key_sender_thread.join();
      }
    }
  }

  std::vector<MacKeyEntry> getNeighborhood(MacKeyEntry *matrix, int rows,
                                           int cols, int targetRow,
                                           int targetCol) {
    std::vector<MacKeyEntry> result;

    for (int dr = -1; dr <= 1; ++dr) {
      for (int dc = -1; dc <= 1; ++dc) {
        int r = targetRow + dr;
        int c = targetCol + dc;

        // Verifica se a posição está dentro dos limites
        if (r >= 0 && r < rows && c >= 0 && c < cols) {
          int index = r * cols + c;
          result.push_back(matrix[index]);
        }
      }
    }

    return result;
  }

private:
  Protocol *_protocol = nullptr;
  Coord _coord;
  int _id;
  // Key Sender Thread --------------------------------
  std::thread _key_sender_thread;
  std::atomic<bool> _key_sender_thread_running = false;
  // Shared Data --------------------------------------
  SharedData *_shared_data;
};

#endif
