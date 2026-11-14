# Third-party notices

This file covers third-party components currently identified in the source
tree and the configured macOS build. Before each binary release, regenerate
and review the dependency inventory for the exact build (including the JUCE
SPDX inventory described below), then include the applicable notices with the
binary. Build dependencies may change when their pinned versions change.

## NAMplified project materials

Unless a file says otherwise, project-authored source code is licensed under
AGPL-3.0-only; see [`LICENSE`](LICENSE). The NAMplified name, interface artwork,
and application icon are project materials by Ronny Wels (sedosa), copyright
2026. The NAMplified application icon (`Assets/Branding/NAMplifiedAppIcon.svg`,
`.png`, and `.icns`) is original project artwork by Ronny Wels (sedosa).

## JUCE

NAMplified uses JUCE 9.0.5. The JUCE Framework modules are dual-licensed under
AGPLv3 or a commercial JUCE licence. This project uses the AGPLv3 option; the
project's `LICENSE` contains the AGPLv3 text. JUCE's framework dependencies
and their license identifiers are inventoried in the upstream
[`JUCE.spdx.json`](https://github.com/juce-framework/JUCE/blob/9.0.5/JUCE.spdx.json)
for this pinned version. Review the licenses of components included by the
selected JUCE modules when producing a binary; do not assume every package in
that inventory is linked into every build.

## NeuralAmpModelerCore

NAMplified uses NeuralAmpModelerCore v0.5.4 by Steven Atkinson, including its
NAM A1/A2 model parsing and DSP code. The upstream project is licensed under
the MIT License. Its vendored Eigen dependency is licensed under MPL-2.0; see
the [Eigen license text](https://github.com/sdatkinson/NeuralAmpModelerCore/blob/v0.5.4/Dependencies/eigen/COPYING.MPL2).
The bundled AudioDSPTools dependency retains the MIT notice shown below.

```text
MIT License

Copyright (c) 2023 Steven Atkinson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Fonts

The bundled Inter and Space Grotesk font files are distributed under the SIL
Open Font License 1.1. The corresponding license texts are kept alongside the
font files in `Assets/Fonts/OFL.txt` and `Assets/Fonts/SpaceGrotesk-OFL.txt`.

## Preset Builder downloads (macOS)

- **llama.cpp b8648** by the ggml authors, under the MIT License. NAMplified downloads the official architecture-specific archive directly from the [upstream release](https://github.com/ggml-org/llama.cpp/releases/tag/b8648). The archive includes the [upstream license](https://github.com/ggml-org/llama.cpp/blob/b8648/LICENSE), which is retained when the runtime is unpacked.
- **Qwen3-4B-Instruct-2507** by Qwen, under [Apache License 2.0](https://huggingface.co/Qwen/Qwen3-4B-Instruct-2507/blob/main/LICENSE). NAMplified uses the [LM Studio Community Q4_K_M GGUF conversion](https://huggingface.co/lmstudio-community/Qwen3-4B-Instruct-2507-GGUF/blob/dc2e722/Qwen3-4B-Instruct-2507-Q4_K_M.gguf), pinned to revision `dc2e722` and verified by SHA-256.

The runtime and model are fetched on demand from GitHub and Hugging Face after user confirmation. They are separate from NAMplified's app bundle. If a future installer bundles or mirrors either artifact, review the exact artifact's redistribution requirements and include the applicable license and notices.

## TONE3000 brand asset

`Assets/Branding/TONE3000Logo.svg` is the unmodified TONE3000 logo downloaded
from TONE3000's official brand asset package. It is used only to identify the
TONE3000 integration. See the current [API terms and brand requirements](https://www.tone3000.com/api/terms).

## chowdsp_wdf

NAMplified uses `chowdsp_wdf` v1.0.0, a header-only Wave Digital Filter library by Jatin Chowdhury, to model the Boost stage's diode pair.

Copyright (c) 2022, Chowdhury-DSP. All rights reserved.

The library is distributed under the BSD 3-Clause License:

```text
BSD 3-Clause License

Copyright (c) 2022, Chowdhury-DSP
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Airwindows Galactic2

DSP adapted from [Airwindows Galactic2](https://github.com/airwindows/airwindows/tree/94cb6793893027fb418ffac7cdbfcb9b4c82aadb/plugins/LinuxVST/src/Galactic2).

```text
MIT License

Copyright (c) 2018 Chris Johnson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
