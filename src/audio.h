#ifndef AUDIO_H
#define AUDIO_H

#include <string>

#include <vector>

#include "FLAC++/encoder.h"
#include "FLAC++/metadata.h"

#include <opus/opus.h>

#include "websocket.h"

class AudioEncoder {
  public:
    AudioEncoder(websocketpp::connection_hdl hdl, server *m_server)
        : hdl{hdl}, m_server{m_server}, packet(32, 0) {}
    void set_data(int l, double m, int r, double pwr);
    virtual int process(int32_t *data, size_t size) = 0;
    virtual int finish_encoder() = 0;
    virtual ~AudioEncoder() {}

  protected:
    int send(const void *buffer, size_t bytes, unsigned current_frame);
    websocketpp::connection_hdl hdl;
    server *m_server;

    union {
        uint32_t header_u32[6];
        double header_d[3];
    };
    std::vector<uint8_t> packet;
};

class FlacEncoder : public AudioEncoder, public FLAC::Encoder::Stream {
  public:
    FlacEncoder(websocketpp::connection_hdl hdl, server *m_server)
        : AudioEncoder(hdl, m_server), FLAC::Encoder::Stream() {}
    ~FlacEncoder();

  protected:
    virtual FLAC__StreamEncoderWriteStatus
    write_callback(const FLAC__byte buffer[], size_t bytes, unsigned samples,
                   unsigned current_frame);
    int finish_encoder();
    int process(int32_t *data, size_t size);
};

class OpusEncoder : public AudioEncoder {
  public:
    OpusEncoder(websocketpp::connection_hdl hdl, server *m_server, int samplerate);
    ~OpusEncoder();

  protected:
    OpusEncoder *encoder;
    size_t frame_size;
    std::deque<opus_int16> partial_frames;
    int finish_encoder();
    int process(int32_t *data, size_t size);
};

#endif