# Portrait output rotation

## Scope

This design extends the linear-output staging path for a KMS mode whose current
logical orientation may be 0, 90, 180, or 270 degrees. The compositor owns the
transform. Mesa, the kernel, input policy, and the panel mode remain unchanged,
and this document makes no device-performance or device-correctness claim.

For a quarter turn, the logical scene is `W x H` (`1280x720` in the target
configuration) and the KMS mode and final scanout allocations are native `H x W`
(`720x1280`). For identity and 180 degrees, dimensions do not swap. The KMS plane
transform is always normal. A non-normal KMS transform is not a fallback.

## Runtime orientation authority

Gamescope consumes `OrientationState v1` from pocketforge-os/runtime commit
`5da5c97ccd94e44293d25b4888f8c554b49721a0`; it does not reproduce posture,
accelerometer, lock, or preference policy. The direction contract is defined by
`crates/pf-session-authority/src/orientation_source.rs:416-424`: `cw90`, `90`,
and `right side up` are `QuarterTurn::Right`, while `cw270`, `270`, and
`left side up` are `QuarterTurn::Left`. The verbatim cross-source fixture at
`crates/pf-session-authority/tests/orientation/source.rs:35-69,121-126` requires
descriptor `rotation="cw90"`, posture `screen_rotation={screen_main="cw90"}`,
DRM `right side up`, and sensor `right-up` all to produce `Right`.
`crates/pf-session-authority/src/orientation.rs:28-54,367-372,400-407` composes
and publishes that value unchanged.

The resulting mapping is therefore a contract fact:

| RPC `output` | Gamescope logical-to-native transform | Wayland output transform |
|---|---|---|
| `normal` | 0 | `WL_OUTPUT_TRANSFORM_NORMAL` |
| `right` | clockwise 90 | `WL_OUTPUT_TRANSFORM_90` |
| `upsidedown` | 180 | `WL_OUTPUT_TRANSFORM_180` |
| `left` | clockwise 270 | `WL_OUTPUT_TRANSFORM_270` |

The client connects to `/run/pocketforge/session-authority.sock`. Messages are
JSON in the runtime `pf-wire` framing: a four-byte big-endian length, capped at
64 KiB, followed by one body. It first sends
`{"method":"orientation","version":1,"wait_ms":0}`. After a complete state it
reconnects for each long poll with the state's serial in `after_serial` and
`wait_ms:30000`. Unknown versions, result tags or enum values, missing fields,
malformed/trailing JSON, oversized frames, and EOF with an incomplete prefix or
body are failures rather than an orientation observation.

After a failure Gamescope drops the watch cursor, immediately returns to the
connector's fixed-mount transform, then retries a full snapshot with
interruptible exponential backoff from 100 ms to 5 s and bounded jitter. A
complete response resets the backoff. `--force-orientation` suppresses the
subscriber and remains the lifetime diagnostic override.

An accepted change is delivered through a mailbox to the Wayland thread. In
one generation it updates the logical output extent, DRM connector orientation,
renderer/direct-scanout generation, every live surface's v6
`preferred_buffer_transform`, XDG/layer configure sizes, and each Xwayland
output's native mode plus transform. Xwayland consequently reports the new
logical root dimensions and RandR rotation and emits its normal screen-change
notification. Old committed buffers remain compositable, but stale-generation
plane candidates cannot scan out. Active touch contacts are cancelled and
subsequent native touch coordinates use the same inverse transform as pixels;
pointer constraints are recomputed only when an active contact was cancelled.

`presentation=fill` has a deliberate 180-degree compatibility case. When the
logical orientation is `landscape_flipped` but an app permits only `landscape`,
Gamescope applies an app-to-output 180 and then the output 180. The transforms
cancel at the native panel, dimensions stay unchanged, and no bars are added.
If the app permits `landscape_flipped`, the app transform is identity and the
output 180 remains. Gamescope is the only component that rotates either case.

## Algebra and coordinate spaces

Coordinates name pixels from a top-left origin. `90` is a clockwise logical to
native quarter turn and `270` is counter-clockwise. For a logical extent
`W x H` and logical pixel `(x,y)`:

| Transform | Logical to native `(u,v)` | Native to logical `(x,y)` |
|---|---|---|
| 0 | `(x, y)` | `(u, v)` |
| 90 | `(H - 1 - y, x)` | `(v, H - 1 - u)` |
| 180 | `(W - 1 - x, H - 1 - y)` | `(W - 1 - u, H - 1 - v)` |
| 270 | `(y, W - 1 - x)` | `(W - 1 - v, u)` |

Damage and layer bounds are half-open rectangles. For
`R = [x0,x1) x [y0,y1)`:

| Transform | Native rectangle |
|---|---|
| 0 | `[x0,x1) x [y0,y1)` |
| 90 | `[H-y1,H-y0) x [x0,x1)` |
| 180 | `[W-x1,W-x0) x [H-y1,H-y0)` |
| 270 | `[y0,y1) x [W-x1,W-x0)` |

The formulas preserve area and make every transform invertible. Bounds checks happen
before unsigned subtraction, so invalid points or rectangles fail closed.

Cursor positions/hotspots and external-overlay bounds originate in logical
space. A composited frame transforms them exactly once with the rest of the
logical image. A separate cursor layer cannot use the native direct exception.
The bounded system-overlay exception described below instead transforms the
overlay's half-open logical rectangle to a native plane destination and requires
the overlay buffer's own pixels to be independently pre-rotated.

## Images, dimensions, and strides

The accepted staging path gains a third image role per ring slot:

1. The logical optimal composition image is `W x H`, sampled, storage-capable,
   and transfer-source capable.
2. The native optimal rotation image is `H x W`, sampled, storage-capable, and
   transfer-source capable. A final compute dispatch writes it by applying the
   inverse mapping above to each native destination coordinate.
3. The native LINEAR scanout image is `H x W`, transfer-destination, exportable,
   flippable, and imported as a backend framebuffer.

The native rotation and scanout images must have identical format and extent;
they do not alias. The staging copy remains a same-format, same-extent full-image
copy. For a packed format with `B` bytes per pixel, a native row needs at least
`H * B` bytes. An exported dma-buf stride may be larger for alignment; consumers
must use its per-plane stride and never derive row addresses from the minimum.

The frame is ordered as logical compute writes, a shader-write to shader-read
barrier, native rotation writes, a shader-write to transfer-read barrier, then
the accepted native optimal-to-linear copy and foreign-queue release. All three
images are retained by the command buffer. Ring advancement still occurs only
after a successful submission.

Gamescope currently dispatches and copies the complete output. A rotated
composited frame therefore conservatively damages all `H x W` native pixels.
The sub-rectangle algebra above is the required contract if incremental output
damage is later introduced.

## Planes-first and direct scanout

Existing direct client scanout is unchanged when software output rotation is
inactive. With software rotation active, Gamescope evaluates a narrow native
plane exception for either one opaque base or that same base plus one classified
system overlay. Every admitted layer must have:

- a stable nonzero buffer identity, exact content extent, and an integer,
  in-bounds logical rectangle;
- a physical buffer extent equal to the transformed logical rectangle extent
  (`H x W` for the fullscreen base);
- commit-snapshotted client metadata which proves that buffer is already
  pre-rotated in the requested direction;
- an explicit modifier, not `DRM_FORMAT_MOD_INVALID`, exact plane-class
  format/modifier support, and successful framebuffer import; and
- a normal KMS transform.

The base must remain opaque at zpos zero and cover the complete logical output.
The optional overlay must be compositor-classified, have a distinct buffer
identity and higher zpos, and use a supported nonzero opacity and premultiplied
or coverage blend mode. The layer-shell global is exposed only to the peer
whose Wayland credentials have the immediate parent PID of the compositor-
registered `mangoapp` reaper. Process names are not used for admission, so a
process that spoofs or later mutates its name cannot receive native-plane
trust. Xwayland's legacy
`GAMESCOPE_EXTERNAL_OVERLAY` property remains a composition classification
only; it cannot grant native-plane trust. A candidate DRM overlay plane must
support the exact format/modifier and expose alpha, pixel-blend-mode, and zpos
properties. The subsequent liftoff atomic preparation remains the final exact
assignment check; failure forces composition. The native-frame copy changes
only plane placement, scale, black-border state, and the native-output marker,
so adding or removing the overlay retains the base texture/framebuffer
identity and an identical base-plane record. Untrusted application clients
therefore cannot impersonate or reorder the system overlay.

