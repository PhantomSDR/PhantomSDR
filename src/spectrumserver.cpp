#include "spectrumserver.h"
#include "audio.h"
#include "samplereader.h"
#include "waterfallcompression.h"

#include <cstdio>
#include <iostream>

#ifdef CUFFT
#include <cuda.h>
#include <cuda_runtime_api.h>
#endif

#include <boost/algorithm/string.hpp>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <toml++/toml.h>

broadcast_server::broadcast_server(
    std::unique_ptr<SampleConverter> reader, toml::parse_result& config,
    std::unordered_map<std::string, int64_t> &int_config,
    std::unordered_map<std::string, std::string> &str_config)
    : reader{std::move(reader)}, fft_size{(int)int_config["fft_size"]},
      sps{(int)int_config["sps"]}, basefreq{int_config["frequency"]},
      min_waterfall_fft{(int)int_config["waterfall_size"]},
      is_real{str_config["signal"] == "real"}, audio_max_sps{(
                                                   int)int_config["audio_sps"]},
      fft_threads{(int)int_config["fft_threads"]},
      m_docroot{str_config["htmlroot"]}, running{false},
      show_other_users{int_config["otherusers"] > 0},
      server_threads{(int)int_config["threads"]}, frame_num{0} {

    limit_audio = config["limits"]["audio"].value_or(1000);
    limit_waterfall = config["limits"]["waterfall"].value_or(1000);
    limit_events = config["limits"]["events"].value_or(1000);

    // Set the parameters correct for real and IQ input
    // For IQ signal Leftmost frequency of IQ signal needs to be shifted left by
    // the sample rate
    if (is_real) {
        fft_result_size = fft_size / 2;
        basefreq = int_config["frequency"];
    } else {
        fft_result_size = fft_size;
        basefreq = int_config["frequency"] - sps / 2;
    }

    default_frequency = int_config["default_frequency"];
    if (default_frequency == -1) {
        default_frequency = basefreq + sps / 2;
    }

    default_m = (double)(default_frequency - basefreq) * fft_result_size / sps;
    int offsets_3 = (3000LL) * fft_result_size / sps;
    int offsets_5 = (5000LL) * fft_result_size / sps;
    int offsets_96 = (96000LL) * fft_result_size / sps;

    default_mode_str = str_config["modulation"];

    if (str_config["modulation"] == "LSB") {
        default_mode = LSB;
        default_l = default_m - offsets_3;
        default_r = default_m;
    } else if (str_config["modulation"] == "AM") {
        default_mode = AM;
        default_l = default_m - offsets_5;
        default_r = default_m + offsets_5;
    } else if (str_config["modulation"] == "FM") {
        default_mode = FM;
        default_l = default_m - offsets_5;
        default_r = default_m + offsets_5;
    } else if (str_config["modulation"] == "WBFM") {
        default_mode = FM;
        default_l = default_m - offsets_96;
        default_r = default_m + offsets_96;
    } else {
        default_mode = USB;
        default_l = default_m;
        default_r = default_m + offsets_3;
    }

    default_m = std::max(0., std::min((double)fft_result_size, default_m));
    default_l = std::max(0, std::min(fft_result_size, default_l));
    default_r = std::max(0, std::min(fft_result_size, default_r));

    audio_max_fft_size = ceil((double)audio_max_sps * fft_size / sps / 4.) * 4;

    waterfall_compression_str = str_config["waterfall_compression"];
    if (waterfall_compression_str == "zstd") {
        waterfall_compression = WATERFALL_ZSTD;
    } else if (waterfall_compression_str == "av1") {
#ifdef HAS_LIBAOM
        waterfall_compression = WATERFALL_AV1;
#else
        throw "AV1 support not compiled in";
#endif
    }

    audio_compression_str = str_config["audio_compression"];
    if (audio_compression_str == "flac") {
        audio_compression = AUDIO_FLAC;
    } else if (audio_compression_str == "opus") {
#ifdef HAS_OPUS
        audio_compression = AUDIO_OPUS;
#else
        throw "Opus support not compiled in";
#endif
    }

    fft_accelerator accelerator = CPU_FFTW;
    if (str_config["accelerator"] == "cuda") {
        accelerator = GPU_cuFFT;
        std::cout << "Using CUDA" << std::endl;
    } else if (str_config["accelerator"] == "opencl") {
        accelerator = GPU_clFFT;
        std::cout << "Using OpenCL" << std::endl;
    }

    if (accelerator == GPU_cuFFT) {
#ifdef CUFFT
        int count;
        cudaGetDeviceCount(&count);
        if (!count) {
            std::cout << "No CUDA GPUs found" << std::endl;
            std::exit(0);
        }
        fft = std::make_unique<cuFFT>(fft_size, fft_threads);
#else
        throw "CUDA support is not compiled in";
#endif
    } else if (accelerator == GPU_clFFT) {
#ifdef CLFFT
        fft = std::make_unique<clFFT>(fft_size, fft_threads);
#else
        throw "OpenCL support is not compiled in";
#endif
    } else {
        fft = std::make_unique<FFTW>(fft_size, fft_threads);
    }

    // Calculate number of downsampling levels for fft
    downsample_levels = 0;
    for (int cur_fft = fft_result_size; cur_fft >= min_waterfall_fft;
         cur_fft /= 2) {
        downsample_levels++;
    }

    // Initialize the websocket server
    m_server.init_asio();
    m_server.clear_access_channels(websocketpp::log::alevel::frame_header |
                                   websocketpp::log::alevel::frame_payload);

    m_server.set_open_handler(
        std::bind(&broadcast_server::on_open, this, std::placeholders::_1));
    // m_server.set_close_handler(bind(&broadcast_server::on_close, this,
    // ::_1)); m_server.set_message_handler(bind(&broadcast_server::on_message,
    // this, ::_1, ::_2));
    m_server.set_http_handler(
        std::bind(&broadcast_server::on_http, this, std::placeholders::_1));

    // Init data structures
    waterfall_slices.resize(downsample_levels);
    waterfall_slice_mtx.resize(downsample_levels);

    waterfall_processing = 0;
    signal_processing = 0;
}

