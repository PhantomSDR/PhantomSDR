
#include <complex.h>

#include "fft.h"
#include "signal.h"

AudioClient::AudioClient(
    connection_hdl hdl, PacketSender &sender,
    audio_compressor audio_compression, bool is_real, int audio_fft_size,
    int audio_max_sps, int fft_result_size,
    std::multimap<std::pair<int, int>, std::shared_ptr<AudioClient>>
        &signal_slices,
    std::mutex &signal_slice_mtx)
    : Client(hdl, sender, AUDIO), is_real{is_real},
      audio_fft_size{audio_fft_size}, fft_result_size{fft_result_size},
      signal_slices{signal_slices}, signal_slice_mtx{signal_slice_mtx} {

    if (audio_compression == AUDIO_FLAC) {
        std::unique_ptr<FlacEncoder> encoder =
            std::make_unique<FlacEncoder>(hdl, sender);
        encoder->set_channels(1);
        encoder->set_verify(false);
        encoder->set_compression_level(5);
        encoder->set_sample_rate(audio_max_sps);
        encoder->set_bits_per_sample(16);
        encoder->set_streamable_subset(true);
        encoder->init();
        this->encoder = std::move(encoder);
    }
#ifdef HAS_LIBOPUS
    else if (audio_compression == AUDIO_OPUS) {
        encoder =
            std::make_unique<OpusEncoder>(hdl, sender, audio_max_sps);
    }
#endif

    unique_id = generate_unique_id();
    frame_num = 0;
    processing = 0;

    // Audio demodulation scratch data structures
    audio_fft_input =
        fftwf_malloc_unique_ptr<std::complex<float>>(audio_fft_size);
    audio_complex_baseband =
        fftwf_malloc_unique_ptr<std::complex<float>>(audio_fft_size);
    audio_complex_baseband_prev =
        fftwf_malloc_unique_ptr<std::complex<float>>(audio_fft_size);

    audio_real.resize(audio_fft_size);
    audio_real_prev.resize(audio_fft_size);
    audio_real_int16.resize(audio_fft_size);

    dc = DCBlocker<float>(audio_max_sps / 750);
    agc = AGC<float>(0.05, 44100. / (double)audio_max_sps / 10000.);
    ma = MovingAverage<float>(10);
    mm = MovingMode<int>(10);

    {
        std::scoped_lock lg(fftwf_planner_mutex);
        fftwf_plan_with_nthreads(1);
        p_complex = fftwf_plan_dft_1d(
            audio_fft_size, (fftwf_complex *)audio_fft_input.get(),
            (fftwf_complex *)audio_complex_baseband.get(), FFTW_BACKWARD,
            FFTW_ESTIMATE);
        p_real = fftwf_plan_dft_c2r_1d(audio_fft_size,
                                       (fftwf_complex *)audio_fft_input.get(),
                                       audio_real.data(), FFTW_ESTIMATE);
    }
}

void AudioClient::set_audio_range(int l, double m, int r) {
    audio_mid = m;
    this->l = l;
    this->r = r;

    // Change the data structures to reflect the changes
    std::scoped_lock lk(signal_slice_mtx);
    auto node = signal_slices.extract(it);
    node.key() = {l, r};
    it = signal_slices.insert(std::move(node));
}
void AudioClient::set_audio_demodulation(demodulation_mode demodulation) {
    this->demodulation = demodulation;
}
const std::string &AudioClient::get_unique_id() { return unique_id; }

