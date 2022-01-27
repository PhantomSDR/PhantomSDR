// NPM mode libraries
    
self.FlacDecoder = require('libflacjs/lib/decoder').Decoder;
self.FlacEncoder = require('libflacjs/lib/encoder').Encoder;

// Wrappers for individual libraries
getZstd = require('./libzstd.js')
getLiquidDSP = require('./LiquidDSP.js')
getNoiseProcessing = require('./NoiseProcessing.js')

// Force wasm files to be visible to webpack
//import 'libflacjs/dist/libflac.min.wasm.wasm'
//import 'libflacjs/dist/libflac.min.js.mem'
//import './jsDSP.wasm'
//import './jsDSPnoWasm.js.mem'

self.jsDSP = async (options) => {
    options = options || {}
    if (!("wasm" in options)) {
        options.wasm = true
    }

    let jsDSPModule
    if (options.wasm) {
        jsDSPModule = require('./jsDSP.js')
        self.Flac = require('libflacjs/dist/libflac.min.wasm.js')
    } else {
        jsDSPModule = (await import('./jsDSPnoWasm.js')).default
        self.Flac = await import('libflacjs/dist/libflac.min.js')
    }
    return jsDSPModule(options).then((jsDSPModule) => {
        let jsDSP = {}
        jsDSP.NoiseProcessing = getNoiseProcessing(jsDSPModule)
        jsDSP.LiquidDSP = getLiquidDSP(jsDSPModule)
        jsDSP.Zstd = getZstd(jsDSPModule)

        return jsDSP
    })
}
