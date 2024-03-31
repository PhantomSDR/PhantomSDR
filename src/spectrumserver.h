#ifndef SPECTRUMSERVER_H
#define SPECTRUMSERVER_H

#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <toml++/toml.h>

#include "client.h"
#include "fft.h"
#include "samplereader.h"
#include "signal.h"
#include "waterfall.h"
#include "websocket.h"

using websocketpp::connection_hdl;

typedef std::set<connection_hdl, std::owner_less<connection_hdl>>
    event_con_list;

class broadcast_server : public PacketSender {
  public:
    broadcast_server(std::unique_ptr<SampleConverterBase> reader,
                     toml::parse_result &config);
    void run(uint16_t port);
    void stop();

    // Websocket handlers
    void on_open(connection_hdl hdl);
    void on_open_unknown(connection_hdl hdl);
    void send_basic_info(connection_hdl hdl);
    void on_message(connection_hdl hdl, server::message_ptr msg,
                    std::shared_ptr<Client> &d);
    void on_close(connection_hdl hdl);
    void on_http(connection_hdl hdl);

    // Events socket
    std::string get_event_info();
    std::string get_initial_state_info();
    void on_open_events(connection_hdl hdl);
    void on_message_control(connection_hdl hdl);
    void on_close_events(connection_hdl hdl);
    void set_event_timer();
    void on_timer(websocketpp::lib::error_code const &ec);

    // Main FFT loop to process input samples
    void fft_task();

    // Signal functions, audio demodulation
    void on_open_signal(connection_hdl hdl, conn_type signal_type);
    void on_close_signal(connection_hdl hdl, std::shared_ptr<AudioClient> &d);
    std::vector<std::future<void>> signal_loop();

    // Waterfall functions
    void on_open_waterfall(connection_hdl hdl);
    void on_close_waterfall(connection_hdl hdl,
                            std::shared_ptr<WaterfallClient> &d);
    std::vector<std::future<void>> waterfall_loop(int8_t *fft_power_quantized);

    virtual void send_binary_packet(
        connection_hdl hdl,
        const std::initializer_list<std::pair<const void *, size_t>> &bufs);
    virtual void send_binary_packet(connection_hdl hdl, const void *data,
                                    size_t size);
    virtual void
    send_text_packet(connection_hdl hdl,
                     const std::initializer_list<std::string> &data);
    virtual void send_text_packet(connection_hdl hdl, const std::string &data);
    virtual std::string ip_from_hdl(connection_hdl hdl);
    virtual void log(connection_hdl hdl, const std::string &msg);

    virtual waterfall_slices_t &get_waterfall_slices();
    virtual waterfall_mutexes_t &get_waterfall_slice_mtx();
    virtual signal_slices_t &get_signal_slices();
    virtual std::mutex &get_signal_slice_mtx();

    virtual void register_server();

    virtual void broadcast_signal_changes(const std::string &unique_id, int l,
                                          double m, int r);

  private:
    std::unique_ptr<FFT> fft;
    std::unique_ptr<SampleConverterBase> reader;
    server m_server;
    server::timer_ptr m_timer;

    // Server parameters
    int fft_size;
    int fft_result_size;
    int sps;
    int64_t basefreq;
    int min_waterfall_fft;
    bool is_real;
    int downsample_levels;
    int audio_max_sps;
    int audio_fft_size;
    int audio_max_fft_size;
    int fft_threads;
    std::string input_format;
    std::string m_docroot;
    bool running;
    bool show_other_users;
    int server_threads;
    int frame_num;
    waterfall_compressor waterfall_compression;
    std::string waterfall_compression_str;
    audio_compressor audio_compression;
    std::string audio_compression_str;

    // Default parameters
    int64_t default_frequency;
    double default_m;
    int default_l;
    int default_r;
    std::string default_mode_str;
    demodulation_mode default_mode;

/*

    id: String,
    name: String,
    hardware: Option<String>,
    antenna: Option<String>,
    bandwidth: f64,
    users: Option<i32>,
    remarks: Option<String>,
    description: Option<String>,
    base_frequency: f64,
    port: Option<i32>,
    url: Option<String>,
*/

    // Registration details
    struct {
        std::string password;
        std::string name;
        std::string hardware;
        std::string antenna;
        double bandwidth;
        int users;
        std::string remarks;
        std::string description;
        double base_frequency;
        bool https;
        std::optional<int> port;
        std::optional<std::string> url;
    } registration;
    bool registration_enable;

    // Limits
    int limit_audio;
    int limit_waterfall;
    int limit_events;
    // Tracks which clients wants which signal
    // Maintains a sorted list of signal slice mapped to the connection
    std::multimap<std::pair<int, int>, std::shared_ptr<AudioClient>>
        signal_slices;
    std::mutex signal_slice_mtx;

    // Tracks which part of the waterfall the clients are requesting
    // Maintains a tiered list based on downsampling rate of waterfall slices to
    // the connection
    std::vector<
        std::multimap<std::pair<int, int>, std::shared_ptr<WaterfallClient>>>
        waterfall_slices;
    std::deque<std::mutex> waterfall_slice_mtx;

    event_con_list events_connections;
    std::unordered_map<std::string, std::tuple<int, double, int>>
        signal_changes;
    std::mutex signal_changes_mtx;

    // FFT output to send to clients
    std::complex<float> *fft_buffer;
    // std::shared_mutex fft_mutex;
    std::condition_variable_any fft_processed;

    // Dedicated threads for FFT
    std::thread fft_thread;
};

#endif