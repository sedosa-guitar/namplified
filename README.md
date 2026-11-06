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

The signal chain includes Noise Gate, Compressor, Boost, Pre EQ, two amp slots,
two parallel IR slots, Delay, Reverb, Chorus, Post EQ, and input/output gain
controls. Each amp slot can use either a NAM capture or one of twelve
algorithmic amp models from the MIT-licensed 0xFX DSP. The built-in models are
inspired by known amplifier circuits; they are not official manufacturer
models. NAM files load in the background, with paired mono instances for stereo
routing. IR slots accept WAV, AIFF, FLAC, or OGG files. Their blend weights are
normalized and summed from the same input signal, so IRs are never cascaded.

Each block has an enable control where applicable, and parameters and selected
file paths are saved with the plugin state. Pre-amp pedals and post-IR effects
can be reordered within their section. Amp slots can run in series or in
parallel with the existing routing and IR-blend controls. NAM Core 0.5.4
supports model file versions 0.5 through 0.7; NAM captures in this build must
be mono-in/mono-out.
