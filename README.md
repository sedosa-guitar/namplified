<p align="center">
  <img src="Assets/Branding/NAMplifiedAppIcon.svg" alt="NAMplified app icon" width="144">
</p>

# NAMplified

**A guitar signal-chain plugin built around Neural Amp Modeler captures.**

NAMplified is a JUCE-based audio effect for shaping a guitar sound from input to output. Build a chain from pedals, NAM captures, cabinet impulse responses, and post-amp effects. The interface keeps the signal path visible while exposing each block's controls when selected.

> **Project status:** In active development. The plugin currently builds macOS Audio Unit, VST3, and standalone targets. Distribution packages and installation instructions are not ready yet.

See [CHANGELOG.md](CHANGELOG.md) for a summary of the project history.

## Features

### Build a signal chain

- **Input section:** input trim and global pitch shift from −24 to +24 semitones.
- **Pre-amp blocks:** Noise Gate, 1176-style compressor, Boost, and five-band Pre EQ. Reorder these blocks within the section and enable or disable them individually.
- **Two NAM slots:** load a Neural Amp Modeler (NAM) capture into either slot, adjust its input/output levels, and shape it with a Bass/Mid/Treble EQ placed before or after the capture.
- **Two cabinet IR slots:** load cabinet impulse responses, shape each IR with its own three-band EQ, and blend the two cabinet paths.
- **Post-amp blocks:** Delay, Reverb, Chorus, and nine-band Post EQ. Reorder and enable them individually.
- **Output section:** output trim and a signal meter. Input and output meters show signal level and briefly indicate clipping.
- **Presets:** save, recall, and delete user presets; reset the chain to its default setup. Saving copies referenced NAM captures and IRs into a matching `_Assets` folder beside the preset, so both can be backed up or moved together. Unsaved parameter and capture changes are indicated in the interface.
- **MIDI CC Learn:** assign incoming MIDI CC messages to parameters and switches by selecting MIDI Learn, clicking a control, then moving a controller. Mappings are shared across presets and saved in the user's application data folder.

The default chain starts with Input → Noise Gate → Amp 1 → IR 1 → Reverb → Output. Optional blocks can be enabled as needed; the chain editor presents the available stages and lets you reorder movable blocks inside their sections.

### NAM routing

Each of the two NAM slots loads a compatible `.nam` capture. Capture files can be switched with the previous/next controls when they share a folder. Preset saving copies referenced NAM captures and IRs to a matching `_Assets` folder.

The amp slots can run **in series** or **in parallel**. In series, the output of one active amp feeds the next. In parallel, both amp paths run side by side and can be blended. Cabinet routing offers two choices for parallel amps:

1. **Paired IRs:** Amp 1 feeds IR 1 and Amp 2 feeds IR 2; the cabinet paths can be blended.
2. **Shared IR 1:** the amp outputs are blended first, then processed through IR 1.

Both NAM slots can run in series or parallel without changing the overall routing layout.

### Cabinet IRs

- Load up to two cabinet impulse responses in WAV, AIFF, FLAC, or OGG format.
- Blend the two IR paths in parallel; the IRs are summed from their input rather than cascaded one after another.
- Use **IR Shape EQ** on each cabinet slot for low, mid, and high tone shaping.
- In Shared IR 1 routing, both amp paths feed the shared first IR.

### Effects and tone shaping

- **Boost:** choose the built-in Clean Boost or 808 Drive, or switch the same block to a NAM capture. The NAM option can load individual captures and step through captures in the same folder.
- **Compressor:** an 1176-inspired FET-style compressor with selectable ratios, including All Buttons In, plus attack, release, blend, output, tone, and EQ controls.
- **Pre EQ:** five adjustable bands before the amp section.
- **Post EQ:** nine adjustable bands after the effects section, with an expanded editor view.
- **Delay:** Digital or Analog character, Mono/Stereo/Ping Pong routing, plus time, feedback, mix, tone, ducking, and wet-path high-pass / low-pass filters. Analog mode adds bandwidth-limited, saturated repeats with subtle wow and flutter.
- **Reverb:** Separate Room, Plate, and Cloud feedback-delay-network voices, with early reflections, diffusion, and modulation shaped for each mode. Size/decay, damping, pre-delay, width, wet-path high-pass / low-pass, and mix remain adjustable. Room reverb is enabled by default at a subtle mix.
- **Chorus:** rate, depth, and mix controls.
- **Pitch:** global pitch shifting before the input signal enters the chain, with a range of −24 to +24 semitones.

## Signal flow

```text
Input trim → Pitch → Pre-amp blocks → Amp 1 / Amp 2 → Cabinet IR 1 / IR 2
           → Delay / Reverb / Chorus / Post EQ → Output trim
```

The exact route depends on which blocks are enabled and whether the amp section is set to Series or Parallel Blend. In parallel mode, the amp and cabinet paths are drawn separately in the chain view.

## MIDI control

NAMplified accepts MIDI input. In the standalone app, choose a MIDI input device in **Options → Audio/MIDI Settings**; in a DAW, enable the track's MIDI input for NAMplified. Click **MIDI Learn**, click a slider, knob, selector, or block enable switch, and send a MIDI CC from the controller. The assigned CC controls that parameter across presets. MIDI assignments are stored separately from presets on the current computer.

## NAM compatibility

NAMplified uses [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) 0.5.4. The current build supports NAM model file versions 0.5 through 0.7 and expects mono-in/mono-out captures. Stereo processing is handled by running paired mono instances where applicable. NAM A2 architecture parsing is included; individual models still need to use an architecture supported by the bundled NAM Core version.

### TONE3000 model browser

