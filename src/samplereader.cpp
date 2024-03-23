#include "samplereader.h"
#include "utils.h"

#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

FileSampleReader::FileSampleReader(FILE *f) : f{f} {}
int FileSampleReader::read(void *arr, int num) {
    return fread_unlocked(arr, sizeof(uint8_t), num, f);
}

SampleConverterBase::SampleConverterBase(std::unique_ptr<SampleReader> reader)
    : reader(std::move(reader)) {}

template <typename T>
SampleConverter<T>::SampleConverter(std::unique_ptr<SampleReader> reader)
    : SampleConverterBase(std::move(reader)) {}

template <typename T> struct type_ {
    using type = T;
};

template <typename T, typename T_signed> void convert(float *arr, T_signed *scratch, float scale, size_t num) {
    arr = std::assume_aligned<64>(arr);
    scratch = std::assume_aligned<64>(scratch);
    //[[assume(num % (64 / sizeof(T)) == 0)]];
    [[assume(num > 0)]];
    for (size_t i = 0; i < num; i++) {
        if constexpr (std::is_unsigned<T>::value) {
            scratch[i] = scratch[i] ^ ((T)1 << (sizeof(T) * 8 - 1));
        }
        arr[i] = ((float)scratch[i]) / scale;
    }
}

template <typename T> void SampleConverter<T>::read(float *arr, int num) {
    // Use the last part of the array as a scratch buffer
    T *scratch;
    if constexpr (sizeof(T) > sizeof(float)) {
        scratch = new T[num];
    } else {
        scratch = ((T *)&arr[num]) - num;
    }
    reader->read(scratch, sizeof(T) * num);
    auto constexpr static unsigned_type = []() {
        if constexpr (std::is_unsigned<T>::value) {
            return type_<typename std::make_signed<T>::type>{};
        } else {
            return type_<T>{};
        }
    };
    typedef typename decltype(unsigned_type())::type T_signed;
    constexpr float scale = []() {
        if constexpr (std::is_integral<T>::value) {
            return (float)std::numeric_limits<T_signed>::max() + 1.;
        } else {
            return 1.;
        }
    }();
    convert<T, T_signed>(arr, (T_signed*)scratch, scale, num);
    if constexpr (sizeof(T) > sizeof(float)) {
        delete[] scratch;
    }
}

template class SampleConverter<uint8_t>;
template class SampleConverter<int8_t>;
template class SampleConverter<uint16_t>;
template class SampleConverter<int16_t>;
template class SampleConverter<uint32_t>;
template class SampleConverter<int32_t>;
template class SampleConverter<uint64_t>;
template class SampleConverter<int64_t>;
template class SampleConverter<float>;
template class SampleConverter<double>;