#include "audio.h"

#include <boost/container/small_vector.hpp>
#include <iostream>

AudioEncoder::AudioEncoder(websocketpp::connection_hdl hdl,
                           PacketSender &sender)
    : hdl{hdl}, sender{sender} {
    set_data(0, 0, 0, 0, 0);
    stream = ZSTD_createCStream();
}

AudioEncoder::~AudioEncoder() {
    ZSTD_freeCStream(stream);
}

void AudioEncoder::set_data(uint64_t frame_num, int l, double m, int r,
                            double pwr) {
    packet["frame_num"] = frame_num;
    packet["l"] = l;
    packet["m"] = m;
    packet["r"] = r;
    packet["pwr"] = pwr;
}

int AudioEncoder::send(const void *buffer, size_t bytes, unsigned) {
    try {
        packet["data"] = json::binary(
            std::vector<uint8_t>((uint8_t *)buffer, (uint8_t *)buffer + bytes));
        auto cbor = json::to_cbor(packet);
        sender.send_binary_packet(hdl, cbor.data(), cbor.size());
        return 0;
    } catch (...) {
        return 1;
    }
}

FLAC__StreamEncoderWriteStatus
FlacEncoder::write_callback(const FLAC__byte buffer[], size_t bytes, unsigned,
                            unsigned current_frame) {
    return send(buffer, bytes, current_frame)
               ? FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR
               : FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

int FlacEncoder::process(int32_t *data, size_t size) {
    return this->process_interleaved(data, size);
}

int FlacEncoder::finish_encoder() { return this->finish(); }

FlacEncoder::~FlacEncoder() { this->finish(); }

#ifdef HAS_LIBOPUS
OpusEncoder::OpusEncoder(websocketpp::connection_hdl hdl, PacketSender &sender,
                         int samplerate)
    : AudioEncoder(hdl, sender) {
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
#endif