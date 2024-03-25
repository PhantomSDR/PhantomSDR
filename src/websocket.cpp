#include "client.h"
#include "signal.h"
#include "spectrumserver.h"
#include "waterfall.h"

#include "glaze/glaze.hpp"

void broadcast_server::on_open(connection_hdl hdl) {
    server::connection_ptr con = m_server.get_con_from_hdl(hdl);
    std::string path = con->get_resource();

    if (path == "/audio") {
        on_open_signal(hdl, AUDIO);
    } else if (path == "/signal") {
        // on_open_signal(hdl, SIGNAL);
    } else if (path == "/waterfall") {
        on_open_waterfall(hdl);
    } else if (path == "/waterfall_raw") {
        // on_open_waterfall_raw(hdl);
    } else if (path == "/events") {
        on_open_events(hdl);
    } else {
        on_open_unknown(hdl);
    }
}

void broadcast_server::on_open_unknown(connection_hdl hdl) {
    server::connection_ptr con = m_server.get_con_from_hdl(hdl);
    con->set_close_handler([](connection_hdl) {}); // No-op

    // Immediately close
    websocketpp::lib::error_code ec;
    m_server.close(hdl, websocketpp::close::status::going_away, "", ec);
}

void broadcast_server::send_basic_info(connection_hdl hdl) {

    // Example format:
    // "{\"sps\":1000000,\"fft_size\":65536,\"clientid\":\"123\",\"basefreq\":123}";
    // Craft a JSON string for the client
    glz::json_t json = {
        {"sps", sps},
        {"audio_max_sps", audio_max_sps},
        {"audio_max_fft", audio_max_fft_size},
        {"fft_size", fft_size},
        {"fft_result_size", fft_result_size},
        {"waterfall_size", min_waterfall_fft},
        {"basefreq", basefreq},
        {"total_bandwidth", is_real ? sps / 2 : sps},
        {"defaults",
         {{"frequency", default_frequency},
          {"modulation", default_mode_str},
          {"l", default_l},
          {"m", default_m},
          {"r", default_r}}},
        {"waterfall_compression", waterfall_compression_str},
        {"audio_compression", audio_compression_str},
    };
    m_server.send(hdl, glz::write_json(json), websocketpp::frame::opcode::text);
}

// PacketSender---------------------------------------------------------------
void broadcast_server::send_binary_packet(
    connection_hdl hdl,
    const std::initializer_list<std::pair<const void *, size_t>> &bufs) {
    auto con = m_server.get_con_from_hdl(hdl);
    auto total_size =
        std::accumulate(bufs.begin(), bufs.end(), 0,
                        [](size_t acc, auto &p) { return acc + p.second; });
    auto msg_ptr =
        con->get_message(websocketpp::frame::opcode::binary, total_size);
    for (auto &[buf, len] : bufs) {
        msg_ptr->append_payload(buf, len);
    }
    con->send(msg_ptr);
}
void broadcast_server::send_binary_packet(connection_hdl hdl, const void *buf,
                                          size_t len) {
    m_server.send(hdl, buf, len, websocketpp::frame::opcode::binary);
}
void broadcast_server::send_text_packet(
    connection_hdl hdl, const std::initializer_list<std::string> &data) {
    auto con = m_server.get_con_from_hdl(hdl);
    auto total_size = std::accumulate(
        data.begin(), data.end(), 0,
        [](size_t acc, const std::string &str) { return acc + str.size(); });
    auto msg_ptr =
        con->get_message(websocketpp::frame::opcode::text, total_size);
    for (auto &str : data) {
        msg_ptr->append_payload(str);
    }
    con->send(msg_ptr);
}
void broadcast_server::send_text_packet(connection_hdl hdl,
                                        const std::string &str) {
    m_server.send(hdl, str, websocketpp::frame::opcode::text);
}
void broadcast_server::log(connection_hdl, const std::string &str) {
    m_server.get_alog().write(websocketpp::log::alevel::app, str);
}
std::string broadcast_server::ip_from_hdl(connection_hdl hdl) {
    return m_server.get_con_from_hdl(hdl)->get_remote_endpoint();
}
waterfall_slices_t &broadcast_server::get_waterfall_slices() {
    return waterfall_slices;
}
waterfall_mutexes_t &broadcast_server::get_waterfall_slice_mtx() {
    return waterfall_slice_mtx;
}
signal_slices_t &broadcast_server::get_signal_slices() { return signal_slices; }
std::mutex &broadcast_server::get_signal_slice_mtx() {
    return signal_slice_mtx;
}

