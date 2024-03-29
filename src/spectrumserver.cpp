#include "spectrumserver.h"
#include "samplereader.h"

#include <cstdio>
#include <iostream>
#include <random>
#include <thread>
#include <boost/algorithm/string.hpp>
#include <toml++/toml.h>
#include <cstdlib> // For rand() and srand()
#include <ctime>   // For time()
#include <sstream> // For std::stringstream
#include <iostream> // For std::cout
#include <curl/curl.h> // For cURL functionality

toml::table config;

broadcast_server::broadcast_server(
    std::unique_ptr<SampleConverterBase> reader, toml::parse_result &config)
    : reader{std::move(reader)}, frame_num{0} {

    server_threads = config["server"]["threads"].value_or(1);

    // Read in configuration
    std::optional<int> sps_config = config["input"]["sps"].value<int>();
    if (!sps_config.has_value()) {
        throw "Missing sample rate";
    }
    sps = sps_config.value();

    std::optional<int64_t> frequency =
        config["input"]["frequency"].value<int64_t>();
    if (!frequency.has_value()) {
        throw "Missing frequency";
    }

    std::string accelerator_str =
        config["input"]["accelerator"].value_or("none");

    fft_threads = config["input"]["fft_threads"].value_or(1);

    std::optional<std::string> signal_type =
        config["input"]["signal"].value<std::string>();
    std::string signal_type_str =
        signal_type.has_value()
            ? boost::algorithm::to_lower_copy(signal_type.value())
            : "";
    if (!signal_type.has_value() ||
        (signal_type_str != "real" && signal_type_str != "iq")) {
        throw "Invalid signal type, specify either real or IQ input";
    }

    is_real = signal_type_str == "real";

    fft_size = config["input"]["fft_size"].value_or(131072);
    audio_max_sps = config["input"]["audio_sps"].value_or(12000);
    min_waterfall_fft = config["input"]["waterfall_size"].value_or(1024);
    show_other_users = config["server"]["otherusers"].value_or(1) > 0;

    default_frequency =
        config["input"]["defaults"]["frequency"].value_or(basefreq);
    default_mode_str = boost::algorithm::to_upper_copy<std::string>(
        config["input"]["defaults"]["modulation"].value_or("USB"));

    waterfall_compression_str =
        config["input"]["waterfall_compression"].value_or("zstd");
    audio_compression_str =
        config["input"]["audio_compression"].value_or("flac");

    m_docroot = config["server"]["html_root"].value_or("html/");

    limit_audio = config["limits"]["audio"].value_or(1000);
    limit_waterfall = config["limits"]["waterfall"].value_or(1000);
    limit_events = config["limits"]["events"].value_or(1000);

    // Set the parameters correct for real and IQ input
    // For IQ signal Leftmost frequency of IQ signal needs to be shifted left by
    // the sample rate
    if (is_real) {
        fft_result_size = fft_size / 2;
        basefreq = frequency.value();
    } else {
        fft_result_size = fft_size;
        basefreq = frequency.value() - sps / 2;
    }

    if (default_frequency == -1) {
        default_frequency = basefreq + sps / 2;
    }

    if (is_real) {
        default_m =
            (double)(default_frequency - basefreq) * fft_result_size * 2 / sps;
    } else {
        default_m =
            (double)(default_frequency - basefreq) * fft_result_size / sps;
    }
    int offsets_3 = (3000LL) * fft_result_size / sps;
    int offsets_5 = (5000LL) * fft_result_size / sps;
    int offsets_96 = (96000LL) * fft_result_size / sps;

    if (default_mode_str == "LSB") {
        default_mode = LSB;
        default_l = default_m - offsets_3;
        default_r = default_m;
    } else if (default_mode_str == "AM") {
        default_mode = AM;
        default_l = default_m - offsets_5;
        default_r = default_m + offsets_5;
    } else if (default_mode_str == "FM") {
        default_mode = FM;
        default_l = default_m - offsets_5;
        default_r = default_m + offsets_5;
    } else if (default_mode_str == "WBFM") {
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

    if (waterfall_compression_str == "zstd") {
        waterfall_compression = WATERFALL_ZSTD;
    } else if (waterfall_compression_str == "av1") {
#ifdef HAS_LIBAOM
        waterfall_compression = WATERFALL_AV1;
#else
        throw "AV1 support not compiled in";
#endif
    }

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
    if (accelerator_str == "cuda") {
        accelerator = GPU_cuFFT;
        std::cout << "Using CUDA" << std::endl;
    } else if (accelerator_str == "opencl") {
        accelerator = GPU_clFFT;
        std::cout << "Using OpenCL" << std::endl;
    } else if (accelerator_str == "mkl") {
        accelerator = CPU_mklFFT;
        std::cout << "Using MKL" << std::endl;
    }

    // Calculate number of downsampling levels for fft
    downsample_levels = 0;
    for (int cur_fft = fft_result_size; cur_fft >= min_waterfall_fft;
         cur_fft /= 2) {
        downsample_levels++;
    }

    if (accelerator == GPU_cuFFT) {
#ifdef CUFFT
        fft = std::make_unique<cuFFT>(fft_size, fft_threads, downsample_levels);
#else
        throw "CUDA support is not compiled in";
#endif
    } else if (accelerator == GPU_clFFT) {
#ifdef CLFFT
        fft = std::make_unique<clFFT>(fft_size, fft_threads, downsample_levels);
#else
        throw "OpenCL support is not compiled in";
#endif
    } else if (accelerator == CPU_mklFFT) {
#ifdef MKL
        fft =
            std::make_unique<mklFFT>(fft_size, fft_threads, downsample_levels);
#else
        throw "MKL support is not compiled in";
#endif
    } else {
        fft = std::make_unique<FFTW>(fft_size, fft_threads, downsample_levels);
    }
    fft->set_output_additional_size(audio_max_fft_size);

    // Initialize the websocket server
    m_server.init_asio();
    m_server.clear_access_channels(websocketpp::log::alevel::frame_header |
                                   websocketpp::log::alevel::frame_payload);

    m_server.set_open_handler(
        std::bind(&broadcast_server::on_open, this, std::placeholders::_1));
    m_server.set_http_handler(
        std::bind(&broadcast_server::on_http, this, std::placeholders::_1));

    // Init data structures
    waterfall_slices.resize(downsample_levels);
    waterfall_slice_mtx.resize(downsample_levels);
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

    set_event_timer();
    std::vector<std::thread> threads;
    // Spawn one less thread, use main thread as well
    for (int i = 0; i < server_threads - 1; i++) {
        threads.emplace_back(std::thread([&] { m_server.run(); }));
    }
    m_server.run();
    for (int i = 0; i < server_threads - 1; i++) {
        threads[i].join();
    }
    fft_thread.join();
}

// To register on http://sdr-list.xyz
void broadcast_server::update_websdr_list() {
    // Seed the random number generator
    std::srand(std::time(nullptr));

    int port = config["server"]["port"].value_or(9002);
    std::optional<int64_t> center_frequency = config["input"]["frequency"].value<int64_t>();
    std::optional<int64_t> bandwidth = config["input"]["sps"].value<int64_t>();
    std::string antenna = config["server"]["antenna"].value_or("N/A");
    std::string websdr_name = config["server"]["websdr_name"].value_or("WebSDR_" + std::to_string(std::rand()));
    std::string signal_type = config["input"]["signal"].value_or("real");

    std::string websdr_id = std::to_string(std::rand());
    if(signal_type == "real")
    {
        bandwidth = bandwidth.value_or(30000000) / 2;
    }

    if(center_frequency.value_or(15000000) == 0){
        center_frequency = bandwidth.value_or(30000000) / 2;

    }

    while(true) {
        int user_count = static_cast<int>(events_connections.size());

        // Construct JSON payload manually
        std::string json_data = "{";
        json_data += "\"id\": \"" + websdr_id + "\", ";
        json_data += "\"name\": \"" + websdr_name + "\", ";
        json_data += "\"antenna\": \"" + antenna + "\", ";
        json_data += "\"bandwidth\": " + std::to_string(bandwidth.value_or(30000000)) + ", ";
        json_data += "\"users\": " + std::to_string(user_count) + ", ";
        json_data += "\"center_frequency\": " + std::to_string(center_frequency.value_or(15000000)) + ", ";
        json_data += "\"port\": " + std::to_string(port) + "}";

        // Initialize cURL
        CURL *curl;
        CURLcode res;

        // Initialize the cURL session
        curl = curl_easy_init();
        if(curl) {
            // Set the URL for the POST request
            curl_easy_setopt(curl, CURLOPT_URL, "http://api.sdr-list.xyz:5000/update_websdr");


            // Dont print to stdout - This is the only way to do it sadly...
            FILE *devnull = fopen("/dev/null", "w+");
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, devnull);


            // Set the JSON data
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());

            // Disable verbose output
            curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

            // Set the Content-Type header
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, "Host: api.sdr-list.xyz:5000"); // Add Host header
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            // Set the Content-Length header
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_data.length());

            // Perform the request
            res = curl_easy_perform(curl);

            // Check for errors
            if(res != CURLE_OK)
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;

            // Clean up
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

        }

        // Delay for 10 seconds before sending the next request
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}


