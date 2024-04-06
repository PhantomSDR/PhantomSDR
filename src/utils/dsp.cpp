#include "dsp.h"
#include <cstdint>
//#include <immintrin.h>
#include <memory>

void build_hann_window(float *arr, int num) {
    // Use a Hann window
    for (int i = 0; i < num; i++) {
        arr[i] = 0.5 * (1 - cosf(2 * M_PI * i / num));
    }
}

void build_blackman_harris_window(float *arr, int num) {
    const float a0 = 0.35875f;
    const float a1 = 0.48829f;
    const float a2 = 0.14128f;
    const float a3 = 0.01168f;

    for (int i = 0; i < num; i++) {
        arr[i] = a0 - (a1 * cosf((2.0f * M_PI * i) / (num - 1))) +
                 (a2 * cosf((4.0f * M_PI * i) / (num - 1))) -
                 (a3 * cosf((6.0f * M_PI * i) / (num - 1)));
    }
}

//__attribute__((target("default")))
void polar_discriminator_fm(std::complex<float> *buf, std::complex<float> prev,
                            float *output, size_t len) {
    buf = (std::complex<float> *)__builtin_assume_aligned(buf, 64);
    output = (float *)__builtin_assume_aligned(output, 64);
    for (size_t i = 0; i < len; i++) {
        output[i] = std::arg(buf[i] * std::conj(prev));
        prev = buf[i];
    }
}

/* clang-format off */
/*
__attribute__((target("avx2")))
void polar_discriminator_fm(std::complex<float> *buf, std::complex<float> prev,
float* output, size_t len) { for(size_t i = 0;i < len;i += 8) {
        // IQIQIQIQ
        __m256 buf1 = _mm256_load_ps((float*)(buf + i));
        // IQIQIQIQ
        __m256 buf2 = _mm256_load_ps((float*)(buf + i + 4));
        // deinterleave into II II II II
        buf1 = _mm256_shuffle_ps(buf1, buf2, _MM_SHUFFLE(0, 2, 0, 2));
        // deinterleave into QQ QQ QQ QQ
        buf2 = _mm256_shuffle_ps(buf1, buf2, _MM_SHUFFLE(1, 3, 1, 3));
        // shuffle into IIIIIIII
        buf1 = _mm256_permutevar8x32_ps(buf1, _mm256_set_epi32(7, 6, 3, 2, 5, 4, 1, 0));
        // shuffle into QQQQQQQQ
        buf2 = _mm256_permutevar8x32_ps(buf2, _mm256_set_epi32(7, 6, 3, 2, 5, 4, 1, 0));
        // shift right 0IIIIIII
        __m256 buf1_prev = _mm256_permutevar8x32_ps(buf1, _mm256_set_epi32(6, 5, 4, 3, 2, 1, 0, 0));
        // shift right 0QQQQQQQ
        __m256 buf2_prev = _mm256_permutevar8x32_ps(buf2, _mm256_set_epi32(6, 5, 4, 3, 2, 1, 0, 0));
        // replace first element of buf1_prev with prev.real()
        buf1_prev = _mm256_blend_ps(buf1_prev, _mm256_set1_ps(prev.real()), 1);
        // replace first element of buf2_prev with prev.imag()
        buf2_prev = _mm256_blend_ps(buf2_prev, _mm256_set1_ps(prev.imag()), 1);
        // buf1_diff = buf1 * buf1_prev + buf2 * buf2_prev
        __m256 buf1_diff = _mm256_fmadd_ps(buf1, buf1_prev, _mm256_mul_ps(buf2, buf2_prev));
        // buf2_diff = buf2 * buf1_prev - buf1 * buf2_prev
        __m256 buf2_diff = _mm256_fmsub_ps(buf2, buf1_prev, _mm256_mul_ps(buf1, buf2_prev));
        // atan2(buf2_diff, buf1_diff)
        __m256 buf_out = atan2f_approximation(buf2_diff, buf1_diff);
        // store to output
        _mm256_store_ps(output + i, buf_out);
        // store last element of buf1 to prev
        prev = buf[i + 7];
    }
}*/
/* clang-format on */

