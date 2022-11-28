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
void broadcast_server::waterfall_loop(int8_t *fft_power_quantized) {

    for (int i = 0; i < downsample_levels; i++) {

        // Iterate over each waterfall client and send each slice
        std::scoped_lock lg(waterfall_slice_mtx[i]);
        for (auto &[slice, data] : waterfall_slices[i]) {
            auto &[l_idx, r_idx] = slice;
            // If the client is slow, avoid unnecessary buffering and
            // drop the packet
            if (m_server.get_con_from_hdl(data->hdl)->get_buffered_amount() <=
                1000000) {
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
        // waterfall_loop(fft_power, fft_power_quantized_full,
        // fft_power_scratch);
        waterfall_processing = 0;
    }
}