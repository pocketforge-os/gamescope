#include "output_rotation.hpp"

#include <cmath>
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

ClientTransform expectedClientTransform( Transform transform )
{
	switch ( transform )
	{
		case Transform::Rotate90:
			return ClientTransform::Rotate90;
		case Transform::Rotate270:
			return ClientTransform::Rotate270;
		default:
			return ClientTransform::Normal;
	}
}

enum class ClientTransformProof
{
	Proven,
	NotProven,
	Ambiguous,
};

ClientTransformProof proveClientTransform( const ClientTransformMetadata &metadata,
	Transform outputTransform )
{
	const ClientTransform expected = expectedClientTransform( outputTransform );
	switch ( metadata.clientClass )
	{
		case ClientClass::Wayland:
			if ( metadata.waylandBufferTransform != expected )
				return ClientTransformProof::NotProven;

			// A native Wayland buffer transform is the proof. A second non-normal
			// Vulkan declaration would make it impossible to rule out a double
			// transform, even when both values name the same quarter turn.
			if ( metadata.vulkanPreTransform != ClientTransform::Unknown &&
				metadata.vulkanPreTransform != ClientTransform::Normal )
				return ClientTransformProof::Ambiguous;
			return ClientTransformProof::Proven;

		case ClientClass::Xwayland:
			// Xwayland owns the Wayland surface and leaves that path normal. The
			// commit-snapshotted Gamescope WSI preTransform is the only accepted
			// proof for this client class.
			if ( metadata.waylandBufferTransform != ClientTransform::Normal )
				return ClientTransformProof::Ambiguous;
			return metadata.vulkanPreTransform == expected
				? ClientTransformProof::Proven
				: ClientTransformProof::NotProven;

		case ClientClass::Unknown:
		default:
			return ClientTransformProof::NotProven;
	}
}

} // namespace

Extent transformExtent( Extent logicalExtent, Transform transform )
{
	if ( transform == Transform::Rotate90 || transform == Transform::Rotate270 )
		return { logicalExtent.height, logicalExtent.width };
	return logicalExtent;
}

Transform transformFromPanelOrientation( GamescopePanelOrientation orientation )
{
	switch ( orientation )
	{
		case GAMESCOPE_PANEL_ORIENTATION_90:
			return Transform::Rotate90;
		case GAMESCOPE_PANEL_ORIENTATION_270:
			return Transform::Rotate270;
		default:
			return Transform::Normal;
	}
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
		DirectScanoutDecision decision;
		decision.rejection = rejection;
		return decision;
	};

	if ( input.outputTransform != Transform::Rotate90 &&
		input.outputTransform != Transform::Rotate270 )
		return reject( DirectScanoutRejection::NoSoftwareRotation );
	if ( input.layerCount == 0 || input.layerCount > MaxNativeDirectLayers )
		return reject( DirectScanoutRejection::UnsupportedLayerCount );
	if ( !input.normalKmsTransform )
		return reject( DirectScanoutRejection::KmsTransformNotNormal );

	DirectScanoutDecision decision;
	decision.eligible = true;
	decision.rejection = DirectScanoutRejection::Accepted;
	decision.kmsTransform = Transform::Normal;
	decision.recordCount = input.layerCount;

	for ( uint32_t i = 0; i < input.layerCount; i++ )
	{
		const DirectScanoutLayerInput &layer = input.layers[i];
		const bool isBase = i == 0;
		if ( ( isBase && layer.role != LayerRole::Base ) ||
			( !isBase && layer.role != LayerRole::SystemOverlay ) )
			return reject( DirectScanoutRejection::UnclassifiedLayer );

		if ( layer.bufferIdentity == 0 )
			return reject( DirectScanoutRejection::AmbiguousBufferIdentity );
		if ( isBase && layer.opacity != 1.0f )
			return reject( DirectScanoutRejection::NotOpaqueBaseLayer );
		if ( !validExtent( input.logicalExtent ) || layer.logicalRect.width == 0 ||
			layer.logicalRect.height == 0 ||
			!contains( input.logicalExtent, layer.logicalRect ) )
			return reject( DirectScanoutRejection::InvalidLogicalRectangle );
		if ( isBase && layer.logicalRect != Rect{ 0, 0,
			input.logicalExtent.width, input.logicalExtent.height } )
			return reject( DirectScanoutRejection::NotOpaqueBaseLayer );

		const Extent expectedBufferExtent = transformExtent(
			{ layer.logicalRect.width, layer.logicalRect.height }, input.outputTransform );
		if ( layer.bufferExtent != expectedBufferExtent )
			return reject( DirectScanoutRejection::NonNativeExtent );
		if ( layer.contentExtent != layer.bufferExtent )
			return reject( DirectScanoutRejection::ContentExtentMismatch );

		switch ( proveClientTransform( layer.client, input.outputTransform ) )
		{
			case ClientTransformProof::NotProven:
				return reject( DirectScanoutRejection::ClientTransformNotProven );
			case ClientTransformProof::Ambiguous:
				return reject( DirectScanoutRejection::AmbiguousClientTransform );
			case ClientTransformProof::Proven:
				break;
		}

		if ( !layer.formatCompatible )
			return reject( DirectScanoutRejection::IncompatibleFormat );
		if ( !layer.explicitModifier )
			return reject( DirectScanoutRejection::AmbiguousModifier );
		if ( !layer.modifierCompatible )
			return reject( DirectScanoutRejection::IncompatibleModifier );
		if ( !layer.framebufferImportable )
			return reject( DirectScanoutRejection::FramebufferNotImportable );

		if ( isBase )
		{
			if ( layer.zpos != 0 )
				return reject( DirectScanoutRejection::InvalidLayerOrder );
			if ( layer.blendMode != PlaneBlendMode::Opaque )
				return reject( DirectScanoutRejection::IncompatibleBlend );
		}
		else
		{
			if ( layer.zpos <= input.layers[0].zpos )
				return reject( DirectScanoutRejection::InvalidLayerOrder );
			if ( !std::isfinite( layer.opacity ) || layer.opacity <= 0.0f ||
				layer.opacity > 1.0f || !layer.alphaCompatible )
				return reject( DirectScanoutRejection::IncompatibleAlpha );
			if ( ( layer.blendMode != PlaneBlendMode::Premultiplied &&
				layer.blendMode != PlaneBlendMode::Coverage ) ||
				!layer.blendCompatible )
				return reject( DirectScanoutRejection::IncompatibleBlend );
			if ( !layer.zposCompatible )
				return reject( DirectScanoutRejection::IncompatibleZpos );
			if ( layer.bufferIdentity == input.layers[0].bufferIdentity )
				return reject( DirectScanoutRejection::AmbiguousBufferIdentity );
		}

		const std::optional<Rect> destination = logicalToNative(
			layer.logicalRect, input.logicalExtent, input.outputTransform );
		if ( !destination )
			return reject( DirectScanoutRejection::InvalidLogicalRectangle );

		decision.records[i] = {
			.bufferIdentity = layer.bufferIdentity,
			.sourceRect = { 0, 0, layer.bufferExtent.width, layer.bufferExtent.height },
			.destinationRect = *destination,
			.zpos = layer.zpos,
			.opacity = layer.opacity,
			.blendMode = layer.blendMode,
			.kmsTransform = Transform::Normal,
		};
	}

	return decision;
}

} // namespace gamescope::output_rotation
