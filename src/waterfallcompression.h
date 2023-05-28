#ifndef WATERFALLCOMPRESSION_H
#define WATERFALLCOMPRESSION_H

#include "client.h"

#ifdef HAS_LIBAOM
#include "aom/aom_encoder.h"
#include "aom/aomcx.h"
#endif

#include <zstd.h>

#define WATERFALL_COALESCE 8

class WaterfallEncoder {
  public:
    WaterfallEncoder(connection_hdl hdl, PacketSender &sender)
        : hdl{hdl}, sender{sender} {}
    virtual int send(const void *buffer, size_t bytes) = 0;
    void set_data(uint64_t frame_num, int l, int r);
    virtual ~WaterfallEncoder(){};

  protected:
    void send_packet(void *packet, size_t bytes);
    websocketpp::connection_hdl hdl;
    PacketSender &sender;

    struct {
        uint64_t frame_num;
        uint32_t l, r;
    } header;
};

class ZstdEncoder : public WaterfallEncoder {
  public:
    ZstdEncoder(connection_hdl hdl, PacketSender &sender, int waterfall_size);
    int send(const void *buffer, size_t bytes);
    virtual ~ZstdEncoder();

  protected:
    ZSTD_CStream *stream;
};

#ifdef HAS_LIBAOM
class AV1Encoder : public WaterfallEncoder {
  public:
    AV1Encoder(connection_hdl hdl, PacketSender &sender, int waterfall_size);
    int send(const void *buffer, size_t bytes, unsigned current_frame, int l,
             int r);
    virtual ~AV1Encoder();

  protected:
    aom_codec_iface_t *encoder;
    aom_image_t image;
    aom_codec_enc_cfg_t cfg;
    aom_codec_ctx_t codec;
    int frames;
    int line;
    uint32_t header_multi_u32[4 * WATERFALL_COALESCE];
    uint8_t header_compressed_u8[4 * WATERFALL_COALESCE * 4 * 2];
};
#endif
#endif