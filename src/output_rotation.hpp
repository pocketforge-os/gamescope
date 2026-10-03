#pragma once

#include <cstdint>
#include <optional>

namespace gamescope::output_rotation
{

// These names describe the logical-to-native pixel transform. Rotate90 is a
// clockwise quarter turn and Rotate270 is a counter-clockwise quarter turn.
enum class Transform : uint32_t
{
	Normal,
	Rotate90,
	Rotate270,
};

struct Extent
{
	uint32_t width;
	uint32_t height;

	bool operator==( const Extent & ) const = default;
};

struct Point
{
	uint32_t x;
	uint32_t y;

	bool operator==( const Point & ) const = default;
};

// Half-open integer rectangle: [x, x + width) x [y, y + height).
struct Rect
{
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;

	bool operator==( const Rect & ) const = default;
};

struct Layout
{
	Extent logicalExtent;
	Extent nativeExtent;
	uint32_t bytesPerPixel;
	uint32_t minimumRowBytes;
	uint32_t rowPitch;
	uint64_t allocationBytes;
};

Extent transformExtent( Extent logicalExtent, Transform transform );
std::optional<Layout> makeLayout( Extent logicalExtent, Transform transform,
	uint32_t bytesPerPixel, uint32_t rowPitchAlignment );

std::optional<Point> logicalToNative( Point logical, Extent logicalExtent,
	Transform transform );
std::optional<Point> nativeToLogical( Point native, Extent logicalExtent,
	Transform transform );
std::optional<Rect> logicalToNative( Rect logical, Extent logicalExtent,
	Transform transform );
std::optional<Rect> nativeToLogical( Rect native, Extent logicalExtent,
	Transform transform );

enum class CaptureSpace
{
	Logical,
	Native,
};

struct FrameContract
{
	Extent compositionExtent;
	Extent captureExtent;
	Extent rotatedExtent;
	Extent scanoutExtent;
	CaptureSpace captureSpace;
	Transform kmsTransform;
	bool rotateBeforeStaging;
};

FrameContract frameContract( Extent logicalExtent, Transform outputTransform );

enum class DirectScanoutRejection
{
	Accepted,
	NoSoftwareRotation,
	MultipleLayers,
	NotOpaqueBaseLayer,
	NonNativeExtent,
	ContentExtentMismatch,
	ClientTransformNotNormal,
	KmsTransformNotNormal,
	IncompatibleFormat,
	AmbiguousModifier,
	IncompatibleModifier,
	FramebufferNotImportable,
};

struct DirectScanoutInput
{
	Transform outputTransform;
	uint32_t layerCount;
	Extent logicalExtent;
	Extent bufferExtent;
	Extent contentExtent;
	bool baseLayer;
	bool opaque;
	bool normalClientTransform;
	bool normalKmsTransform;
	bool formatCompatible;
	bool explicitModifier;
	bool modifierCompatible;
	bool framebufferImportable;
};

struct DirectScanoutDecision
{
	bool eligible;
	DirectScanoutRejection rejection;
	Transform kmsTransform;
};

DirectScanoutDecision directScanoutDecision( const DirectScanoutInput &input );

} // namespace gamescope::output_rotation
