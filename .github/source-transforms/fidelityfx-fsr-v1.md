# FidelityFX FSR snapshot receipt v1

- Canonical upstream: `https://github.com/GPUOpen-Effects/FidelityFX-FSR.git`
- Upstream base: `a21ffb8f6c13233ba336352bdff293894c706575`
- PocketForge source pin: `dcd34005bc7d815e64688d2bd92a495da3d5e9ef`
- Aggregate snapshot SHA-256: `3fae1917e5d85f7914f181721a5e59b9e4a11a3c5cd836ef50ae246ffdbba063`

The two rows for `snapshot-fsr` in `.github/vendored-sources.tsv` are direct
byte copies of `ffx-fsr/ffx_a.h` and `ffx-fsr/ffx_fsr1.h` from the reviewed
PocketForge source pin. The aggregate digest hashes the sorted
`target-path<TAB>file-sha256<LF>` records. The fork's source-owned regression
proves the corrected `ffx_a.h`; the Gamescope validator compares both source
blobs and targets byte-for-byte.
