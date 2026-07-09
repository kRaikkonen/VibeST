# White-box practice amp — C++ build

Real-time standalone of the OD-1 + Princeton Reverb white-box engine.
The engine headers (`src/engine/*.hpp`) are ports of the validated Python
prototype in `../proto`, checked sample-for-sample against it (`test/`).

## Vendored dependencies (git-ignored — fetch before building)

```sh
# JUCE (only needed for the future VST3 target, not the standalone)
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE

# RtAudio — bundles the ASIO host layer used by the standalone
git clone --depth 1 https://github.com/thestk/rtaudio.git rtaudio
```

A C++20 compiler is required. Development used a portable MinGW-w64 GCC 16
(unzipped to `../toolchain`, also git-ignored). `miniaudio.h` (WASAPI backend)
is committed under `src/`.

## Build (MinGW, from this directory)

Console front-end (WASAPI + ASIO):
```sh
g++ -O3 -std=c++20 -static -D__WINDOWS_ASIO__ -Irtaudio -Irtaudio/include \
    -o PrincetonPractice.exe src/standalone/practice_amp.cpp \
    rtaudio/RtAudio.cpp rtaudio/include/asio.cpp rtaudio/include/asiodrivers.cpp \
    rtaudio/include/asiolist.cpp rtaudio/include/iasiothiscallresolver.cpp \
    -lcomctl32 -lole32
```

GUI front-end (adds `-mwindows -municode` and `-lcomdlg32`). First compile the app
icon resource (`app.rc` -> `docs/logo.ico`) with `windres`, then link the `.o`:
```sh
windres app.rc -O coff -o app_res.o
g++ -O3 -std=c++20 -static -mwindows -municode -D__WINDOWS_ASIO__ \
    -Irtaudio -Irtaudio/include -o "VibeST Practice AMP v0.1.3.exe" \
    src/standalone/practice_amp_gui.cpp app_res.o \
    rtaudio/RtAudio.cpp rtaudio/include/asio.cpp rtaudio/include/asiodrivers.cpp \
    rtaudio/include/asiolist.cpp rtaudio/include/iasiothiscallresolver.cpp \
    -lcomctl32 -lcomdlg32 -lole32
```

The spring-reverb impulse response is generated from the physical model at
startup (`pa::springTankIr`), so no data files are needed at runtime.

## Verify against the Python golden reference

```sh
cd test
python dump_reference.py && g++ -O2 -std=c++20 test_od1.cpp -o t && ./t
python dump_princeton.py && g++ -O2 -std=c++20 test_princeton.cpp -o t && ./t
```

## Layout

- `src/engine/` — white-box circuit engine (`od1.hpp`, `princeton.hpp`,
  `tubes`/diode/BJT models, `dsp.hpp` FFT/convolution/resamplers)
- `src/standalone/` — `engine_core.hpp` (audio wiring, rate ladder, FIFOs),
  `asio_io.hpp` (RtAudio/ASIO), console + Win32 GUI front-ends
- `test/` — C++ vs Python sample-accuracy harness
