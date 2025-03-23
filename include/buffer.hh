#ifndef BUFFER_HH
#define BUFFER_HH

#include <cstring>

// Buffer temporario
class Buffer {
public:

    Buffer();

    ~Buffer();

    unsigned char * data;
    size_t length;
};

#endif