void broadcast_server::run(uint16_t port) {
    // Start the threads and handle the network
    running = true;
    m_server.set_listen_backlog(8192);
    m_server.set_reuse_addr(true);
    try {
        m_server.listen(port);
    } catch (...) { // Listen on IPv4 only if IPv6 is not supported
        m_server.listen(websocketpp::lib::asio::ip::tcp::v4(), port);
    }
    m_server.start_accept();
    fft_thread = std::thread(&broadcast_server::fft_task, this);
    // signal_thread = std::thread(&broadcast_server::signal_task, this);
    // waterfall_thread = std::thread(&broadcast_server::waterfall_task, this);
    set_event_timer();

    if (server_threads == 1) {
        m_server.run();
    } else {
        std::vector<std::thread> threads;
        for (int i = 0; i < server_threads; i++) {
            threads.emplace_back(std::thread([&] { m_server.run(); }));
        }
        for (int i = 0; i < server_threads; i++) {
            threads[i].join();
        }
    }
    fft_thread.join();
    // signal_thread.join();
    // waterfall_thread.join();

    operator delete[](fft_power, std::align_val_t(32));
    operator delete[](fft_power_scratch, std::align_val_t(32));
    operator delete[](fft_power_quantized_full, std::align_val_t(32));
}
void broadcast_server::stop() {
    running = false;
    signal_processing = 1;
    waterfall_processing = 1;
    fft_processed.notify_all();

    m_server.stop_listening();
    for (auto &[slice, data] : signal_slices) {
        websocketpp::lib::error_code ec;
        try {
            m_server.close(data->hdl, websocketpp::close::status::going_away,
                           "", ec);
        } catch (...) {
        }
    }
    for (auto &waterfall_slice : waterfall_slices) {
        for (auto &[slice, data] : waterfall_slice) {
            websocketpp::lib::error_code ec;
            try {
                m_server.close(data->hdl,
                               websocketpp::close::status::going_away, "", ec);
            } catch (...) {
            }
        }
    }
    for (auto &it : events_connections) {
        websocketpp::lib::error_code ec;
        try {
            m_server.close(it, websocketpp::close::status::going_away, "", ec);
        } catch (...) {
        }
    }
}
broadcast_server *g_signal;

