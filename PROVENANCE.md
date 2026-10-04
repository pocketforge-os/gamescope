# PocketForge Gamescope provenance

## Upstream and audited base

- Authoritative upstream: <https://github.com/ValveSoftware/gamescope>
- PocketForge fork: <https://github.com/pocketforge-os/gamescope>
- Audited base commit: `5fb8dce4a09d0a68d097b9faf9513782106bc843`
- Audited base tree: `74d70414bbbbe4f3f009ecd3cf6a3a07bfdccf27`
- Primary repository licence file: `LICENSE`, SHA-256
  `907dd845489cd09c25f18b6819f476805285e2adcad8c099e3506260023e9e5f`

The fork retains complete upstream ancestry. PocketForge commits are applied
after the audited base and must not rewrite it. The primary project licence is
BSD-2-Clause; the additional notices already present in `LICENSE` and every
dependency licence remain part of the corresponding source and binary
distribution obligations.

## Direct source closure

All gitlinks are initialized at the commit recorded by the superproject.
PocketForge release builds must fail if a checkout is missing or reports a
different commit. "Upstream" below means that the unmodified upstream URL in
`.gitmodules` is retained. A dependency moves to a `pocketforge-os` controlled
fork only when PocketForge patches that dependency; that change must pin a
commit, explain the patch, and update this table and the licence inventory in
the same pull request.

| Path | Exact gitlink | PocketForge build decision | Fork decision | Licence evidence at the pin |
| --- | --- | --- | --- | --- |
| `src/reshade` | `696b14cd6006ae9ca174e6164450619ace043283` | Build the effect compiler sources listed by `src/meson.build` directly into Gamescope. | Upstream; no PocketForge patch. | BSD-3-Clause, `LICENSE.md`, SHA-256 `237ded5b8344f820113efab1e65e91e1f159d9202c5b4856606a0590d3ffdab0`. |
| `subprojects/libdisplay-info` | `47a5590e9c4eb35d67651b8c05a55f1a48259329` | Build the pinned Meson fallback as a static library; do not substitute a system copy in the PocketForge target profile. | Upstream; no PocketForge patch. | MIT, `LICENSE`, SHA-256 `15b396244e58830c5614b9394f4deccfe684970cd507f299383ab57ad339eedd`. |
| `subprojects/libliftoff` | `8b08dc1c14fd019cc90ddabe34ad16596b0691f4` | Build the pinned static Meson fallback. Gamescope already requires this fallback. | Upstream; no PocketForge patch. | MIT, `LICENSE`, SHA-256 `9b230152f28fc7898665f40da7c311a1bf238a68b12cc39d9c83bbfc117a7b6b`. |
| `subprojects/openvr` | `ff87f683f41fe26cc9353dd9d9d7028357fd8e1a` | Do not build in the PocketForge handheld target profile (`enable_openvr_support=false`). Keep the exact gitlink in the audited source closure. | Upstream; no PocketForge patch. | BSD-3-Clause, `LICENSE`, SHA-256 `f56ff606104d4ef18e617921a75c73ad73b5a1a1d70c69590c29de16919e04ad`. |
| `subprojects/vkroots` | `5106d8a0df95de66cc58dc1ea37e69c99afc9540` | Build the pinned Meson fallback. Gamescope already requires this fallback. | Upstream; no PocketForge patch. | Generator scripts LGPL-2.1; generated/header material Apache-2.0 OR MIT, `LICENSE`, SHA-256 `66d083f861a7f030f00a7edd80b616f3d762fed5d0ddc34eb133a9e792e2691d`. |
| `subprojects/wlroots` | `88a869855742281c98c22cab9641b317b8d065ef` | Build the pinned static Meson fallback with the options declared in `src/meson.build`; do not substitute a system copy in the PocketForge target profile. | Upstream; no PocketForge patch. | MIT, `LICENSE`, SHA-256 `35d427c043dcafe8893b9e7247348f599847c81d9a067703587c80707f3d58df`; the unused `tinywl` example retains its CC0 notice. |
| `thirdparty/SPIRV-Headers` | `d790ced752b5bfc06b6988baadef6eb2d16bdf96` | Consume the pinned headers directly when compiling the ReShade sources. | Upstream; no PocketForge patch. | MIT-style Khronos licence, `LICENSE`, SHA-256 `9b243f6f0bf44e295ff411a0f7b7642d1d0dff7cdc42507e9f7206f439e51b5a`. |

## Meson source closure

`.github/meson-sources.lock` is the machine-readable source manifest for every
Meson subproject selected by the native and aarch64 configurations. Each row
records the exact revision, immutable archive URL, archive filename and root,
SHA-256, Meson location, and licence-file SHA-256. The complete selected set is:

| Meson input | Kind and exact revision | Immutable archive and SHA-256 | Build and licence decision |
| --- | --- | --- | --- |
| `libdisplay-info` | gitlink `47a5590e9c4eb35d67651b8c05a55f1a48259329` | <https://gitlab.freedesktop.org/emersion/libdisplay-info/-/archive/47a5590e9c4eb35d67651b8c05a55f1a48259329/libdisplay-info-47a5590e9c4eb35d67651b8c05a55f1a48259329.tar.gz>, `e7c446673180b3f8f19890e2d5223fa1058d706368a2c5c69c0e6443f811b3e3` | Build the exact checked-out fallback; preserve MIT `LICENSE` (`15b396244e58830c5614b9394f4deccfe684970cd507f299383ab57ad339eedd`). |
| `libliftoff` | gitlink `8b08dc1c14fd019cc90ddabe34ad16596b0691f4` | <https://gitlab.freedesktop.org/emersion/libliftoff/-/archive/8b08dc1c14fd019cc90ddabe34ad16596b0691f4/libliftoff-8b08dc1c14fd019cc90ddabe34ad16596b0691f4.tar.gz>, `8de28aee6f90f47b7fc7037dcd2360166197c0b5d2033f3afdbd34f2ea1bf216` | Build the exact checked-out fallback; preserve MIT `LICENSE` (`9b230152f28fc7898665f40da7c311a1bf238a68b12cc39d9c83bbfc117a7b6b`). |
| `vkroots` | gitlink `5106d8a0df95de66cc58dc1ea37e69c99afc9540` | <https://github.com/Joshua-Ashton/vkroots/archive/5106d8a0df95de66cc58dc1ea37e69c99afc9540.tar.gz>, `37b77586e91f7ebee70380dcddd73bf01ae4acef1053e6be41d0485ede022422` | Build the exact checked-out fallback; preserve LGPL-2.1 generator and Apache-2.0 OR MIT generated/header terms in `LICENSE` (`66d083f861a7f030f00a7edd80b616f3d762fed5d0ddc34eb133a9e792e2691d`). |
| `wlroots` | gitlink `88a869855742281c98c22cab9641b317b8d065ef` | <https://gitlab.freedesktop.org/wlroots/wlroots/-/archive/88a869855742281c98c22cab9641b317b8d065ef/wlroots-88a869855742281c98c22cab9641b317b8d065ef.tar.gz>, `587256827f7e1bcbb7384d1c04c0e21682565169e7a906560f723820bcf78af2` | Build the exact checked-out fallback; preserve MIT `LICENSE` (`35d427c043dcafe8893b9e7247348f599847c81d9a067703587c80707f3d58df`) and the unused example's CC0 notice. |
| `glm` | wrap archive for `0af55ccecd98d4e5a8d1fad7de25ba429d60e863` | <https://github.com/g-truc/glm/archive/0af55ccecd98d4e5a8d1fad7de25ba429d60e863.tar.gz>, `e7f187d83523f505eb38dd25d297ea6c0d4ed856d733e808f18253f5a8fa88a0` | Build only the hash-verified archive plus the committed Meson patch directory; preserve MIT `copying.txt` (`62d2d642c7d054d4fb4c9b42faad617d6c88fcd91e317f8035aa9f277cc159c3`). |
| `stb` | wrap archive for `5736b15f7ea0ffb08dd38af21067c314d6a3aae9` | <https://github.com/nothings/stb/archive/5736b15f7ea0ffb08dd38af21067c314d6a3aae9.tar.gz>, `d00921d49b06af62aa6bfb97c1b136bec661dd11dd4eecbcb0da1f6da7cedb4c` | Build only the hash-verified archive plus the committed Meson patch directory; preserve upstream `LICENSE` and its MIT-or-public-domain choice (`bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63`). |

The manifest-driven `.github/scripts/prime-meson-sources.sh` cache path ends in
the SHA-256 of the manifest itself. A cold invocation downloads to a partial
file, verifies the archive and embedded licence, then atomically admits it. A
warm or `PF_MESON_CACHE_OFFLINE=1` invocation verifies the cached bytes again.
Only verified `glm` and `stb` archives enter `subprojects/packagecache` because
the other four inputs are exact checked-out gitlinks. Missing offline inputs,
existing cache mismatches, packagecache mismatches, and licence mismatches all
fail closed; the helper never repairs a mismatch by fetching replacement bytes.

Every worker, coordinator, and CI build must prime this committed manifest
before configuring Meson. The build directory must be fresh, or must be
explicitly reconfigured with `--wrap-mode=nodownload`, before any compile;
`meson compile` must never reuse a build directory configured without that
option. CI starts each real build from a fresh directory. Its RED check begins
with an otherwise complete verified cache and packagecache, removes only the
required `glm` wrap archive, ensures the extracted `glm` subproject is absent,
and requires the representative native `meson setup --wrap-mode=nodownload`
to fail with Meson's disabled-download error. CI then restores and re-verifies
the archive from the digest-keyed cache before the real build.

