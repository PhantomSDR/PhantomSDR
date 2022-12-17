#include <cassert>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>

#include "fft.h"
#include "utils.h"

#include <volk/volk.h>

std::mutex fftwf_planner_mutex;

static inline float vec_log2(float val) {
    uint32_t *bit_exponent = (uint32_t *)&val;
    float log_val = (float)((int)((*bit_exponent >> 23) & 0xFF) - 128);
    // Set exponent to 0
    *bit_exponent &= ~(255 << 23);
    *bit_exponent += 127 << 23;
    log_val += ((-0.34484843f) * val + 2.02466578f) * val - 0.67487759f;
    return log_val;
}
static inline void power_and_quantize(float *complexbuf, float *powerbuf,
                                      int8_t *quantizedbuf, float normalize,
                                      size_t outbuf_len) {
    /*complexbuf = (float *)__builtin_assume_aligned(complexbuf, 32);
    powerbuf = (float *)__builtin_assume_aligned(powerbuf, 32);
    quantizedbuf = (int8_t *)__builtin_assume_aligned(quantizedbuf, 32);
    // Get the compiler to auto-vectorize*
    outbuf_len -= outbuf_len & 15;*/
    for (size_t i = 0; i < outbuf_len; i++) {
        complexbuf[i * 2] /= normalize;
        complexbuf[i * 2 + 1] /= normalize;
        float re = complexbuf[i * 2];
        float im = complexbuf[i * 2 + 1];
        float power = re * re + im * im;
        powerbuf[i] = power;
        quantizedbuf[i] =
            std::max(-128., vec_log2(power) * 0.3010299956639812 * 20 + 127);
    }
}
static inline void half_and_quantize(float *powerbuf, float *halfbuf,
                                     int8_t *quantizedbuf, size_t outbuf_len) {
    powerbuf = (float *)__builtin_assume_aligned(powerbuf, 32);
    halfbuf = (float *)__builtin_assume_aligned(halfbuf, 32);
    quantizedbuf = (int8_t *)__builtin_assume_aligned(quantizedbuf, 32);
    // Get the compiler to auto-vectorize
    outbuf_len -= outbuf_len & 15;
    for (size_t i = 0; i < outbuf_len; i++) {
        float power = powerbuf[i * 2] + powerbuf[i * 2 + 1];
        halfbuf[i] = power;
        quantizedbuf[i] =
            std::max(-128., vec_log2(power) * 0.3010299956639812 * 20 + 127);
    }
}

FFT::FFT(size_t size, int nthreads, int downsample_levels)
    : size{size}, nthreads{nthreads},
      downsample_levels{downsample_levels}, inbuf{0}, outbuf{0} {
    windowbuf = new (std::align_val_t(32)) float[size];
    build_hann_window(windowbuf, size);
}
FFT::~FFT() { operator delete[](windowbuf, std::align_val_t(32)); }

void FFT::set_size(size_t size) { this->size = size; }
void FFT::set_output_additional_size(size_t size) { additional_size = size; }

float *FFT::get_input_buffer() { return inbuf; }
float *FFT::get_output_buffer() { return outbuf; }
int8_t *FFT::get_quantized_buffer() { return quantizedbuf; }

FFTW::FFTW(size_t size, int nthreads, int downsample_levels)
    : FFT(size, nthreads, downsample_levels), p{0} {}

float *FFTW::malloc(size_t size) {
    return (float *)fftwf_malloc(sizeof(float) * size);
}

void FFTW::free(float *buf) { fftwf_free(buf); }

