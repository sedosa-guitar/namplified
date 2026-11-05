# NAMinator

This JUCE project builds Audio Unit, VST3, and standalone macOS targets.

## Build on macOS

Install Xcode (including its macOS SDK) and CMake 3.22 or newer. Then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The first configure downloads JUCE 9.0.5 and NeuralAmpModelerCore 0.5.4 unless
`JUCE_SOURCE_DIR` points to an existing JUCE source checkout. Build products
are written under `build`.

The standalone app can be opened from the generated `TheLead_artefacts` folder.
The AU and VST3 bundles are generated alongside it.

The chain includes independent Noise Gate, Compressor, Boost, Overdrive, Pre EQ,
three NAM capture slots, three parallel IR slots, Delay, Reverb, Chorus,
Post EQ, and input/output gain controls. NAM files load in the background; each
amp slot uses a pair of mono model instances to preserve stereo routing. IR
slots accept WAV, AIFF, FLAC, or OGG files. Their blend weights are normalized
and summed from the same input signal, so IRs are never cascaded. Reverb IRs can
also be loaded in those parallel slots.

Each block has an enable control where applicable, and parameters and selected
file paths are saved with the plugin state. The pre-amp pedals and post-IR
effects can each be moved up or down within their section. NAM slots can run in
series (for example, Boost → OD → Amp captures) or in parallel with per-slot
blend controls (for example, a NAM pedal and NAM amp into the same IR section).
Each NAM slot can hold a pedal or amp capture. NAM Core 0.5.4 supports
model file versions 0.5 through 0.7; this build's amp slots require
mono-in/mono-out captures.
