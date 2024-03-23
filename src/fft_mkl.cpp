#include "fft.h"

mklFFT::mklFFT(size_t size, int nthreads, int downsample_levels)
    : FFT(size, nthreads, downsample_levels) {}

float *mklFFT::malloc(size_t size) {
    return (float *)fftwf_malloc(sizeof(float) * size);
}

void mklFFT::free(float *buf) { fftwf_free(buf); }

int mklFFT::plan_c2c(direction d, int options) {

    inbuf = this->malloc(size * 2);
    outbuf = this->malloc(size * 2 + additional_size * 2);
    outbuf_len = size;
    powerbuf = new (std::align_val_t(32)) float[size * 2];
    quantizedbuf = new (std::align_val_t(32)) int8_t[size * 2];

    DftiCreateDescriptor(&descriptor, DFTI_SINGLE, DFTI_COMPLEX, 1,
                         size);       // Specify size and precision
    DftiSetValue(descriptor, DFTI_PLACEMENT,
                 DFTI_NOT_INPLACE);   // Out of place FFT
    DftiCommitDescriptor(descriptor); // Finalize the descriptor

    return 0;
}
int mklFFT::plan_r2c(int options) {

    inbuf = this->malloc(size);
    outbuf = this->malloc(size + 2);
    outbuf_len = size / 2;
    powerbuf = new (std::align_val_t(32)) float[size];
    quantizedbuf = new (std::align_val_t(32)) int8_t[size];

    DftiCreateDescriptor(&descriptor, DFTI_SINGLE, DFTI_REAL, 1,
                         size);       // Specify size and precision
    DftiSetValue(descriptor, DFTI_PLACEMENT,
                 DFTI_NOT_INPLACE);   // Out of place FFT
    DftiCommitDescriptor(descriptor); // Finalize the descriptor

    return 0;
}
int mklFFT::load_real_input(float *a1, float *a2) {
    volk_32f_x2_multiply_32f(inbuf, a1, windowbuf, size / 2);
    volk_32f_x2_multiply_32f(&inbuf[size / 2], a2, &windowbuf[size / 2],
                             size / 2);
    return 0;
}
int mklFFT::load_complex_input(float *a1, float *a2) {
    volk_32fc_32f_multiply_32fc((lv_32fc_t *)inbuf, (lv_32fc_t *)a1, windowbuf,
                                size / 2);
    volk_32fc_32f_multiply_32fc((lv_32fc_t *)&inbuf[size], (lv_32fc_t *)a2,
                                &windowbuf[size / 2], size / 2);
    return 0;
}
int mklFFT::execute() {
    DftiComputeForward(descriptor, inbuf, outbuf); // Compute the Forward FFT
    // Calculate the waterfall buffers

    int base_idx = 0;
    bool is_real = outbuf_len == size / 2;
    // For IQ input, the lowest frequency is in the middle
    if (!is_real) {
        base_idx = size / 2 + 1;
    }
    // outbuf is complex so we need to multiply by 2
    // Also normalize the power by the number of bins
    power_and_quantize(&outbuf[base_idx * 2], powerbuf, quantizedbuf, size,
                       outbuf_len - base_idx, size_log2);
    power_and_quantize(outbuf, &powerbuf[outbuf_len - base_idx],
                       &quantizedbuf[outbuf_len - base_idx], size, base_idx,
                       size_log2);

    int out_len = outbuf_len;
    int8_t *quantized_offset_buf = quantizedbuf;
    float *power_offset_buf = powerbuf;
    for (int i = 0; i < downsample_levels - 1; i++) {
        half_and_quantize(power_offset_buf, power_offset_buf + out_len,
                          quantized_offset_buf + out_len, out_len / 2,
                          size_log2 - i - 1);
        power_offset_buf += out_len;
        quantized_offset_buf += out_len;
        out_len /= 2;
    }
    return 0;
}
mklFFT::~mklFFT() {
    DftiFreeDescriptor(&descriptor); // Free the descriptor
    this->free(inbuf);
    this->free(outbuf);
    operator delete[](powerbuf, std::align_val_t(32));
    operator delete[](quantizedbuf, std::align_val_t(32));
}