void broadcast_server::on_message(connection_hdl, server::message_ptr msg,
                                  std::shared_ptr<Client> &client) {

    // Limit the amount of data received
    std::string payload = msg->get_payload().substr(0, 1024);
    client->on_message(payload);
}

void broadcast_server::on_open_signal(connection_hdl hdl,
                                      conn_type signal_type) {
    send_basic_info(hdl);

    int audio_fft_size = ceil((double)audio_max_sps * fft_size / sps / 4.) * 4;
    std::shared_ptr<AudioClient> client = std::make_shared<AudioClient>(
        hdl, *this, audio_compression, is_real, audio_fft_size, audio_max_sps,
        fft_result_size);

    client->set_audio_demodulation(default_mode);
    {
        std::scoped_lock lg(signal_slice_mtx);
        auto it = signal_slices.insert({{0, 0}, client});
        client->it = it;
    }
    // Default slice
    client->set_audio_range(default_l, default_m, default_r);

    server::connection_ptr con = m_server.get_con_from_hdl(hdl);

    con->set_close_handler(std::bind(&AudioClient::on_close, client));
    con->set_message_handler(std::bind(
        &broadcast_server::on_message, this, std::placeholders::_1,
        std::placeholders::_2, std::static_pointer_cast<Client>(client)));
}

// Iterates through the client list to send the slices
std::vector<std::future<void>> broadcast_server::signal_loop() {
    int base_idx = 0;
    if (!is_real) {
        base_idx = fft_size / 2 + 1;
    }
    std::scoped_lock lg(signal_slice_mtx);
    auto &io_service = m_server.get_io_service();

    // Completion futures
    std::vector<std::future<void>> futures;
    futures.reserve(signal_slices.size());

    // Send the apprioriate signal slice to the client
    for (auto &[slice, data] : signal_slices) {
        auto &[l_idx, r_idx] = slice;
        // If the client is slow, avoid unnecessary buffering and drop the
        // audio
        auto con = m_server.get_con_from_hdl(data->hdl);
        if (con->get_buffered_amount() > 1000000) {
            continue;
        }
        // Equivalent to
        // data->send_audio(&fft_buffer[(l_idx + base_idx) % fft_result_size],
        // frame_num);
        futures.emplace_back(io_service.post(boost::asio::use_future(std::bind(
            &AudioClient::send_audio, data,
            &fft_buffer[(l_idx + base_idx) % fft_result_size], frame_num))));
    }
    return futures;
}

void broadcast_server::on_open_waterfall(connection_hdl hdl) {
    send_basic_info(hdl);

    // Set default to the entire spectrum
    std::shared_ptr<WaterfallClient> client = std::make_shared<WaterfallClient>(
        hdl, *this, waterfall_compression, min_waterfall_fft);
    {
        std::scoped_lock lk(waterfall_slice_mtx[0]);
        auto it = waterfall_slices[0].insert({{0, min_waterfall_fft}, client});
        client->it = it;
    }
    client->set_waterfall_range(downsample_levels - 1, 0, min_waterfall_fft);

    server::connection_ptr con = m_server.get_con_from_hdl(hdl);
    con->set_close_handler(std::bind(&WaterfallClient::on_close, client));
    con->set_message_handler(std::bind(
        &broadcast_server::on_message, this, std::placeholders::_1,
        std::placeholders::_2, std::static_pointer_cast<Client>(client)));
}

std::vector<std::future<void>>
broadcast_server::waterfall_loop(int8_t *fft_power_quantized) {
    // Completion futures
    std::vector<std::future<void>> futures;
    futures.reserve(signal_slices.size());

    auto &io_service = m_server.get_io_service();
    for (int i = 0; i < downsample_levels; i++) {
        // Iterate over each waterfall client and send each slice
        std::scoped_lock lg(waterfall_slice_mtx[i]);
        for (auto &[slice, data] : waterfall_slices[i]) {
            auto &[l_idx, r_idx] = slice;
            // If the client is slow, avoid unnecessary buffering and
            // drop the packet
            auto con = m_server.get_con_from_hdl(data->hdl);
            if (con->get_buffered_amount() > 1000000) {
                continue;
            }
            // Equivalent to
            // data->send_waterfall(&fft_power_quantized[l_idx],frame_num);
            futures.emplace_back(
                io_service.post(boost::asio::use_future(
                    std::bind(&WaterfallClient::send_waterfall, data,
                              &fft_power_quantized[l_idx], frame_num))));
        }

        // Prevent overwrite of previous level's quantized waterfall
        fft_power_quantized += (fft_result_size >> i);
    }
    return futures;
}