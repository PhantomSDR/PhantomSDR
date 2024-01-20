project(
    'spectrumdistributor',
    'cpp',
    default_options : [
        'optimization=3',
        #'b_lto=true',
        #'b_sanitize=address'
    ]
)

add_project_arguments('-march=native', language: 'cpp')
add_project_arguments('-ggdb3', language: 'cpp')
add_project_arguments(['-std=c++23','-Wall','-Wextra','-Wpedantic'], language: 'cpp')
#add_project_link_arguments(['-rdynamic', '-no-pie'], language: 'cpp')

cpp = meson.get_compiler('cpp')

cmake = import('cmake')

thread_dep = dependency('threads')

fft_deps = []
fftw3f_dep = dependency('fftw3f')
fftw3f_deps = [
    fftw3f_dep,
    cpp.find_library('fftw3f_omp'),
]
fft_deps += fftw3f_deps
fft_deps += dependency('openmp')

#message('CMake targets:\n - ' + '\n - '.join(cmake.target_list()))

boost_dep = dependency('boost', modules : ['system', 'program_options', 'iostreams'])
glaze_dep = dependency('glaze', required : false)
if (not glaze_dep.found()
    or get_option('wrap_mode') == 'forcefallback'
    or 'glaze' in get_option('force_fallback_for'))

    glaze = cmake.subproject('glaze')
    glaze_dep = glaze.dependency('glaze_glaze')
endif


liquid_dep = cpp.find_library('liquid', required : false)
if liquid_dep.found()
    add_project_arguments('-DHAS_LIQUID', language : 'cpp')
endif

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

#if not volk_dep.found() or get_option('wrap_mode') == 'forcefallback'
#    libvolk = cmake.subproject('libvolk')
#    volk_dep = libvolk.dependency('volk')
#endif

zlib_dep = dependency('zlib')
websocketpp_dep = subproject('websocketpp').get_variable('websocketpp_dep')
tomlplusplus_dep = dependency('tomlplusplus')

cuda_dep = dependency('cuda', required : false)
cufft_dep = dependency('cufft', required : false)
opencl_dep = dependency('OpenCL', required : false)
clfft_dep = dependency('clFFT', required : false)
stdcppfs_dep = cpp.find_library('stdc++fs', required : false)

cuda_srcs = []

if add_languages('cuda', required : false) and cuda_dep.found() and cufft_dep.found()
    cuda = import('unstable-cuda')
    fft_deps += cuda_dep
    fft_deps += cufft_dep
    add_project_arguments('-DCUFFT', language : ['cpp', 'cuda'] )
    add_project_arguments(cuda.nvcc_arch_flags(meson.get_compiler('cuda'), 'All'), language: 'cuda')
    add_project_arguments('-O3', language: 'cuda')
    add_project_arguments('-Xptxas=-v', language: 'cuda')
    cuda_srcs += files('src/fft_cuda.cu')
endif

if opencl_dep.found() and clfft_dep.found()
    fft_deps += opencl_dep
    fft_deps += clfft_dep
    add_project_arguments('-DCLFFT', language : 'cpp')
endif

#if mkl_dep.found()
#    fft_deps += mkl_dep
#    add_project_arguments('-DMKL', language : 'cpp')
#endif

srcs = [
    'src/spectrumserver.cpp',
    'src/samplereader.cpp',

    'src/websocket.cpp',
    'src/http.cpp',

    'src/fft.cpp',
    'src/client.cpp',
    'src/signal.cpp',
    'src/waterfall.cpp',
    'src/events.cpp',
    'src/audio.cpp',
    'src/waterfallcompression.cpp',

    'src/utils/dsp.cpp',
    'src/utils/audioprocessing.cpp',

    'src/fft_impl.cpp',
    'src/utils.cpp',
    'src/compression.cpp',
    
]
srcs += cuda_srcs

executable(
    'spectrumserver',
    srcs,
    dependencies : [
        stdcppfs_dep,
        thread_dep,
        fft_deps,
        websocketpp_dep,
        boost_dep,
        tomlplusplus_dep,
        glaze_dep,
        codec_deps,
        zlib_dep,
        liquid_dep,
    ],
    link_language : 'cpp',
)
