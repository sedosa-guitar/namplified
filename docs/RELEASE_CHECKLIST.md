# NAMplified public release checklist

Copy this checklist into the release draft for each version. Record the
version, Git tag, commit SHA, and tested artifact names in that draft. Check
items against the files that will actually be published.

## Preflight notes

- The current local Standalone, AU, and VST3 builds are arm64 and declare macOS 14.4 as their minimum. Confirm the target versions and architectures again on the final artifacts.
- All three local builds contain the seven intended factory NAM captures and ten Sedosa presets. No loose NAMs, IRs, TONE3000 sidecars, or GGUF files were found in the bundles. The source build defaults to excluding the local Virtual Cab WAVs.
- Source review confirmed that the Preset Builder downloads llama.cpp and Qwen on demand and that TONE3000 uses a publishable OAuth client ID and PKCE. Recheck packaged files before release.
- The TONE3000 API terms effective July 7, 2026 were reviewed for Free Tier eligibility, branding, and attribution. Verify those requirements in the packaged app and recheck the terms before publication.

## Source and notices

- [ ] Identify the exact Git commit used to build the release. Make its corresponding source, including the build files and pinned dependencies or instructions to obtain them, available from the download page.
- [ ] Review the dependency inventory for each published format (Standalone, AU, VST3). Check the pinned JUCE SPDX inventory against the modules shipped and verify the notices for NAM Core, Eigen, chowdsp_wdf, fonts, and any other shipped dependency.
- [ ] Include NAMplified's [AGPL license](../LICENSE), the reviewed [third-party notices](../THIRD_PARTY_NOTICES.md), and applicable third-party license texts in the distribution. Open the finished installer or archive and verify the files are there.
- [ ] Confirm that llama.cpp and the Qwen GGUF remain on-demand downloads. If either is included in the package or mirrored, review the exact artifact's redistribution requirements and include the applicable notices.

## TONE3000

- [ ] Recheck the [API terms](https://www.tone3000.com/api/terms), including eligibility for the free tier, branding, and attribution.
- [ ] Verify that the shipped configuration contains only the publishable OAuth client ID. Confirm that no API secret, access token, or development credential was packaged.
- [ ] Inspect the exact ZIP or installer. Confirm it contains the intended factory assets and no TONE3000 captures or catalog content.
- [ ] Install the packaged build and sign in through **Browse TONE3000**. Choose a tone, select one capture, and verify that it loads and plays.
- [ ] Check the downloaded `.t3k.json` sidecar for the creator, license, and tone URL. Save a preset, restart, and verify that the capture and attribution are still available.

## Test the finished download

- [ ] Verify that the package contains the intended Sedosa presets and factory captures. Check that a clean start restores them in each shipped format and that local Virtual Cab WAVs and user-library assets are absent.
- [ ] Record the supported macOS versions and architectures. Verify signing and notarization for the artifacts being published.
- [ ] Install from the finished package on a clean macOS setup. Launch Standalone and verify AU and VST3 discovery and loading in compatible hosts, as applicable.
- [ ] Test the Preset Builder first-run flow. Verify the confirmation, download, cancellation, checksum verification, Finder location, and a second build using the cached files offline.
- [ ] Verify all published download and source links, update installation instructions, and add the release notes to the changelog.

## Publish the release

- [ ] Build the latest release ZIP and generate its SHA-256 checksum.
- [ ] Publish the same ZIP and SHA-256 checksum on both the project website and GitHub Releases.

## Release page draft

Replace the placeholders with the actual public links before publishing.

- **NAMplified [version] for macOS** — [installer or archive link]
- **Corresponding source for this build** — [source link for the exact release tag or commit]
- **Licenses and third-party notices** — [link or location inside the download]
- **Installation and supported systems** — [instructions link]
