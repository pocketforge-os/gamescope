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

The two Meson wraps are immutable inputs as well:

| Wrap | Exact revision | Build and licence decision |
| --- | --- | --- |
| `glm` | `0af55ccecd98d4e5a8d1fad7de25ba429d60e863` | Build the wrap with Gamescope's local Meson packaging; retain the upstream MIT licence. |
| `stb` | `5736b15f7ea0ffb08dd38af21067c314d6a3aae9` | Build the wrap with Gamescope's local Meson packaging; retain stb's upstream MIT-or-public-domain choice and notices. |

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

1. `master` is protected integration history. Changes use a focused topic
   branch and pull request; direct pushes and force pushes are prohibited.
2. PocketForge patches are separate commits after the audited base. A source
   behavior patch must not be hidden in a provenance, packaging, or CI-only
   change.
3. Upstream synchronization is an explicit reviewed merge. The pull request
   updates the audited commit/tree, gitlink and wrap pins, licence inventory,
   and build evidence together.
4. Submodules and wraps never follow a mutable branch or tag. An unmodified
   dependency keeps its authoritative upstream URL. A patched dependency uses
   a controlled PocketForge fork and immutable gitlink until the patch is
   upstream and the fork can be retired by a reviewed provenance change.
5. Release inputs are committed source or immutable, checksum-verified source
   archives. Binary vendor substitutions are not accepted.

## CI and release boundary

Repository CI is a regression gate: it verifies the recursive gitlink graph,
performs the native build and upstream unit tests, and performs an aarch64 Meson
build. It runs only on ephemeral container runners and rejects pull requests
whose head repository is not this repository before a job can be scheduled.

CI success alone is not a PocketForge release receipt. A release build must use
an offline, digest-locked distfile set for this commit, every built gitlink and
wrap, the compiler/sysroot and all enabled system libraries; emit an SPDX SBOM
and licence digests; and reproduce from two clean roots. Patches and their
digests must be committed and recorded by the downstream packaging change.