// Does the demodulation and sends the audio to the client
// buf is given offseted by l
void AudioClient::send_audio(std::complex<float> *buf, size_t frame_num) {
    try {
        int audio_l = l - l;
        int audio_r = r - l;
        int audio_m = floor(audio_mid) - l;
        int audio_m_idx = floor(audio_mid);

        int len = audio_r - audio_l;
        // If the user request for the raw IQ signal, do not demodulate
        if (type == SIGNAL) {
            sender.send_binary_packet(hdl, buf,
                                      sizeof(std::complex<float>) * len);
            return;
        }

        float average_power = std::accumulate(
            buf, buf + len, 0.0f,
            [](float a, std::complex<float>& b) { return a + std::norm(b); });

        // average_power /= len;

        // Main demodulation logic for the frequency
        if (demodulation == USB || demodulation == LSB) {
            if (demodulation == USB) {
                // For USB, just copy the bins to the audio frequencies
                std::fill(audio_fft_input.get(),
                          audio_fft_input.get() + audio_fft_size, 0.0f);
                // User requested for [l, r)
                // IFFT bins are [audio_m, audio_m + audio_fft_size)
                // intersect and copy
                int copy_l = std::max(audio_l, audio_m);
                int copy_r = std::min(audio_r, audio_m + audio_fft_size);
                std::copy(buf + copy_l - audio_l, buf + copy_r - audio_l,
                          audio_fft_input.get() + copy_l - audio_m);
                fftwf_execute(p_real);
            } else if (demodulation == LSB) {
                // For LSB, just copy the inverted bins to the audio frequencies
                std::fill(audio_fft_input.get(),
                          audio_fft_input.get() + audio_fft_size, 0.0f);
                // User requested for [l, r)
                // IFFT bins are [audio_m - audio_fft_size + 1, audio_m + 1)
                // intersect and copy
                int copy_l = std::max(audio_l, audio_m - audio_fft_size + 1);
                int copy_r = std::min(audio_r, audio_m + 1);
                // last element should be at audio_fft_size - 1
                std::reverse_copy(buf + copy_l - audio_l,
                                  buf + copy_r - audio_l,
                                  audio_fft_input.get() + audio_m - copy_r + 1);
                fftwf_execute(p_real);
            }
            // On every other frame, the audio waveform is inverted due to the
            // 50% overlap This only happens when downconverting by either even
            // or odd bins, depending on modulation
            if (demodulation == USB && frame_num % 2 == 1 &&
                ((audio_m_idx % 2 == 0 && !is_real) ||
                 (audio_m_idx % 2 == 1 && is_real))) {
                dsp_negate_float(audio_real.data(), audio_fft_size);
            } else if (demodulation == LSB && frame_num % 2 == 1 &&
                       ((audio_m_idx % 2 == 1 && !is_real) ||
                        (audio_m_idx % 2 == 0 && is_real))) {
                dsp_negate_float(audio_real.data(), audio_fft_size);
            }

            // Overlap and add the audio waveform, due to the 50% overlap
            dsp_add_float(audio_real.data(), audio_real_prev.data(),
                          audio_fft_size / 2);
        } else if (demodulation == AM || demodulation == FM) {
            // For AM, copy the bins to the complex baseband frequencies
            std::fill(audio_fft_input.get(),
                      audio_fft_input.get() + audio_fft_size, 0.0f);
            // Bins are [audio_l, audio_r)
            // Positive IFFT bins are [audio_m, audio_m + audio_fft_size / 2)
            // Negative IFFT bins are [audio_m - audio_fft_size / 2 + 1,
            // audio_m) intersect and copy
            int pos_copy_l = std::max(audio_l, audio_m);
            int pos_copy_r = std::min(audio_r, audio_m + audio_fft_size / 2);
            std::copy(buf + pos_copy_l - audio_l, buf + pos_copy_r - audio_l,
                      audio_fft_input.get() + pos_copy_l - audio_m);
            int neg_copy_l =
                std::max(audio_l, audio_m - audio_fft_size / 2 + 1);
            int neg_copy_r = std::min(audio_r, audio_m);
            // last element should be at audio_fft_size - 1
            std::copy(buf + neg_copy_l - audio_l, buf + neg_copy_r - audio_l,
                      audio_fft_input.get() + audio_fft_size -
                          (audio_m - neg_copy_l));

            /*int strongest_bin = std::max_element(audio_fft_input,
            audio_fft_input + audio_fft_size,
                                                 [](const fftwf_complex &a,
            const fftwf_complex &b) { return a[0] * a[0] + a[1] * a[1] < b[0] *
            b[0] + b[1] * b[1];
                                                 }) -
                                audio_fft_input;
            int strongest_bin_mode = d->mm.insert(strongest_bin);
            float mag = std::sqrt(audio_fft_input[strongest_bin][0] *
            audio_fft_input[strongest_bin][0] +
                             audio_fft_input[strongest_bin][1] *
            audio_fft_input[strongest_bin][1]); float strongest_bin_ma =
            d->ma.insert(mag); float strongest_bin_mode_mag =
            std::sqrt(audio_fft_input[strongest_bin_mode][0] *
            audio_fft_input[strongest_bin_mode][0] +
                                                audio_fft_input[strongest_bin_mode][1]
            * audio_fft_input[strongest_bin_mode][1]); if (strongest_bin_ma >
            strongest_bin_mode_mag) { audio_fft_input[strongest_bin_mode][0] *=
            strongest_bin_ma / strongest_bin_mode_mag;
                audio_fft_input[strongest_bin_mode][1] *= strongest_bin_ma /
            strongest_bin_mode_mag; std::cout<<"carrier strength
            adjust"<<std::endl;
            }
            std::cout<<"strongest_bin: "<<strongest_bin<<" strongest_bin_mode:
            "<<strongest_bin_mode<<" mag: "<<mag<<" strongest_bin_ma:
            "<<strongest_bin_ma<<" strongest_bin_mode_mag:
            "<<strongest_bin_mode_mag<<std::endl;*/

            auto prev = audio_complex_baseband[audio_fft_size / 2 - 1];
            std::copy(audio_complex_baseband.get() + audio_fft_size / 2,
                      audio_complex_baseband.get() + audio_fft_size,
                      audio_complex_baseband_prev.get());
            // Copy the bins to the complex baseband frequencies
            // Remove DC
            fftwf_execute(p_complex);
            if (frame_num % 2 == 1 && ((audio_m_idx % 2 == 0 && !is_real) ||
                                       (audio_m_idx % 2 == 1 && is_real))) {
                // If the center frequency is even and the frame number is odd,
                // or if the center frequency is odd and the frame number is
                // even, then the signal is inverted
                dsp_negate_complex(audio_complex_baseband.get(),
                                   audio_fft_size);
            }
            dsp_add_complex(audio_complex_baseband.get(),
                            audio_complex_baseband_prev.get(),
                            audio_fft_size / 2);
            if (demodulation == AM) {
                // Envelope detection for AM
                dsp_am_demod(audio_complex_baseband.get(), audio_real.data(),
                             audio_fft_size / 2);
            }
            if (demodulation == FM) {
                // Polar discriminator for FM
                polar_discriminator_fm(audio_complex_baseband.get(), prev,
                                       audio_real.data(), audio_fft_size / 2);
            }
        }
        
        // Check if any audio_real is nan
        for (int i = 0; i < audio_fft_size / 2; i++) {
            if (std::isnan(audio_real[i])) {
                throw std::runtime_error("NaN found in audio_real");
            }
        }

        // DC removal
        dc.removeDC(audio_real.data(), audio_fft_size / 2);

        // AGC
        agc.applyAGC(audio_real.data(), audio_fft_size / 2);
        // Quantize into 16 bit audio to save bandwidth
        dsp_float_to_int16(audio_real.data(), audio_real_int16.data(),
                           65536 / 4, audio_fft_size / 2);

        // Copy the half to add in the next frame
        std::copy(audio_real.begin() + (audio_fft_size / 2), audio_real.end(),
                  audio_real_prev.begin());

        // Set audio details
        encoder->set_data(frame_num, audio_l, audio_mid, audio_r, average_power);

        // Encode audio and send it off
        encoder->process(audio_real_int16.data(), audio_fft_size / 2);

        // Increment the frame number
        frame_num++;
    } catch (const std::exception &exc) {
        // std::cout << "client disconnect" << std::endl;
    }
    processing = 0;
}

void AudioClient::on_window_message(int new_l, std::optional<double> &m,
                                    int new_r, std::optional<int> &level) {
    if (!m.has_value()) {
        return;
    }
    if (new_l < 0 || new_r > fft_result_size) {
        return;
    }
    if (new_r - new_l > audio_fft_size) {
        return;
    }
    double new_m = m.value();
    set_audio_range(new_l, new_m, new_r);
}

void AudioClient::on_demodulation_message(std::string &demodulation) {
    // Update the demodulation type
    if (demodulation == "USB") {
        this->demodulation = USB;
    } else if (demodulation == "LSB") {
        this->demodulation = LSB;
    } else if (demodulation == "AM") {
        this->demodulation = AM;
    } else if (demodulation == "FM") {
        this->demodulation = FM;
    }
}

void AudioClient::on_close() {
    std::scoped_lock lk(signal_slice_mtx);
    signal_slices.erase(it);
    /*if (show_other_users) {
        std::scoped_lock lk(signal_changes_mtx);
        signal_changes[d->unique_id] = {-1, -1, -1};
    }*/
}
AudioClient::~AudioClient() {
    fftwf_destroy_plan(p_real);
    fftwf_destroy_plan(p_complex);
}