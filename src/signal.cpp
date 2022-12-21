
#include "fft.h"
#include "spectrumserver.h"

void broadcast_server::on_open_signal(connection_hdl hdl,
                                      conn_type signal_type) {
    send_basic_info(hdl);

    // Audio demodulation scratch data structures
    std::shared_ptr<conn_data> d = std::make_shared<conn_data>();

    audio_fft_size = ceil((double)audio_max_sps * fft_size / sps / 4.) * 4;
    fftwf_complex *audio_fft_input =
        (fftwf_complex *)fftwf_malloc(sizeof(fftwf_complex) * audio_fft_size);
    fftwf_complex *audio_complex_baseband =
        (fftwf_complex *)fftwf_malloc(sizeof(fftwf_complex) * audio_fft_size);

    if (signal_type != SIGNAL) {
        if (audio_compression == AUDIO_FLAC) {
            std::unique_ptr<FlacEncoder> encoder =
                std::make_unique<FlacEncoder>(hdl, &m_server);
            encoder->set_channels(1);
            encoder->set_verify(false);
            encoder->set_compression_level(5);
            encoder->set_sample_rate(audio_max_sps);
            encoder->set_bits_per_sample(16);
            //encoder->set_streamable_subset(true);
            encoder->init();
            d->encoder = std::move(encoder);
        }
#ifdef HAS_LIBOPUS
        else if (audio_compression == AUDIO_OPUS) {
            std::unique_ptr<OpusEncoder> encoder =
                std::make_unique<OpusEncoder>(hdl, &m_server, audio_max_sps);
            d->encoder = std::move(encoder);
        }
#endif
    }

    d->unique_id = generate_unique_id();
    d->type = signal_type;
    d->hdl = hdl;
    d->audio_mid = default_m;
    d->frame_num = 0;
    d->l = default_l;
    d->r = default_r;
    d->demodulation = default_mode;
    d->processing = 0;

    d->audio_fft_size = audio_fft_size;
    d->audio_fft_input = audio_fft_input;
    d->audio_complex_baseband = audio_complex_baseband;
    d->audio_real.resize(audio_fft_size);
    d->audio_real_prev.resize(audio_fft_size);
    d->audio_real_int16.resize(audio_fft_size);

    {
        std::scoped_lock lg(fftwf_planner_mutex);
        fftwf_plan_with_nthreads(1);
        d->p_complex = fftwf_plan_dft_1d(audio_fft_size, audio_fft_input,
                                         audio_complex_baseband, FFTW_BACKWARD,
                                         FFTW_ESTIMATE);
        d->p_real = fftwf_plan_dft_c2r_1d(audio_fft_size, audio_fft_input,
                                          d->audio_real.data(), FFTW_ESTIMATE);
    }

    server::connection_ptr con = m_server.get_con_from_hdl(hdl);
    con->set_close_handler(std::bind(&broadcast_server::on_close_signal, this,
                                     std::placeholders::_1, d));
    con->set_message_handler(std::bind(&broadcast_server::on_message, this,
                                       std::placeholders::_1,
                                       std::placeholders::_2, d));

    if (show_other_users) {
        std::scoped_lock lk(signal_changes_mtx);
        signal_changes[d->unique_id] = {d->l, d->audio_mid, d->r};
    }

    // Default slice
    {
        std::scoped_lock lg(signal_slice_mtx);
        auto it = signal_slices.insert({{d->l, d->r}, d});
        d->it = it;
    }
}
void broadcast_server::on_close_signal(connection_hdl hdl,
                                       std::shared_ptr<conn_data> &d) {
    if (show_other_users) {
        std::scoped_lock lk(signal_changes_mtx);
        signal_changes[d->unique_id] = {-1, -1, -1};
    }
    {
        std::scoped_lock lk(signal_slice_mtx);
        signal_slices.erase(d->it);
    }
}

