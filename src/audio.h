#ifndef AUDIO_H
#define AUDIO_H

#include <string>
#include <vector>

#include "FLAC++/encoder.h"
#include "FLAC++/metadata.h"

#ifdef HAS_LIBOPUS
#include <opus/opus.h>
#endif

#include "client.h"

class AudioEncoder {
  public:
    AudioEncoder(websocketpp::connection_hdl hdl, PacketSender& sender);
    void set_data(uint64_t frame_num, int l, double m, int r, double pwr);
    virtual int process(int32_t *data, size_t size) = 0;
    virtual int finish_encoder() = 0;
    virtual ~AudioEncoder() {}

  protected:
    int send(const void *buffer, size_t bytes, unsigned current_frame);
    websocketpp::connection_hdl hdl;
    PacketSender& sender;

    struct {
        uint64_t frame_num;
        uint32_t l, r;
        double m, pwr;
    } header;
    
    std::vector<uint8_t> packet;
};

class FlacEncoder : public AudioEncoder, public FLAC::Encoder::Stream {
  public:
    FlacEncoder(websocketpp::connection_hdl hdl, PacketSender& sender)
        : AudioEncoder(hdl, sender), FLAC::Encoder::Stream() {}
    ~FlacEncoder();

  protected:
    virtual FLAC__StreamEncoderWriteStatus
    write_callback(const FLAC__byte buffer[], size_t bytes, unsigned samples,
                   unsigned current_frame);
    int finish_encoder();
    int process(int32_t *data, size_t size);
};

#ifdef HAS_LIBOPUS
class OpusEncoder : public AudioEncoder {
  public:
    OpusEncoder(websocketpp::connection_hdl hdl, PacketSender& sender, int samplerate);
    ~OpusEncoder();

  protected:
    OpusEncoder *encoder;
    size_t frame_size;
    std::deque<opus_int16> partial_frames;
    int finish_encoder();
    int process(int32_t *data, size_t size);
};
#endif

#endif