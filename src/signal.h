#ifndef SIGNAL_H
#define SIGNAL_H

#include "audio.h"
#include "client.h"
#include "utils.h"
#include "utils/audioprocessing.h"

#include <complex>

#include <boost/align/aligned_allocator.hpp>

#ifdef HAS_LIQUID
#include <liquid/liquid.h>
#endif

template <typename T>
using AlignedAllocator = boost::alignment::aligned_allocator<T, 64>;

// fftwf_malloc allocator for vector
template <typename T> struct fftwfAllocator {
    typedef T value_type;
    fftwfAllocator() {}
    template <typename U> fftwfAllocator(const fftwfAllocator<U> &) {}
    T *allocate(std::size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::bad_alloc();
        if (auto p = static_cast<T *>(fftwf_malloc(n * sizeof(T))))
            return p;
        throw std::bad_alloc();
    }
    void deallocate(T *p, std::size_t) { fftwf_free(p); }
};

struct ComplexDeleter {
    void operator()(std::complex<float> *f) {
        fftwf_free(reinterpret_cast<fftwf_complex *>(f));
    }
};

template <typename T>
static inline std::unique_ptr<T[], ComplexDeleter>
fftwf_malloc_unique_ptr(size_t n) {
    T *ptr = reinterpret_cast<T *>(fftwf_malloc(n * sizeof(T)));
    if constexpr (std::is_trivial<T>::value) {
        memset(ptr, 0, n * sizeof(T));
    } else {
        fill(ptr, ptr + n, T());
    }
    return std::unique_ptr<T[], ComplexDeleter>(ptr);
}

class AudioClient : public Client {
  public:
    AudioClient(connection_hdl hdl, PacketSender &sender,
                audio_compressor audio_compression, bool is_real,
                int audio_fft_size, int audio_max_sps, int fft_result_size);
    void set_audio_range(int l, double audio_mid, int r);
    void set_audio_demodulation(demodulation_mode demodulation);
    const std::string &get_unique_id();

    virtual void on_window_message(int l, std::optional<double> &m, int r,
                                   std::optional<int> &level);
    virtual void on_demodulation_message(std::string &demodulation);
    void on_close();

    void send_audio(std::complex<float> *buf, size_t frame_num);
    virtual ~AudioClient();

    std::multimap<std::pair<int, int>, std::shared_ptr<AudioClient>>::iterator
        it;

  protected:
    // User requested demodulation mode
    demodulation_mode demodulation;

    // Scratch space for the slice the user requested
    fftwf_complex *fft_slice_buf;
    uint8_t *waterfall_slice_buf;

    // Scratch space for audio demodulation
    bool is_real;
    int audio_fft_size;
    int fft_result_size;
    int audio_rate;
    std::unique_ptr<std::complex<float>[], ComplexDeleter> audio_fft_input;

    // IQ data for demodulation
    std::unique_ptr<std::complex<float>[], ComplexDeleter>
        audio_complex_baseband;
    std::unique_ptr<std::complex<float>[], ComplexDeleter>
        audio_complex_baseband_prev;

    std::unique_ptr<std::complex<float>[], ComplexDeleter>
        audio_complex_baseband_carrier;
    std::unique_ptr<std::complex<float>[], ComplexDeleter>
        audio_complex_baseband_carrier_prev;
    
    std::vector<float, AlignedAllocator<float>> audio_real;
    std::vector<float, AlignedAllocator<float>> audio_real_prev;
    std::vector<int32_t, AlignedAllocator<int32_t>> audio_real_int16;

    // IFFT plans for demodulation
    fftwf_plan p_complex;
    fftwf_plan p_complex_carrier;
    fftwf_plan p_real;

    // For DC offset removal and AGC implementatino
    DCBlocker<float> dc;
    AGC agc;
    MovingAverage<float> ma;
    MovingMode<int> mm;

#ifdef HAS_LIQUID
    nco_crcf mixer;
#endif

    // Compression codec variables for Audio
    std::unique_ptr<AudioEncoder> encoder;

    signal_slices_t
        &signal_slices;
    std::mutex &signal_slice_mtx;
};

#endif