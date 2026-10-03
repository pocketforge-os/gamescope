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

Cursor positions/hotspots and external-overlay bounds remain in logical space.
They are composited into the logical image and transformed exactly once with the
rest of that image. A frame with a separate cursor or overlay plane cannot use
the native direct-scanout exception.

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
inactive. With software rotation active, Gamescope first evaluates a narrow
native-buffer exception. It requires all of the following:

- exactly one opaque base layer;
- buffer and content extents exactly `H x W`, with no hidden padding extent;
- normal client pre-transform metadata and normal KMS transform;
- an explicit modifier, not `DRM_FORMAT_MOD_INVALID`;
- exact primary-plane format/modifier compatibility and successful framebuffer
  import.

An accepted client buffer is already physical scanout content. Gamescope presents
it at the full native extent with KMS rotation 0 and does no Vulkan work. Missing
metadata, a logical `W x H` buffer, a transformed client, multiple layers,
format/modifier ambiguity, or import failure forces full composition. There is
no attempt to recover by programming a KMS rotation property.

## Capture contract

Screenshots and PipeWire are logical capture points. They remain landscape
`W x H` and observe the completed logical composition before final-output
rotation. They never read the native optimal image or the LINEAR scanout image,
and they do not depend on KMS ownership. The native images are presentation
artifacts only. Any future native-panel diagnostic capture must be a separately
named mode rather than silently changing the screenshot or PipeWire contract.

## Test contract

Dependency-free tests use an asymmetric labeled image so transpose, direction,
mirror, and off-by-one errors differ visibly. They cover both transforms and
their inverses, extent and aligned-stride calculations, damage rectangles,
cursor points, external-overlay rectangles, logical capture versus native
scanout, KMS-normal presentation, the positive native direct-scanout case, and
negative controls for every fail-closed eligibility input.

A software-Vulkan test separately runs the production compute shader at the
target dimensions. Every pixel in its asymmetric `1280x720` source is unique;
the test validates the complete `720x1280` LINEAR staging image for both quarter
turns while honoring the implementation-reported row pitch.
