# NAMplified public release checklist

Copy this checklist into the release draft for each version. Record the
version, Git tag, commit SHA, and tested artifact names in that draft. Check
items against the files that will actually be published.

## Source and notices

- [ ] Identify the exact Git commit used to build the release. Make its corresponding source, including the build files and pinned dependencies or instructions to obtain them, available from the download page.
- [ ] Review the dependency inventory for each published format (Standalone, AU, VST3). Check the pinned JUCE SPDX inventory against the modules shipped and verify the notices for NAM Core, Eigen, chowdsp_wdf, fonts, and any other shipped dependency.
- [ ] Include NAMplified's [AGPL license](../LICENSE), the reviewed [third-party notices](../THIRD_PARTY_NOTICES.md), and applicable third-party license texts in the distribution. Open the finished installer or archive and verify the files are there.
- [ ] Confirm that llama.cpp and the Qwen GGUF remain on-demand downloads. If either is included in the package or mirrored, review the exact artifact's redistribution requirements and include the applicable notices.

## TONE3000

- [ ] Recheck the [API terms](https://www.tone3000.com/api/terms), including eligibility for the free tier, branding, and attribution.
- [ ] Verify that the shipped configuration contains only the publishable OAuth client ID. Confirm that no API secret, access token, or development credential was packaged.
- [ ] Inspect the packaged contents for bundled TONE3000 captures or catalog content. Test the authenticated user selection and download flow, including preservation of creator, metadata, and license information.

## Test the finished download

- [ ] Record the supported macOS versions and architectures. Verify signing and notarization for the artifacts being published.
- [ ] Install from the finished package on a clean macOS setup. Launch Standalone and verify AU and VST3 discovery and loading in compatible hosts, as applicable.
- [ ] Test the Preset Builder first-run flow. Verify the confirmation, download, cancellation, checksum verification, Finder location, and a second build using the cached files offline.
- [ ] Verify all published download and source links, update installation instructions, and add the release notes to the changelog.

## Release page draft

Replace the placeholders with the actual public links before publishing.

- **NAMplified [version] for macOS** — [installer or archive link]
- **Corresponding source for this build** — [source link for the exact release tag or commit]
- **Licenses and third-party notices** — [link or location inside the download]
- **Installation and supported systems** — [instructions link]
