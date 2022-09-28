#include "spectrumserver.h"
#include "waterfallcompression.h"

#include <volk/volk.h>

#include <zstd.h>

void broadcast_server::on_open_waterfall(connection_hdl hdl) {
    send_basic_info(hdl);

    // Set default to the entire spectrum
    std::shared_ptr<conn_data> d = std::make_shared<conn_data>();
    d->type = WATERFALL;
    d->hdl = hdl;
    d->level = downsample_levels - 1;
    d->l = 0;
    d->r = min_waterfall_fft;
    if (waterfall_compression == WATERFALL_ZSTD) {
        d->waterfall_encoder =
            std::make_unique<ZstdEncoder>(hdl, &m_server, min_waterfall_fft);
    } 
#ifdef HAS_LIBAOM
    else if (waterfall_compression == WATERFALL_AV1) {
        d->waterfall_encoder =
            std::make_unique<AV1Encoder>(hdl, &m_server, min_waterfall_fft);
    }
#endif

    server::connection_ptr con = m_server.get_con_from_hdl(hdl);
    con->set_close_handler(std::bind(&broadcast_server::on_close_waterfall,
                                     this, std::placeholders::_1, d));
    con->set_message_handler(std::bind(&broadcast_server::on_message, this,
                                       std::placeholders::_1,
                                       std::placeholders::_2, d));

    int last_level = downsample_levels - 1;
    {
        std::scoped_lock lk(waterfall_slice_mtx[last_level]);
        auto it =
            waterfall_slices[last_level].insert({{0, min_waterfall_fft}, d});
        d->it = it;
    }
}
void broadcast_server::on_close_waterfall(connection_hdl hdl,
                                          std::shared_ptr<conn_data> &d) {
    int level = d->level;
    {
        std::scoped_lock lk(waterfall_slice_mtx[level]);
        waterfall_slices[level].erase(d->it);
    }
}

void broadcast_server::waterfall_send(std::shared_ptr<conn_data> &d,
                                      int8_t *buf, int len) {
    try {
        d->waterfall_encoder->send(buf, len, frame_num, d->l << d->level,
                                   d->r << d->level);
    } catch (...) {
        // std::cout << "waterfall client disconnect" << std::endl;
    }
    d->processing = 0;
}
void broadcast_server::waterfall_loop(float *fft_power,
                                      int8_t *fft_power_quantized_full,
                                      float *fft_power_scratch) {

    int base_idx = 0;
    // For IQ input, the lowest frequency is in the middle
    if (!is_real) {
        base_idx = fft_size / 2 + 1;
    }

    fft_power = (float *)__builtin_assume_aligned(fft_power, 32);
    fft_power_scratch =
        (float *)__builtin_assume_aligned(fft_power_scratch, 32);
    fft_power_quantized_full =
        (int8_t *)__builtin_assume_aligned(fft_power_quantized_full, 32);

    int8_t *fft_power_quantized;
    /* Equivalent code:
     * for(int i = base_idx;i < fft_result_size;i++) {
     *     fft_power[0][i - base_idx] = fft_buffer[i][0] *
     * fft_buffer[i][0] + fft_buffer[i][1] * fft_buffer[i][1];
     * }
     * for(int i = 0;i < base_idx;i++) {
     *     fft_power[0][i + (fft_result_size - base_idx)] =
     * fft_buffer[i][0] * fft_buffer[i][0] + fft_buffer[i][1] *
     * fft_buffer[i][1];
     * }
     */
    // Calculate the power level for level 0
    // For IQ signal, the lowest frequency is in the middle, so shift
    // according to base_idx
    volk_32fc_magnitude_squared_32f(fft_power,
                                    (lv_32fc_t *)&fft_buffer[base_idx],
                                    fft_result_size - base_idx);
    volk_32fc_magnitude_squared_32f(&fft_power[fft_result_size - base_idx],
                                    (lv_32fc_t *)fft_buffer, base_idx);

    fft_power_quantized = fft_power_quantized_full;
    // Downsample the rest of the levels
    for (int i = 0; i < downsample_levels; i++) {

        // Equivalent code: 6 * log2(fft_power[j]) + 64
        // Process in chunks of 4096 to take advantage of cache
        for (int k = 0; k < (fft_result_size >> i); k += 4096) {
            if (i > 0) {
                for (int j = k; j < k + 4096; j++) {
                    fft_power[j] =
                        std::max(fft_power[j * 2], fft_power[j * 2 + 1]);
                }
            }
            std::copy(&fft_power[k], &fft_power[k + 4096],
                      &fft_power_scratch[k]);
            // volk_32f_s32f_multiply_32f(&fft_power_scratch[k],
            // &fft_power_scratch[k], 64);
            volk_32f_log2_32f(&fft_power_scratch[k], &fft_power_scratch[k],
                              4096);
            volk_32f_s32f_multiply_32f(&fft_power_scratch[k],
                                       &fft_power_scratch[k], 6, 4096);
            for (int j = k; j < k + 4096; j++) {
                fft_power_scratch[j] = fft_power_scratch[j] + 128;
            }

            volk_32f_s32f_convert_8i(&fft_power_quantized[k],
                                     &fft_power_scratch[k], 1, 4096);
        }

        // Iterate over each waterfall client and send each slice
        std::scoped_lock lg(waterfall_slice_mtx[i]);
        for (auto &[slice, data] : waterfall_slices[i]) {
            auto &[l_idx, r_idx] = slice;
            // If the client is slow, avoid unnecessary buffering and
            // drop the packet
            if (m_server.get_con_from_hdl(data->hdl)
                    ->get_buffered_amount() <= 1000000) {
                if (server_threads == 1) {
                    waterfall_send(data, &fft_power_quantized[l_idx],
                                   r_idx - l_idx);
                } else {
                    if (!data->processing) {
                        data->processing = 1;
                        m_server.get_io_service().post(
                            m_server.get_con_from_hdl(data->hdl)
                                ->get_strand()
                                ->wrap(std::bind(
                                    &broadcast_server::waterfall_send, this,
                                    data, &fft_power_quantized[l_idx],
                                    r_idx - l_idx)));
                    }
                }
            }
        }

        // Prevent overwrite of previous level's quantized waterfall
        fft_power_quantized += (fft_result_size >> i);
    }
    waterfall_processing = 0;
}
void broadcast_server::waterfall_task() {
    while (running) {
        std::shared_lock lk(fft_mutex);
        // Wait for the FFT to be ready
        fft_processed.wait(lk, [&] { return waterfall_processing; });
        waterfall_loop(fft_power, fft_power_quantized_full, fft_power_scratch);
        waterfall_processing = 0;
    }
}