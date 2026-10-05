# PocketForge Gamescope provenance

## Root and policy

| Field | Value |
| --- | --- |
| Canonical upstream | `https://github.com/ValveSoftware/gamescope` |
| PocketForge fork | `https://github.com/pocketforge-os/gamescope` |
| Protected implementation base | `79399f4b34b5571ba5b03f61c4717eaf93ff75b3` |
| Base tree | `faec3e9f2a1fc1eb788369d02bbedf36a34cc36b` |
| Licence | `LICENSE`, SHA-256 `907dd845489cd09c25f18b6819f476805285e2adcad8c099e3506260023e9e5f` |

PocketForge applies a fork-all/no-drop source policy. Every source-capable
gitlink, Meson wrap, recursive locator, test-only fallback and compiled
snapshot remains represented below even when a particular product profile
does not select it. System packages and build tools are locked by the image and
sysroot boundary; they are not source acquisition edges in this repository.
The wlroots `xserver` fallback remains system-only because its admitted tree has
no local source locator.

The machine authority is `.github/pocketforge-source-closure.tsv`. Its
33 edge rows are checked against Git object metadata, nested `.gitmodules`,
nested wraps, snapshot bytes, source-owned transform tests, licence bytes and
the admitted object cache. `upstream_url` is provenance only. Build admission
uses only each row's public `pocketforge-os` URL and full commit.

## Complete source graph

`Tree/content` is the fork commit tree for Git inputs and the committed-byte
digest for snapshots. `Tests` records the retained or exercised surface; “no
registered test found” does not mean that repository content was dropped.

