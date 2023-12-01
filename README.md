# Spectrum Distributor

## Features
- Web interface
- Common demodulation modes
- Can handle high sample rate SDRs (70MSPS real, 35MSPS IQ)
- Support for both IQ and real data

## Screenshots
![Screenshot](/docs/screenshot.jpg)

## Building
Optional dependencies such as cuFFT or clFFT can be installed too.
### Ubuntu Prerequisites
```
apt install build-essential cmake pkg-config meson libfftw3-dev rapidjson-dev libwebsocketpp-dev libflac++-dev libvolk2-dev zlib1g-dev libzstd-dev libboost-all-dev libopus-dev
```

### Fedora Prerequisites
```
dnf install g++ meson cmake fftw3-devel rapidjson-devel websocketpp-devel flac-devel volk-devel zlib-devel boost-devel libzstd-devel opus-devel
```

### Building the binary
```
git clone --recursive https://github.com/PhantomSDR/PhantomSDR.git
cd PhantomSDR
meson build --optimization 3
cd build
ninja
```

## Examples
Remember to set the frequency and sample rate correctly. Default html directory is 'html/', change it with the `htmlroot` option in config.toml.
### RTL-SDR
```
rtl_sdr -f 145000000 -s 3200000 - | ./build/spectrumserver --config config.toml
```
### HackRF
```
rx_sdr -f 145000000 -s 20000000 -d driver=hackrf - | ./build/spectrumserver --config config.toml
```
