#include "fft.h"
#include "spectrumserver.h"
#include "utils.h"

#include <numeric>

#include <fftw3.h>

#include <volk/volk.h>

// Main FFT loop to process input samples
void broadcast_server::fft_task() {
    float *input_buffer;

    // This is the buffer where it converts to a float
    float *fft_input_buffer;

    float *fft_window;

    std::unique_ptr<FFT> &fft = this->fft;

    // Buffers are bigger than needed, to prevent overflow errors
    input_buffer = new (std::align_val_t(32)) float[fft_size * 2];
    fft_window = new (std::align_val_t(32)) float[fft_size];

    // Power levels to send to the client
    fft_power = new (std::align_val_t(32)) float[fft_size];
    fft_power_scratch = new (std::align_val_t(32)) float[fft_size];
    fft_power_quantized_full = new (std::align_val_t(32)) int8_t[fft_size * 2];

    build_hann_window(fft_window, fft_size);

    // FFT planning
    if (is_real) {
        fft->plan_r2c(FFTW_MEASURE);
    } else {
        fft->plan_c2c(FFT::FORWARD, FFTW_MEASURE);
    }

    fft_input_buffer = fft->get_input_buffer();
    fft_buffer = (fftwf_complex *)fft->get_output_buffer();

    // Target fps is 10, *2 since 50% overlap
    int skip_num = std::max(1, (int)floor(((float)sps / fft_size) / 10.) * 2);
    std::cout << "Waterfall is sent every " << skip_num << " FFTs" << std::endl;

    MovingAverage<double> sps_measured(60);
    auto prev_data = std::chrono::steady_clock::now();
    auto signal_loop_fn = std::bind(&broadcast_server::signal_loop, this);
    auto waterfall_loop_fn =
        std::bind(&broadcast_server::waterfall_loop, this, fft_power,
                  fft_power_quantized_full, fft_power_scratch);

    while (running) {
        // Read, convert and scale the input
        // 50% overlap is hardcoded for favourable downconverter properties
        int second_half = frame_num % 2;
        if (is_real) {

            reader->read(&input_buffer[second_half * fft_size / 2],
                         fft_size / 2);
            // Copy and window the signal
            if (second_half) {
                /* Equivalent code:
                 * for(int i = 0;i < fft_size; i++) {
                 *   fft_input_buffer[i] = input_buffer[i] * fft_window[i];
                 * }
                 */
                volk_32f_x2_multiply_32f(fft_input_buffer, input_buffer,
                                         fft_window, fft_size);
            } else {
                /* Equivalent code:
                 * for(int i = 0;i < fft_size / 2; i++) {
                 *     fft_input_buffer[i] = input_buffer[i + fft_size / 2] *
                 * fft_window[i];
                 * }
                 * for(int i = 0;i < fft_size / 2; i++) {
                 *     fft_input_buffer[i + fft_size / 2] = input_buffer[i] *
                 * fft_window[i + fft_size / 2];
                 * }
                 */
                volk_32f_x2_multiply_32f(fft_input_buffer,
                                         &input_buffer[fft_size / 2],
                                         fft_window, fft_size / 2);
                volk_32f_x2_multiply_32f(
                    &fft_input_buffer[fft_size / 2], input_buffer,
                    &fft_window[fft_size / 2], fft_size / 2);
            }
        } else {
            // IQ signal has twice the number of floats
            if (second_half) {
                /* Equivalent code:
                 * for(int i = 0;i < fft_size; i++) {
                 *     fft_input_buffer[i * 2] = input_buffer[i * 2] *
                 * fft_window[i]; fft_input_buffer[i * 2 + 1] = input_buffer[i *
                 * 2 + 1] * fft_window[i];
                 * }
                 */
                // Window the half that is not modified
                volk_32fc_32f_multiply_32fc((lv_32fc_t *)fft_input_buffer,
                                            (lv_32fc_t *)input_buffer,
                                            fft_window, fft_size / 2);
                // Do in chunks of 4096 to have better cache performance
                for (int i = 0; i < fft_size; i += 4096) {
                    reader->read(&input_buffer[fft_size + i], 4096);
                    volk_32fc_32f_multiply_32fc(
                        (lv_32fc_t *)&fft_input_buffer[fft_size + i],
                        (lv_32fc_t *)&input_buffer[fft_size + i],
                        &fft_window[fft_size / 2 + i / 2], 4096 / 2);
                }
            } else {

                /* Equivalent code:
                 * for(int i = 0;i < fft_size / 2; i++) {
                 *    fft_input_buffer[i * 2] = input_buffer[(i + fft_size / 2)
                 * * 2] * fft_window[i]; fft_input_buffer[i * 2 + 1] =
                 * input_buffer[(i + fft_size / 2) * 2 + 1] * fft_window[i];
                 * }
                 * for(int i = 0;i < fft_size / 2; i++) {
                 *    fft_input_buffer[(i + fft_size / 2) * 2] = input_buffer[i
                 * * 2] * fft_window[i + fft_size / 2]; fft_input_buffer[(i +
                 * fft_size / 2) * 2 + 1] = input_buffer[i * 2 + 1] *
                 * fft_window[i + fft_size / 2];
                 * }
                 * volk_32fc_32f_multiply_32fc((lv_32fc_t*)&fft_input_buffer[fft_size],
                 * (lv_32fc_t*)input_buffer, &fft_window[fft_size / 2], fft_size
                 * / 2);
                 */

                // Do in chunks of 4096 to have better cache
                for (int i = 0; i < fft_size; i += 4096) {
                    reader->read(&input_buffer[i], 4096);
                    volk_32fc_32f_multiply_32fc(
                        (lv_32fc_t *)&fft_input_buffer[fft_size + i],
                        (lv_32fc_t *)&input_buffer[i],
                        &fft_window[fft_size / 2 + i / 2], 4096 / 2);
                }
                // Window the half that is not modified
                volk_32fc_32f_multiply_32fc(
                    (lv_32fc_t *)fft_input_buffer,
                    (lv_32fc_t *)&input_buffer[fft_size], fft_window,
                    fft_size / 2);
            }
        }

        if (signal_slices.size() + std::accumulate(waterfall_slices.begin(),
                                                   waterfall_slices.end(), 0,
                                                   [](int val, signal_list &l) {
                                                       return val + l.size();
                                                   }) ==
            0) {
            continue;
        }
        fft->execute();
        // Normalize the power by the number of bins
        volk_32f_s32f_normalize((float *)fft_buffer, fft_size,
                                fft_result_size * 2);
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

        auto cur_data = std::chrono::steady_clock::now();
        std::chrono::duration<double> diff_time = cur_data - prev_data;
        sps_measured.insert(diff_time.count());
        prev_data = cur_data;
        if (frame_num % 10 == 0) {
            // std::cout<<"SPS: "<<std::fixed<<(double)(fft_size / 2) /
            // sps_measured.getAverage()<<std::endl;
        }
    }

    operator delete[](input_buffer, std::align_val_t(32));
    operator delete[](fft_window, std::align_val_t(32));
}