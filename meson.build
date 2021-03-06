project(
    'spectrumdistributor',
    'cpp',
    default_options : [
        'cpp_std=c++17',
        'optimization=3',
    ]
)

add_project_arguments('-march=native', language: 'cpp')
add_project_arguments('-g', language: 'cpp')
add_project_arguments(['-Wall','-Wpedantic'], language: 'cpp')
#add_project_link_arguments(['-rdynamic', '-no-pie'], language: 'cpp')

cpp = meson.get_compiler('cpp')

cmake = import('cmake')

thread_dep = dependency('threads')

fftw3f_deps = [dependency('fftw3f')]
fftw3f_deps += cpp.find_library('fftw3f_threads')


boost_dep = dependency('boost', modules : ['system', 'program_options', 'iostreams', 'json'])
rapidjson_dep = dependency('RapidJSON')

codec_deps = []
zstd_dep = dependency('libzstd')
flacpp_dep = dependency('flac++')
codec_deps += zstd_dep
codec_deps += flacpp_dep

aom_dep = dependency('aom', required : false)
if aom_dep.found()
    codec_deps += aom_dep
    add_project_arguments('-DHAS_LIBAOM', language : 'cpp')
endif

opus_dep = dependency('opus', required : false)
if opus_dep.found()
    codec_deps += opus_dep
    add_project_arguments('-DHAS_LIBOPUS', language : 'cpp')
endif

volk_dep = dependency('volk')
#if not volk_dep.found() or get_option('wrap_mode') == 'forcefallback'
    #libvolk = cmake.subproject('libvolk')
    #volk_dep = libvolk.dependency('volk')
#endif

zlib_dep = dependency('zlib')
websocketpp_dep = dependency('websocketpp')
tomlplusplus_dep = dependency('tomlplusplus')


cuda_dep = dependency('cuda', required : false)
cufft_dep = dependency('cufft', required : false)
opencl_dep = dependency('OpenCL', required : false)
clfft_dep = dependency('clFFT', required : false)

stdcppfs_dep = cpp.find_library('stdc++fs', required : false)

if cuda_dep.found() and cufft_dep.found()
    fftw3f_deps += cuda_dep
    fftw3f_deps += cufft_dep
    add_project_arguments('-DCUFFT', language : 'cpp')
endif

if opencl_dep.found() and clfft_dep.found()
    fftw3f_deps += opencl_dep
    fftw3f_deps += clfft_dep
    add_project_arguments('-DCLFFT', language : 'cpp')
endif


srcs = [
    'src/spectrumserver.cpp',
    'src/samplereader.cpp',

    'src/websocket.cpp',
    'src/http.cpp',

    'src/fft.cpp',
    'src/signal.cpp',
    'src/waterfall.cpp',
    'src/events.cpp',
    'src/audio.cpp',
    'src/waterfallcompression.cpp',

    'src/fft_impl.cpp',
    'src/utils.cpp',
    'src/compression.cpp',
    
]

executable(
    'spectrumserver',
    srcs,
    dependencies : [
        stdcppfs_dep,
        thread_dep,
        fftw3f_deps,
        websocketpp_dep,
        boost_dep,
        tomlplusplus_dep,
        rapidjson_dep,
        codec_deps,
        volk_dep,
        zlib_dep,
    ]
)
