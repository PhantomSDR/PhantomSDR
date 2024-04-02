#ifndef FFT_H
#define FFT_H

#include <functional>
#include <mutex>

#ifdef CUFFT
#include <cufft.h>
#endif

#ifdef CLFFT
#define CL_HPP_TARGET_OPENCL_VERSION 300
#include <CL/opencl.hpp>
#include <clFFT.h>
#endif

#ifdef MKL
#include "mkl_dfti.h"
#endif

#include <fftw3.h>

// Global lock for FFTW planner
extern std::mutex fftwf_planner_mutex;

enum fft_accelerator {
    CPU_FFTW,
    GPU_cuFFT,
    GPU_clFFT,
    CPU_mklFFT,
};

class FFT {
  public:
    enum direction { FORWARD, BACKWARD };
    FFT(size_t size, int nthreads, int downsample_levels, int brightness_offset);
    virtual float *malloc(size_t size) = 0;
    virtual void free(float *buf) = 0;
    virtual int plan_c2c(direction d, int options) = 0;
    virtual int plan_r2c(int options) = 0;
    virtual void set_output_additional_size(size_t size);
    virtual void set_size(size_t size);
    virtual float *get_input_buffer();
    virtual float *get_output_buffer();
    virtual int8_t *get_quantized_buffer();
    virtual int load_real_input(float *a1, float *a2) = 0;
    virtual int load_complex_input(float *a1, float *a2) = 0;
    virtual int execute() = 0;
    virtual ~FFT();

  protected:
    size_t size;
    int size_log2;
    int nthreads;
    int downsample_levels;
    int additional_size;
    size_t outbuf_len;
    float *windowbuf;
    float *inbuf;
    float *outbuf;
    float *powerbuf;
    int8_t *quantizedbuf;
};

class noFFT : public FFT {
  public:
    noFFT(size_t size, int nthreads) : FFT(size, nthreads, 1, 0) {}
    virtual float *malloc(size_t size) {
        return (float *)::malloc(sizeof(float) * size);
    }
    virtual void free(float *buf) { ::free(buf); }
    virtual int plan_c2c(direction, int) {
        inbuf = this->malloc(size * 2);
        outbuf = this->malloc(size * 4);
        return 0;
    }
    virtual int plan_r2c(int) {
        inbuf = this->malloc(size * 2);
        outbuf = this->malloc(size * 4);
        return 0;
    }
    virtual int execute() { return 0; }
    ~noFFT() {
        if (inbuf) {
            this->free(inbuf);
        }
        if (outbuf) {
            this->free(outbuf);
        }
    }
};

class FFTW : public FFT {
  public:
    FFTW(size_t size, int nthreads, int downsample_levels, int brightness_offset);
    virtual float *malloc(size_t size);
    virtual void free(float *buf);
    virtual int plan_c2c(direction d, int options);
    virtual int plan_r2c(int options);
    virtual int load_real_input(float *a1, float *a2);
    virtual int load_complex_input(float *a1, float *a2);
    virtual int execute();
    virtual ~FFTW();

  protected:
    fftwf_plan p;
};

#ifdef MKL
class mklFFT : public FFT {
  public:
    mklFFT(size_t size, int nthreads, int downsample_levels, int brightness_offset);
    virtual float *malloc(size_t size);
    virtual void free(float *buf);
    virtual int plan_c2c(direction d, int options);
    virtual int plan_r2c(int options);
    virtual int load_real_input(float *a1, float *a2);
    virtual int load_complex_input(float *a1, float *a2);
    virtual int execute();
    virtual ~mklFFT();

  protected:
    DFTI_DESCRIPTOR_HANDLE descriptor;
};
#endif

#ifdef CUFFT
class cuFFT : public FFT {
  public:
    cuFFT(size_t size, int nthreads, int downsample_levels, int brightness_offset);
    virtual float *malloc(size_t size);
    virtual void free(float *buf);
    virtual int plan_c2c(direction d, int options);
    virtual int plan_r2c(int options);
    virtual int load_real_input(float *a1, float *a2);
    virtual int load_complex_input(float *a1, float *a2);
    virtual int execute();
    virtual ~cuFFT();

  protected:
    cufftType type;
    int cuda_direction;
    cufftHandle plan;
    bool has_cuda_malloc;
    float *cuda_windowbuf;
    float *cuda_inbuf;
    float *cuda_outbuf;
    float *cuda_powerbuf;
    int8_t *cuda_quantizedbuf;
};
#endif

#ifdef CLFFT
class clFFT : public FFT {
  public:
    clFFT(size_t size, int nthreads, int downsample_levels, int brightness_offset);
    virtual float *malloc(size_t size);
    virtual void free(float *buf);
    virtual int plan_c2c(direction d, int options);
    virtual int plan_r2c(int options);
    virtual int load_real_input(float *a1, float *a2);
    virtual int load_complex_input(float *a1, float *a2);
    virtual int execute();
    virtual ~clFFT();

  protected:
    cl::Platform platform;
    cl::Device device;
    cl::Context context;
    cl::CommandQueue queue;

    cl::Program::Sources sources;
    cl::Program program;

    cl::KernelFunctor<cl::Buffer &, cl_int, cl::Buffer &, cl::Buffer &>
        window_real;
    cl::KernelFunctor<cl::Buffer &, cl_int, cl::Buffer &, cl::Buffer &>
        window_complex;
    cl::KernelFunctor<cl::Buffer &, cl::Buffer &, cl::Buffer &, cl_float,
                      cl_int, cl_int, cl_int>
        power_and_quantize;
    cl::KernelFunctor<cl::Buffer &, cl::Buffer &, cl::Buffer &, cl_int, cl_int,
                      cl_int>
        half_and_quantize;

    clfftPlanHandle planHandle;
    clfftDim dim;
    size_t clLengths[1];
    clfftSetupData fftSetup;

    clfftLayout inlayout;
    clfftLayout outlayout;
    std::unordered_map<float *, cl::Buffer> buffers;
    cl::Buffer cl_windowbuf;
    cl::Buffer cl_inbuf;
    cl::Buffer cl_outbuf;
    cl::Buffer cl_powerbuf;
    cl::Buffer cl_quantizedbuf;
    clfftDirection d;
};
#endif

#endif