int FFTW::plan_c2c(direction d, int options) {
    assert(!p);

    inbuf = this->malloc(size * 2);
    outbuf = this->malloc(size * 2 + additional_size * 2);
    outbuf_len = size;
    powerbuf = new (std::align_val_t(32)) float[size * 2];
    quantizedbuf = new (std::align_val_t(32)) int8_t[size * 2];

    std::scoped_lock lk(fftwf_planner_mutex);
    fftwf_plan_with_nthreads(nthreads);
    p = fftwf_plan_dft_1d(size, (fftwf_complex *)inbuf, (fftwf_complex *)outbuf,
                          d == FORWARD ? FFTW_FORWARD : FFTW_BACKWARD, options);
    return 0;
}
int FFTW::plan_r2c(int options) {
    assert(!p);

    inbuf = this->malloc(size);
    outbuf = this->malloc(size + 2);
    outbuf_len = size / 2;
    powerbuf = new (std::align_val_t(32)) float[size];
    quantizedbuf = new (std::align_val_t(32)) int8_t[size];

    std::scoped_lock lk(fftwf_planner_mutex);
    fftwf_plan_with_nthreads(nthreads);
    p = fftwf_plan_dft_r2c_1d(size, inbuf, (fftwf_complex *)outbuf, options);
    return 0;
}
int FFTW::plan_c2r(int options) {
    assert(!p);

    inbuf = this->malloc(size * 2);
    outbuf = this->malloc(size * 2);
    outbuf_len = size;

    std::scoped_lock lk(fftwf_planner_mutex);
    fftwf_plan_with_nthreads(nthreads);
    p = fftwf_plan_dft_c2r_1d(size, (fftwf_complex *)inbuf, outbuf, options);
    return 0;
}
int FFTW::load_real_input(float *a1, float *a2) {
    volk_32f_x2_multiply_32f(inbuf, a1, windowbuf, size / 2);
    volk_32f_x2_multiply_32f(&inbuf[size / 2], a2, &windowbuf[size / 2],
                             size / 2);
    return 0;
}
int FFTW::load_complex_input(float *a1, float *a2) {
    volk_32fc_32f_multiply_32fc((lv_32fc_t *)inbuf, (lv_32fc_t *)a1, windowbuf,
                                size / 2);
    volk_32fc_32f_multiply_32fc((lv_32fc_t *)&inbuf[size], (lv_32fc_t *)a2,
                                &windowbuf[size / 2], size / 2);
    return 0;
}
int FFTW::execute() {
    fftwf_execute(p);
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
                       outbuf_len - base_idx);
    power_and_quantize(outbuf, &powerbuf[outbuf_len - base_idx],
                       &quantizedbuf[outbuf_len - base_idx], size, base_idx);

    int out_len = outbuf_len;
    int8_t *quantized_offset_buf = quantizedbuf;
    float *power_offset_buf = powerbuf;
    for (int i = 0; i < downsample_levels - 1; i++) {
        half_and_quantize(power_offset_buf, power_offset_buf + out_len,
                          quantized_offset_buf + out_len, out_len / 2);
        power_offset_buf += out_len;
        quantized_offset_buf += out_len;
        out_len /= 2;
    }
    return 0;
}
FFTW::~FFTW() {
    if (p) {
        fftwf_destroy_plan(p);
    }
    this->free(inbuf);
    this->free(outbuf);
    operator delete[](powerbuf, std::align_val_t(32));
    operator delete[](quantizedbuf, std::align_val_t(32));
}

#ifdef CLFFT

std::string kernel_window_real = R"<rawliteral>(
    void kernel window_real(global float* inbuf, global const float* windowbuf){ 
        int i = get_global_id(0);
        inbuf[i] *= windowbuf[i];
    }
    void kernel window_complex(global float* inbuf, global const float* windowbuf){ 
        int i = get_global_id(0);
        inbuf[i*2] *= windowbuf[i];
        inbuf[i*2+1] *= windowbuf[i];
    }
    inline char log_power(float power) {
        return convert_char_sat_rtz(20 * log10(power) + 127);
    }
    #pragma OPENCL EXTENSION cl_khr_byte_addressable_store : enable
    void kernel power_and_quantize(global float *complexbuf, global float *powerbuf,
                                   global char *quantizedbuf, float normalize,
                                   int complexbuf_offset, int outputbuf_offset) {
        int i = get_global_id(0);
        int i_complex = i + complexbuf_offset;
        int i_output = i + outputbuf_offset;
        complexbuf[i_complex * 2] /= normalize;
        complexbuf[i_complex * 2 + 1] /= normalize;
        float re = complexbuf[i_complex * 2];
        float im = complexbuf[i_complex * 2 + 1];
        float power = re * re + im * im;
        powerbuf[i_output] = power;
        quantizedbuf[i_output] = log_power(power);
    }
    void kernel half_and_quantize(global const float *powerbuf, global float *halfbuf,
                                  global char *quantizedbuf,
                                  int powerbuf_offset, int outputbuf_offset) {
        int i = get_global_id(0);
        float power = powerbuf[powerbuf_offset + i * 2] + powerbuf[powerbuf_offset + i * 2 + 1];
        halfbuf[i + outputbuf_offset] = power;
        quantizedbuf[i + outputbuf_offset] = log_power(power);
    }
)<rawliteral>";

