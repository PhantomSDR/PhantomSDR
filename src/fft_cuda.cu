#include <cassert>

#include "fft.h"

cuFFT::cuFFT(size_t size, int nthreads, int downsample_levels)
    : FFT(size, nthreads, downsample_levels), plan{0} {
    int count;
    cudaGetDeviceCount(&count);
    if (!count) {
        throw std::runtime_error("No CUDA devices found");
    }

    cudaMalloc(&cuda_windowbuf, sizeof(float) * size);
    cudaMemcpy(cuda_windowbuf, windowbuf, sizeof(float) * size,
               cudaMemcpyHostToDevice);
    operator delete[](windowbuf, std::align_val_t(32));
    windowbuf = NULL;
}

float *cuFFT::malloc(size_t size) {
    float *ptr;
    cudaError_t err =
        cudaHostAlloc(&ptr, sizeof(float) * size, cudaHostAllocMapped);
    return ptr;
}
void cuFFT::free(float *ptr) { cudaFreeHost(ptr); }

int cuFFT::plan_c2c(direction d, int options) {
    assert(!plan);

    cudaMalloc(&cuda_inbuf, sizeof(float) * size * 2);
    cudaMallocManaged(&outbuf, sizeof(float) * (size + additional_size) * 2);
    cuda_outbuf = outbuf;
    cudaMalloc(&cuda_powerbuf, sizeof(float) * size * 2);
    cudaMallocManaged(&quantizedbuf, sizeof(int8_t) * size * 2);
    cuda_quantizedbuf = quantizedbuf;
    outbuf_len = size;

    type = CUFFT_C2C;
    cufftPlan1d(&plan, size, CUFFT_C2C, 1);
    cuda_direction = d == FORWARD ? CUFFT_FORWARD : CUFFT_INVERSE;
    return 0;
}
int cuFFT::plan_r2c(int options) {
    assert(!plan);

    cudaMalloc(&cuda_inbuf, sizeof(float) * size);
    cudaMallocManaged(&outbuf, sizeof(float) * (size + 2));
    cuda_outbuf = outbuf;
    cudaMalloc(&cuda_powerbuf, sizeof(float) * size * 2);
    cudaMallocManaged(&quantizedbuf, sizeof(int8_t) * size * 2);
    cuda_quantizedbuf = quantizedbuf;
    outbuf_len = size / 2;

    type = CUFFT_R2C;
    cufftPlan1d(&plan, size, CUFFT_R2C, 1);
    return 0;
}

__global__ void window_real(float *output, float *input, float *window,
                            size_t len) {
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;
    for (int i = index; i < len; i += stride)
        output[i] = input[i] * window[i];
}
__global__ void window_complex(float *output, float *input, float *window,
                               size_t len) {
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;
    for (int i = index; i < len; i += stride) {
        output[i * 2] = input[i * 2] * window[i];
        output[i * 2 + 1] = input[i * 2 + 1] * window[i];
    }
}
int cuFFT::load_real_input(float *a1, float *a2) {
    cudaHostGetDevicePointer(&a1, a1, 0);
    cudaHostGetDevicePointer(&a2, a2, 0);
    int blockSize = 1024;
    int numBlocks = (size / 2 + blockSize - 1) / blockSize;
    window_real<<<numBlocks, blockSize>>>(cuda_inbuf, a1, cuda_windowbuf,
                                          size / 2);
    window_real<<<numBlocks, blockSize>>>(&cuda_inbuf[size / 2], a2,
                                          &cuda_windowbuf[size / 2], size / 2);
    return 0;
}
int cuFFT::load_complex_input(float *a1, float *a2) {
    cudaHostGetDevicePointer(&a1, a1, 0);
    cudaHostGetDevicePointer(&a2, a2, 0);
    int blockSize = 1024;
    int numBlocks = (size / 2 + blockSize - 1) / blockSize;
    window_complex<<<numBlocks, blockSize>>>(cuda_inbuf, a1, cuda_windowbuf,
                                             size / 2);
    window_complex<<<numBlocks, blockSize>>>(
        &cuda_inbuf[size], a2, &cuda_windowbuf[size / 2], size / 2);
    return 0;
}