// Does the demodulation and sends the audio to the client
void broadcast_server::signal_send(std::shared_ptr<conn_data> &d,
                                   fftwf_complex *buf, int len) {
    try {
        // Prevent the network code from deleting the buffers
        conn_type type = d->type;
        connection_hdl hdl = d->hdl;

        // If the user request for the raw IQ signal, do not demodulate
        if (type == SIGNAL) {
            m_server.send(hdl, buf, sizeof(fftwf_complex) * len,
                          websocketpp::frame::opcode::binary);
            return;
        }
        // Copy the data over
        int frame_num = d->frame_num;
        int audio_l = d->l;
        int audio_r = d->r;
        int audio_m = floor(d->audio_mid);
        demodulation_mode demodulation = d->demodulation;

        fftwf_complex *audio_fft_input = d->audio_fft_input;
        fftwf_complex *audio_complex_baseband = d->audio_complex_baseband;
        std::vector<float> &audio_real = d->audio_real;
        std::vector<float> &audio_real_prev = d->audio_real_prev;
        std::vector<int32_t> &audio_real_int16 = d->audio_real_int16;
        int audio_fft_size = d->audio_fft_size;

        float average_power = 0;
        for (int i = 0; i < len; i++) {
            average_power += buf[i][0] * buf[i][0] + buf[i][1] * buf[i][1];
        }
        // average_power /= len;

        // Main demodulation logic for the frequency
        if (demodulation == USB) {
            // For USB, just copy the bins to the audio frequencies
            memset(audio_fft_input, 0, sizeof(fftwf_complex) * audio_fft_size);
            for (int i = audio_l; i < audio_r; i++) {
                if (i - audio_m >= 0 && i - audio_m < audio_fft_size) {
                    audio_fft_input[i - audio_m][0] = buf[i - audio_l][0];
                    audio_fft_input[i - audio_m][1] = buf[i - audio_l][1];
                }
            }
            fftwf_execute(d->p_real);
        } else if (demodulation == LSB) {
            // For LSB, just copy the inverted bins to the audio frequencies
            memset(audio_fft_input, 0, sizeof(fftwf_complex) * audio_fft_size);
            for (int i = audio_r - 1; i >= audio_l; i--) {
                if (audio_m - i >= 0 && audio_m - i < audio_fft_size) {
                    audio_fft_input[audio_m - i][0] = buf[i - audio_l][0];
                    audio_fft_input[audio_m - i][1] = buf[i - audio_l][1];
                }
            }
            fftwf_execute(d->p_real);
            std::reverse(audio_real.begin(), audio_real.end());
        } else if (demodulation == AM || demodulation == FM) {
            // For AM, copy the bins to the complex baseband frequencies
            memset(audio_fft_input, 0, sizeof(fftwf_complex) * audio_fft_size);
            int negative_bins = std::max(0, audio_m - audio_l);
            int negative_bin_start = audio_fft_size - negative_bins;
            for (int i = audio_l; i < audio_r; i++) {
                int arr_idx = i - audio_l;
                if (arr_idx >= audio_fft_size) {
                    break;
                }
                if (i - audio_m >= 0) {
                    audio_fft_input[i - audio_m][0] = buf[arr_idx][0];
                    audio_fft_input[i - audio_m][1] = buf[arr_idx][1];
                } else {
                    audio_fft_input[negative_bin_start + arr_idx][0] =
                        buf[arr_idx][0];
                    audio_fft_input[negative_bin_start + arr_idx][1] =
                        buf[arr_idx][1];
                }
            }
            float lastI = audio_complex_baseband[audio_fft_size - 1][0];
            float lastQ = audio_complex_baseband[audio_fft_size - 1][1];
            // Remove DC
            //audio_fft_input[0][0] = 0;
            //audio_fft_input[0][1] = 0;
            fftwf_execute(d->p_complex);
            if (demodulation == AM) {
                // Envelope detection for AM
                for (int i = 0; i < audio_fft_size; i++) {
                    audio_real[i] = std::sqrt(audio_complex_baseband[i][0] *
                                             audio_complex_baseband[i][0] +
                                         audio_complex_baseband[i][1] *
                                             audio_complex_baseband[i][1]);
                }
            }
            if (demodulation == FM) {
                // Polar discriminator for FM
                for (int i = 0; i < audio_fft_size; i++) {
                    float I = audio_complex_baseband[i][0];
                    float Q = audio_complex_baseband[i][1];

                    //(I + jQ) * (lastI - jlastQ)
                    float newI = I * lastI + Q * lastQ;
                    float newQ = -I * lastQ + Q * lastI;
                    audio_real[i] = std::atan2(newQ, newI);
                    lastI = I;
                    lastQ = Q;
                }
            }
        }

        // On every other frame, the audio waveform is inverted due to the 50%
        // overlap This only happens when downconverting by either even or odd
        // bins, depending on modulation
        if (demodulation == USB && frame_num % 2 == 1 &&
            ((audio_m % 2 == 0 && !is_real) || (audio_m % 2 == 1 && is_real))) {
            for (int i = 0; i < audio_fft_size; i++) {
                audio_real[i] = -audio_real[i];
            }
        } else if (demodulation == LSB && frame_num % 2 == 1 &&
                   ((audio_m % 2 == 1 && !is_real) || (audio_m % 2 == 0 && is_real))) {
            for (int i = 0; i < audio_fft_size; i++) {
                audio_real[i] = -audio_real[i];
            }
        }

        // Overlap and add the audio waveform, due to the 50% overlap
        for (int i = 0; i < audio_fft_size / 2; i++) {
            // For FM amplitude does not matter. Since there are edge effects,
            // do overlap save and only save the middle part
            if (demodulation == FM) {
                audio_real[i] = audio_real[i + audio_fft_size / 4];
            } else {
                audio_real[i] = (audio_real[i] + audio_real_prev[i]) / 2;
            }
        }

        // Check if any audio_real is nan
        for (int i = 0; i < audio_fft_size / 2; i++) {
            if (std::isnan(audio_real[i])) {
                throw std::runtime_error("NaN found in audio_real");
        }
        }
        


        // DC removal
        d->dc.removeDC(audio_real.data(), audio_fft_size / 2);

        // AGC
        d->agc.applyAGC(audio_real.data(), audio_fft_size / 2);

        // Quantize into 16 bit audio to save bandwidth
        for (int i = 0; i < audio_fft_size / 2; i++) {
            audio_real_int16[i] =
                ((audio_real[i] * 65536 / 32 + 32768.5) - 32768);
        }

        // Copy the half to add in the next frame
        std::copy(audio_real.begin() + (audio_fft_size / 2), audio_real.end(),
                  audio_real_prev.begin());

        // Set audio details
        d->encoder->set_data(audio_l, d->audio_mid, audio_r, average_power);

        // Encode audio and send it off
        d->encoder->process(audio_real_int16.data(), audio_fft_size / 2);

        // Increment the frame number
        d->frame_num++;
    } catch (...) {
        // std::cout << "client disconnect" << std::endl;
    }
    d->processing = 0;
}