clFFT::clFFT(size_t size, int nthreads, int downsample_levels)
    : FFT(size, nthreads, downsample_levels), dim{CLFFT_1D}, clLengths{size} {
    int err;

    std::vector<cl::Platform> all_platforms;
    cl::Platform::get(&all_platforms);
    if (all_platforms.size() == 0) {
        throw std::runtime_error("No OpenCL platforms found");
    }

    platform = all_platforms[0];
    std::cout << "Using OpenCL platform: "
              << platform.getInfo<CL_PLATFORM_NAME>() << "\n";

    std::vector<cl::Device> all_devices;
    platform.getDevices(CL_DEVICE_TYPE_ALL, &all_devices);
    if (all_devices.size() == 0) {
        throw std::runtime_error("No OpenCL devices found");
    }

    device = all_devices[0];
    std::cout << "Using OpenCL device: " << device.getInfo<CL_DEVICE_NAME>()
              << "\n";

    context = cl::Context(device);
    queue = cl::CommandQueue(context, device);

    err = clfftInitSetupData(&fftSetup);
    err = clfftSetup(&fftSetup);

    cl_windowbuf = cl::Buffer(context, CL_MEM_READ_WRITE, sizeof(float) * size);
    queue.enqueueWriteBuffer(cl_windowbuf, CL_TRUE, 0, sizeof(float) * size,
                             windowbuf);

    sources.emplace_back(kernel_window_real.c_str(), kernel_window_real.size());
    program = cl::Program(context, sources);

    if (program.build({device}) != CL_SUCCESS) {
        std::cout << "Error building: "
                  << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << "\n";
        throw std::runtime_error("Error building OpenCL program");
    }

    window_real =
        cl::make_kernel<cl::Buffer, cl::Buffer>(program, "window_real");
    window_complex =
        cl::make_kernel<cl::Buffer, cl::Buffer>(program, "window_complex");
    power_and_quantize =
        cl::make_kernel<cl::Buffer, cl::Buffer, cl::Buffer, cl_float, cl_int,
                        cl_int>(program, "power_and_quantize");
    half_and_quantize =
        cl::make_kernel<cl::Buffer, cl::Buffer, cl::Buffer, cl_int, cl_int>(
            program, "half_and_quantize");
    
    operator delete[](windowbuf, std::align_val_t(32));
    windowbuf = NULL;
}