int main(int argc, char **argv) {
    // Parse the options
    po::options_description desc("Options");
    desc.add_options()("help", "produce help message")(
        "config,c", po::value<std::string>()->default_value("config.toml"),
        "config file");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 1;
    }

    auto config = toml::parse_file(vm["config"].as<std::string>());

    std::unordered_map<std::string, std::string> str_config;
    std::unordered_map<std::string, int64_t> int_config;

    int_config["port"] = config["server"]["port"].value_or(9002);
    str_config["host"] = config["server"]["host"].value_or("0.0.0.0");
    int_config["threads"] = config["server"]["threads"].value_or(1);
     
    std::optional<int> sps = config["input"]["sps"].value<int>();
    if (!sps.has_value()) {
        std::cout << "Missing sample rate" << std::endl;
        return 0;
    }
    int_config["sps"] = sps.value();

    std::optional<int64_t> frequency =
        config["input"]["frequency"].value<int64_t>();
    if (!frequency.has_value()) {
        std::cout << "Missing frequency" << std::endl;
        return 0;
    }
    int_config["frequency"] = frequency.value();

    std::optional<std::string> signal_type =
        config["input"]["signal"].value<std::string>();
    std::string signal_type_str =
        signal_type.has_value()
            ? boost::algorithm::to_lower_copy(signal_type.value())
            : "";
    if (!signal_type.has_value() ||
        (signal_type_str != "real" && signal_type_str != "iq")) {
        std::cout << "Specify either real or IQ input" << std::endl;
        return 0;
    }
    str_config["signal"] = signal_type_str;

    std::optional<std::string> driver_type =
        config["input"]["driver"]["name"].value<std::string>();
    if (!driver_type.has_value()) {
        std::cout << "Specify an input driver" << std::endl;
        return 0;
    }
    std::string driver_str = driver_type.value();
    str_config["driver"] = driver_str;

    std::string input_format =
        config["input"]["driver"]["format"].value_or("f32");
    boost::algorithm::to_lower(input_format);
    str_config["input_format"] = input_format;

    str_config["accelerator"] = config["input"]["accelerator"].value_or("none");

    int_config["fft_size"] = config["input"]["fft_size"].value_or(131072);
    int_config["audio_sps"] = config["input"]["audio_sps"].value_or(12000);
    int_config["waterfall_size"] =
        config["input"]["waterfall_size"].value_or(1024);
    int_config["fft_threads"] = config["input"]["fft_threads"].value_or(1);
    int_config["otherusers"] = config["server"]["otherusers"].value_or(1);

    int_config["default_frequency"] =
        config["input"]["defaults"]["frequency"].value_or(-1);
    str_config["modulation"] = boost::algorithm::to_upper_copy<std::string>(
        config["input"]["defaults"]["modulation"].value_or("USB"));

    str_config["waterfall_compression"] =
        config["input"]["waterfall_compression"].value_or("zstd");
    str_config["audio_compression"] =
        config["input"]["audio_compression"].value_or("flac");

    // Initialise FFT threads if requested for multithreaded
    if (int_config["fft_threads"] > 1) {
        fftwf_init_threads();
    }
    str_config["htmlroot"] = config["server"]["html_root"].value_or("html/");

    // Set input to binary
    freopen(NULL, "rb", stdin);
    std::unique_ptr<SampleReader> reader =
        std::make_unique<FileSampleReader>(stdin);
    std::unique_ptr<SampleConverter> driver;

    if (input_format == "u8") {
        driver = std::make_unique<Uint8SampleConverter>(std::move(reader));
    } else if (input_format == "s8") {
        driver = std::make_unique<Int8SampleConverter>(std::move(reader));
    } else if (input_format == "u16") {
        driver = std::make_unique<Uint16SampleConverter>(std::move(reader));
    } else if (input_format == "s16") {
        driver = std::make_unique<Int16SampleConverter>(std::move(reader));
    } else if (input_format == "f32") {
        driver = std::make_unique<Float32SampleConverter>(std::move(reader));
    } else if (input_format == "f64") {
        driver = std::make_unique<Float64SampleConverter>(std::move(reader));
    } else {
        std::cout << "Unknown input format: " << input_format << std::endl;
        return 1;
    }

    broadcast_server server(std::move(driver), config, int_config, str_config);
    g_signal = &server;
    std::signal(SIGINT, [](int) { g_signal->stop(); });
    server.run(int_config["port"]);
    std::exit(0);
}
