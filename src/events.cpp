#include "spectrumserver.h"

#define RAPIDJSON_HAS_STDSTRING 1
#include "rapidjson/allocators.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

std::string broadcast_server::get_event_info() {
    if (!signal_changes.size()) {
        return "";
    }
    rapidjson::Document d;
    d.SetObject();

    rapidjson::Document::AllocatorType &allocator = d.GetAllocator();

    // Put in the number of clients connected
    size_t waterfall_users = 0;
    for (auto &it : waterfall_slices) {
        waterfall_users += it.size();
    }
    d.AddMember("waterfall_clients", waterfall_users, allocator);
    d.AddMember("signal_clients", signal_slices.size(), allocator);

    rapidjson::Value changes(rapidjson::kObjectType);

    if (show_other_users) {
        std::scoped_lock lk(signal_changes_mtx);
        for (auto &[userid, range] : signal_changes) {
            rapidjson::Value new_range(rapidjson::kArrayType);
            auto &[l, m, r] = range;
            new_range.PushBack(rapidjson::Value(l).Move(), allocator);
            new_range.PushBack(rapidjson::Value(m).Move(), allocator);
            new_range.PushBack(rapidjson::Value(r).Move(), allocator);

            changes.AddMember(
                rapidjson::Value(userid.c_str(), allocator).Move(), new_range,
                allocator);
        }
        signal_changes.clear();
    }

    d.AddMember("signal_changes", changes, allocator);

    // Stringify and send basic info
    rapidjson::StringBuffer json_buffer;
    json_buffer.Clear();
    rapidjson::Writer<rapidjson::StringBuffer> writer(json_buffer);
    d.Accept(writer);

    return json_buffer.GetString();
}

std::string broadcast_server::get_initial_state_info() {

    rapidjson::Document d;
    d.SetObject();

    rapidjson::Document::AllocatorType &allocator = d.GetAllocator();

    rapidjson::Value changes(rapidjson::kObjectType);

    for (auto &[slice, data] : signal_slices) {
        rapidjson::Value new_range(rapidjson::kArrayType);
        new_range.PushBack(rapidjson::Value(data->l).Move(), allocator);
        new_range.PushBack(rapidjson::Value(data->audio_mid).Move(),
                           allocator);
        new_range.PushBack(rapidjson::Value(data->r).Move(), allocator);

        changes.AddMember(
            rapidjson::Value(data->unique_id.c_str(), allocator).Move(),
            new_range, allocator);
    }
    d.AddMember("signal_list", changes, allocator);

    // Stringify and send basic info
    rapidjson::StringBuffer json_buffer;
    json_buffer.Clear();
    rapidjson::Writer<rapidjson::StringBuffer> writer(json_buffer);
    d.Accept(writer);

    return json_buffer.GetString();
}
void broadcast_server::on_open_events(connection_hdl hdl) {
    events_connections.insert(hdl);
    m_server.send(hdl, get_initial_state_info(),
                  websocketpp::frame::opcode::text);
    
    std::shared_ptr<conn_data> d = std::make_shared<conn_data>();
    d->hdl = hdl;
    d->type = EVENTS;

    server::connection_ptr con = m_server.get_con_from_hdl(hdl);
    con->set_close_handler(std::bind(&broadcast_server::on_close_events, this, std::placeholders::_1));
    con->set_message_handler(std::bind(&broadcast_server::on_message, this, std::placeholders::_1, std::placeholders::_2, d));
}
void broadcast_server::on_close_events(connection_hdl hdl) {
    events_connections.erase(hdl);
}

void broadcast_server::set_event_timer() {
    m_timer = m_server.set_timer(
        1000, std::bind(&broadcast_server::on_timer, this,
                                     std::placeholders::_1));
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