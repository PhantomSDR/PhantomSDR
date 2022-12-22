#include "spectrumserver.h"

#define RAPIDJSON_HAS_STDSTRING 1
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

void broadcast_server::on_open(connection_hdl hdl) {
    server::connection_ptr con = m_server.get_con_from_hdl(hdl);
    std::string path = con->get_resource();

    if (path == "/audio") {
        on_open_signal(hdl, AUDIO);
    } else if (path == "/signal") {
        on_open_signal(hdl, SIGNAL);
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
    rapidjson::Document d;
    d.SetObject();
    d.AddMember("sps", sps, d.GetAllocator());
    d.AddMember("audio_max_sps", audio_max_sps, d.GetAllocator());
    d.AddMember("audio_max_fft", audio_max_fft_size, d.GetAllocator());
    d.AddMember("fft_size", fft_size, d.GetAllocator());
    d.AddMember("fft_result_size", fft_result_size, d.GetAllocator());
    d.AddMember("waterfall_size", min_waterfall_fft, d.GetAllocator());

    d.AddMember("basefreq", basefreq, d.GetAllocator());
    if (is_real) {
        d.AddMember("total_bandwidth", sps / 2, d.GetAllocator());
    } else {
        d.AddMember("total_bandwidth", sps, d.GetAllocator());
    }

    rapidjson::Value defaults(rapidjson::kObjectType);
    defaults.AddMember("frequency", default_frequency, d.GetAllocator());
    defaults.AddMember("modulation", default_mode_str, d.GetAllocator());
    defaults.AddMember("l", default_l, d.GetAllocator());
    defaults.AddMember("m", default_m, d.GetAllocator());
    defaults.AddMember("r", default_r, d.GetAllocator());

    d.AddMember("waterfall_compression", waterfall_compression_str,
                d.GetAllocator());
    d.AddMember("audio_compression", audio_compression_str, d.GetAllocator());

    d.AddMember("defaults", defaults, d.GetAllocator());

    // Stringify and send basic info
    rapidjson::StringBuffer json_buffer;
    json_buffer.Clear();
    rapidjson::Writer<rapidjson::StringBuffer> writer(json_buffer);
    d.Accept(writer);

    m_server.send(hdl, json_buffer.GetString(),
                  websocketpp::frame::opcode::text);
}

void broadcast_server::on_message(connection_hdl hdl, server::message_ptr msg,
                                  std::shared_ptr<conn_data> &d) {

    // Limit the amount of data received
    std::string payload = msg->get_payload().substr(0, 1024);
    std::string ip = m_server.get_con_from_hdl(hdl)->get_remote_endpoint();

    // Parse the JSON
    rapidjson::Document document;
    document.Parse(payload.c_str());
    conn_type type = d->type;

    // Error handing, ignore unexpected inputs
    if (document.HasParseError()) {
        return;
    }
    std::ostringstream command_log;
    std::string type_str;
    if (type == SIGNAL) {
        type_str = "Signal";
    } else if (type == AUDIO) {
        type_str = "Audio";
    } else if (type == WATERFALL) {
        type_str = "Waterfall";
    } else if (type == EVENTS) {
        type_str = "Events";
    }
    command_log << ip;
    command_log << " [" << type_str << " User: " << d->user_id << "]";
    command_log << " Message: " + payload;
    m_server.get_alog().write(websocketpp::log::alevel::app, command_log.str());

    if (!document.HasMember("cmd")) {
        return;
    }
    // Command to change to a different slice
    if (document["cmd"] == "window") {
        if (!document.HasMember("l") || !document.HasMember("r")) {
            return;
        }
        if (!document["l"].IsInt() || !document["r"].IsInt()) {
            return;
        }
        int new_l = document["l"].GetInt();
        int new_r = document["r"].GetInt();
        if (new_l < 0 || new_r > fft_result_size) {
            return;
        }
        if (type == SIGNAL || type == AUDIO) {
            if (!document.HasMember("m") || !document["m"].IsNumber()) {
                return;
            }
            if (new_r - new_l > audio_max_fft_size) {
                return;
            }
            // Change the data structures to reflect the changes
            {
                std::scoped_lock lk1(signal_slice_mtx);
                auto it = d->it;

                auto node = signal_slices.extract(it);
                node.key() = {new_l, new_r};
                it = signal_slices.insert(std::move(node));

                double new_m = document["m"].GetDouble();
                d->it = it;
                d->l = new_l;
                d->r = new_r;
                d->audio_mid = new_m;

                if (show_other_users) {
                    std::scoped_lock lk3(signal_changes_mtx);
                    signal_changes[d->unique_id] = {new_l, new_m, new_r};
                }
            }
        } else if (type == WATERFALL) {

            // Calculate which level should it be at
            // Each level decreases the amount of points available by 2
            // Use floating point to prevent integer rounding errors
            // At most min_waterfall_fft samples shall be sent
            float new_l_f = new_l;
            float new_r_f = new_r;
            int new_level = downsample_levels - 1;
            for (int i = 0; i < downsample_levels; i++) {
                new_level = i;
                if (new_r_f - new_l_f <= min_waterfall_fft) {
                    break;
                }
                new_l_f /= 2;
                new_r_f /= 2;
            }
            new_l = round(new_l_f);
            new_r = round(new_r_f);

            // Since the parameters are modified, output the new parameters

            {
                std::ostringstream command_log;
                command_log << ip;
                command_log << " [Waterfall User: " << d->user_id << "]";
                command_log << " Waterfall Level: " << new_level;
                command_log << " Waterfall L: " << new_l;
                command_log << " Waterfall R: " << new_r;
                m_server.get_alog().write(websocketpp::log::alevel::app,
                                          command_log.str());
            }

            // Change the waterfall data structures to reflect the changes
            auto it = d->it;
            int level = d->level;
            auto node = ([&] {
                std::scoped_lock lkl1(waterfall_slice_mtx[level]);
                return waterfall_slices[level].extract(it);
            })();

            {
                std::scoped_lock lkl2(waterfall_slice_mtx[new_level]);
                node.key() = {new_l, new_r};
                it = waterfall_slices[new_level].insert(std::move(node));
            }
            d->it = it;
            d->level = new_level;
            d->l = new_l;
            d->r = new_r;
        }
    } else if (document["cmd"] == "demodulation") {
        if (!document.HasMember("demodulation") ||
            !document["demodulation"].IsString()) {
            return;
        }
        std::string demodulation_str = document["demodulation"].GetString();
        // Update the demodulation type
        if (demodulation_str == "USB") {
            d->demodulation = USB;
        } else if (demodulation_str == "LSB") {
            d->demodulation = LSB;
        } else if (demodulation_str == "AM") {
            d->demodulation = AM;
        } else if (demodulation_str == "FM") {
            d->demodulation = FM;
        }
    } else if (document["cmd"] == "userid") {
        if (!document.HasMember("userid") || !document["userid"].IsString()) {
            return;
        }
        // Used for correlating between signal and waterfall sockets
        std::string user_id = document["userid"].GetString();
        d->user_id = user_id.substr(0, 32);
    }
}