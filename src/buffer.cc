#include "buffer.hh"

Buffer::Buffer()
{
    data = new unsigned char[1522];
    memset(data, 0, 1522);
    length = 0;
}

Buffer::~Buffer()
{
    delete data;
}