NAM blocks include **Browse TONE3000**. The user signs in to their own TONE3000 account in the browser, chooses a NAM tone, then chooses one compatible A1 or A2 capture. NAMplified downloads only that selected `.nam` model into the current computer's application data folder and loads it into the selected NAM slot. The OAuth token is kept in memory for this operation; NAMplified does not upload captures. When a user saves a preset, the preset manager copies any referenced NAM captures and IR files into that preset's `_Assets` folder. A sidecar metadata file and the downloaded filename retain the tone, model, creator, and license details.

This integration uses the TONE3000 **publishable key** as its public OAuth client ID. It does not contain a TONE3000 secret key. Before using Browse TONE3000, register `http://127.0.0.1:49157/tone3000/callback` as an allowed redirect URI in the TONE3000 API settings for that key. The browser flow requires network access and a free TONE3000 account. The integration downloads individual model files; it does not use or recreate TONE3000's partner-only whole-tone ZIP download.

The Browse button uses the official TONE3000 logo asset, kept unmodified in `Assets/Branding/TONE3000Logo.svg`, following TONE3000's [logo usage guidance](https://www.tone3000.com/api). This README links to the [TONE3000 website](https://www.tone3000.com).

An automated TONE3000 response stated that a free, open-source product with no paid features or upsell may use the free tier without a separate agreement or sign-off. The integration must use OAuth 2.0 with PKCE, keep the secret key out of distributed binaries, preserve creator and license details, follow the design requirements, and stay within the default 100 requests per minute. The free tier is limited to the `select_tone` and `load_tone` flows and the `favorited`, `downloaded`, `created`, `trending`, and `latest` endpoints. NAMplified downloads only the capture selected by the user and does not host or redistribute it. TONE3000 may revoke access if an integration stops meeting its guidelines. Recheck the current terms before each public release; this note records the response received and is not a separate TONE3000 endorsement or legal opinion.

See the [official API documentation](https://www.tone3000.com/api) and [API terms](https://www.tone3000.com/api/terms). NAMplified is an independent project and is not endorsed by TONE3000.

## Screenshots

### Series mode and NAM capture controls

![Series signal chain with a NAM capture selected and its tone controls open](docs/images/series-nam-capture.png)

### Parallel amps with a shared IR

![Parallel amp paths summed into a shared cabinet IR, with IR shaping controls open](docs/images/parallel-shared-ir-shaping.png)

### Compressor controls

![Compressor detail panel with ratio, attack, release, tone, blend, output and EQ controls](docs/images/compressor-parallel.png)

### Pre EQ in the signal-chain view

![Pre EQ detail panel open below a parallel amp signal chain](docs/images/pre-eq-inline.png)

### Expanded Pre EQ editor

![Expanded Pre EQ editor with five draggable bands and a band-width control](docs/images/pre-eq-full-screen.png)

## Build from source (macOS)

### Requirements

- macOS and Xcode Command Line Tools (or Xcode), including the macOS SDK
- CMake 3.22 or newer
- Git and network access on the first configure, unless dependencies are supplied locally

### Configure and build

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The first configure fetches JUCE 9.0.5, NeuralAmpModelerCore 0.5.4, and `chowdsp_wdf` 1.0.0. To use an existing JUCE checkout, pass its path with `-DJUCE_SOURCE_DIR=/path/to/JUCE`.

Build products are placed under `build`. The CMake project currently generates **AU**, **VST3**, and **Standalone** macOS targets. The standalone application can be opened from the generated `NAMplified_artefacts` directory; plugin bundles are generated alongside it.

## Installation

> **Placeholder:** Release builds and verified installation steps are not published yet. These sections will be completed when distributable packages are available.

> **Release reminder:** Before preparing installer files, recheck the current TONE3000 free-tier terms, confirm the published build contains no secret key, and verify the required logo, attribution, creator, and license details.

### macOS standalone

<!-- TODO: Add download link, supported macOS versions, installation steps, and first-launch audio setup. -->

### Audio Unit (AU)

<!-- TODO: Add package location, installation steps, host rescanning instructions, and signing/notarization details. -->

### VST3

<!-- TODO: Add package location, installation steps, host rescanning instructions, and signing/notarization details. -->

### Windows / Linux

<!-- TODO: State platform support only after those targets are built and verified. -->

## Third-party software and licenses

Third-party attributions and license information are collected in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). This includes JUCE 9.0.5 (used under its AGPLv3 option), NeuralAmpModelerCore 0.5.4 (MIT, with Eigen MPL-2.0), `chowdsp_wdf` (BSD 3-Clause), and bundled fonts (SIL OFL 1.1). JUCE's upstream SPDX inventory identifies its bundled dependencies; release packages still need a per-build review of notices for the exact modules and formats included.

**NAMplified's own code is licensed under the GNU Affero General Public License, version 3 only (AGPL-3.0-only).** See [`LICENSE`](LICENSE). Third-party components retain their own licenses and notices listed above and in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

Copyright (C) 2026 Ronny Wels (sedosa). Git commits may also appear under the author's GitLab identity `scalanakamoto`.

NAMplified is currently distributed free of charge, without paid features or donation links. This describes the maintainers' distribution; AGPL-3.0-only still permits compliant third-party forks to charge for copies.

When publishing plugin binaries or installers, provide access to the corresponding source code for that exact release and retain the required third-party license notices. The current repository is not yet a release package: installation steps, signing/notarization, and final per-binary dependency notices remain to be completed.

## Known limitations

- Current CMake targets are for macOS only.
- NAM captures must be mono-in/mono-out and compatible with the bundled NAM Core version.
- Installation packages and signing/notarization guidance are still to be added.

## Contributing

Contribution and issue-reporting guidance will be added before the public GitHub release.

<!-- TODO: Add contribution guidelines, support contact, and issue templates. -->
