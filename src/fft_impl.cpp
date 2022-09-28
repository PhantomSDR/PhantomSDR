#include <cassert>
#include <cstdlib>
#include <iostream>
#include <mutex>

#ifdef CUFFT
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <cufft.h>
#endif

#include "fft.h"

std::mutex fftwf_planner_mutex;

FFT::FFT(size_t size, int nthreads)
    : size{size}, nthreads{nthreads}, inbuf{0}, outbuf{0} {}

void FFT::set_size(size_t size) { this->size = size; }
void FFT::set_output_additional_size(size_t size) { additional_size = size; }
float *FFT::get_input_buffer() { return inbuf; }

float *FFT::get_output_buffer() { return outbuf; }

FFTW::FFTW(size_t size, int nthreads) : FFT(size, nthreads), p{0} {}

float *FFTW::malloc(size_t size) {
    return (float *)fftwf_malloc(sizeof(float) * size);
}

void FFTW::free(float *buf) { fftwf_free(buf); }

int FFTW::plan_c2c(direction d, int options) {
    assert(!p);

    inbuf = this->malloc(size * 2);
    outbuf = this->malloc(size * 4);

    std::scoped_lock lk(fftwf_planner_mutex);
    fftwf_plan_with_nthreads(nthreads);
    p = fftwf_plan_dft_1d(size, (fftwf_complex *)inbuf, (fftwf_complex *)outbuf,
                          d == FORWARD ? FFTW_FORWARD : FFTW_BACKWARD, options);
    return 0;
}
int FFTW::plan_r2c(int options) {
    assert(!p);

    inbuf = this->malloc(size * 2);
    outbuf = this->malloc(size * 4);

    std::scoped_lock lk(fftwf_planner_mutex);
    fftwf_plan_with_nthreads(nthreads);
    p = fftwf_plan_dft_r2c_1d(size, inbuf, (fftwf_complex *)outbuf, options);
    return 0;
}
int FFTW::plan_c2r(int options) {
    assert(!p);

    inbuf = this->malloc(size * 2);
    outbuf = this->malloc(size * 2);

    std::scoped_lock lk(fftwf_planner_mutex);
    fftwf_plan_with_nthreads(nthreads);
    p = fftwf_plan_dft_c2r_1d(size, (fftwf_complex *)inbuf, outbuf, options);
    return 0;
}
int FFTW::execute() {
    fftwf_execute(p);
    return 0;
}
FFTW::~FFTW() {
    if (p) {
        fftwf_destroy_plan(p);
    }
    if (inbuf) {
        this->free(inbuf);
    }
    if (outbuf) {
        this->free(outbuf);
    }
}

#ifdef CUFFT
cuFFT::cuFFT(size_t size, int nthreads) : FFT(size, nthreads), plan{0} {}

float* cuFFT::malloc(size_t size) {
    return new float[size];
}
void cuFFT::free(float* ptr) {
    delete[] ptr;
}
int cuFFT::get_cuda_ptrs() {
    cudaMalloc(&cuda_inbuf, sizeof(float) * size * 2);
    cudaMalloc(&cuda_outbuf, sizeof(float) * size * 4);
    has_cuda_malloc = true;
    return 0;
}
int cuFFT::plan_c2c(direction d, int options) {
    assert(!plan);

    inbuf = this->malloc(size * 2);
    outbuf = this->malloc(size * 4);
    get_cuda_ptrs();

    type = CUFFT_C2C;
    cufftPlan1d(&plan, size, CUFFT_C2C, 1);
    cuda_direction = d == FORWARD ? CUFFT_FORWARD : CUFFT_INVERSE;
    return 0;
}
int cuFFT::plan_r2c(int options) {
    assert(!plan);

    inbuf = this->malloc(size * 2);
    outbuf = this->malloc(size * 4);
    get_cuda_ptrs();

    type = CUFFT_R2C;
    cufftPlan1d(&plan, size, CUFFT_R2C, 1);
    return 0;
}

int cuFFT::plan_c2r(int options) {
    assert(!plan);

    inbuf = this->malloc(size * 2);
    outbuf = this->malloc(size * 2);
    get_cuda_ptrs();

    type = CUFFT_C2R;
    cufftPlan1d(&plan, size, CUFFT_C2R, 1);
    return 0;
}
int cuFFT::execute() {
    cudaMemcpy(cuda_inbuf, inbuf, sizeof(float) * size * 2, cudaMemcpyHostToDevice);
    if (type == CUFFT_C2C) {
        cufftExecC2C(plan, (cufftComplex *)cuda_inbuf,
                     (cufftComplex *)cuda_outbuf, cuda_direction);
    } else if (type == CUFFT_R2C) {
        cufftExecR2C(plan, (cufftReal *)cuda_inbuf,
                     (cufftComplex *)cuda_outbuf);
    } else if (type == CUFFT_C2R) {
        cufftExecC2R(plan, (cufftComplex *)cuda_inbuf,
                     (cufftReal *)cuda_outbuf);
    }
    cudaMemcpy(outbuf, cuda_outbuf, sizeof(float) * size * 4, cudaMemcpyDeviceToHost);
    return 0;
}
cuFFT::~cuFFT() {
    if (plan) {
        cufftDestroy(plan);
    }
    if (has_cuda_malloc) {
        cudaFree(cuda_inbuf);
        cudaFree(cuda_outbuf);
    }
    if (inbuf) {
        this->free(inbuf);
    }
    if (outbuf) {
        this->free(outbuf);
    }
}

