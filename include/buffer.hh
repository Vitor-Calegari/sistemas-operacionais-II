#ifndef BUFFER_HH
#define BUFFER_HH

template<typename Data>
class Buffer: private Data
{
public:

    Buffer() {}

    Buffer(unsigned int size): _size(size) {}

    Data * data() {return this;}

    unsigned int size() {return _size;}

    void setSize(unsigned int newSize) {_size = newSize;};

private:
    unsigned int _size;
};

#endif