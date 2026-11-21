# Pitch transposition in standard E

The current pitch shifter is effective for lowering isolated notes on the two
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

| Input | Semitones | NAMplified | Rubber Band |
| --- | ---: | ---: | ---: |
| Low E | −2 | 100% | 100% |
| High E | −2 | 0.06% | 100% |
| Six open strings | −2 | 24% | 40% |
| Six open strings | −5 | 3% | 25% |

The existing pitch regression test passed. It covers bypass and two isolated
octave shifts. Rubber Band reported 47 ms of internal start delay at −2
semitones, before any host or interface latency.

For a live transpose comparison, use the same dry guitar DI for each candidate.
Include palm-muted low notes, power chords and open chords at −1 and −2
semitones. Measure the onset delay and audition the shifted recordings for
attack, pitch stability and artifacts.
