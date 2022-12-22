#ifndef SPECTRUMSERVER_H
#define SPECTRUMSERVER_H

#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <fftw3.h>
#include <toml++/toml.h>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "audio.h"
#include "fft.h"
#include "samplereader.h"
#include "utils.h"
#include "waterfallcompression.h"
#include "websocket.h"

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

enum demodulation_mode { USB, LSB, AM, FM };

enum waterfall_compressor { WATERFALL_ZSTD, WATERFALL_AV1 };

enum audio_compressor { AUDIO_FLAC, AUDIO_OPUS };

typedef struct conn_data conn_data;

// Maintains a sorted list of signal slice mapped to the connection
typedef std::multimap<std::pair<int, int>, std::shared_ptr<conn_data>>
    signal_list;

// Maintains a tiered list based on downsampling rate of waterfall slices to the
// connection
typedef std::vector<signal_list> waterfall_list;

struct conn_data {
    // Type of connection
    conn_type type;

    // User ID is sent for each client with waterfall + signal
    std::string user_id;

    // Unique id
    std::string unique_id;

    // Connection handle
    connection_hdl hdl;

    // Iterator of the data structure containing it
    signal_list::iterator it;

    // 0 frequency of the downconverted signal
    double audio_mid;

    int level;
    int frame_num;

    // User requested frequency range
    int l;
    int r;

    // User requested demodulation mode
    demodulation_mode demodulation;

    // Scratch space for the slice the user requested
    fftwf_complex *fft_slice_buf;
    uint8_t *waterfall_slice_buf;

    // Scratch space for audio demodulation
    int audio_fft_size;
    fftwf_complex *audio_fft_input;
    fftwf_complex *audio_complex_baseband;
    std::vector<float> audio_real;
    std::vector<float> audio_real_prev;
    std::vector<int32_t> audio_real_int16;

    // IFFT plans for demodulation
    fftwf_plan p_complex;
    fftwf_plan p_real;

    // For DC offset removal and AGC implementatino
    DCBlocker<float> dc;
    AGC<float> agc;

    // Compression codec variables for waterfall and Audio
    std::unique_ptr<WaterfallEncoder> waterfall_encoder;
    std::unique_ptr<AudioEncoder> encoder;

    int processing;

    ~conn_data() {
        if (this->type == AUDIO || this->type == SIGNAL) {
            // Cleanup the allocated memory
            fftwf_destroy_plan(this->p_real);
            fftwf_destroy_plan(this->p_complex);
            fftwf_free(this->audio_fft_input);
            fftwf_free(this->audio_complex_baseband);
        }
    }
};

struct conn_info {
    // Type of connection
    conn_type type;

    // User ID is sent for each client with waterfall + signal
    std::string user_id;

    // Iterator to the element inside the sorted list of slices
    signal_list::iterator it;

    // For waterfall
    int level;
};

typedef std::map<connection_hdl, conn_info, std::owner_less<connection_hdl>>
    con_list;
typedef std::set<connection_hdl, std::owner_less<connection_hdl>>
    event_con_list;

class broadcast_server {
  public:
    broadcast_server(std::unique_ptr<SampleConverter> reader,
                     toml::parse_result &config,
                     std::unordered_map<std::string, int64_t> &int_config,
                     std::unordered_map<std::string, std::string> &str_config);
    void run(uint16_t port);
    void stop();

    // Websocket handlers
    void on_open(connection_hdl hdl);
    void on_open_unknown(connection_hdl hdl);
    void send_basic_info(connection_hdl hdl);
    void on_message(connection_hdl hdl, server::message_ptr msg,
                    std::shared_ptr<conn_data> &d);
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
    void on_close_signal(connection_hdl hdl, std::shared_ptr<conn_data> &d);
    void signal_send(std::shared_ptr<conn_data> &d, fftwf_complex *buf,
                     int len);
    void signal_loop();
    void signal_loop_multi();
    void signal_task();

    // Waterfall functions
    void on_open_waterfall(connection_hdl hdl);
    void on_close_waterfall(connection_hdl hdl, std::shared_ptr<conn_data> &d);
    void waterfall_send(std::shared_ptr<conn_data> &d, int8_t *buf, int len);
    void waterfall_loop(int8_t *fft_power_quantized);
    void waterfall_task();

  private:
    std::unique_ptr<FFT> fft;
    std::unique_ptr<SampleConverter> reader;
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

    // Limits
    int limit_audio;
    int limit_waterfall;
    int limit_events;
    // Tracks which clients wants which signal
    signal_list signal_slices;
    std::mutex signal_slice_mtx;

    // Tracks which part of the waterfall the clients are requesting
    waterfall_list waterfall_slices;
    std::deque<std::mutex> waterfall_slice_mtx;

    event_con_list events_connections;
    std::unordered_map<std::string, std::tuple<int, double, int>>
        signal_changes;
    std::mutex signal_changes_mtx;

    // FFT output to send to clients
    fftwf_complex *fft_buffer;
    std::shared_mutex fft_mutex;
    std::condition_variable_any fft_processed;

    int waterfall_processing;
    int signal_processing;

    // Dedicated threads for FFT
    std::thread fft_thread;
};

#endif