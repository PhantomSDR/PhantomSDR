# Spectrum Distributor
## Setup Instructions
Check out: https://github.com/PhantomSDR/PhantomSDR/wiki/Setup
## Receiver list
https://phantomsdr.github.io/servers

Will be empty for a while until someone decides to host!

## Features
- Web interface allowing many users (>100 on a good setup)
- Common demodulation modes
- Can handle high sample rate SDRs (70MSPS real, 35MSPS IQ)
- Support for both IQ and real data

## Screenshots

With an RX888 SDR:
![Screenshot](/docs/screenshot.jpg)

## Building
Optional dependencies such as cuFFT or clFFT can be installed too.
### Ubuntu Prerequisites
```
apt install build-essential cmake pkg-config meson libfftw3-dev libwebsocketpp-dev libflac++-dev zlib1g-dev libzstd-dev libboost-all-dev libopus-dev libliquid-dev
```

### Fedora Prerequisites
```
dnf install g++ meson cmake fftw3-devel websocketpp-devel flac-devel zlib-devel boost-devel libzstd-devel opus-devel liquid-dsp-devel
```

### Building the binary
```
git clone --recursive https://github.com/PhantomSDR/PhantomSDR.git
cd PhantomSDR
meson build --optimization 3
meson compile -C build
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