`enable_openvr_support=false` excludes the pinned OpenVR gitlink from both CI
configurations. The wraps shipped inside wlroots for `libdisplay-info`,
`libdrm`, `libliftoff`, `libxkbcommon`, `pixman`, `seatd`, `wayland-protocols`,
and `wayland`, and the `v4l-utils` wrap inside libdisplay-info, are not selected:
their requirements are satisfied by the exact top-level fallbacks or container
packages. Some of those upstream wrap files name mutable revisions, so CI uses
`--wrap-mode=nodownload`; a missing dependency is an error and can never cause
Meson to fetch one of them. No other Meson subproject or wrap is selected by
the documented configurations.

`src/reshade` contains its own recursive gitlinks. Gamescope does not compile or
link those nested repositories: its Meson target names only ReShade's effect
compiler sources, and uses the separate top-level `thirdparty/SPIRV-Headers`
gitlink. CI still checks out the recursive graph so an unexpected or missing
gitlink is visible. The pins and admission decisions are:

| ReShade nested path | Exact gitlink | Decision / upstream licence at the pin |
| --- | --- | --- |
| `deps/d3d12` | `de28d93dfa9ebf3e473127c1c657e1920a5345ee` | Not built or redistributed in the target; MIT licence retained. |
| `deps/fpng` | `6926f5a0a78f22d42b074a0ab8032e07736babd4` | Not built or redistributed; must receive a fresh licence audit before any future enablement. |
| `deps/gl3w` | `3a33275633ce4be433332dc776e6a5b3bdea6506` | Not built or redistributed; must receive a fresh licence audit before any future enablement. |
| `deps/imgui` | `c6aa051629753f0ef0d26bf775a8b6a92aa213b2` | Not built or redistributed; MIT licence retained. |
| `deps/minhook` | `8fda4f5481fed5797dc2651cd91e238e9b3928c6` | Not built or redistributed; BSD-2-Clause licence retained. |
| `deps/openxr` | `dc1e23937fe45eabcce80f6588cf47449edb29d1` | Not built or redistributed; upstream Apache-2.0 and accompanying Khronos notices retained. |
| `deps/spirv` | `c0df742ec0b8178ad58c68cff3437ad4b6a06e26` | Not built or redistributed; upstream Khronos licence retained. |
| `deps/stb` | `ae721c50eaf761660b4f90cc590453cdb0c2acd0` | Not built or redistributed; upstream MIT-or-public-domain notice retained. |
| `deps/utfcpp` | `6be08bbea14ffa0a5c594257fb6285a054395cd7` | Not built or redistributed; BSL-1.0 licence retained. |
| `deps/utfcpp/extern/ftest` | `c4ad4af0946b73ce1a40cbc72205d15d196c7e06` | Not built or redistributed; BSL-1.0 licence retained. |
| `deps/vma` | `a6bfc237255a6bac1513f7c1ebde6d8aed6b5191` | Not built or redistributed; MIT licence retained. |
| `deps/vulkan` | `7b3466a1f47a9251ac1113efbe022ff016e2f95b` | Not built or redistributed; Apache-2.0 OR MIT licence retained. |

## Patch and branch policy

1. `pocketforge` is the protected default and PocketForge integration branch.
   Its bootstrap ref was created directly at the unchanged audited commit
   `5fb8dce4a09d0a68d097b9faf9513782106bc843`, before any content change, so
   the first governance change can itself remain reviewable. Changes use a
   focused topic branch and pull request; direct pushes and force pushes are
   prohibited.
2. `master` preserves the later upstream history that existed when the fork
   was bootstrapped (then at
   `0e590c755e79c23607378495d10ceb4308b01a59`). It was neither rewritten nor
   deleted and is not the PocketForge pull-request base.
3. PocketForge patches are separate commits after the audited base. A source
   behavior patch must not be hidden in a provenance, packaging, or CI-only
   change.
4. Upstream synchronization is an explicit reviewed merge into `pocketforge`.
   The pull request updates the audited commit/tree, gitlink and wrap pins,
   licence inventory, and build evidence together.
5. Submodules and wraps never follow a mutable branch or tag. An unmodified
   dependency keeps its authoritative upstream URL. A patched dependency uses
   a controlled PocketForge fork and immutable gitlink until the patch is
   upstream and the fork can be retired by a reviewed provenance change.
6. Release inputs are committed source or immutable, checksum-verified source
   archives. Binary vendor substitutions are not accepted.

## CI and release boundary

Repository CI is a regression gate: it verifies the recursive gitlink graph;
exercises cold and offline-warm manifest caches; proves that missing and
hash-mismatched offline artifacts fail; proves that a real Meson setup cannot
use an absent required wrap; performs the native build and upstream unit tests;
and performs an aarch64 Meson build. Both real Meson configurations are fresh
and use `--wrap-mode=nodownload`. CI runs only on ephemeral container runners
and rejects pull requests whose head repository is not this repository before
a job can be scheduled.

CI now proves the digest-locked, offline Meson source closure for this commit.
CI success alone is not a PocketForge release receipt: release packaging must
also lock the compiler/sysroot and every enabled system library, emit an SPDX
SBOM and complete licence bundle, and reproduce from two clean roots. Patches
and their digests must be committed and recorded by the downstream packaging
change.
