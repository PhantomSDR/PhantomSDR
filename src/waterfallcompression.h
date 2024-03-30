#ifndef WATERFALLCOMPRESSION_H
#define WATERFALLCOMPRESSION_H

#include "client.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

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
    virtual int send(const void *buffer, size_t bytes, uint64_t frame_num, int l, int r) = 0;
    virtual ~WaterfallEncoder(){};

  protected:
    void set_data(uint64_t frame_num, int l, int r);
    void send_packet(void *packet, size_t bytes);
    websocketpp::connection_hdl hdl;
    PacketSender &sender;

    json packet;
};

class ZstdEncoder : public WaterfallEncoder {
  public:
    ZstdEncoder(connection_hdl hdl, PacketSender &sender, int waterfall_size);
    int send(const void *buffer, size_t bytes, uint64_t frame_num, int l, int r);
    virtual ~ZstdEncoder();

  protected:
    ZSTD_CStream *stream;
};

#ifdef HAS_LIBAOM
class AV1Encoder : public WaterfallEncoder {
  public:
    AV1Encoder(connection_hdl hdl, PacketSender &sender, int waterfall_size);
    int send(const void *buffer, size_t bytes, uint64_t frame_num, int l, int r);
    virtual ~AV1Encoder();

  protected:
    aom_codec_iface_t *encoder;
    aom_image_t image;
    aom_codec_enc_cfg_t cfg;
    aom_codec_ctx_t codec;
    int frames;
    int line;
    struct {
        uint64_t frame_num;
        uint32_t bytes;
        uint32_t l, r;
    } header_multi[WATERFALL_COALESCE];
    uint8_t header_multi_compressed[4 * WATERFALL_COALESCE * 4 * 2];
};
#endif
#endif