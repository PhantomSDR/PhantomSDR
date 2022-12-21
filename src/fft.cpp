#include "fft.h"
#include "spectrumserver.h"
#include "utils.h"

#include <numeric>

#include <fftw3.h>

#include <volk/volk.h>

// Main FFT loop to process input samples
void broadcast_server::fft_task() {

    // This is the buffer where it converts to a float

    std::unique_ptr<FFT> fft = std::move(this->fft);

    // Twice as many floats if it is complex
    float *input_buffers[3];
    int input_buffer_size = fft_size / 2 * (2 - is_real);
    int input_buffer_idx = 0;
    input_buffers[0] = fft->malloc(input_buffer_size);
    input_buffers[1] = fft->malloc(input_buffer_size);
    input_buffers[2] = fft->malloc(input_buffer_size);

    // FFT planning
    if (is_real) {
        fft->plan_r2c(FFTW_MEASURE | FFTW_DESTROY_INPUT);
    } else {
        fft->plan_c2c(FFT::FORWARD, FFTW_MEASURE | FFTW_DESTROY_INPUT);
    }
    fft_buffer = (fftwf_complex *)fft->get_output_buffer();

    // Target fps is 10, *2 since 50% overlap
    int skip_num = std::max(1, (int)floor(((float)sps / fft_size) / 10.) * 2);
    std::cout << "Waterfall is sent every " << skip_num << " FFTs" << std::endl;

    MovingAverage<double> sps_measured(60);
    auto prev_data = std::chrono::steady_clock::now();

    auto signal_loop_fn = std::bind(&broadcast_server::signal_loop, this);
    auto waterfall_loop_fn = std::bind(&broadcast_server::waterfall_loop, this,
                                       fft->get_quantized_buffer());

    std::future<void> buffer_read = std::async(std::launch::async, [] {});

    while (running) {
        // Read, convert and scale the input
        // 50% overlap is hardcoded for favourable downconverter properties
        buffer_read.wait();
        float *buf0 = input_buffers[input_buffer_idx];
        float *buf1 = input_buffers[(input_buffer_idx + 1) % 3];
        float *buf2 = input_buffers[(input_buffer_idx + 2) % 3];
        if (is_real) {
            // Read into buf2 asynchronously
            buffer_read = std::async(std::launch::async,
                                     [buf2, fft_size = fft_size, this] {
                                         reader->read(buf2, fft_size / 2);
                                     });

            fft->load_real_input(buf0, buf1);
        } else {
            // IQ data has twice as many floats
            buffer_read = std::async(std::launch::async,
                                     [buf2, fft_size = fft_size, this] {
                                         reader->read(buf2, fft_size);
                                     });
            fft->load_complex_input(buf0, buf1);
        }

        input_buffer_idx = (input_buffer_idx + 1) % 3;
        // If no users, save CPU and skip the FFT
        /*if (signal_slices.size() + std::accumulate(waterfall_slices.begin(),
                                                   waterfall_slices.end(), 0,
                                                   [](int val, signal_list &l) {
                                                       return val + l.size();
                                                   }) ==
            0) {
            continue;
        }*/

        fft->execute();
        if (!is_real) {

            // If the user requested a range near the 0 frequency,
            // the data will wrap around, copy the front to the back to make
            // it contiguous
            memcpy(&fft_buffer[fft_result_size], &fft_buffer[0],
                   sizeof(fftwf_complex) * audio_max_fft_size);
        }

        // Enqueue tasks once the fft is ready
        if (!signal_processing) {
            signal_processing = 1;
            m_server.get_io_service().post(signal_loop_fn);
        }
        if (frame_num % skip_num == 0) {
            if (!waterfall_processing) {
                waterfall_processing = 1;
                m_server.get_io_service().post(waterfall_loop_fn);
            }
        }
        frame_num++;

        /*auto cur_data = std::chrono::steady_clock::now();
        std::chrono::duration<double> diff_time = cur_data - prev_data;
        sps_measured.insert(diff_time.count());
        prev_data = cur_data;
        if (frame_num % 10 == 0) {
            // std::cout<<"SPS: "<<std::fixed<<(double)(fft_size / 2) /
            // sps_measured.getAverage()<<std::endl;
        }*/
    }
    fft->free(input_buffers[0]);
    fft->free(input_buffers[1]);
    fft->free(input_buffers[2]);
}