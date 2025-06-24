#ifndef BUFFER_HH
#define BUFFER_HH

#include <cstddef>
#include <cstdint>
class Buffer {
public:
  static constexpr size_t BUFFER_SIZE = 1514;

  enum BufferType { EthernetFrame, SharedMemFrame };

  Buffer(BufferType buf_type = EthernetFrame)
      : _type(buf_type), _size(0), _max_size(0), _in_use(false) {
  }

  // Construtor: define a capacidade máxima do buffer.
  // Args:
  //   cap: A capacidade máxima de bytes que o buffer pode conter.
  Buffer(int max_size) : _size(0), _max_size(max_size), _in_use(false) {
  }

  // Retorna um ponteiro para o objeto Data contido no buffer.
  template <typename T>
  T *data() {
    return reinterpret_cast<T *>(&_data);
  }

  // Retorna o tamanho atual dos dados válidos no buffer (em bytes).
  int size() {
    return _size;
  }

  BufferType type() const {
    return _type;
  }

  // Define o tamanho atual dos dados válidos.
  // Garante que o tamanho não exceda a capacidade.
  // Args:
  //   newSize: O novo tamanho dos dados.
  void setSize(int newSize) {
    // Mínimo entre newSize e _max_size
    _size = (newSize <= _max_size) ? newSize : _max_size;
  }

  void setMaxSize(int maxSize) {
    _max_size = maxSize;
  }

  int64_t get_receive_time() const {
    return _receive_time;
  }

  void set_receive_time(int64_t rec_time) {
    _receive_time = rec_time;
  }

  // Retorna a capacidade máxima do buffer (em bytes).
  int maxSize() {
    return _max_size;
  }

  // --- Métodos para Gerenciamento de Pool (usados pela NIC) ---

  // Verifica se o buffer está atualmente em uso no pool.
  bool is_in_use() const {
    return _in_use;
  }

  // Marca o buffer como em uso. Chamado pela NIC::alloc.
  void mark_in_use() {
    _in_use = true;
  }

  // Marca o buffer como livre. Chamado pela NIC::free.
  // Reseta o tamanho para evitar usar dados antigos.
  void mark_free() {
    _in_use = false;
    _size = 0; // Importante resetar o tamanho ao liberar
  }

private:
  BufferType _type;
  int _size;     // Tamanho atual dos dados válidos
  int _max_size; // Capacidade máxima do buffer
  bool _in_use;  // Flag para gerenciamento em um pool de buffers
  std::byte _data[BUFFER_SIZE] = {};
  int64_t _receive_time;
};

#endif
