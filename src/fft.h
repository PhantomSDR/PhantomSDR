#ifndef FFT_H
#define FFT_H

#include <mutex>

#ifdef CUFFT
#include <cufft.h>
#endif

#ifdef CLFFT
#include <clFFT.h>
#endif

#include <fftw3.h>

// Global lock for FFTW planner
extern std::mutex fftwf_planner_mutex;

enum fft_accelerator {
    CPU_FFTW,
    GPU_cuFFT,
    GPU_cuFFTZeroCopy,
    GPU_cuFFTUnifiedMemory,
    GPU_clFFT,
};

class FFT {
  public:
    enum direction { FORWARD, BACKWARD };
    FFT(size_t size, int nthreads);
    virtual float *malloc(size_t size) = 0;
    virtual void free(float *buf) = 0;
    virtual int plan_c2c(direction d, int options) = 0;
    virtual int plan_r2c(int options) = 0;
    virtual int plan_c2r(int options) = 0;
    virtual void set_output_additional_size(size_t size);
    virtual void set_size(size_t size);
    virtual float *get_input_buffer();
    virtual float *get_output_buffer();
    virtual int execute() = 0;
    virtual ~FFT() {}

  protected:
    size_t size;
    int nthreads;
    int additional_size;
    float *inbuf;
    float *outbuf;
};

class noFFT : public FFT {
  public:
    noFFT(size_t size, int nthreads) : FFT(size, nthreads) {}
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
    virtual int plan_c2r(int) {
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

class errorFFT : public noFFT {
  public:
    errorFFT(std::string error) : noFFT(0, 0) { throw error; }
    ~errorFFT() {}
};

class FFTW : public FFT {
  public:
    FFTW(size_t size, int nthreads);
    virtual float *malloc(size_t size);
    virtual void free(float *buf);
    virtual int plan_c2c(direction d, int options);
    virtual int plan_r2c(int options);
    virtual int plan_c2r(int options);
    virtual int execute();
    virtual ~FFTW();

  protected:
    fftwf_plan p;
};

#ifdef CUFFT
class cuFFT : public FFT {
  public:
    cuFFT(size_t size, int nthreads);
    virtual float *malloc(size_t size);
    virtual void free(float *buf);
    virtual int plan_c2c(direction d, int options);
    virtual int plan_r2c(int options);
    virtual int plan_c2r(int options);
    virtual int get_cuda_ptrs();
    virtual int execute();
    virtual ~cuFFT();

  protected:
    cufftType type;
    int cuda_direction;
    cufftHandle plan;
    bool has_cuda_malloc;
    void *cuda_inbuf;
    void *cuda_outbuf;
};

class cuFFTUnifiedMemory : public cuFFT {
  public:
    cuFFTUnifiedMemory(size_t size, int nthreads);
    virtual float *malloc(size_t size);
    virtual void free(float *buf);
    virtual int get_cuda_ptrs();
    virtual int execute();
    virtual ~cuFFTUnifiedMemory();
};

class cuFFTZeroCopy : public cuFFT {
  public:
    cuFFTZeroCopy(size_t size, int nthreads);
    virtual float *malloc(size_t size);
    virtual void free(float *buf);
    virtual int get_cuda_ptrs();
    virtual int execute();
    virtual ~cuFFTZeroCopy();
};
#endif

#ifdef CLFFT
class clFFT : public FFT {
  public:
    clFFT(size_t size, int nthreads);
    virtual float *malloc(size_t size);
    virtual void free(float *buf);
    virtual int plan_c2c(direction d, int options);
    virtual int plan_r2c(int options);
    virtual int plan_c2r(int options);
    virtual int execute();
    virtual ~clFFT();

  protected:
    cl_int err;
    cl_platform_id platform;
    cl_device_id device;
    cl_context_properties props[3];
    cl_context ctx;
    cl_command_queue queue;
    float *X;
    cl_event event = NULL;

    clfftPlanHandle planHandle;
    clfftDim dim;
    size_t clLengths[1];
    clfftSetupData fftSetup;

    clfftLayout inlayout;
    clfftLayout outlayout;
    cl_mem cl_inbuf;
    cl_mem cl_outbuf;
    clfftDirection d;
};
#endif

#endif