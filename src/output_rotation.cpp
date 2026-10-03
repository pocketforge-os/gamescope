#include "output_rotation.hpp"

#include <limits>

namespace gamescope::output_rotation
{

namespace
{

bool validExtent( Extent extent )
{
	return extent.width != 0 && extent.height != 0;
}

bool contains( Extent extent, Point point )
{
	return point.x < extent.width && point.y < extent.height;
}

bool contains( Extent extent, Rect rect )
{
	return uint64_t{ rect.x } + rect.width <= extent.width &&
		uint64_t{ rect.y } + rect.height <= extent.height;
}

} // namespace

Extent transformExtent( Extent logicalExtent, Transform transform )
{
	if ( transform == Transform::Rotate90 || transform == Transform::Rotate270 )
		return { logicalExtent.height, logicalExtent.width };
	return logicalExtent;
}

std::optional<Layout> makeLayout( Extent logicalExtent, Transform transform,
	uint32_t bytesPerPixel, uint32_t rowPitchAlignment )
{
	if ( !validExtent( logicalExtent ) || bytesPerPixel == 0 || rowPitchAlignment == 0 )
		return std::nullopt;

	const Extent nativeExtent = transformExtent( logicalExtent, transform );
	const uint64_t minimumRowBytes = uint64_t{ nativeExtent.width } * bytesPerPixel;
	const uint64_t remainder = minimumRowBytes % rowPitchAlignment;
	const uint64_t padding = remainder == 0 ? 0 : rowPitchAlignment - remainder;
	const uint64_t alignedRowBytes = minimumRowBytes + padding;
	if ( minimumRowBytes > std::numeric_limits<uint32_t>::max() ||
		alignedRowBytes < minimumRowBytes ||
		alignedRowBytes > std::numeric_limits<uint32_t>::max() )
		return std::nullopt;

	const uint64_t allocationBytes = alignedRowBytes * nativeExtent.height;
	if ( nativeExtent.height != 0 && allocationBytes / nativeExtent.height != alignedRowBytes )
		return std::nullopt;

	return Layout{
		.logicalExtent = logicalExtent,
		.nativeExtent = nativeExtent,
		.bytesPerPixel = bytesPerPixel,
		.minimumRowBytes = uint32_t( minimumRowBytes ),
		.rowPitch = uint32_t( alignedRowBytes ),
		.allocationBytes = allocationBytes,
	};
}

std::optional<Point> logicalToNative( Point logical, Extent logicalExtent,
	Transform transform )
{
	if ( !validExtent( logicalExtent ) || !contains( logicalExtent, logical ) )
		return std::nullopt;

	switch ( transform )
	{
		case Transform::Normal:
			return logical;
		case Transform::Rotate90:
			return Point{ logicalExtent.height - 1 - logical.y, logical.x };
		case Transform::Rotate270:
			return Point{ logical.y, logicalExtent.width - 1 - logical.x };
	}
	return std::nullopt;
}

std::optional<Point> nativeToLogical( Point native, Extent logicalExtent,
	Transform transform )
{
	if ( !validExtent( logicalExtent ) ||
		!contains( transformExtent( logicalExtent, transform ), native ) )
		return std::nullopt;

	switch ( transform )
	{
		case Transform::Normal:
			return native;
		case Transform::Rotate90:
			return Point{ native.y, logicalExtent.height - 1 - native.x };
		case Transform::Rotate270:
			return Point{ logicalExtent.width - 1 - native.y, native.x };
	}
	return std::nullopt;
}

std::optional<Rect> logicalToNative( Rect logical, Extent logicalExtent,
	Transform transform )
{
	if ( !validExtent( logicalExtent ) || !contains( logicalExtent, logical ) )
		return std::nullopt;

	switch ( transform )
	{
		case Transform::Normal:
			return logical;
		case Transform::Rotate90:
			return Rect{
				logicalExtent.height - logical.y - logical.height,
				logical.x,
				logical.height,
				logical.width,
			};
		case Transform::Rotate270:
			return Rect{
				logical.y,
				logicalExtent.width - logical.x - logical.width,
				logical.height,
				logical.width,
			};
	}
	return std::nullopt;
}

std::optional<Rect> nativeToLogical( Rect native, Extent logicalExtent,
	Transform transform )
{
	if ( !validExtent( logicalExtent ) ||
		!contains( transformExtent( logicalExtent, transform ), native ) )
		return std::nullopt;

	switch ( transform )
	{
		case Transform::Normal:
			return native;
		case Transform::Rotate90:
			return Rect{
				native.y,
				logicalExtent.height - native.x - native.width,
				native.height,
				native.width,
			};
		case Transform::Rotate270:
			return Rect{
				logicalExtent.width - native.y - native.height,
				native.x,
				native.height,
				native.width,
			};
	}
	return std::nullopt;
}

FrameContract frameContract( Extent logicalExtent, Transform outputTransform )
{
	const Extent nativeExtent = transformExtent( logicalExtent, outputTransform );
	return {
		.compositionExtent = logicalExtent,
		.captureExtent = logicalExtent,
		.rotatedExtent = nativeExtent,
		.scanoutExtent = nativeExtent,
		.captureSpace = CaptureSpace::Logical,
		.kmsTransform = Transform::Normal,
		.rotateBeforeStaging = outputTransform != Transform::Normal,
	};
}

DirectScanoutDecision directScanoutDecision( const DirectScanoutInput &input )
{
	auto reject = []( DirectScanoutRejection rejection ) {
		return DirectScanoutDecision{ false, rejection, Transform::Normal };
	};

	if ( input.outputTransform != Transform::Rotate90 &&
		input.outputTransform != Transform::Rotate270 )
		return reject( DirectScanoutRejection::NoSoftwareRotation );
	if ( input.layerCount != 1 )
		return reject( DirectScanoutRejection::MultipleLayers );
	if ( !input.baseLayer || !input.opaque )
		return reject( DirectScanoutRejection::NotOpaqueBaseLayer );

	const Extent nativeExtent = transformExtent( input.logicalExtent, input.outputTransform );
	if ( input.bufferExtent != nativeExtent )
		return reject( DirectScanoutRejection::NonNativeExtent );
	if ( input.contentExtent != input.bufferExtent )
		return reject( DirectScanoutRejection::ContentExtentMismatch );
	if ( !input.normalClientTransform )
		return reject( DirectScanoutRejection::ClientTransformNotNormal );
	if ( !input.normalKmsTransform )
		return reject( DirectScanoutRejection::KmsTransformNotNormal );
	if ( !input.formatCompatible )
		return reject( DirectScanoutRejection::IncompatibleFormat );
	if ( !input.explicitModifier )
		return reject( DirectScanoutRejection::AmbiguousModifier );
	if ( !input.modifierCompatible )
		return reject( DirectScanoutRejection::IncompatibleModifier );
	if ( !input.framebufferImportable )
		return reject( DirectScanoutRejection::FramebufferNotImportable );

	return { true, DirectScanoutRejection::Accepted, Transform::Normal };
}

} // namespace gamescope::output_rotation
