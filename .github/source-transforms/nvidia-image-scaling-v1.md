# NVIDIA Image Scaling snapshot receipt v1

- Canonical upstream: `https://github.com/NVIDIAGameWorks/NVIDIAImageScaling.git`
- Upstream and PocketForge source pin: `35e13ba316c98eeecf16f37eae70ce88019911f6`
- Aggregate snapshot SHA-256: `fbec4e3d79c3d4072aa50bb94ff4dc277a235c3345c34a5b2e627ecc6919c82d`

The six rows for `snapshot-nis` in `.github/vendored-sources.tsv` are direct
byte copies from the named paths at the source pin. The aggregate digest hashes
the sorted `target-path<TAB>file-sha256<LF>` records. The validator compares
every source blob and target byte-for-byte; there is no content transform.
