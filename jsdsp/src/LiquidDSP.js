

getLiquidDSP = function(LiquidDSP) {

        DSP = {}
        DSP.LiquidDSP = LiquidDSP
        DSP.estimate_req_filter_len = LiquidDSP.cwrap('estimate_req_filter_len', 'number', ['number', 'number'])
        DSP.liquid_firdes_kaiser = LiquidDSP.cwrap('liquid_firdes_kaiser', 'number', ['number', 'number', 'number', 'number', 'number'])

        DSP.firfilt_rrrf_create = LiquidDSP.cwrap('firfilt_rrrf_create', 'number', ['number', 'number'])
        DSP.firfilt_rrrf_execute_block = LiquidDSP.cwrap('firfilt_rrrf_execute_block', 'number', ['number', 'number', 'number', 'number'])
        DSP.firfilt_rrrf_destroy = LiquidDSP.cwrap('firfilt_rrrf_destroy', 'number', ['number'])

        DSP.getFloat32Array = (offset, length) => {
            return new Float32Array(new Float32Array(LiquidDSP.HEAPU8.buffer, offset, length))
        }

        DSP.FirDesKaiser = (ft, fc, As, mu) => {
            let h_len = DSP.estimate_req_filter_len(ft, As)
            let h = LiquidDSP._malloc(4 * h_len)
            DSP.liquid_firdes_kaiser(h_len, fc, As, mu, h)
            LiquidDSP._free(h)
            let arr = DSP.getFloat32Array(h, h_len)
            return arr
        }

        DSP.FirFilt = function(h) {
            this.h = LiquidDSP._malloc(h.length * 4)
            new Float32Array(LiquidDSP.HEAPU8.buffer, h, h.length).set(h)
            this.q = DSP.firfilt_rrrf_create(this.h, h.length)
            this.in = LiquidDSP._malloc(4 * 16384)
            this.inarr = new Float32Array(LiquidDSP.HEAPU8.buffer, this.in, 16384)

            this.execute = (arr) => {
                this.inarr.set(arr)
                DSP.firfilt_rrrf_execute_block(this.q, this.in, arr.length, this.in)
                arr = DSP.getFloat32Array(this.in, arr.length)
                return arr
            }

            this.destroy = () => {
                LiquidDSP._free(this.h)
                LiquidDSP._free(this.in)
                DSP.firfilt_rrrf_destroy(this.q)
            }
        }

        DSP.Resamp = function(r, m, fc, As, N) {
            this.q = LiquidDSP._resamp_rrrf_create(r, m, fc, As, N)

            this.in = LiquidDSP._malloc(4 * 16384)
            this.out = LiquidDSP._malloc(4 * 16384)
            this.inarr = new Float32Array(LiquidDSP.HEAPU8.buffer, this.in, 16384)
            this.outarr = new Float32Array(LiquidDSP.HEAPU8.buffer, this.out, 16384)
            this.outlen = LiquidDSP._malloc(4)

            this.execute = (arr) => {
                this.inarr.set(arr)
                LiquidDSP._resamp_rrrf_execute_block(this.q, this.in, arr.length, this.out, this.outlen)
                arr = DSP.getFloat32Array(this.out, LiquidDSP.getValue(this.outlen, 'i32'))
                return arr
            }

            this.destroy = () => {
                LiquidDSP._free(this.in)
                LiquidDSP._free(this.out)
                LiquidDSP._resamp_rrrf_destroy(this.q)
            }
        }

        DSP.AGC = function() {
            this.q = LiquidDSP._agc_rrrf_create()

            this.in = LiquidDSP._malloc(4 * 16384)
            this.inarr = new Float32Array(LiquidDSP.HEAPU8.buffer, this.in, 16384)

            this.execute = (arr) => {
                this.inarr.set(arr)
                LiquidDSP._agc_rrrf_execute_block(this.q, this.in, arr.length, this.out)
                arr = DSP.getFloat32Array(this.in, arr.length)
                return arr
            }

            this.destroy = () => {
                LiquidDSP._free(this.in)
                LiquidDSP._agc_rrrf_destroy(this.q)
            }
        }

        return DSP
}

module.exports = getLiquidDSP