The overlay destination is obtained from the same rectangle algebra as damage.
For logical `[900,1100) x [500,600)`, 90 produces native
`[120,220) x [900,1100)` and 270 produces
`[500,600) x [180,380)`. Its source is the complete independently rotated
`100x200` buffer. Both plane records request KMS rotation 0.

Cursor, notification, override, blank compatibility, third-party, or third
layers are unclassified and force composition. Cursor handling is deliberately
unchanged: because Gamescope's DRM hardware-cursor path is disabled here, a
drawn cursor is already a full-composition condition and cannot silently become
the admitted overlay.

### Committed transform evidence

At the accepted upstream source commit `5fb8dce4`, the Gamescope WSI layer sends
`VkSwapchainCreateInfoKHR::preTransform` in
`layer/VkLayer_FROG_gamescope_wsi.cpp:1302-1310`; the private protocol describes
that field only as the swapchain's `VkSurfaceTransformFlagBitsKHR` at
`protocol/gamescope-swapchain.xml:86-92`. The server stores it at
`src/wlserver.cpp:935-956`, snapshots the surface feedback into the commit queue
at `src/wlserver.cpp:134-168`, and copies it into `commit_t` at
`src/steamcompmgr.cpp:1383-1407`. The base-only accessor at
`src/steamcompmgr.cpp:2069-2077` therefore cannot prove a Wayland buffer
transform or describe an overlay.

The upstream commit queue did not retain `wl_surface.set_buffer_transform`.
This path now snapshots `wlr_surface.current.transform` alongside the buffer and
the WSI feedback, copies both into `commit_t`, and carries them on each
`FrameInfo_t::Layer_t`. The sources are interpreted separately:

- a native Wayland layer requires a matching committed Wayland buffer transform
  (90 for a 90 output, 270 for a 270 output) and permits only absent or identity
  Vulkan feedback; a second non-normal declaration is ambiguous/double and is
  rejected;
- an Xwayland layer requires a normal Wayland surface transform plus a matching
  non-identity WSI `preTransform` snapshotted with that commit; and
- identity-only, opposite-direction, flipped/180, unknown-client, absent, or
  conflicting metadata is not physical-orientation proof.

An accepted buffer is already physical scanout content. Gamescope presents it
at its native destination with KMS rotation 0 and does no Vulkan work. There is
no attempt to recover by programming a KMS rotation property.

## Capture contract

Screenshots and PipeWire are logical capture points. They remain landscape
`W x H` and observe the completed logical composition before final-output
rotation. They never read the native optimal image or the LINEAR scanout image,
and they do not depend on KMS ownership. The native images are presentation
artifacts only. Any future native-panel diagnostic capture must be a separately
named mode rather than silently changing the screenshot or PipeWire contract.

When a committed layer is itself physically pre-rotated, the normal compositor
sampling path applies that layer's inverse buffer mapping while producing the
logical image. Screenshot and PipeWire repaints therefore see logical pixels,
not a scaled native buffer, and a fallback from native planes to composition
does not double-rotate the client or overlay.

## Test contract

Hermetic tests use asymmetric labeled base and overlay images so
transpose, direction, mirror, and off-by-one errors differ visibly. They cover
all four transforms and their inverses, extent and aligned-stride calculations,
damage rectangles, cursor points, external-overlay rectangles, logical capture
versus native scanout, KMS-normal policy, base-only and two-plane plane records,
base-record identity across overlay show/hide, the distinct Wayland and
Vulkan/Xwayland metadata paths, and negative controls for every fail-closed
eligibility input.

A software-Vulkan test separately runs the production compute shader at the
target dimensions. Every pixel in its asymmetric source is unique; the test
validates the complete LINEAR staging image for 0/90/180/270 while honoring the
implementation-reported row pitch.

The runtime tests copy the runtime-owned direction fixture literally, exercise
strict framed parsing and real private Unix sockets, and cover an absent
authority, reconnect/backoff reset, a locked snapshot, live-client transition,
duplicate-state no-op, both 180-degree fill cases, and interruptible shutdown.
The Xwayland test starts the built compositor with the CI-pinned Xwayland,
creates and maps a real X client, then checks root dimensions, RandR rotation,
and screen-change notifications across normal/right/upsidedown/left. It also
proves that an equivalent state with a new serial emits no RandR change.
