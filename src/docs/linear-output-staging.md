# Linear output staging

## Scope and constraints

This document defines a Gamescope-only fallback for composited DRM output when
no single exportable image modifier supports both Vulkan compute composition and
KMS scanout. It does not change Mesa or the kernel, does not add or infer GPU
capabilities, and makes no device-specific performance or support claim.

The existing direct-client-scanout path is outside this fallback. A frame which
passes `drm_prepare` without composition continues to commit the client's
buffers directly and creates neither a composition dispatch nor a staging copy.

## Output modes

Output allocation selects one mode per output format:

1. **Combined** keeps the existing path. Each ring image is exportable and
   flippable, and its selected DRM modifier supports `SAMPLED_IMAGE`,
   `STORAGE_IMAGE`, and `TRANSFER_SRC`. The compute compositor writes that image
   directly.
2. **Staged** owns two same-sized, same-format rings. The composition ring uses
   optimal tiling and `SAMPLED | STORAGE | TRANSFER_SRC`. The scanout ring uses
   the explicit `DRM_FORMAT_MOD_LINEAR` modifier and
   `TRANSFER_DST | EXPORTABLE | FLIPPABLE | OUTPUT_IMAGE`. After composition,
   one full-image `vkCmdCopyImage` copies the optimal image to the matching
   linear scanout image.

Staged mode is eligible only when all of these facts are independently true:

- the DRM output format maps to one Vulkan format for both images;
- optimal tiling advertises every feature required by the composition image;
- `DRM_FORMAT_MOD_LINEAR` is present in the selected KMS plane's modifier list;
- Vulkan advertises `TRANSFER_DST` for the same format and linear DRM modifier;
- an external-memory `VkImage` with that exact format, modifier, usage, and
  dma-buf handle type passes `vkGetPhysicalDeviceImageFormatProperties2`;
- creation and allocation succeed at the output extent;
- allocation, dma-buf export, and backend framebuffer import all succeed.

The intersection is exact: a Vulkan modifier supported for another usage or a
KMS modifier belonging only to an unrelated format/plane is not evidence for
this mode. `DRM_FORMAT_MOD_INVALID` is not treated as linear. Primary and
partial-overlay output allocations use the modifier set of the plane class on
which that output will be presented.

## Resources and lifetime

`VulkanOutput_t` remains the owner of three output slots. In staged mode each
slot contains:

- an optimal composition texture, visible only to Vulkan; and
- a linear scanout texture, with the exported dma-buf and imported backend
  framebuffer owned by `CVulkanTexture`.

The two images in a slot have identical width, height, DRM format, Vulkan
format, layer count, and sample count. They never alias memory. Partial-overlay
slots follow the same rule; the existing cross-image memory reuse optimization
is used only by combined mode.

Recording a command retains references to every source and destination through
`CVulkanCmdBuffer`. Submission retains the command buffer until its sequence is
complete. The DRM presentation path waits for that sequence before calling
`drm_prepare`, so KMS never receives a framebuffer before its staging copy has
completed. The existing framebuffer reference held by the pending/current DRM
commit keeps the exported allocation alive after the Vulkan submission ends.

Output recreation first idles the Vulkan device, then drops both rings and their
backend framebuffer imports together. Ring advancement remains one step after a
successful submission, so composition and scanout indices cannot diverge.

## Layouts, barriers, ownership, and fences

The staged frame is recorded in one command buffer and submitted once:

1. The optimal image transitions to `GENERAL` for compute shader writes.
2. Before the copy, a barrier makes shader writes available to transfer reads;
   the optimal image remains Vulkan-queue owned and uses `GENERAL`.
3. The linear image is acquired from the external/foreign queue family when it
   was previously scanned out (or discarded on first use), transitions to
   `GENERAL`, and becomes a transfer-write destination.
4. `vkCmdCopyImage` copies the full color subresource from optimal to linear.
5. The existing submit-time flush makes transfer writes available, applies the
   backend present layout, and releases only the linear image to
   `VK_QUEUE_FAMILY_FOREIGN_EXT` (or `EXTERNAL` where modifiers are unavailable).

The optimal image is never externally owned. Command submission's Vulkan fence
orders compute, barrier, and copy. The current `vulkan_wait(sequence)` before
DRM preparation is the Vulkan-to-KMS completion fence for this path; this
change does not invent an implicit dma-buf synchronization assumption or alter
the atomic KMS commit fencing model.

## Frame behavior

`vulkan_composite` chooses the optimal image as its compute target only for a
normal DRM output in staged mode. An explicit `pOutputOverride`, nested Vulkan
swapchain output, and the screenshot-only compositor calls keep their existing
targets and do not stage.

PipeWire capture consumes the completed optimal composition image before the
output staging copy is recorded. Same-format capture still uses `copyImage`;
scaled or converted capture still samples the optimal image. Screenshot calls
continue to render into their dedicated screenshot texture. Thus capture is
neither read back from linear memory nor made dependent on KMS ownership.

`vulkan_get_last_output_image` returns the linear member of the last submitted
slot in staged mode, because that is the image passed to DRM. It returns the
existing combined image otherwise. Deferred partial composition uses the same
slot arithmetic for both rings.

## Reuse and observability

The existing three-slot ring and sequence wait define reuse. No image is
reallocated per frame. Output recreation resets the ring index only after the
device is idle.

Two monotonic process-lifetime counters provide evidence without changing frame
selection:

- `composition_dispatches` increments once for each final output composition
  dispatch recorded by `vulkan_composite`;
- `staging_copies` increments once for each optimal-to-linear output copy.

Intermediate FSR/NIS/blur dispatches and PipeWire conversion dispatches do not
increment `composition_dispatches`. Direct scanout increments neither counter.
A composited staged frame therefore advances both counters by one; a composited
combined frame advances only `composition_dispatches`.

## Errors and fallback policy

Capability probing is deterministic and side-effect free. Allocation attempts
combined mode first, then staged mode only when its complete eligibility check
passes. A rejected modifier, unsupported usage, export failure, framebuffer
import failure, format mismatch, or partial ring allocation failure tears down
the incomplete candidate and reports one precise error.

There is no per-frame fallback from a failed copy to stale output, CPU copy, or
an unverified modifier. If neither combined nor staged allocation can construct
the whole required ring, output creation fails through its existing caller.
Assertions are not used for expected capability rejection. A failed submission
does not advance the ring or either successful-operation counter.

## Test contract

Deterministic unit tests exercise a Vulkan-independent planner and state model:

- exact format and modifier intersection, including wrong-plane, wrong-format,
  missing-linear, and missing-usage rejection;
- combined preference and staged eligibility;
- resource-pair/ring invariants and no index advance after failure;
- barrier/ownership order and first-use discard versus reuse acquisition;
- counter behavior for direct, combined, staged, and failed frames;
- explicit-output, screenshot, and PipeWire source selection.

An optional software-Vulkan integration test may validate same-format
optimal-to-linear transfer and pixels under lavapipe when the runner exposes the
required Vulkan capabilities. It must report a skip when external dma-buf or DRM
modifier support is absent; such a skip is not evidence of hardware scanout or
of any particular GPU's capabilities.