void dsp_negate_float(float *arr, size_t len) {
    //[[assume(len % (64 / sizeof(float)) == 0)]];
    [[assume(len > 0)]];
    arr = std::assume_aligned<64>(arr);
    for (size_t i = 0; i < len; i++) {
        arr[i] = -arr[i];
    }
}
void dsp_negate_complex(std::complex<float> *arr, size_t len) {
    //[[assume(len % (64 / sizeof(std::complex<float>)) == 0)]];
    [[assume(len > 0)]];
    arr = std::assume_aligned<64>(arr);
    for (size_t i = 0; i < len; i++) {
        arr[i] = -arr[i];
    }
}

void dsp_add_float(float *arr1, float *arr2, size_t len) {
    //[[assume(len % (64 / sizeof(float)) == 0)]];
    [[assume(len > 0)]];
    arr1 = std::assume_aligned<64>(arr1);
    arr2 = std::assume_aligned<64>(arr2);
    for (size_t i = 0; i < len; i++) {
        arr1[i] += arr2[i];
    }
}
void dsp_add_complex(std::complex<float> *arr1, std::complex<float> *arr2,
                     size_t len) {
    //[[assume(len % (64 / sizeof(std::complex<float>)) == 0)]];
    [[assume(len > 0)]];
    arr1 = std::assume_aligned<64>(arr1);
    arr2 = std::assume_aligned<64>(arr2);
    for (size_t i = 0; i < len; i++) {
        arr1[i] += arr2[i];
    }
}



//__attribute__((target("default")))
void dsp_am_demod(std::complex<float> *arr, float *output, size_t len) {
    //[[assume(len % (64 / sizeof(std::complex<float>)) == 0)]];
    [[assume(len > 0)]];
    arr = std::assume_aligned<64>(arr);
    output = std::assume_aligned<64>(output);
    for (size_t i = 0; i < len; i++) {
        float re = arr[i].real();
        float im = arr[i].imag();
        output[i] = std::sqrt(re * re + im * im);
    }
}
/*__attribute__((target("avx2")))
void dsp_am_demod(std::complex<float> *arr, float *output, size_t len) {
    for(size_t i = 0;i < len;i += 8) {
        __m256 arr1 = _mm256_load_ps((float*)(arr + i));
        __m256 arr2 = _mm256_load_ps((float*)(arr + i + 4));
        arr1 = _mm256_mul_ps(arr1, arr1);
        arr2 = _mm256_mul_ps(arr2, arr2);
        __m256 mag = _mm256_hadd_ps(arr1, arr2);
        mag = _mm256_permutevar8x32_ps(mag, _mm256_set_epi32(7, 6, 3, 2, 5, 4,
1, 0)); mag = _mm256_sqrt_ps(mag); _mm256_store_ps(output + i, mag);
    }
}
__attribute__((target("sse4.1")))
void dsp_am_demod(std::complex<float> *arr, float *output, size_t len) {
    for(size_t i = 0;i < len;i += 4) {
        __m128 arr1 = _mm_load_ps((float*)(arr + i));
        __m128 arr2 = _mm_load_ps((float*)(arr + i + 2));
        arr1 = _mm_mul_ps(arr1, arr1);
        arr2 = _mm_mul_ps(arr2, arr2);
        __m128 mag = _mm_hadd_ps(arr1, arr2);
        mag = _mm_sqrt_ps(mag);
        _mm_store_ps(output + i, mag);
    }
}*/

void dsp_float_to_int16(float *arr, int32_t *output, float mult, size_t len) {
    //[[assume(len % (64 / sizeof(float)) == 0)]];
    [[assume(len > 0)]];
    arr = std::assume_aligned<64>(arr);
    output = std::assume_aligned<64>(output);
    const int32_t minimum = -32768;
    const int32_t maximum = 32767;

    for (size_t i = 0; i < len; i++) {
        output[i] = (int32_t)(arr[i] * mult + 32768.5f) - 32768;
        output[i] =
            std::max(std::min(output[i], maximum), minimum);
    }
}