| Parent / path | Canonical upstream @ base | Full-history PocketForge fork @ immutable pin | Tree/content | Licence path / SHA-256 | Selectors / tests | Patch status |
| --- | --- | --- | --- | --- | --- | --- |
| `gamescope` / `src/reshade` | `misyltoad/reshade@696b14cd6006ae9ca174e6164450619ace043283` | `pocketforge-os/reshade@696b14cd6006ae9ca174e6164450619ace043283` | `8363a1418caaa81e40beba1aa3d2f1554ced7e57` | `LICENSE.md` / `237ded5b8344f820113efab1e65e91e1f159d9202c5b4856606a0590d3ffdab0` | native, aarch64, OpenVR / effect compiler and recursive inventory | unpatched |
| `gamescope` / `subprojects/libdisplay-info` | `emersion/libdisplay-info@47a5590e9c4eb35d67651b8c05a55f1a48259329` | `pocketforge-os/libdisplay-info@f05a3063aa72c899c974a75a20843d20b72553ef` | `3e3d2546049a3087bba3abafc2f326a89e15ccfe` | `LICENSE` / `15b396244e58830c5614b9394f4deccfe684970cd507f299383ab57ad339eedd` | native, aarch64, OpenVR, upstream tests / forced fallback, 64 tests, v4l tool | patched locator/tests |
| `gamescope` / `subprojects/libliftoff` | `emersion/libliftoff@8b08dc1c14fd019cc90ddabe34ad16596b0691f4` | `pocketforge-os/libliftoff@8b08dc1c14fd019cc90ddabe34ad16596b0691f4` | `c0a233ab7d53f622bc80cb61b7d42315af73a1bb` | `LICENSE` / `9b230152f28fc7898665f40da7c311a1bf238a68b12cc39d9c83bbfc117a7b6b` | native, aarch64, OpenVR, upstream tests / libliftoff suite | unpatched |
| `gamescope` / `subprojects/openvr` | `ValveSoftware/openvr@ff87f683f41fe26cc9353dd9d9d7028357fd8e1a` | `pocketforge-os/openvr@ff87f683f41fe26cc9353dd9d9d7028357fd8e1a` | `0269debfa525d160ecb56275504b0fe127f5f55c` | `LICENSE` / `f56ff606104d4ef18e617921a75c73ad73b5a1a1d70c69590c29de16919e04ad` | OpenVR / enabled compile | unpatched |
| `gamescope` / `subprojects/vkroots` | `misyltoad/vkroots@5106d8a0df95de66cc58dc1ea37e69c99afc9540` | `pocketforge-os/vkroots@5106d8a0df95de66cc58dc1ea37e69c99afc9540` | `7bc91f8d3d69021a37efc34397759c01b6a1d3ab` | `LICENSE` / `66d083f861a7f030f00a7edd80b616f3d762fed5d0ddc34eb133a9e792e2691d` | native, aarch64, OpenVR / fallback compile | unpatched |
| `gamescope` / `subprojects/wlroots` | `wlroots/wlroots@88a869855742281c98c22cab9641b317b8d065ef` | `pocketforge-os/wlroots@edbc105b776f4735924e46666b23969c8551f54f` | `57c6ad937e553297f732a77d00d86cdc736c3bbb` | `LICENSE` / `35d427c043dcafe8893b9e7247348f599847c81d9a067703587c80707f3d58df` | native, aarch64, OpenVR, upstream metadata / all options; no registered tests at pin | patched locators |
| `gamescope` / `thirdparty/SPIRV-Headers` | `KhronosGroup/SPIRV-Headers@d790ced752b5bfc06b6988baadef6eb2d16bdf96` | `pocketforge-os/SPIRV-Headers@d790ced752b5bfc06b6988baadef6eb2d16bdf96` | `b39b63d59b61a3d416b81227a021584855ca7f40` | `LICENSE` / `9b243f6f0bf44e295ff411a0f7b7642d1d0dff7cdc42507e9f7206f439e51b5a` | native, aarch64, OpenVR / product and upstream compile tests | unpatched |
| `gamescope` / `subprojects/glm.wrap` | `g-truc/glm@0af55ccecd98d4e5a8d1fad7de25ba429d60e863` | `pocketforge-os/glm@0af55ccecd98d4e5a8d1fad7de25ba429d60e863` | `5ab920679b0ed95e2304c62dad3d7a0e61148f8b` | `copying.txt` / `62d2d642c7d054d4fb4c9b42faad617d6c88fcd91e317f8035aa9f277cc159c3` | native, aarch64, OpenVR / Gamescope tests and Meson overlay | unpatched |
| `gamescope` / `subprojects/stb.wrap` | `nothings/stb@5736b15f7ea0ffb08dd38af21067c314d6a3aae9` | `pocketforge-os/stb@5736b15f7ea0ffb08dd38af21067c314d6a3aae9` | `6e6bf23c585181f39bbdd271ba22d0edd0b4a555` | `LICENSE` / `bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63` | native, aarch64, OpenVR / product compile and Meson overlay | unpatched |
| `reshade` / `src/reshade/deps/d3d12` | `microsoft/DirectX-Headers@de28d93dfa9ebf3e473127c1c657e1920a5345ee` | `pocketforge-os/DirectX-Headers@de28d93dfa9ebf3e473127c1c657e1920a5345ee` | `82c5eae73f62862104b92343e02b3826766b1ce3` | `LICENSE` / `903df5512f7d02609fed0c780a9b704f5a3eeb6e4d84ebe42a29845c81899a3c` | recursive retained / upstream test surfaces | unpatched |
| `reshade` / `src/reshade/deps/fpng` | `richgel999/fpng@6926f5a0a78f22d42b074a0ab8032e07736babd4` | `pocketforge-os/fpng@6926f5a0a78f22d42b074a0ab8032e07736babd4` | `bdfd5201a8ca24aba25583334e8b83255f6b6ae8` | `README.md` / `12f989bf53fee618778886bc028cc92484bb6a906a98451aea2d840a8295d10c` | recursive retained / no registered test found | unpatched |
| `reshade` / `src/reshade/deps/gl3w` | `skaslev/gl3w@3a33275633ce4be433332dc776e6a5b3bdea6506` | `pocketforge-os/gl3w@3a33275633ce4be433332dc776e6a5b3bdea6506` | `f729e1a8ed0a63bc2b6d873c39d9bd0a9f81991a` | `UNLICENSE` / `e0e03ff770a493833206786f6e8d83c378f2e0d5f3197d5e51ae97efb4db945b` | recursive retained / no registered test found | unpatched |
| `reshade` / `src/reshade/deps/imgui` | `ocornut/imgui@c6aa051629753f0ef0d26bf775a8b6a92aa213b2` | `pocketforge-os/imgui@c6aa051629753f0ef0d26bf775a8b6a92aa213b2` | `dd6b94fc5f7e37a429a2257933e8ba97b05d772b` | `LICENSE.txt` / `0ebf8feb061536bdebd7c6a365b64808ae3479048b1c44113e19037680c5f2be` | recursive retained / no registered test found | unpatched |
| `reshade` / `src/reshade/deps/minhook` | `TsudaKageyu/minhook@8fda4f5481fed5797dc2651cd91e238e9b3928c6` | `pocketforge-os/minhook@8fda4f5481fed5797dc2651cd91e238e9b3928c6` | `159d499c5603f0b8ac9efdc693f88e0328f432a5` | `LICENSE.txt` / `d5c2224982b0b95a16b098a561335fa93f3eaf4d0aa964b7843edb986df78dc8` | recursive retained / no registered test found | unpatched |
| `reshade` / `src/reshade/deps/openxr` | `KhronosGroup/OpenXR-SDK@dc1e23937fe45eabcce80f6588cf47449edb29d1` | `pocketforge-os/OpenXR-SDK@dc1e23937fe45eabcce80f6588cf47449edb29d1` | `aeb18604291363eec1a58f643d4a371e60184c43` | `LICENSE` / `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30` | recursive retained / test and conformance options | unpatched |
| `reshade` / `src/reshade/deps/spirv` | `KhronosGroup/SPIRV-Headers@c0df742ec0b8178ad58c68cff3437ad4b6a06e26` | `pocketforge-os/SPIRV-Headers@c0df742ec0b8178ad58c68cff3437ad4b6a06e26` | `429e7b84cdff6b43792fb66c7779a5dfe9ce4f6f` | `LICENSE` / `9b243f6f0bf44e295ff411a0f7b7642d1d0dff7cdc42507e9f7206f439e51b5a` | recursive retained / no registered test found | unpatched |
| `reshade` / `src/reshade/deps/stb` | `nothings/stb@ae721c50eaf761660b4f90cc590453cdb0c2acd0` | `pocketforge-os/stb@ae721c50eaf761660b4f90cc590453cdb0c2acd0` | `2c2c7971a7c5d55e2025acc6ccf84893427fe7f8` | `LICENSE` / `bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63` | recursive retained / no registered test found | unpatched |
| `reshade` / `src/reshade/deps/utfcpp` | `nemtrif/utfcpp@6be08bbea14ffa0a5c594257fb6285a054395cd7` | `pocketforge-os/utfcpp@3fa88c70174a51f6d1d227a3d7c08b9f914e1083` | `80a66543ba90a7f7783fcae086561e8426c87c22` | `LICENSE` / `c9bff75738922193e67fa726fa225535870d2aa1059f91452c411736284ad566` | recursive retained / utfcpp suite and ftest | patched locator |
| `reshade` / `src/reshade/deps/vma` | `GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator@a6bfc237255a6bac1513f7c1ebde6d8aed6b5191` | `pocketforge-os/VulkanMemoryAllocator@a6bfc237255a6bac1513f7c1ebde6d8aed6b5191` | `9017d2bf42407d35bcd6cc328d1024d476d4eea5` | `LICENSE.txt` / `cdb520614db3ec62e667ece01e64e6afa21948fa51e85e748b1494597a7be907` | recursive retained / no registered test found | unpatched |
| `reshade` / `src/reshade/deps/vulkan` | `KhronosGroup/Vulkan-Headers@7b3466a1f47a9251ac1113efbe022ff016e2f95b` | `pocketforge-os/Vulkan-Headers@7b3466a1f47a9251ac1113efbe022ff016e2f95b` | `6036aad807caceaca92b64037c90ad8df70b25d4` | `LICENSE.md` / `ac24e5ea920e4318e4d02c4086ae51f53cfb03feed06c18df1019e7ada1ec7bc` | recursive retained / upstream compile tests | unpatched |
| `utfcpp` / `src/reshade/deps/utfcpp/extern/ftest` | `nemtrif/ftest@c4ad4af0946b73ce1a40cbc72205d15d196c7e06` | `pocketforge-os/ftest@c4ad4af0946b73ce1a40cbc72205d15d196c7e06` | `136d1a15c8b0e634167b5314345206912fde4720` | `LICENSE` / `2c6a01b25167bc49e6575185c7379b0ddff608b6b3e35d89d2d185473c7ff6bf` | recursive retained / ftest smoke | unpatched |
| `wlroots` / `subprojects/wlroots/subprojects/libdisplay-info.wrap` | `emersion/libdisplay-info@47a5590e9c4eb35d67651b8c05a55f1a48259329` | `pocketforge-os/libdisplay-info@f05a3063aa72c899c974a75a20843d20b72553ef` | `3e3d2546049a3087bba3abafc2f326a89e15ccfe` | `LICENSE` / `15b396244e58830c5614b9394f4deccfe684970cd507f299383ab57ad339eedd` | wlroots DRM fallback / compatibility and 64 tests | patched locator/tests |
| `wlroots` / `subprojects/wlroots/subprojects/libdrm.wrap` | `mesa/drm@b97cbde15c5c3abfe44d78e8f57139e50f612fec` | `pocketforge-os/drm@b97cbde15c5c3abfe44d78e8f57139e50f612fec` | `a1897ca978eeb0c5d60b85e73e83472af6f5ab41` | `LICENSES/MIT.txt` / `4af446c9157f0b0826ce5d83d1722a8b28cac398bff91e982f92951821827155` | wlroots fallback / upstream surface retained | unpatched |
| `wlroots` / `subprojects/wlroots/subprojects/libliftoff.wrap` | `emersion/libliftoff@c4226a79b7a52f59bb51789b1eda112658609125` | `pocketforge-os/libliftoff@c4226a79b7a52f59bb51789b1eda112658609125` | `85b0addbbcf22ce799e741408b8c28b82894cdb8` | `LICENSE` / `9b230152f28fc7898665f40da7c311a1bf238a68b12cc39d9c83bbfc117a7b6b` | wlroots fallback / libliftoff suite | unpatched |
| `wlroots` / `subprojects/wlroots/subprojects/libxkbcommon.wrap` | `xkbcommon/libxkbcommon@66e702d83718bb845148322d5561ef1a9ef843c4` | `pocketforge-os/libxkbcommon@66e702d83718bb845148322d5561ef1a9ef843c4` | `c3461b0577fff18ff01906ebfc56c3419745207b` | `LICENSE` / `af18bf467ff48e556394f0a7a792a38022ec33002e1b87f3d66c79af4b1f373a` | wlroots fallback / upstream surface retained | unpatched |
| `wlroots` / `subprojects/wlroots/subprojects/pixman.wrap` | `pixman/pixman@f227dcbe57e8fcfb5174ffd7eb55a69b6692d512` | `pocketforge-os/pixman@f227dcbe57e8fcfb5174ffd7eb55a69b6692d512` | `45f41bdc00412cd38483aeac6a022c51ad0a8cd8` | `COPYING` / `fac9270f0987b96ff4533fca3548c633e02083cbba4a0172a3b149b2e4019793` | wlroots fallback / upstream surface retained | unpatched |
| `wlroots` / `subprojects/wlroots/subprojects/seatd.wrap` | `kennylevinsen/seatd@427b5d956afb589c4c8a1612c75644a9254d2051` | `pocketforge-os/seatd@427b5d956afb589c4c8a1612c75644a9254d2051` | `0ddcbd20aa8e8078e2db5a0ca01025a82281bee1` | `LICENSE` / `282a494803d666616bd726e0279636b5f6a31387ae19a707459074050f2600d3` | wlroots session fallback / upstream surface retained | unpatched |
| `wlroots` / `subprojects/wlroots/subprojects/wayland-protocols.wrap` | `wayland/wayland-protocols@aa62366fb800a0689f4e9de83811ff33f6a91f44` | `pocketforge-os/wayland-protocols@aa62366fb800a0689f4e9de83811ff33f6a91f44` | `672aaff651890bfe9192db1c66bb41b586a3be48` | `COPYING` / `f1a2b233e8a9a71c40f4aa885be08a0842ac85bb8588703c1dd7e6e6502e3124` | wlroots fallback / upstream surface retained | unpatched |
| `wlroots` / `subprojects/wlroots/subprojects/wayland.wrap` | `wayland/wayland@1bd29e0709db70b6463c947f150ea20dcfce9cac` | `pocketforge-os/wayland@1bd29e0709db70b6463c947f150ea20dcfce9cac` | `ff9721ee651805a2ef8af3ef3721ba22b40e8791` | `COPYING` / `6eefcb023622a463168a5c20add95fd24a38c7482622a9254a23b99b7c153061` | wlroots fallback / upstream surface retained | unpatched |
| `libdisplay-info` / `subprojects/libdisplay-info/subprojects/v4l-utils.wrap` | `v4l-utils@1316a80455ef70889bea89491f37cd69170f3ee7` | `pocketforge-os/v4l-utils@1316a80455ef70889bea89491f37cd69170f3ee7` | `8c52844dd9a413450c88639b208f119226bdf4d2` | `COPYING` / `391e4da1c54a422a78d83be7bf84b2dfb8bacdd8ad256fa4374e128655584a8a` | upstream tests / optional `edid-decode` provider | unpatched |
| `libdisplay-info` / `subprojects/wlroots/subprojects/libdisplay-info/subprojects/v4l-utils.wrap` | `v4l-utils@1316a80455ef70889bea89491f37cd69170f3ee7` | `pocketforge-os/v4l-utils@1316a80455ef70889bea89491f37cd69170f3ee7` | `8c52844dd9a413450c88639b208f119226bdf4d2` | `COPYING` / `391e4da1c54a422a78d83be7bf84b2dfb8bacdd8ad256fa4374e128655584a8a` | wlroots DRM fallback / optional `edid-decode` provider and occurrence closure | unpatched |
| `gamescope` / `thirdparty/sol/sol.hpp` | `ThePhD/sol2@2b0d2fe8ba0074e16b499940c4f3126b9c7d3471` | `pocketforge-os/sol2@9755a852628ca0abfa8a05ec1fa475fb2609b2e1` | content `81ea0b4779f70ad4a4fc2b08da302cd2317ad066682f448a611a530c681844e6` | `LICENSE.txt` / `4e171f2251b1170c0a6c9e836a36605565eee238cd64abf50ad43a01c9412ce4` | native, aarch64, OpenVR / scripting and source snapshot test | patched deterministic snapshot |
| `gamescope` / `src/shaders/NVIDIAImageScaling` | `NVIDIAGameWorks/NVIDIAImageScaling@35e13ba316c98eeecf16f37eae70ce88019911f6` | `pocketforge-os/NVIDIAImageScaling@35e13ba316c98eeecf16f37eae70ce88019911f6` | content `fbec4e3d79c3d4072aa50bb94ff4dc277a235c3345c34a5b2e627ecc6919c82d` | `licence.txt` / `62207f1c0fae72800ea7d28ee108e7dbeb852dde20b00c70fc7b0d235e19e46d` | native, aarch64, OpenVR / shader compile and byte proof | exact snapshot |
| `gamescope` / `src/shaders/ffx` | `GPUOpen-Effects/FidelityFX-FSR@a21ffb8f6c13233ba336352bdff293894c706575` | `pocketforge-os/FidelityFX-FSR@dcd34005bc7d815e64688d2bd92a495da3d5e9ef` | content `3fae1917e5d85f7914f181721a5e59b9e4a11a3c5cd836ef50ae246ffdbba063` | `license.txt` / `db089274ce766da70f5b7d791029c3486f9f9e27c8c79c652689603d3192e802` | native, aarch64, OpenVR / shader compile and source snapshot test | patched exact-copy snapshot |

