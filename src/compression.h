#ifndef COMPRESSION_H
#define COMPRESSION_H

#include <string>

class Gzip {
  public:
    static std::string compress(const std::string &data);
    static std::string decompress(const std::string &data);
};
#endif