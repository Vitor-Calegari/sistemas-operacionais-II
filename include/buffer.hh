#ifndef BUFFER_HH
#define BUFFER_HH

#include <algorithm> // Para std::min
#include <cstddef>   // Para std::size_t
#include <new>       // Para std::bad_alloc
#include <cstring>   // Para std::memset

#include "ethernet.hh" // Precisa conhecer Ethernet::Frame

// Define o tipo de dados padrão que o Buffer conterá.
typedef Ethernet::Frame BufferData;

template<typename Data = BufferData>
class Buffer {
public:
    // Construtor agora recebe um ponteiro para os dados já alocados (pela Engine)
    // e a capacidade dessa área de memória.
    explicit Buffer(Data* data_ptr, unsigned int capacity) :
        _data_ptr(data_ptr), // Armazena o ponteiro fornecido
        _capacity(capacity),
        _size(0),
        _in_use(false)
    {
        if (!_data_ptr) {
            // É responsabilidade do chamador (NIC) garantir que data_ptr é válido.
            // Poderia lançar exceção, mas talvez um assert seja melhor aqui.
             throw std::invalid_argument("Buffer constructor received null data pointer");
        }
        // Opcional: Limpar a memória apontada (se a Engine não o fizer)
        // std::memset(_data_ptr, 0, sizeof(Data));
    }

    // Destrutor: NÃO deleta mais _data_ptr. A Engine é dona da memória.
    ~Buffer() {
        _data_ptr = nullptr; // Evita dangling pointer se o buffer for movido
    }

    // --- Gerenciamento de Recurso (Movimentação) ---
    // Proíbe cópia
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // Permite mover
    Buffer(Buffer&& other) noexcept :
        _data_ptr(other._data_ptr), _capacity(other._capacity), _size(other._size), _in_use(other._in_use)
    {
        // Reseta 'other' para indicar que não possui mais os dados
        other._data_ptr = nullptr;
        other._capacity = 0;
        other._size = 0;
        other._in_use = false;
    }

    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            // O ponteiro atual não é deletado (pertence à Engine)
            _data_ptr = other._data_ptr;
            _capacity = other._capacity;
            _size = other._size;
            _in_use = other._in_use;

            // Reseta 'other'
            other._data_ptr = nullptr;
            other._capacity = 0;
            other._size = 0;
            other._in_use = false;
        }
        return *this;
    }

    // Retorna o ponteiro para os dados gerenciados pela Engine.
    Data* data() { return _data_ptr; }
    const Data* data() const { return _data_ptr; }

    // Tamanho atual dos dados *válidos* dentro da memória apontada.
    unsigned int size() const { return _size; }

    // Define o tamanho atual, limitado pela capacidade.
    void setSize(unsigned int newSize) {
        _size = std::min(newSize, _capacity);
    }

    // Capacidade da memória apontada.
    unsigned int capacity() const { return _capacity; }
    // Mantém maxSize por compatibilidade com código existente, mas retorna capacity.
    unsigned int maxSize() const { return _capacity; }

    // --- Métodos para Gerenciamento de Pool (NIC) ---
    bool is_in_use() const { return _in_use; }
    void mark_in_use() { _in_use = true; }
    void mark_free() { _in_use = false; _size = 0; /* Reseta tamanho */ }

private:
    Data* _data_ptr;        // Ponteiro para a memória do frame (alocada pela Engine)
    unsigned int _capacity; // Capacidade da memória apontada por _data_ptr
    unsigned int _size;     // Tamanho atual dos dados válidos
    bool _in_use;           // Flag para gerenciamento no pool da NIC
};

#endif // BUFFER_HH