cuFFTUnifiedMemory::cuFFTUnifiedMemory(size_t size, int nthreads)
    : cuFFT(size, nthreads) {}

float *cuFFTUnifiedMemory::malloc(size_t size) {
    float *ptr;
    cudaMallocManaged((void **)&ptr, sizeof(float) * size);
    return ptr;
}

void cuFFTUnifiedMemory::free(float *buf) { cudaFree(buf); }

int cuFFTUnifiedMemory::get_cuda_ptrs() { return 0; }
int cuFFTUnifiedMemory::execute() {
    cudaMemPrefetchAsync(inbuf, size * sizeof(float) * 2, 0, NULL);
    cudaMemPrefetchAsync(outbuf, size * sizeof(float) * 2, 0, NULL);
    if (type == CUFFT_C2C) {
        cufftExecC2C(plan, (cufftComplex *)inbuf, (cufftComplex *)outbuf,
                     cuda_direction);
    } else if (type == CUFFT_R2C) {
        cufftExecR2C(plan, (cufftReal *)inbuf, (cufftComplex *)outbuf);
    } else if (type == CUFFT_C2R) {
        cufftExecC2R(plan, (cufftComplex *)inbuf, (cufftReal *)outbuf);
    }
    cudaDeviceSynchronize();
    return 0;
}
cuFFTUnifiedMemory::~cuFFTUnifiedMemory() {
    if (inbuf) {
        this->free(inbuf);
    }
    if (outbuf) {
        this->free(outbuf);
    }
}
cuFFTZeroCopy::cuFFTZeroCopy(size_t size, int nthreads)
    : cuFFT(size, nthreads) {}

float *cuFFTZeroCopy::malloc(size_t size) {
    float *ptr;
    cudaHostAlloc((void **)&ptr, sizeof(float) * size, cudaHostAllocMapped);
    return ptr;
}

void cuFFTZeroCopy::free(float *buf) { cudaFreeHost(buf); }

int cuFFTZeroCopy::get_cuda_ptrs() {
    cudaHostGetDevicePointer(&cuda_inbuf, inbuf, 0);
    cudaHostGetDevicePointer(&cuda_outbuf, outbuf, 0);
    return 0;
}
int cuFFTZeroCopy::execute() {
    if (type == CUFFT_C2C) {
        cufftExecC2C(plan, (cufftComplex *)cuda_inbuf,
                     (cufftComplex *)cuda_outbuf, cuda_direction);
    } else if (type == CUFFT_R2C) {
        cufftExecR2C(plan, (cufftReal *)cuda_inbuf,
                     (cufftComplex *)cuda_outbuf);
    } else if (type == CUFFT_C2R) {
        cufftExecC2R(plan, (cufftComplex *)cuda_inbuf,
                     (cufftReal *)cuda_outbuf);
    }
    cudaDeviceSynchronize();
    return 0;
}
cuFFTZeroCopy::~cuFFTZeroCopy() {
    if (inbuf) {
        this->free(inbuf);
    }
    if (outbuf) {
        this->free(outbuf);
    }
}
#endif

#ifdef CLFFT
clFFT::clFFT(size_t size, int nthreads)
    : FFT(size, nthreads), platform{0}, device{0}, props{CL_CONTEXT_PLATFORM, 0,
                                                         0},
      ctx{0}, queue{0}, event{NULL}, dim{CLFFT_1D}, clLengths{size} {
    int err;

    err = clGetPlatformIDs(1, &platform, NULL);
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);

    size_t valueSize;
    clGetDeviceInfo(device, CL_DEVICE_NAME, 0, NULL, &valueSize);
    char *value = new char[valueSize];
    clGetDeviceInfo(device, CL_DEVICE_NAME, valueSize, value, NULL);
    std::cout << "Using device: " << value << std::endl;
    delete[] value;

    props[1] = (cl_context_properties)platform;
    ctx = clCreateContext(props, 1, &device, NULL, NULL, &err);
    queue = clCreateCommandQueue(ctx, device, 0, &err);

    err = clfftInitSetupData(&fftSetup);
    err = clfftSetup(&fftSetup);
}

