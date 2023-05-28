#include "waterfallcompression.h"

#include <boost/container/small_vector.hpp>
#include <iostream>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

void WaterfallEncoder::send_packet(void *packet, size_t bytes) {
    sender.send_binary_packet(hdl, packet, bytes);
}

void WaterfallEncoder::set_data(uint64_t frame_num, int l, int r) {
    header.frame_num = frame_num;
    header.l = l;
    header.r = r;
}
ZstdEncoder::ZstdEncoder(connection_hdl hdl, PacketSender &sender,
                         int waterfall_size)
    : WaterfallEncoder(hdl, sender) {
    stream = ZSTD_createCStream();
}
ZstdEncoder::~ZstdEncoder() { ZSTD_freeCStream(stream); }

int ZstdEncoder::send(const void *buffer, size_t bytes) {
    boost::container::small_vector<uint8_t, 4096> packet;
    packet.resize(ZSTD_compressBound(bytes));
    ZSTD_inBuffer header_buf = {&header, sizeof(header), 0};
    ZSTD_inBuffer data = {buffer, bytes, 0};
    ZSTD_outBuffer packet_out = {packet.data(), packet.size(), 0};
    ZSTD_compressStream2(stream, &packet_out, &header_buf, ZSTD_e_continue);
    ZSTD_compressStream2(stream, &packet_out, &data, ZSTD_e_flush);
    sender.send_binary_packet(hdl, packet_out.dst, packet_out.pos);
    return 0;
}

#ifdef HAS_LIBAOM

AV1Encoder::AV1Encoder(connection_hdl hdl, PacketSender &sender,
                       int waterfall_size)
    : WaterfallEncoder(hdl, sender), frames{0}, line{0} {
    aom_codec_err_t err;
    encoder = aom_codec_av1_cx();
    if (!encoder) {
        std::cout << "no encoder" << '\n';
    }
    if (!aom_img_alloc(&image, AOM_IMG_FMT_I420, waterfall_size,
                       WATERFALL_COALESCE, 1)) {
        std::cout << "no image" << '\n';
    }
    image.monochrome = 1;
    err = aom_codec_enc_config_default(encoder, &cfg, AOM_USAGE_REALTIME);
    if (err) {
        std::cout << "no config" << '\n';
    }
    cfg.g_h = WATERFALL_COALESCE;
    cfg.g_w = waterfall_size;
    cfg.g_bit_depth = AOM_BITS_8;
    cfg.g_input_bit_depth = 8;
    cfg.g_profile = 0;
    cfg.g_pass = AOM_RC_ONE_PASS;
    cfg.g_lag_in_frames = 0;
    cfg.rc_end_usage = AOM_CQ;
    // cfg.rc_target_bitrate = 8;

    cfg.rc_max_quantizer = 63 - 50;
    cfg.rc_min_quantizer = 63 - 52;
    cfg.monochrome = 1;

    err = aom_codec_enc_init(&codec, encoder, &cfg, 0);

    if (err) {
        std::cout << "no codec" << '\n';
    }
    aom_codec_control(&codec, AOME_SET_CPUUSED, 8);
    aom_codec_control(&codec, AOME_SET_CQ_LEVEL, 63 - 51);
    aom_img_add_metadata(&image, 4, (const uint8_t *)header_multi_u32,
                         (1 + 4 * WATERFALL_COALESCE) * 4, AOM_MIF_ANY_FRAME);
}
int AV1Encoder::send(const void *buffer, size_t bytes, unsigned current_frame,
                     int l, int r) {
    aom_codec_err_t err;
    const uint8_t *buffer_arr = (uint8_t *)buffer;
    int stride = image.stride[0];
    for (size_t i = 0; i < bytes; i++) {
        image.planes[0][i + line * stride] = buffer_arr[i] ^ 0x80;
    }
    header_multi_u32[line * 4 + 0] = l;
    header_multi_u32[line * 4 + 1] = r;
    header_multi_u32[line * 4 + 2] = bytes;
    header_multi_u32[line * 4 + 3] = current_frame;

    line++;
    if (line == WATERFALL_COALESCE) {
        // const aom_metadata_t *metadata = aom_img_get_metadata(&image, 0);
        aom_img_remove_metadata(&image);
        header_compressed_u8[0] = 0;
        size_t metadata_sz = ZSTD_compress(
            &header_compressed_u8[1], 4 * WATERFALL_COALESCE * 4 * 2,
            header_multi_u32, 4 * WATERFALL_COALESCE * 4, 5);
        aom_img_add_metadata(&image, 4, (const uint8_t *)header_compressed_u8,
                             metadata_sz + 1, AOM_MIF_ANY_FRAME);
        // memcpy(metadata->payload, header_multi_u32, (1 + 4 * 8) * 4);

        err = aom_codec_encode(&codec, &image, frames, 1, 0);
        if (err < 0) {
            throw std::runtime_error("AV1 Encode");
        }
        const aom_codec_cx_pkt_t *pkt = NULL;
        aom_codec_iter_t iter = NULL;
        while ((pkt = aom_codec_get_cx_data(&codec, &iter)) != NULL) {
            if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
                send_packet(pkt->data.frame.buf, pkt->data.frame.sz);
            }
        }
        line = 0;
    }
    return 0;
}
AV1Encoder::~AV1Encoder() {
    aom_img_free(&image);
    aom_codec_destroy(&codec);
}
#endif