The checked-in ReShade and vkroots identities were historically
`Joshua-Ashton/reshade` and `Joshua-Ashton/vkroots`. GitHub now redirects those
repositories to `misyltoad/reshade` (network source `crosire/reshade`) and
`misyltoad/vkroots`. This is recorded as a transfer, not a new dependency.
The ReShade commit is intentionally unpatched, so its nested upstream URLs are
immutable source metadata. They are never passed to an acquisition tool; the
admission materializer resolves those edges from the manifest's PocketForge
forks instead.

## Snapshots, admission and build boundary

`.github/vendored-sources.tsv` maps every compiled snapshot file to its source
path and transform. The three versioned receipts under
`.github/source-transforms/` record the exact bases, pins and aggregate content
digests. NVIDIA Image Scaling and FSR are byte comparisons. sol2 is regenerated
twice by the source-owned test with fixed Git metadata and only its generated
timestamp normalized.

The connected admission stage may fetch only manifest-listed PocketForge
commits into the digest-keyed bare-repository cache. Validation and
materialization then run without network access. Materialization exports exact
trees instead of running recursive submodule or Meson download logic, applies
only Gamescope's committed glm/stb Meson overlays, and produces a receipt tied
to the Gamescope revision and manifest digest. Each source-tree occurrence is
traversed independently, even when multiple locations use the same project and
pin, and every emitted locator is checked for its sibling materialized source.
Because Meson globally registers
nested fallback names, materialization also derives root wrap aliases for the
direct libdisplay-info and libliftoff gitlinks. Those aliases contain the same
manifest URL and pin; they make the direct version constraints authoritative
without removing or modifying either nested fallback source.

Every Meson configuration uses a fresh build directory and
`--wrap-mode=nodownload`. A missing admitted object is an error. The retained
profiles are native product plus Gamescope/libdisplay-info/libliftoff tests,
OpenVR-enabled compile coverage, and aarch64 compile coverage. Hardware, PVR,
compiler/sysroot locks, SBOM/licence bundle generation, and two-root release
reproduction remain outside this repository-level source-closure receipt.

## Branch discipline

The `pocketforge` branch preserves the protected base and reviewed dependency
defaults. Changes are a linear rebase-style stack; source behavior is separate
from downstream locator policy, every commit carries `Assisted-by: LLM`, and no
merge or `Signed-off-by` trailer is introduced. The Gamescope topic stays off a
pull request and CI until the public-runner isolation gate is cleared.
