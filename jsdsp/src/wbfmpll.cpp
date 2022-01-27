
#include <complex>
#include <liquid/liquid.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <emscripten/bind.h>
using namespace emscripten;

class WBFMStereo {
protected:
    unsigned int n;
    float* h;
    nco_crcf nco_rx;
    firfilt_rrrf filter;
    firfilt_rrrf filter2;
    firfilt_rrrf filter3;
    wdelayf delay;
    ampmodem demod;
public: 
    WBFMStereo(float fs) {
        n = estimate_req_filter_len(2000./fs, 60) | 1;
        // allocate memory for array and design filter
        h = new float[n];
        // define filter length, type, number of bands
        liquid_firdespm_btype btype = LIQUID_FIRDESPM_BANDPASS;
        unsigned int num_bands = 3;
        // band edge description [size: num_bands x 2]
        float bands[6]   = {0.0f,   17000.f/fs,   // WBFM mono
                            18000.f/fs,  20000.f/fs,   // 19kHz pilot
                            21000.f/fs,  0.5f};  // subcarriers

        // desired response [size: num_bands x 1]
        float des[6]     = {0.0f, 1.0f, 0.0f};

        // relative weights [size: num_bands x 1]
        float weights[6] = {1.0f, 1.0f, 1.0f};

        // in-band weighting functions [size: num_bands x 1]
        liquid_firdespm_wtype wtype[3] = {
                                        LIQUID_FIRDESPM_EXPWEIGHT,
                                        LIQUID_FIRDESPM_EXPWEIGHT,
                                        LIQUID_FIRDESPM_EXPWEIGHT};
        firdespm_run(n,num_bands,bands,des,weights,wtype,btype,h);
        nco_rx = nco_crcf_create(LIQUID_VCO);
        nco_crcf_set_frequency(nco_rx, 19000./fs * 2 * M_PI);
        nco_crcf_pll_set_bandwidth(nco_rx, 0.001);

        filter = firfilt_rrrf_create(h,n);
        filter2 = firfilt_rrrf_create_kaiser(estimate_req_filter_len(1000./fs, 60), 15000./fs, 60, 0);
        filter3 = firfilt_rrrf_create_kaiser(estimate_req_filter_len(1000./fs, 60), 15000./fs, 60, 0);
        //printf("%d\n",estimate_req_filter_len(1000./fs, 60));
        delay = wdelayf_create(round(firfilt_rrrf_groupdelay(filter, 19000./fs)));
        demod = ampmodem_create(1, LIQUID_AMPMODEM_DSB, 1);
    }

    void execute(uintptr_t arri, int num, uintptr_t li, uintptr_t ri) {
        float* arr = reinterpret_cast<float*>(arri);
        float* l = reinterpret_cast<float*>(li);
        float* r = reinterpret_cast<float*>(ri);
        for(int i = 0;i < num;i++) {
            float x = arr[i];
            float y;
            firfilt_rrrf_push(filter, x);    // push input sample
            firfilt_rrrf_execute(filter,&y); // compute output
            float e = 2 * y * nco_crcf_cos(nco_rx);
            nco_crcf_pll_step(nco_rx, e);
            liquid_float_complex lr;// = x;
            nco_crcf_mix_down(nco_rx, x, &lr);
            nco_crcf_mix_down(nco_rx, lr, &lr);
            
            float lrreal = std::real(lr);
            //firfilt_rrrf_push(filter2, x); 
            //firfilt_rrrf_execute(filter2, &lrreal);

            wdelayf_push(delay, x);
            wdelayf_read(delay, &x);
            //firfilt_rrrf_push(filter3, x);    // push input sample
            //firfilt_rrrf_execute(filter3, &x);    // push input sample

            l[i] = x+lrreal;
            r[i] = x-lrreal;
            nco_crcf_step(nco_rx);
            //printf("%12.8f + i%12.8f\n", creal(lr), cimag(lr));
            //printf("filtered: %12.8f pll nco: %12.8f phase error: %12.8f frequency: %12.8f initial frequency: %12.8f\n", y, nco_crcf_sin(nco_rx), e, nco_crcf_get_frequency(nco_rx), 19000./fs * 2 * M_PI);
        }
    }

    ~WBFMStereo() {
        firfilt_rrrf_destroy(filter3);
        firfilt_rrrf_destroy(filter2);
        firfilt_rrrf_destroy(filter);
        nco_crcf_destroy(nco_rx);
        delete[] h;
    }
};

EMSCRIPTEN_BINDINGS(WBFMStereo) {
  class_<WBFMStereo>("WBFMStereo")
    .constructor<double>()
    .function("execute", &WBFMStereo::execute, allow_raw_pointers())
    ;
}