// Iterates through the client list to send the slices
void broadcast_server::signal_loop() {
    int base_idx = 0;
    if (!is_real) {
        base_idx = fft_size / 2 + 1;
    }
    std::scoped_lock lg(signal_slice_mtx);
    // Send the apprioriate signal slice to the client
    for (auto &[slice, data] : signal_slices) {
        auto &[l_idx, r_idx] = slice;
        // If the client is slow, avoid unnecessary buffering and drop the
        // audio
        if (m_server.get_con_from_hdl(data->hdl)->get_buffered_amount() <=
            1000000) {
            if (server_threads == 1) {
                signal_send(data,
                            &fft_buffer[(l_idx + base_idx) % fft_result_size],
                            r_idx - l_idx);
            } else {
                if (!data->processing) {
                    data->processing = 1;
                    m_server.get_io_service().post(
                        m_server.get_con_from_hdl(data->hdl)
                            ->get_strand()
                            ->wrap(std::bind(&broadcast_server::signal_send,
                                             this, data,
                                             &fft_buffer[(l_idx + base_idx) %
                                                         fft_result_size],
                                             r_idx - l_idx)));
                }
            }
        }
    }
    signal_processing = 0;
}

void broadcast_server::signal_task() {
    // For IQ input, the lowest frequency is in the middle
    while (running) {
        // Wait for the FFT to be ready
        std::shared_lock lk(fft_mutex);
        fft_processed.wait(lk, [&] { return signal_processing; });
        signal_loop();
        signal_processing = 0;
    }
}