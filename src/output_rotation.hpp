#pragma once

#include "gamescope_shared.h"

#include <array>
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
Transform transformFromPanelOrientation( GamescopePanelOrientation orientation );
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
	UnsupportedLayerCount,
	UnclassifiedLayer,
	NotOpaqueBaseLayer,
	InvalidLogicalRectangle,
	InvalidLayerOrder,
	NonNativeExtent,
	ContentExtentMismatch,
	ClientTransformNotProven,
	AmbiguousClientTransform,
	KmsTransformNotNormal,
	IncompatibleFormat,
	AmbiguousModifier,
	IncompatibleModifier,
	FramebufferNotImportable,
	IncompatibleAlpha,
	IncompatibleBlend,
	IncompatibleZpos,
	AmbiguousBufferIdentity,
};

enum class ClientClass
{
	Unknown,
	Wayland,
	Xwayland,
};

// A geometric transform declared by the client for the committed buffer. The
// value names the physical pre-rotation of logical content; flipped and 180
// degree declarations are normalized to Unsupported by the protocol adapters.
enum class ClientTransform
{
	Unknown,
	Normal,
	Rotate90,
	Rotate270,
	Unsupported,
};

struct ClientTransformMetadata
{
	ClientClass clientClass = ClientClass::Unknown;
	ClientTransform waylandBufferTransform = ClientTransform::Unknown;
	ClientTransform vulkanPreTransform = ClientTransform::Unknown;
};

enum class LayerRole
{
	Unknown,
	Base,
	SystemOverlay,
	Cursor,
};

enum class PlaneBlendMode
{
	Opaque,
	Premultiplied,
	Coverage,
	Unsupported,
};

struct DirectScanoutLayerInput
{
	uint64_t bufferIdentity = 0;
	LayerRole role = LayerRole::Unknown;
	Extent bufferExtent = {};
	Extent contentExtent = {};
	Rect logicalRect = {};
	ClientTransformMetadata client = {};
	int32_t zpos = 0;
	float opacity = 0.0f;
	PlaneBlendMode blendMode = PlaneBlendMode::Unsupported;
	bool formatCompatible = false;
	bool explicitModifier = false;
	bool modifierCompatible = false;
	bool framebufferImportable = false;
	bool alphaCompatible = false;
	bool blendCompatible = false;
	bool zposCompatible = false;
};

struct NativePlaneRecord
{
	uint64_t bufferIdentity = 0;
	Rect sourceRect = {};
	Rect destinationRect = {};
	int32_t zpos = 0;
	float opacity = 0.0f;
	PlaneBlendMode blendMode = PlaneBlendMode::Unsupported;
	Transform kmsTransform = Transform::Normal;

	bool operator==( const NativePlaneRecord & ) const = default;
};

constexpr uint32_t MaxNativeDirectLayers = 2;

struct DirectScanoutInput
{
	Transform outputTransform = Transform::Normal;
	Extent logicalExtent = {};
	uint32_t layerCount = 0;
	bool normalKmsTransform = false;
	std::array<DirectScanoutLayerInput, MaxNativeDirectLayers> layers = {};
};

struct DirectScanoutDecision
{
	bool eligible = false;
	DirectScanoutRejection rejection = DirectScanoutRejection::NoSoftwareRotation;
	Transform kmsTransform = Transform::Normal;
	uint32_t recordCount = 0;
	std::array<NativePlaneRecord, MaxNativeDirectLayers> records = {};
};

DirectScanoutDecision directScanoutDecision( const DirectScanoutInput &input );

} // namespace gamescope::output_rotation
