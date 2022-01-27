#include "audio.h"

void AudioEncoder::set_data(int l, double m, int r, double pwr) {
    this->header_u32[0] = l;
    this->header_u32[1] = r;
    this->header_d[1] = m;
    this->header_d[2] = pwr;
}

int AudioEncoder::send(const void *buffer, size_t bytes,
                       unsigned current_frame) {
    try {
        // Header size
        size_t packet_size = bytes + 3 * 8;
        uint8_t packet[packet_size];
        // Construct packet
        memcpy(packet, this->header_u32, 3 * 8);
        memcpy(packet + (3 * 8), buffer, bytes);
        m_server->send(hdl, packet, packet_size,
                       websocketpp::frame::opcode::binary);
        return 0;
    } catch (...) {
        return 1;
    }
}

FLAC__StreamEncoderWriteStatus
FlacEncoder::write_callback(const FLAC__byte buffer[], size_t bytes,
                            unsigned samples, unsigned current_frame) {
    return send(buffer, bytes, current_frame)
               ? FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR
               : FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

int FlacEncoder::process(int32_t *data, size_t size) {
    return this->process_interleaved(data, size);
}

int FlacEncoder::finish_encoder() { return this->finish(); }

FlacEncoder::~FlacEncoder() { this->finish(); }

OpusEncoder::OpusEncoder(websocketpp::connection_hdl hdl, server *m_server,
                         int samplerate)
    : AudioEncoder(hdl, m_server) {
    int err;
    samplerate = std::min(samplerate, 48000);
    encoder = opus_encoder_create(samplerate, 1, OPUS_APPLICATION_AUDIO, &err);
    frame_size = samplerate * 20 / 1000;
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(80000));
}

int OpusEncoder::process(int32_t *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        partial_frames.emplace_back(data[i]);
    }
    unsigned char packet[1024];
    while (partial_frames.size() > frame_size) {
        std::vector<opus_int16> data_int16(partial_frames.begin(),
                                           partial_frames.begin() + frame_size);
        opus_int32 packet_sz =
            opus_encode(encoder, data_int16.data(), frame_size, packet, 1024);
        if (packet_sz > 1) {
            send(packet, packet_sz, 0);
        }
        partial_frames.erase(partial_frames.begin(),
                                           partial_frames.begin() + frame_size);
    }
    return 0;
}

OpusEncoder::~OpusEncoder() { opus_encoder_destroy(encoder); }

int OpusEncoder::finish_encoder() { return 0; }