float *clFFT::malloc(size_t size) {
    return new (std::align_val_t(32)) float[size];
}
void clFFT::free(float *buf) { delete[] buf; }
int clFFT::plan_c2c(direction d, int options) {
    int err;
    inbuf = this->malloc(size * 2);
    outbuf = this->malloc(size * 4);

    cl_inbuf = clCreateBuffer(ctx, CL_MEM_READ_WRITE, size * 2 * sizeof(*inbuf),
                              NULL, &err);
    cl_outbuf = clCreateBuffer(ctx, CL_MEM_READ_WRITE,
                               size * 2 * sizeof(*inbuf), NULL, &err);

    this->d = d == FORWARD ? CLFFT_FORWARD : CLFFT_BACKWARD;
    inlayout = CLFFT_COMPLEX_INTERLEAVED;
    outlayout = CLFFT_COMPLEX_INTERLEAVED;
    err = clfftCreateDefaultPlan(&planHandle, ctx, dim, clLengths);
    err = clfftSetPlanPrecision(planHandle, CLFFT_SINGLE);
    err = clfftSetLayout(planHandle, CLFFT_COMPLEX_INTERLEAVED,
                         CLFFT_COMPLEX_INTERLEAVED);
    err = clfftSetResultLocation(planHandle, CLFFT_OUTOFPLACE);
    err = clfftBakePlan(planHandle, 1, &queue, NULL, NULL);
    return err;
}
int clFFT::plan_r2c(int options) {
    int err;
    inbuf = this->malloc(size);
    outbuf = this->malloc(size * 2);

    cl_inbuf = clCreateBuffer(ctx, CL_MEM_READ_WRITE, size * sizeof(*inbuf),
                              NULL, &err);
    cl_outbuf = clCreateBuffer(ctx, CL_MEM_READ_WRITE,
                               size * 2 * sizeof(*inbuf), NULL, &err);

    inlayout = CLFFT_REAL;
    outlayout = CLFFT_HERMITIAN_INTERLEAVED;
    err = clfftCreateDefaultPlan(&planHandle, ctx, dim, clLengths);
    err = clfftSetPlanPrecision(planHandle, CLFFT_SINGLE);
    err = clfftSetLayout(planHandle, CLFFT_REAL, CLFFT_HERMITIAN_INTERLEAVED);
    err = clfftSetResultLocation(planHandle, CLFFT_OUTOFPLACE);
    err = clfftBakePlan(planHandle, 1, &queue, NULL, NULL);
    return err;
}
int clFFT::plan_c2r(int options) {
    int err;
    inbuf = this->malloc(size * 2);
    outbuf = this->malloc(size);

    cl_inbuf = clCreateBuffer(ctx, CL_MEM_READ_WRITE, size * 2 * sizeof(*inbuf),
                              NULL, &err);
    cl_outbuf = clCreateBuffer(ctx, CL_MEM_READ_WRITE, size * sizeof(*inbuf),
                               NULL, &err);

    inlayout = CLFFT_HERMITIAN_INTERLEAVED;
    outlayout = CLFFT_REAL;
    err = clfftCreateDefaultPlan(&planHandle, ctx, dim, clLengths);
    err = clfftSetPlanPrecision(planHandle, CLFFT_SINGLE);
    err = clfftSetLayout(planHandle, CLFFT_HERMITIAN_INTERLEAVED, CLFFT_REAL);
    err = clfftSetResultLocation(planHandle, CLFFT_OUTOFPLACE);
    err = clfftBakePlan(planHandle, 1, &queue, NULL, NULL);
    return err;
}

int clFFT::execute() {
    int err;
    /* Execute the plan. */
    err = clEnqueueWriteBuffer(queue, cl_inbuf, CL_TRUE, 0,
                               size * (inlayout == CLFFT_REAL ? 1 : 2) *
                                   sizeof(*inbuf),
                               inbuf, 0, NULL, NULL);
    err = clfftEnqueueTransform(planHandle, d, 1, &queue, 0, NULL, NULL,
                                &cl_inbuf, &cl_outbuf, NULL);

    /* Wait for calculations to be finished. */
    err = clFinish(queue);

    err =
        clEnqueueReadBuffer(queue, cl_outbuf, CL_TRUE, 0,
                            size * 2 * sizeof(*outbuf), outbuf, 0, NULL, NULL);
    return err;
}

clFFT::~clFFT() {
    if (inbuf) {
        this->free(inbuf);
    }
    if (outbuf) {
        this->free(outbuf);
    }
    /* Release the plan. */
    clfftDestroyPlan(&planHandle);

    clReleaseMemObject(cl_inbuf);
    clReleaseMemObject(cl_outbuf);
    /* Release clFFT library. */
    clfftTeardown();

    /* Release OpenCL working objects. */
    clReleaseCommandQueue(queue);
    clReleaseContext(ctx);
}
#endif