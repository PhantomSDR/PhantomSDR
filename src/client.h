#ifndef CLIENT_H
#define CLIENT_H

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <fftw3.h>
#include <websocketpp/connection.hpp>

using websocketpp::connection_hdl;

enum conn_type {
    SIGNAL,
    WATERFALL,
    AUDIO,
    EVENTS,
    WATERFALL_RAW,
    SIGNAL_RAW,
    UNKNOWN
};

constexpr const char *type_to_name(conn_type type) {
    switch (type) {
    case SIGNAL:
        return "Signal";
    case WATERFALL:
        return "Waterfall";
    case AUDIO:
        return "Audio";
    case EVENTS:
        return "Events";
    case WATERFALL_RAW:
        return "Waterfall Raw";
    case SIGNAL_RAW:
        return "Signal Raw";
    default:
        return "Unknown";
    }
}

enum demodulation_mode { USB, LSB, AM, FM };

enum waterfall_compressor { WATERFALL_ZSTD, WATERFALL_AV1 };

enum audio_compressor { AUDIO_FLAC, AUDIO_OPUS };

class WaterfallClient;
class AudioClient;
typedef std::vector<
    std::multimap<std::pair<int, int>, std::shared_ptr<WaterfallClient>>>
    waterfall_slices_t;
typedef std::deque<std::mutex> waterfall_mutexes_t;
typedef std::multimap<std::pair<int, int>, std::shared_ptr<AudioClient>>
    signal_slices_t;

class PacketSender {
  public:
    PacketSender() {}
    virtual void send_binary_packet(
        connection_hdl hdl,
        const std::initializer_list<std::pair<const void *, size_t>> &bufs) = 0;
    virtual void send_binary_packet(connection_hdl hdl, const void *data,
                                    size_t size);
    virtual void
    send_text_packet(connection_hdl hdl,
                     const std::initializer_list<std::string> &data) = 0;
    virtual void send_text_packet(connection_hdl hdl, const std::string &data);
    virtual std::string ip_from_hdl(connection_hdl hdl) = 0;
    virtual void log(connection_hdl hdl, const std::string &msg) = 0;

    virtual waterfall_slices_t &get_waterfall_slices() = 0;
    virtual waterfall_mutexes_t &get_waterfall_slice_mtx() = 0;
    virtual signal_slices_t &get_signal_slices() = 0;
    virtual std::mutex &get_signal_slice_mtx() = 0;

    virtual void broadcast_signal_changes(const std::string &unique_id, int l,
                                          double m, int r) = 0;

    virtual ~PacketSender() {}
};

class Client {
  public:
    Client(connection_hdl hdl, PacketSender &sender, conn_type type);
    void on_message(std::string &msg);

    virtual void on_window_message(int l, std::optional<double> &m, int r,
                                   std::optional<int> &level);
    virtual void on_demodulation_message(std::string &demodulation);
    virtual void on_userid_message(std::string &userid);
    virtual void on_mute(bool mute);

    // Type of connection
    conn_type type;

    // User ID is sent for each client with waterfall + signal
    std::string user_id;

    // Unique id
    std::string unique_id;

    // Connection handle
    connection_hdl hdl;
    PacketSender &sender;

    // 0 frequency of the downconverted signal
    double audio_mid;
    int frame_num;
    bool mute;

    // User requested frequency range
    int l;
    int r;

    int processing;
};

#endif