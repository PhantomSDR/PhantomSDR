#ifndef WATERFALL_H
#define WATERFALL_H

#include "client.h"
#include "waterfallcompression.h"

class WaterfallClient : public Client {
  public:
    WaterfallClient(connection_hdl hdl, PacketSender &sender,
                    waterfall_compressor waterfall_compression,
                    int min_waterfall_fft,
                    std::vector<std::multimap<std::pair<int, int>,
                                              std::shared_ptr<WaterfallClient>>>
                        &waterfall_slices,
                    std::deque<std::mutex> &waterfall_slice_mtx);
    void set_waterfall_range(int level, int l, int r);
    void send_waterfall(int8_t *buf, size_t frame_num);
    virtual void on_window_message(int l, std::optional<double> &m, int r,
                                   std::optional<int> &level);
    void on_close();
    virtual ~WaterfallClient(){};

    std::multimap<std::pair<int, int>,
                  std::shared_ptr<WaterfallClient>>::iterator it;

  protected:
    int min_waterfall_fft;
    int level;
    // Compression codec variables for waterfall
    std::unique_ptr<WaterfallEncoder> waterfall_encoder;

    std::vector<
        std::multimap<std::pair<int, int>, std::shared_ptr<WaterfallClient>>>
        &waterfall_slices;
    std::deque<std::mutex> &waterfall_slice_mtx;
};

#endif