__device__ inline int log_power(float power, int power_offset) {
    return max(-128, __float2int_rz(20 * log10f(power) +
                                    power_offset * 6.020599913279624 + 127));
}
__global__ void power_and_quantize(float *complexbuf, float *powerbuf,
                                   int8_t *quantizedbuf, float normalize,
                                   size_t outbuf_len, int power_offset) {
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;
    for (int i = index; i < outbuf_len; i += stride) {
        complexbuf[i * 2] /= normalize;
        complexbuf[i * 2 + 1] /= normalize;
        float re = complexbuf[i * 2];
        float im = complexbuf[i * 2 + 1];
        float power = re * re + im * im;
        powerbuf[i] = power;
        quantizedbuf[i] = log_power(power, power_offset);
    }
}
__global__ void half_and_quantize(float *powerbuf, float *halfbuf,
                                  int8_t *quantizedbuf, size_t outbuf_len,
                                  int power_offset) {
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;
    for (int i = index; i < outbuf_len; i += stride) {
        float power = powerbuf[i * 2] + powerbuf[i * 2 + 1];
        halfbuf[i] = power;
        quantizedbuf[i] = log_power(power, power_offset);
    }
}

int cuFFT::execute() {
    if (type == CUFFT_C2C) {
        cufftExecC2C(plan, (cufftComplex *)cuda_inbuf,
                     (cufftComplex *)cuda_outbuf, CUFFT_FORWARD);
    } else if (type == CUFFT_R2C) {
        cufftExecR2C(plan, (cufftReal *)cuda_inbuf,
                     (cufftComplex *)cuda_outbuf);
    } else if (type == CUFFT_C2R) {
        cufftExecC2R(plan, (cufftComplex *)cuda_inbuf,
                     (cufftReal *)cuda_outbuf);
    }

    int base_idx = 0;
    bool is_complex = outbuf_len == size;
    // For IQ input, the lowest frequency is in the middle
    if (is_complex) {
        base_idx = size / 2 + 1;
    }
    // outbuf is complex so we need to multiply by 2
    int blockSize = 1024;
    int numBlocks = (outbuf_len - base_idx + blockSize - 1) / blockSize;
    power_and_quantize<<<numBlocks, blockSize>>>(
        &cuda_outbuf[base_idx * 2], cuda_powerbuf, cuda_quantizedbuf, size,
        outbuf_len - base_idx, size_log2);

    numBlocks = (base_idx + blockSize - 1) / blockSize;
    power_and_quantize<<<numBlocks, blockSize>>>(
        cuda_outbuf, &cuda_powerbuf[outbuf_len - base_idx],
        &cuda_quantizedbuf[outbuf_len - base_idx], size, base_idx, size_log2);

    int out_len = outbuf_len;
    int8_t *quantized_offset_buf = cuda_quantizedbuf;
    float *power_offset_buf = cuda_powerbuf;
    for (int i = 0; i < downsample_levels - 1; i++) {
        numBlocks = (out_len / 2 + blockSize - 1) / blockSize;
        half_and_quantize<<<numBlocks, blockSize>>>(
            power_offset_buf, power_offset_buf + out_len,
            quantized_offset_buf + out_len, out_len / 2, size_log2 - i - 1);
        power_offset_buf += out_len;
        quantized_offset_buf += out_len;
        out_len /= 2;
    }

    cudaDeviceSynchronize();
    return 0;
}
cuFFT::~cuFFT() {
    if (plan) {
        cufftDestroy(plan);
        cudaFree(cuda_inbuf);
        cudaFree(cuda_outbuf);
        cudaFree(cuda_windowbuf);
        cudaFree(cuda_powerbuf);
    }
}