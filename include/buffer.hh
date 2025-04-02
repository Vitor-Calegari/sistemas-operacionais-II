#ifndef BUFFER_HH
#define BUFFER_HH

#include <algorithm>

template<typename Data>
class Buffer: private Data
{
public:

    Buffer() : _size(0), _max_size(0), _in_use(false) {}

    // Construtor: define a capacidade máxima do buffer.
    // Args:
    //   cap: A capacidade máxima de bytes que o buffer pode conter.
    Buffer(int max_size): _size(0), _max_size(max_size), _in_use(false) {}

    // Retorna um ponteiro para o objeto Data contido no buffer.
    Data * data() { return this; }

    // Retorna o tamanho atual dos dados válidos no buffer (em bytes).
    int size() { return _size; }

    // Define o tamanho atual dos dados válidos.
    // Garante que o tamanho não exceda a capacidade.
    // Args:
    //   newSize: O novo tamanho dos dados.
    void setSize(int newSize) {
        _size = std::min(newSize, _max_size);
    }

    void setMaxSize(int maxSize) { _max_size = maxSize;}

    // Retorna a capacidade máxima do buffer (em bytes).
    int maxSize() { return _max_size; }

private:
    int _size;         // Tamanho atual dos dados válidos
    int _max_size;     // Capacidade máxima do buffer
    bool _in_use;               // Flag para gerenciamento em um pool de buffers
};

#endif