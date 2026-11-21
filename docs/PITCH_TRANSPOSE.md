# Pitch transposition in standard E

The original delay-based pitch shifter was effective for lowering isolated notes on the two
lowest strings in this test. The results do not support using it for general
polyphonic guitar transposition.

## Measurement

At 48 kHz, the inputs were sustained sine waves at the six open-string
fundamentals (82.41, 110.00, 146.83, 196.00, 246.94 and 329.63 Hz), tested
individually and summed at equal amplitude. The NAMplified processor ran at
128 samples per block. Rubber Band LiveShifter ran separately in mono with
the short window and its native 512-sample block size.

The figures show the share of output power at the expected frequencies,
calculated as input frequency times 2^(semitones / 12). The analysis discarded
the first second and used a Hann window over the following two seconds.

| Input | Semitones | Original shifter | Rubber Band |
| --- | ---: | ---: | ---: |
| Low E | −2 | 100% | 100% |
| High E | −2 | 0.06% | 100% |
| Six open strings | −2 | 24% | 40% |
| Six open strings | −5 | 3% | 25% |

The original pitch regression test passed. It covered bypass and two isolated
octave shifts. Rubber Band reported 47 ms of internal start delay at −2
semitones. The NAMplified integration adds one 512-sample block (about
11 ms at 48 kHz) for adapting the shifter to host block sizes.

For a live transpose comparison, use the same dry guitar DI for each candidate.
Include palm-muted low notes, power chords and open chords at −1 and −2
semitones. Measure the onset delay and audition the shifted recordings for
attack, pitch stability and artifacts.

## Causal filter-bank prototype

[`LiveTransposeProbe.cpp`](../Tests/LiveTransposeProbe.cpp) tests a mono
harmonic resynthesis experiment outside the plugin. It tracks phase in
72 chromatically spaced bands. The adaptive variant briefly uses a faster
analysis after an onset and returns to a slower rate for sustained notes.

```sh
clang++ -O3 -std=c++20 Tests/LiveTransposeProbe.cpp -o /tmp/live-transpose-probe
/tmp/live-transpose-probe
```

To include the Rubber Band baseline, build with
`-DNAM_TRANSPOSE_COMPARE_RUBBERBAND`, the Rubber Band header include path,
and the same library used by the plugin. On macOS that library also links
Accelerate. Clip mode takes `-1` or `-2`, then `adaptive` or
`rubber-band`, followed by input and output paths for 48 kHz mono
little-endian float32 PCM.

At −2 semitones, the adaptive 10 ms configuration produced about 91% of
sustained output power at the expected frequencies for both the synthetic
power chord and the synthetic E-major chord with harmonics. Its onset was
detected about 3 ms after the input on those tests. The 12 ms version
reached about 99% on both chords. The synthetic onset metric is a threshold
crossing and does not measure steady-state alignment.

On the [Pedal Sim CC0 clips](https://github.com/RPHeidrick/pedal-sim/blob/main/samples/README.md),
assembled from real FreePats DI note recordings, RMS-envelope correlation
estimated 7 ms (power chords) and 6 ms (open chords) for the adaptive
10 ms configuration at −2 semitones. The 12 ms configuration gave 15 ms
and 21 ms, respectively. Rubber Band measured 58 ms on both clips by the
same method. Correlation was weak, especially on the open chords, so these
are rough envelope delays. The recordings do not establish subjective
quality or DAW latency.