float *clFFT::malloc(size_t size) {
    return new (std::align_val_t(4096)) float[size];
}
void clFFT::free(float *buf) { operator delete[](buf, std::align_val_t(4096)); }
int clFFT::plan_c2c(direction d, int options) {
    int err;

    cl_inbuf = cl::Buffer(context, CL_MEM_READ_WRITE, sizeof(float) * size * 2);
    cl_outbuf = cl::Buffer(context, CL_MEM_READ_WRITE,
                           sizeof(float) * (size + additional_size) * 2);
    cl_powerbuf =
        cl::Buffer(context, CL_MEM_READ_WRITE, sizeof(float) * size * 2);
    cl_quantizedbuf =
        cl::Buffer(context, CL_MEM_READ_WRITE, sizeof(int8_t) * size * 2);
    outbuf = (float *)this->malloc((size + additional_size) * 2);
    quantizedbuf = (int8_t *)this->malloc(size * 2);
    outbuf_len = size;

    this->d = d == FORWARD ? CLFFT_FORWARD : CLFFT_BACKWARD;
    inlayout = CLFFT_COMPLEX_INTERLEAVED;
    outlayout = CLFFT_COMPLEX_INTERLEAVED;
    err = clfftCreateDefaultPlan(&planHandle, context(), dim, clLengths);
    err = clfftSetPlanPrecision(planHandle, CLFFT_SINGLE);
    err = clfftSetLayout(planHandle, CLFFT_COMPLEX_INTERLEAVED,
                         CLFFT_COMPLEX_INTERLEAVED);
    err = clfftSetResultLocation(planHandle, CLFFT_OUTOFPLACE);
    err = clfftBakePlan(planHandle, 1, &queue(), NULL, NULL);
    return err;
}
int clFFT::plan_r2c(int options) {
    int err;
    outbuf = this->malloc(size * 2);

    cl_inbuf = cl::Buffer(context, CL_MEM_READ_WRITE, sizeof(float) * size);
    cl_outbuf =
        cl::Buffer(context, CL_MEM_READ_WRITE, sizeof(float) * (size + 2) * 2);
    cl_powerbuf =
        cl::Buffer(context, CL_MEM_READ_WRITE, sizeof(float) * size * 2);
    cl_quantizedbuf =
        cl::Buffer(context, CL_MEM_READ_WRITE, sizeof(int8_t) * size * 2);
    outbuf_len = size / 2;
    outbuf = (float *)this->malloc(size * 2);
    quantizedbuf = (int8_t *)this->malloc(size * 2);

    inlayout = CLFFT_REAL;
    outlayout = CLFFT_HERMITIAN_INTERLEAVED;
    this->d = CLFFT_FORWARD;
    err = clfftCreateDefaultPlan(&planHandle, context(), dim, clLengths);
    err = clfftSetPlanPrecision(planHandle, CLFFT_SINGLE);
    err = clfftSetLayout(planHandle, CLFFT_REAL, CLFFT_HERMITIAN_INTERLEAVED);
    err = clfftSetResultLocation(planHandle, CLFFT_OUTOFPLACE);
    err = clfftBakePlan(planHandle, 1, &queue(), NULL, NULL);
    return err;
}
int clFFT::plan_c2r(int options) { return 0; }
int clFFT::load_real_input(float *a1, float *a2) {
    // Load a1 into first half of cl_inbuf and a2 into second half
    queue.enqueueWriteBuffer(cl_inbuf, CL_FALSE, 0, sizeof(float) * size / 2,
                             a1);
    queue.enqueueWriteBuffer(cl_inbuf, CL_FALSE, sizeof(float) * size / 2,
                             sizeof(float) * size / 2, a2);
    window_real(cl::EnqueueArgs(queue, cl::NDRange(size)), cl_inbuf,
                cl_windowbuf);
    return 0;
}
int clFFT::load_complex_input(float *a1, float *a2) {
    // Load a1 into first half of cl_inbuf and a2 into second half
    queue.enqueueWriteBuffer(cl_inbuf, CL_FALSE, 0, sizeof(float) * size, a1);
    queue.enqueueWriteBuffer(cl_inbuf, CL_FALSE, sizeof(float) * size,
                             sizeof(float) * size, a2);
    window_complex(cl::EnqueueArgs(queue, cl::NDRange(size)), cl_inbuf,
                   cl_windowbuf);
    return 0;
}
int clFFT::execute() {
    int err;
    /* Execute the plan. */
    err = clfftEnqueueTransform(planHandle, d, 1, &queue(), 0, NULL, NULL,
                                &cl_inbuf(), &cl_outbuf(), NULL);

    int base_idx = 0;
    bool is_complex = outbuf_len == size;
    // For IQ input, the lowest frequency is in the middle
    if (is_complex) {
        base_idx = size / 2 + 1;
    }
    power_and_quantize(
        cl::EnqueueArgs(queue, cl::NDRange(outbuf_len - base_idx)), cl_outbuf,
        cl_powerbuf, cl_quantizedbuf, (float)size, base_idx, 0);
    if (base_idx)
        power_and_quantize(cl::EnqueueArgs(queue, cl::NDRange(base_idx)),
                           cl_outbuf, cl_powerbuf, cl_quantizedbuf, (float)size,
                           0, outbuf_len - base_idx);

    int out_len = outbuf_len;
    int offset = 0;
    for (int i = 0; i < downsample_levels - 1; i++) {
        half_and_quantize(cl::EnqueueArgs(queue, cl::NDRange(out_len / 2)),
                          cl_powerbuf, cl_powerbuf, cl_quantizedbuf, offset,
                          offset + out_len);
        offset += out_len;
        out_len /= 2;
    }
    queue.enqueueReadBuffer(cl_outbuf, CL_FALSE, 0,
                            sizeof(float) * outbuf_len * 2, outbuf);
    queue.enqueueReadBuffer(cl_quantizedbuf, CL_FALSE, 0,
                            sizeof(int8_t) * outbuf_len * 2, quantizedbuf);
    queue.finish();
    return err;
}

clFFT::~clFFT() {
    /* Release the plan. */
    clfftDestroyPlan(&planHandle);

    /* Release clFFT library. */
    clfftTeardown();
}
#endif