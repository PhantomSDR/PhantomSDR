#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <sstream>

#include "compression.h"
std::string Gzip::compress(const std::string &data) {
    std::stringstream compressed;
    std::stringstream origin(data);

    boost::iostreams::filtering_streambuf<boost::iostreams::input> out;
    out.push(boost::iostreams::gzip_compressor(boost::iostreams::gzip_params(
        boost::iostreams::gzip::best_compression)));
    out.push(origin);
    boost::iostreams::copy(out, compressed);

    return compressed.str();
}

std::string Gzip::decompress(const std::string &data) {
    std::stringstream compressed(data);
    std::stringstream decompressed;

    boost::iostreams::filtering_streambuf<boost::iostreams::input> out;
    out.push(boost::iostreams::gzip_decompressor());
    out.push(compressed);
    boost::iostreams::copy(out, decompressed);

    return decompressed.str();
}