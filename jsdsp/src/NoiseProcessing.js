getNoiseProcessing = function(NoiseProcessing) {

        wdsp_anr_create = NoiseProcessing.cwrap('wdsp_anr_create', 'number', ['number','number','number','number','number'])
        wdsp_anr_filter = NoiseProcessing.cwrap('wdsp_anr_filter', 'void', ['number','number','number','number','number'])
        wdsp_anr_destroy = NoiseProcessing.cwrap('wdsp_anr_destroy', 'void', ['number'])

        class WdspANR {
            constructor(nr_type, taps, dly, gain, leakage) {
                this.nr_type = nr_type
                this.wdsp_anr = wdsp_anr_create(nr_type, taps, dly, gain, leakage)
                this.fptr = NoiseProcessing._malloc(16384 * 4)
                this.farr = new Float32Array(NoiseProcessing.HEAPU8.buffer, this.fptr, 16384)
            }
            filter(arr) {
                this.farr.set(arr)
                wdsp_anr_filter(this.wdsp_anr, this.nr_type, arr.length, this.fptr, this.fptr)
                arr.set(this.farr.subarray(0, arr.length))
                return arr
            }
            destroy() {
                NoiseProcessing._free(this.fptr)
                wdsp_anr_destroy(this.wdsp_anr)
            }
        }


        wild_nb_init = NoiseProcessing.cwrap('wild_nb_init', 'number', ['number','number','number'])
        wild_nb_blank = NoiseProcessing.cwrap('wild_nb_blank', 'void', ['number','number','number'])
        wild_nb_destroy = NoiseProcessing.cwrap('wild_nb_destroy', 'void', ['number'])

        class WildNB {
            constructor(thresh, taps, samples) {
                this.wild_nb = wild_nb_init(thresh, taps, samples)
                this.fptr = NoiseProcessing._malloc(16384 * 4)
                this.farr = new Float32Array(NoiseProcessing.HEAPU8.buffer, this.fptr, 16384)
            }
            filter(arr) {
                this.farr.set(arr)
                wild_nb_blank(this.wild_nb, arr.length, this.fptr)
                arr.set(this.farr.subarray(0, arr.length))
                return arr
            }
            destroy() {
                NoiseProcessing._free(this.fptr)
                wild_nb_destroy(this.wild_nb)
            }
        }

        nr_spectral_init = NoiseProcessing.cwrap('nr_spectral_init', 'number', ['number','number','number', 'number'])
        nr_spectral_process = NoiseProcessing.cwrap('nr_spectral_process', 'void', ['number','number','number','number'])
        nr_spectral_destroy = NoiseProcessing.cwrap('wild_nb_destroy', 'void', ['number'])

        class SpectralNR {
            constructor(snd_rate, gain, alpha, asnr) {
                this.spectral_nr = nr_spectral_init(snd_rate, gain, alpha, asnr)
                this.fptr = NoiseProcessing._malloc(16384 * 4)
                this.farr = new Float32Array(NoiseProcessing.HEAPU8.buffer, this.fptr, 16384)
            }
            filter(arr) {
                this.farr.set(arr)
                nr_spectral_process(this.spectral_nr, arr.length, this.fptr, this.fptr)
                arr.set(this.farr.subarray(0, arr.length))
                return arr
            }
            destroy() {
                NoiseProcessing._free(this.fptr)
                nr_spectral_destroy(this.spectral_nr)
            }
        }

        let noise = {}

        noise.WdspANR = WdspANR
        noise.WildNB = WildNB
        noise.SpectralNR = SpectralNR

        return noise
}

module.exports = getNoiseProcessing