void broadcast_server::stop() {
    running = false;
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
    std::string config_file;
    for (int i = 1; i < argc; i++) {
        if ((std::string(argv[i]) == "-c" ||
             std::string(argv[i]) == "--config") &&
            i + 1 < argc) {
            config_file = argv[i + 1];
            i++;
        }
        if (std::string(argv[i]) == "-h" || std::string(argv[i]) == "--help") {
            std::cout
                << "Options:\n"
                   "--help                             produce help message\n"
                   "-c [ --config ] arg (=config.toml) config file\n";
            return 0;
        }
    }

    config = toml::parse_file(config_file);

    std::string host = config["server"]["host"].value_or("0.0.0.0");

    std::optional<std::string> driver_type =
        config["input"]["driver"]["name"].value<std::string>();
    if (!driver_type.has_value()) {
        std::cout << "Specify an input driver" << std::endl;
        return 0;
    }
    std::string driver_str = driver_type.value();

    std::string input_format =
        config["input"]["driver"]["format"].value_or("f32");
    boost::algorithm::to_lower(input_format);

    // Initialise FFT threads if requested for multithreaded
    int fft_threads = config["input"]["fft_threads"].value_or(1);
    if (fft_threads > 1) {
        fftwf_init_threads();
    }

    // Set input to binary
    freopen(NULL, "rb", stdin);
    std::unique_ptr<SampleReader> reader =
        std::make_unique<FileSampleReader>(stdin);
    std::unique_ptr<SampleConverterBase> driver;

    if (input_format == "u8") {
        driver = std::make_unique<SampleConverter<uint8_t>>(std::move(reader));
    } else if (input_format == "s8") {
        driver = std::make_unique<SampleConverter<int8_t>>(std::move(reader));
    } else if (input_format == "u16") {
        driver = std::make_unique<SampleConverter<uint16_t>>(std::move(reader));
    } else if (input_format == "s16") {
        driver = std::make_unique<SampleConverter<int16_t>>(std::move(reader));
    } else if (input_format == "f32") {
        driver = std::make_unique<SampleConverter<float>>(std::move(reader));
    } else if (input_format == "f64") {
        driver = std::make_unique<SampleConverter<double>>(std::move(reader));
    } else {
        std::cout << "Unknown input format: " << input_format << std::endl;
        return 1;
    }


    int port = config["server"]["port"].value_or(9002);
    bool register_online = config["server"]["register_online"].value_or(false);
    broadcast_server server(std::move(driver), config);

    if(register_online) {
        std::thread websdr_thread(&broadcast_server::update_websdr_list, &server); // Pass the instance to the thread
        websdr_thread.detach(); // or websdr_thread.join();
    }
    g_signal = &server;
    std::signal(SIGINT, [](int) { g_signal->stop(); });
    server.run(port);
    std::exit(0);
}
