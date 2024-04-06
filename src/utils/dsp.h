#ifndef DSP_H
#define DSP_H
#define _USE_MATH_DEFINES
#include <math.h>
#include <complex>
#include <cstddef>
#include <cstdint>

void build_hann_window(float *arr, int num);
void build_blackman_harris_window(float *arr, int num);
void polar_discriminator_fm(std::complex<float> *buf, std::complex<float> prev,
                            float *output, size_t len);

void dsp_negate_float(float *arr, size_t len);
void dsp_negate_complex(std::complex<float> *arr, size_t len);
void dsp_add_float(float *arr1, float *arr2, size_t len);
void dsp_add_complex(std::complex<float> *arr1, std::complex<float> *arr2,
                     size_t len);
void dsp_am_demod(std::complex<float> *arr, float *output, size_t len);
void dsp_float_to_int16(float *arr, int32_t *output, float mult, size_t len);

#endif
