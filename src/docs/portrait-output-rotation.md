# Portrait output rotation

## Scope

This design extends the linear-output staging path for a portrait KMS mode when
Gamescope exposes a landscape logical session and the KMS plane has no rotation
property. The compositor owns the transform. Mesa, the kernel, input policy, and
the panel mode remain unchanged, and this document makes no device-performance
or device-correctness claim.

The logical scene is `W x H` (`1280x720` in the target configuration). The KMS
mode and every final scanout allocation are native `H x W` (`720x1280`). The KMS
plane transform is always normal. A non-normal KMS transform is not a fallback.

## Algebra and coordinate spaces

Coordinates name pixels from a top-left origin. `90` is a clockwise logical to
native quarter turn and `270` is counter-clockwise. For logical pixel `(x,y)`:

| Transform | Logical to native `(u,v)` | Native to logical `(x,y)` |
|---|---|---|
| 90 | `(H - 1 - y, x)` | `(v, H - 1 - u)` |
| 270 | `(y, W - 1 - x)` | `(W - 1 - v, u)` |

Damage and layer bounds are half-open rectangles. For
`R = [x0,x1) x [y0,y1)`:

| Transform | Native rectangle |
|---|---|
| 90 | `[H-y1,H-y0) x [x0,x1)` |
| 270 | `[y0,y1) x [W-x1,W-x0)` |

The formulas preserve area and make 90/270 true inverses. Bounds checks happen
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

Dependency-free tests use asymmetric labeled base and overlay images so
transpose, direction, mirror, and off-by-one errors differ visibly. They cover
both transforms and their inverses, extent and aligned-stride calculations,
damage rectangles, cursor points, external-overlay rectangles, logical capture
versus native scanout, KMS-normal policy, base-only and two-plane plane records,
base-record identity across overlay show/hide, the distinct Wayland and
Vulkan/Xwayland metadata paths, and negative controls for every fail-closed
eligibility input.

A software-Vulkan test separately runs the production compute shader at the
target dimensions. Every pixel in its asymmetric `1280x720` source is unique;
the test validates the complete `720x1280` LINEAR staging image for both quarter
turns while honoring the implementation-reported row pitch.
