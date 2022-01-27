getZstd = function (Zstd) {
    let ZstdStream = {}
    ZSTD_e_continue = 0
    ZSTD_e_flush = 1
    ZSTD_e_end = 2
    ZstdStream.Zstd = Zstd

    /*ZstdStream.ZSTD_compressStream2_simpleArgs = Zstd.cxwrap('ZSTD_compressStream2_simpleArgs', 'number',
        ['number', 'number', 'number', 'number', 'array', 'number', 'number', 'number'])

    ZstdStream.ZSTD_decompressStream_simpleArgs = Zstd.cxwrap('ZSTD_decompressStream_simpleArgs', 'number',
        ['number', 'number', 'number', 'number', 'array', 'number', 'number'])*/

    ZstdStream.ZSTD_decompress = Zstd.cwrap('ZSTD_decompress', 'number', ['number', 'number', 'array', 'number'])
    ZstdStream.ZSTD_compress = Zstd.cwrap('ZSTD_compress', 'number', ['number', 'number', 'array', 'number', 'number'])

    ZstdStream.simpleDst = Zstd._malloc(1024)
    ZstdStream.ZstdCompressSimple = function (arr, compressionlevel) {
        compressedBytes = ZstdStream.ZSTD_compress(ZstdStream.simpleDst, 1024, arr, arr.length, compressionlevel)
        return new Uint8Array(Zstd.HEAPU8.buffer, ZstdStream.simpleDst, compressedBytes)
    }
    ZstdStream.ZstdDecompressSimple = function (arr) {
        decompressedBytes = ZstdStream.ZSTD_decompress(ZstdStream.simpleDst, 1024, arr, arr.length)
        return new Uint8Array(Zstd.HEAPU8.buffer, ZstdStream.simpleDst, decompressedBytes)
    }
    /*
    ZstdStream.ZstdCompressStream = function () {
        this.CStream = Zstd._ZSTD_createCStream()
        this.CStreamSize = Zstd._ZSTD_CStreamOutSize()
        this.CStreamOut = Zstd._malloc(this.CStreamSize)
        this.CStreamDstPos = Zstd._malloc(8)
        this.CStreamSrcPos = Zstd._malloc(8)

        this.compress = function (arr, endop) {
            Zstd.setValue(this.CStreamSrcPos, 0, 'i64')
            Zstd.setValue(this.CStreamDstPos, 0, 'i64')
            ZstdStream.ZSTD_compressStream2_simpleArgs(
                this.CStream,
                this.CStreamOut, this.CStreamSize, this.CStreamDstPos,
                arr, arr.length, this.CStreamSrcPos, endop)
            var DstLen = Zstd.getValue(this.CStreamDstPos, 'i64')
            return new Uint8Array(Zstd.HEAPU8.buffer, this.CStreamOut, DstLen)
        }

        this.delete = function () {
            Zstd._ZSTD_freeCStream(this.CStream)
            Zstd._free(this.CStreamOut)
            Zstd._free(this.CStreamDstPos)
            Zstd._free(this.CStreamSrcPos)
        }
    }


    ZstdStream.ZstdDecompressStream = function () {
        this.DStream = Zstd._ZSTD_createDStream()
        this.DStreamSize = Zstd._ZSTD_DStreamOutSize()
        this.DStreamOut = Zstd._malloc(this.DStreamSize)
        this.DStreamDstPos = Zstd._malloc(8)
        this.DStreamSrcPos = Zstd._malloc(8)
        Zstd._ZSTD_initDStream(this.DStream)
        Zstd._ZSTD_DCtx_setFormat(this.DStream, 1)

        this.decompress = function (arr) {
            Zstd.setValue(this.DStreamSrcPos, 0, 'i64')
            Zstd.setValue(this.DStreamDstPos, 0, 'i64')
            ret = ZstdStream.ZSTD_decompressStream_simpleArgs(
                this.DStream,
                this.DStreamOut, this.DStreamSize, this.DStreamDstPos,
                arr, arr.length, this.DStreamSrcPos)
            console.log(ret)
            var DstLen = Zstd.getValue(this.DStreamDstPos, 'i64')
            return new Uint8Array(Zstd.HEAPU8.buffer, this.DStreamOut, DstLen)
        }

        this.delete = function () {
            Zstd._ZSTD_freeDStream(this.DStream)
            Zstd._free(this.DStreamOut)
            Zstd._free(this.DStreamDstPos)
            Zstd._free(this.DStreamSrcPos)
        }
    }*/

    return ZstdStream
}
module.exports = getZstd;