#include "samplereader.h"
#include <iostream>
#include <vector>
#include <volk/volk.h>

SampleConverter::SampleConverter(std::unique_ptr<SampleReader> reader)
    : reader(std::move(reader)) {}

FileSampleReader::FileSampleReader(FILE *f) : f{f} {}
int FileSampleReader::read(void *arr, int num) {
    return fread(arr, sizeof(uint8_t), num, f);
}

Uint8SampleConverter::Uint8SampleConverter(std::unique_ptr<SampleReader> reader)
    : SampleConverter(std::move(reader)) {}
void Uint8SampleConverter::read(float *arr, int num) {
    // Use the last part of the array as a scratch buffer
    uint8_t *scratch = ((uint8_t *)&arr[num]) - num;
    reader->read(scratch, sizeof(uint8_t) * num);
    // Shift from 0 - 255 to -128 - 127
    for (int i = 0; i < num; i++) {
        scratch[i] ^= 0x80;
    }
    volk_8i_s32f_convert_32f(arr, (int8_t *)scratch, 128, num);
}

Int8SampleConverter::Int8SampleConverter(std::unique_ptr<SampleReader> reader)
    : SampleConverter(std::move(reader)) {}
void Int8SampleConverter::read(float *arr, int num) {
    // Use the last part of the array as a scratch buffer
    int8_t *scratch = ((int8_t *)&arr[num]) - num;
    reader->read(scratch, sizeof(int8_t) * num);
    volk_8i_s32f_convert_32f(arr, scratch, 128, num);
}

Uint16SampleConverter::Uint16SampleConverter(
    std::unique_ptr<SampleReader> reader)
    : SampleConverter(std::move(reader)) {}
void Uint16SampleConverter::read(float *arr, int num) {
    // Use the last part of the array as a scratch buffer
    uint16_t *scratch = ((uint16_t *)&arr[num]) - num;
    reader->read(scratch, sizeof(uint16_t) * num);
    // Shift the ranges from unsigned to signed
    for (int i = 0; i < num; i++) {
        scratch[i] ^= 0x8000;
    }
    volk_16i_s32f_convert_32f(arr, (int16_t *)scratch, 65536, num);
}

Int16SampleConverter::Int16SampleConverter(std::unique_ptr<SampleReader> reader)
    : SampleConverter(std::move(reader)) {}
void Int16SampleConverter::read(float *arr, int num) {
    // Use the last part of the array as a scratch buffer
    int16_t *scratch = ((int16_t *)&arr[num]) - num;
    reader->read(scratch, sizeof(int16_t) * num);
    volk_16i_s32f_convert_32f(arr, scratch, 32768, num);
}

Float32SampleConverter::Float32SampleConverter(
    std::unique_ptr<SampleReader> reader)
    : SampleConverter(std::move(reader)) {}
void Float32SampleConverter::read(float *arr, int num) {
    reader->read(arr, sizeof(float) * num);
}

Float64SampleConverter::Float64SampleConverter(
    std::unique_ptr<SampleReader> reader)
    : SampleConverter(std::move(reader)) {}
void Float64SampleConverter::read(float *arr, int num) {
    std::vector<double> input_arr(num);
    reader->read(input_arr.data(), sizeof(double) * num);
    volk_64f_convert_32f(arr, input_arr.data(), num);
}
