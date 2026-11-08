# 0xFX amplifier DSP

NAMplified uses the amplifier DSP and shared biquad implementation from
[0xFX](https://github.com/averagenative/0xFX), version 1.3.1, commit
`5b248779c96dce6e64d0ed29cfa598014dc4d3c4`.

Only `engine/internal/amp.c`, `engine/internal/biquad.c`, and the headers needed
to compile those files are included here. The 0xFX user interface, routing,
pedals, cabinet processing, and other effects are not included. The files are
used under the MIT License; see [LICENSE](LICENSE).
