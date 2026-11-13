# Changelog

All notable changes to NAMplified are summarized here. This changelog is reconstructed from the Git commit history; the repository currently has no version tags or published release entries.

## Unreleased development snapshot

### Updates since November 8

- Added the local Preset Builder beta with on-demand model setup and level-matched A/B auditioning from a recorded or imported guitar DI.
- Added adjustable frequency splitting for parallel NAM amps and a polarity invert option for each amp.
- Improved the capture browser and the TONE3000 flow for selecting and loading an individual capture.
- Refined the signal-chain layout, controls, icons, and backgrounds.
- Updated the README with current screenshots and moved unfinished documentation tasks to a local file.

### 2026-11-08

- Removed the built-in amp simulations and the NAM/Amp Sim source switch; NAM capture loading and the Clean Boost, 808 Drive, and NAM Boost options remain.
- Renamed the application and plugin to **NAMplified** and refreshed the app icon and branding.
- Added an About window with project and author information, and made the interface typography more consistent.
- Added MIDI CC Learn for assigning hardware controls to plugin parameters and switches.
- Added capture switching within a folder and improved preset asset handling. Presets can include the NAM captures and IR files they reference in a matching `_Assets` folder.
- Added an option to use a NAM capture in the Boost block, alongside Clean Boost and 808 Drive.
- Added selectable Pre/Post tone shaping for NAM amp slots.
- Improved the built-in delay and reverb DSP, added master output control, and refined effect parameter presentation.
- Updated the README, macOS app icon resources, and related project documentation.

### 2026-11-07

- Added the TONE3000 browser integration for selecting and downloading an individual NAM capture.
- Added twelve built-in amp simulations based on the 0xFX amp DSP, with model-specific controls and descriptions.
- Added amp source selection between NAM captures and amp simulations.
- Improved series/parallel routing, cabinet IR blending, block reordering, and disabled-state presentation.
- Added per-cabinet IR shaping controls and expanded the EQ editors.
- Fixed a range of routing, parameter display, and preset issues; expanded README feature and license information.

### 2026-11-06

- Established the signal-chain interface with draggable effect blocks, series and parallel amp routing, paired or shared cabinet IR routing, presets, and block detail panels.
- Added input/output signal meters, global pitch shift, analog delay mode, and initial built-in boost and compressor processing.
- Improved typography, colors, tooltips, and the overall signal-chain layout.
- Added initial amp simulation support and EQ refinements.

### 2026-11-05

- Started the NAMplified project as a JUCE guitar signal-chain plugin with NAM capture support.

