#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dav1d/dav1d.h>

#include <emscripten.h>
#include <emscripten/bind.h>
using namespace emscripten;

static void free_callback(const uint8_t *buf, void *cookie) {
  //free((void*)buf);
}

class Dav1dDecoder {
  public:
  Dav1dContext* ctx;
  Dav1dSettings s;
  Dav1dPicture pic;
  int err;
  Dav1dDecoder() {
    dav1d_default_settings(&s);
    err = dav1d_open(&ctx, &s);
  }


  val dav1d_decode(val frame) {
    Dav1dData data = { 0 };
    std::vector<uint8_t> frame_vec = convertJSArrayToNumberVector<uint8_t>(frame);
    memset(&pic, 0, sizeof(pic));
    dav1d_data_wrap(&data, frame_vec.data(), frame_vec.size(), free_callback, NULL/*cookie*/);
    err = dav1d_send_data(ctx, &data);
    if(err < 0 && err != DAV1D_ERR(EAGAIN))
      return val("send data");
    err = dav1d_get_picture(ctx, &pic);
    if (err < 0)
      return val("get picture");
    //if (pic.p.layout != DAV1D_PIXEL_LAYOUT_I420 || pic.p.bpc != 8)
    //  return val("layout");
    
    val ret = val::object();

    /*if (!pic.frame_hdr->show_frame) {
      return val("show frame");
    }*/
    ret.set("width", val(pic.p.w));
    ret.set("height", val(pic.p.h));

    val stride = val::array();
    stride.call<void>("push", pic.stride[0]);
    stride.call<void>("push", pic.stride[1]);
    val plane = val::array();
    val plane0{typed_memory_view(pic.p.h * pic.stride[0], (uint8_t*)pic.data[0])};
    val plane1{typed_memory_view(pic.p.h * pic.stride[1], (uint8_t*)pic.data[1])};
    val plane2{typed_memory_view(pic.p.h * pic.stride[1], (uint8_t*)pic.data[2])};
    plane.call<void>("push", plane0);
    plane.call<void>("push", plane1);
    plane.call<void>("push", plane2);

    ret.set("stride", stride);
    ret.set("plane", plane);
    ret.set("itut_t35", typed_memory_view(pic.itut_t35->payload_size, pic.itut_t35->payload));
    return ret;
  }

  void dav1d_picture_free() {
    dav1d_picture_unref(&pic);
  }

  int get_err() const {
    return err;
  }
  void set_err(int err) {
    this->err = err;
  }

  ~Dav1dDecoder() {
    dav1d_close(&ctx);
  }
};

EMSCRIPTEN_BINDINGS(Dav1d) {
    class_<Dav1dDecoder>("Dav1dDecoder")
      .constructor<>()
      .function("dav1d_decode", &Dav1dDecoder::dav1d_decode)
      .function("dav1d_picture_free", &Dav1dDecoder::dav1d_picture_free)
      .property("err", &Dav1dDecoder::get_err, &Dav1dDecoder::set_err);
}