#ifndef WATERFALL_H
#define WATERFALL_H

#include "websocket.h"

#ifdef HAS_LIBAOM
#include "aom/aom_encoder.h"
#include "aom/aomcx.h"
#endif

#define WATERFALL_COALESCE 8

class WaterfallEncoder {
  public:
    WaterfallEncoder(websocketpp::connection_hdl hdl, server *m_server)
        : hdl{hdl}, m_server{m_server} {}
    virtual int send(const void *buffer, size_t bytes, unsigned current_frame,
                     int l, int r) = 0;
    virtual ~WaterfallEncoder(){};

  protected:
    void send_packet(void *packet, size_t bytes);
    websocketpp::connection_hdl hdl;
    server *m_server;

    uint32_t header_u32[2];
};

class ZstdEncoder : public WaterfallEncoder {
  public:
    ZstdEncoder(websocketpp::connection_hdl hdl, server *m_server,
                int waterfall_size)
        : WaterfallEncoder(hdl, m_server), last(waterfall_size, 0) {}
    int send(const void *buffer, size_t bytes, unsigned current_frame, int l,
             int r);
    ~ZstdEncoder() {}

  protected:
    std::vector<uint8_t> last;
};

#ifdef HAS_LIBAOM
class AV1Encoder : public WaterfallEncoder {
  public:
    AV1Encoder(websocketpp::connection_hdl hdl, server *m_server,
               int waterfall_size);
    int send(const void *buffer, size_t bytes, unsigned current_frame, int l,
             int r);
    ~AV1Encoder();

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