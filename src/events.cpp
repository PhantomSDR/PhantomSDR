#include "spectrumserver.h"

#include "glaze/glaze.hpp"

/* clang-format off */
struct event_info {
    size_t waterfall_clients;
    size_t signal_clients;
    std::unordered_map<std::string, std::tuple<int, double, int>> signal_changes;
};

template <> 
struct glz::meta<event_info>
{
    using T = event_info;
    static constexpr auto value = object(
        "waterfall_clients", &T::waterfall_clients,
        "signal_clients", &T::signal_clients,
        "signal_changes", &T::signal_changes
    );
};
/* clang-format on */

std::string broadcast_server::get_event_info() {
    if (!signal_changes.size()) {
        return "";
    }
    event_info info;
    // Put in the number of clients connected
    info.waterfall_clients = std::accumulate(
        waterfall_slices.begin(), waterfall_slices.end(), 0,
        [](size_t acc, const auto &it) { return acc + it.size(); });
    info.signal_clients = signal_slices.size();
    if (show_other_users) {
        std::scoped_lock lk(signal_changes_mtx);
        info.signal_changes = std::move(signal_changes);
    }
    return glz::write_json(info);
}

std::string broadcast_server::get_initial_state_info() {

    event_info info;
    // Put in the number of clients connected
    info.waterfall_clients = std::accumulate(
        waterfall_slices.begin(), waterfall_slices.end(), 0,
        [](size_t acc, const auto &it) { return acc + it.size(); });
    info.signal_clients = signal_slices.size();
    if (show_other_users) {
        std::scoped_lock lk(signal_slice_mtx);
        info.signal_changes.reserve(signal_slices.size());
        for (auto &[slice, data] : signal_slices) {
            info.signal_changes.emplace(data->get_unique_id(),
                                        std::tuple<int, double, int>{
                                            data->l, data->audio_mid, data->r});
        }
    }
    return glz::write_json(info);
}
void broadcast_server::broadcast_signal_changes(const std::string &unique_id,
                                                int l, double audio_mid,
                                                int r) {
    if (!show_other_users) {
        return;
    }
    std::scoped_lock lk(signal_changes_mtx);
    signal_changes[unique_id] = {l, audio_mid, r};
}

void broadcast_server::on_open_events(connection_hdl hdl) {
    events_connections.insert(hdl);
    m_server.send(hdl, get_initial_state_info(),
                  websocketpp::frame::opcode::text);

    server::connection_ptr con = m_server.get_con_from_hdl(hdl);
    con->set_close_handler(std::bind(&broadcast_server::on_close_events, this,
                                     std::placeholders::_1));
    con->set_message_handler([](connection_hdl, server::message_ptr) {
        // Ignore messages
    });
}
void broadcast_server::on_close_events(connection_hdl hdl) {
    events_connections.erase(hdl);
}

void broadcast_server::set_event_timer() {
    m_timer = m_server.set_timer(1000, std::bind(&broadcast_server::on_timer,
                                                 this, std::placeholders::_1));
}

void broadcast_server::on_timer(websocketpp::lib::error_code const &ec) {
    if (ec) {
        // There was an error, stop sending control messages
        m_server.get_alog().write(websocketpp::log::alevel::app,
                                  "Timer Error: " + ec.message());
        return;
    }
    std::string info = get_event_info();
    // Broadcast count to all connections
    if (info.length() != 0) {
        for (auto &it : events_connections) {
            try {
                m_server.send(it, info, websocketpp::frame::opcode::text);
            } catch (...) {
            }
        }
    }
    // Send info every second
    if (running) {
        set_event_timer();
    }
}
