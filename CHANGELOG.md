# Changelog

All notable changes to NAMplified are summarized here. This changelog is reconstructed from the Git commit history; the repository currently has no version tags or published release entries.

## Unreleased development snapshot

### 2026-11-12 to 2026-11-13

- Refined editor buttons, icon placement, and panel backgrounds.
- Replaced the README screenshots with current views of the amp controls, routing modes, TONE3000, and Preset Builder. Moved unfinished documentation tasks out of the public README.

### 2026-11-11

- Added the local Preset Builder beta. Describe a tone, record or import a dry guitar DI, and compare the original chain with two generated variants at matched levels.
- Added first-run downloads for the local runtime and model, with user confirmation, SHA-256 verification, and reuse of downloaded files. Added a TONE3000 search shortcut for suggested captures.
- Added a standalone Audio / MIDI settings window for configuring audio devices and MIDI inputs.
- Updated the menu, logo presentation, and text rendering.
- Added the public release checklist and updated third-party license and distribution notes.

### 2026-11-09 to 2026-11-10

- Added an adjustable frequency split in parallel mode. Amp 1 receives lows and Amp 2 receives highs when both amps are active.
- Added a polarity invert option for each NAM amp.
- Improved capture browsing and the TONE3000 flow for selecting a tone and loading an individual capture. Updated capture switching to apply changes immediately.
- Refined the signal-chain layout, signal indicators, block detail panels, and knob styling.

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

