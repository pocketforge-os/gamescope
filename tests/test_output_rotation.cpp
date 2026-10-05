#include "output_rotation.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

using namespace gamescope::output_rotation;

namespace
{

int g_failures = 0;

#define CHECK( ... ) check( (__VA_ARGS__), #__VA_ARGS__, __LINE__ )

void check( bool value, const char *expression, int line )
{
	if ( value )
		return;

	std::fprintf( stderr, "test_output_rotation.cpp:%d: CHECK(%s) failed\n", line, expression );
	g_failures++;
}

template<size_t Size>
void checkImage( const std::vector<uint32_t> &actual,
	const std::array<uint32_t, Size> &expected, int line )
{
	if ( actual.size() == expected.size() &&
		std::equal( actual.begin(), actual.end(), expected.begin() ) )
		return;

	std::fprintf( stderr, "test_output_rotation.cpp:%d: asymmetric image mismatch\n", line );
	g_failures++;
}

#define CHECK_IMAGE( actual, expected ) checkImage( actual, expected, __LINE__ )

std::vector<uint32_t> transformImage( std::span<const uint32_t> source,
	Extent logicalExtent, Transform transform )
{
	const Extent nativeExtent = transformExtent( logicalExtent, transform );
	std::vector<uint32_t> result( uint64_t{ nativeExtent.width } * nativeExtent.height );
	for ( uint32_t y = 0; y < logicalExtent.height; y++ )
	{
		for ( uint32_t x = 0; x < logicalExtent.width; x++ )
		{
				const auto native = logicalToNative( Point{ x, y }, logicalExtent, transform );
			CHECK( native.has_value() );
			if ( native )
				result[uint64_t{ native->y } * nativeExtent.width + native->x] =
					source[uint64_t{ y } * logicalExtent.width + x];
		}
	}
	return result;
}

void test_asymmetric_pixels_and_inverse_mappings()
{
	CHECK( transformFromPanelOrientation( GAMESCOPE_PANEL_ORIENTATION_90 ) == Transform::Rotate90 );
	CHECK( transformFromPanelOrientation( GAMESCOPE_PANEL_ORIENTATION_270 ) == Transform::Rotate270 );
	CHECK( transformFromPanelOrientation( GAMESCOPE_PANEL_ORIENTATION_0 ) == Transform::Normal );
	CHECK( transformFromPanelOrientation( GAMESCOPE_PANEL_ORIENTATION_180 ) == Transform::Normal );

	// Logical 4x3 image. Every edge, row, and column is distinguishable.
	const std::array<uint32_t, 12> source = {
		0xA1, 0xB2, 0xC3, 0xD4,
		0xE5, 0xF6, 0x17, 0x28,
		0x39, 0x4A, 0x5B, 0x6C,
	};
	const std::array<uint32_t, 12> expected90 = {
		0x39, 0xE5, 0xA1,
		0x4A, 0xF6, 0xB2,
		0x5B, 0x17, 0xC3,
		0x6C, 0x28, 0xD4,
	};
	const std::array<uint32_t, 12> expected270 = {
		0xD4, 0x28, 0x6C,
		0xC3, 0x17, 0x5B,
		0xB2, 0xF6, 0x4A,
		0xA1, 0xE5, 0x39,
	};

	const Extent logical = { 4, 3 };
	CHECK( transformExtent( logical, Transform::Rotate90 ) == Extent{ 3, 4 } );
	CHECK( transformExtent( logical, Transform::Rotate270 ) == Extent{ 3, 4 } );
	CHECK_IMAGE( transformImage( source, logical, Transform::Rotate90 ), expected90 );
	CHECK_IMAGE( transformImage( source, logical, Transform::Rotate270 ), expected270 );
	CHECK( transformImage( source, logical, Transform::Rotate90 ) !=
		transformImage( source, logical, Transform::Rotate270 ) );

	for ( Transform transform : { Transform::Rotate90, Transform::Rotate270 } )
	{
		for ( uint32_t y = 0; y < logical.height; y++ )
		{
			for ( uint32_t x = 0; x < logical.width; x++ )
			{
				const auto native = logicalToNative( Point{ x, y }, logical, transform );
				CHECK( native.has_value() );
				if ( native )
				{
					const auto roundTrip = nativeToLogical( *native, logical, transform );
					CHECK( roundTrip == Point{ x, y } );
				}
			}
		}
	}

	CHECK( !logicalToNative( Point{ logical.width, 0 }, logical, Transform::Rotate90 ) );
	CHECK( !nativeToLogical( Point{ 3, 0 }, logical, Transform::Rotate270 ) );
}

void test_layout_stride_damage_cursor_and_overlay_coordinates()
{
	const Extent logical = { 1280, 720 };
	const auto layout90 = makeLayout( logical, Transform::Rotate90, 4, 256 );
	const auto layout270 = makeLayout( logical, Transform::Rotate270, 4, 256 );
	CHECK( layout90.has_value() );
	CHECK( layout270.has_value() );
	if ( layout90 && layout270 )
	{
		CHECK( layout90->nativeExtent == Extent{ 720, 1280 } );
		CHECK( layout90->minimumRowBytes == 2880 );
		CHECK( layout90->rowPitch == 3072 );
		CHECK( layout90->rowPitch >= layout90->minimumRowBytes );
		CHECK( layout90->allocationBytes == uint64_t{ 3072 } * 1280 );
		CHECK( layout270->nativeExtent == layout90->nativeExtent );
		CHECK( layout270->rowPitch == layout90->rowPitch );
	}
	CHECK( !makeLayout( logical, Transform::Rotate90, 0, 256 ) );
	CHECK( !makeLayout( logical, Transform::Rotate90, 4, 0 ) );

	const Rect damage = { 100, 50, 300, 200 };
	CHECK( logicalToNative( damage, logical, Transform::Rotate90 ) ==
		Rect{ 470, 100, 200, 300 } );
	CHECK( logicalToNative( damage, logical, Transform::Rotate270 ) ==
		Rect{ 50, 880, 200, 300 } );
	CHECK( nativeToLogical( Rect{ 470, 100, 200, 300 }, logical, Transform::Rotate90 ) ==
		damage );
	CHECK( nativeToLogical( Rect{ 50, 880, 200, 300 }, logical, Transform::Rotate270 ) ==
		damage );
	CHECK( !logicalToNative( Rect{ 1200, 700, 100, 30 }, logical, Transform::Rotate90 ) );
	CHECK( !nativeToLogical( Rect{ 700, 1200, 30, 100 }, logical, Transform::Rotate270 ) );

	const Point cursor = { 123, 45 };
	CHECK( logicalToNative( cursor, logical, Transform::Rotate90 ) == Point{ 674, 123 } );
	CHECK( logicalToNative( cursor, logical, Transform::Rotate270 ) == Point{ 45, 1156 } );

	const Rect externalOverlay = { 900, 500, 200, 100 };
	CHECK( logicalToNative( externalOverlay, logical, Transform::Rotate90 ) ==
		Rect{ 120, 900, 100, 200 } );
	CHECK( logicalToNative( externalOverlay, logical, Transform::Rotate270 ) ==
		Rect{ 500, 180, 100, 200 } );
}

void test_capture_and_scanout_contract()
{
	const Extent logical = { 1280, 720 };
	for ( Transform transform : { Transform::Rotate90, Transform::Rotate270 } )
	{
		const FrameContract contract = frameContract( logical, transform );
		CHECK( contract.compositionExtent == logical );
		CHECK( contract.captureExtent == logical );
		CHECK( contract.captureSpace == CaptureSpace::Logical );
		CHECK( contract.rotatedExtent == Extent{ 720, 1280 } );
		CHECK( contract.scanoutExtent == Extent{ 720, 1280 } );
		CHECK( contract.kmsTransform == Transform::Normal );
		CHECK( contract.rotateBeforeStaging );
	}

	const FrameContract normal = frameContract( logical, Transform::Normal );
	CHECK( normal.compositionExtent == logical );
	CHECK( normal.scanoutExtent == logical );
	CHECK( !normal.rotateBeforeStaging );
}

ClientTransform matchingClientTransform( Transform transform )
{
	return transform == Transform::Rotate90
		? ClientTransform::Rotate90
		: ClientTransform::Rotate270;
}

ClientTransform wrongClientTransform( Transform transform )
{
	return transform == Transform::Rotate90
		? ClientTransform::Rotate270
		: ClientTransform::Rotate90;
}

DirectScanoutLayerInput eligibleLayer( LayerRole role, Rect logicalRect,
	Transform transform, uint64_t bufferIdentity )
{
	const Extent logicalLayerExtent = { logicalRect.width, logicalRect.height };
	const Extent bufferExtent = transformExtent( logicalLayerExtent, transform );
	return {
		.bufferIdentity = bufferIdentity,
		.role = role,
		.bufferExtent = bufferExtent,
		.contentExtent = bufferExtent,
		.logicalRect = logicalRect,
		.client = {
			.clientClass = ClientClass::Wayland,
			.waylandBufferTransform = matchingClientTransform( transform ),
			.vulkanPreTransform = ClientTransform::Unknown,
		},
		.zpos = role == LayerRole::Base ? 0 : 3,
		.opacity = role == LayerRole::Base ? 1.0f : 0.75f,
		.blendMode = role == LayerRole::Base
			? PlaneBlendMode::Opaque
			: PlaneBlendMode::Premultiplied,
		.formatCompatible = true,
		.explicitModifier = true,
		.modifierCompatible = true,
		.framebufferImportable = true,
		.alphaCompatible = true,
		.blendCompatible = true,
		.zposCompatible = true,
	};
}

DirectScanoutInput eligibleDirectInput( Transform transform = Transform::Rotate90 )
{
	DirectScanoutInput input = {
		.outputTransform = transform,
		.logicalExtent = { 1280, 720 },
		.layerCount = 1,
		.normalKmsTransform = true,
	};
	input.layers[0] = eligibleLayer( LayerRole::Base,
		Rect{ 0, 0, 1280, 720 }, transform, 0xBA5E );
	return input;
}

void expectRejected( const DirectScanoutInput &input, DirectScanoutRejection expected )
{
	const DirectScanoutDecision decision = directScanoutDecision( input );
	CHECK( !decision.eligible );
	CHECK( decision.rejection == expected );
}

void test_native_base_and_system_overlay_plane_records()
{
	const Rect overlayLogical = { 900, 500, 200, 100 };
	for ( Transform transform : { Transform::Rotate90, Transform::Rotate270 } )
	{
		DirectScanoutInput baseOnlyInput = eligibleDirectInput( transform );
		const DirectScanoutDecision baseOnly = directScanoutDecision( baseOnlyInput );
		CHECK( baseOnly.eligible );
		CHECK( baseOnly.rejection == DirectScanoutRejection::Accepted );
		CHECK( baseOnly.recordCount == 1 );
		CHECK( baseOnly.records[0].bufferIdentity == 0xBA5E );
		CHECK( baseOnly.records[0].sourceRect == Rect{ 0, 0, 720, 1280 } );
		CHECK( baseOnly.records[0].destinationRect == Rect{ 0, 0, 720, 1280 } );
		CHECK( baseOnly.records[0].kmsTransform == Transform::Normal );

		DirectScanoutInput withOverlayInput = baseOnlyInput;
		withOverlayInput.layerCount = 2;
		withOverlayInput.layers[1] = eligibleLayer( LayerRole::SystemOverlay,
			overlayLogical, transform, 0x0A11 );
		const DirectScanoutDecision withOverlay = directScanoutDecision( withOverlayInput );
		CHECK( withOverlay.eligible );
		CHECK( withOverlay.recordCount == 2 );
		CHECK( withOverlay.records[0] == baseOnly.records[0] );
		CHECK( withOverlay.records[1].bufferIdentity == 0x0A11 );
		CHECK( withOverlay.records[1].sourceRect == Rect{ 0, 0, 100, 200 } );
		CHECK( withOverlay.records[1].destinationRect ==
			logicalToNative( overlayLogical, Extent{ 1280, 720 }, transform ) );
		CHECK( withOverlay.records[1].kmsTransform == Transform::Normal );

		// Showing and removing the overlay cannot mutate or replace the base.
		const DirectScanoutDecision afterRemoval = directScanoutDecision( baseOnlyInput );
		CHECK( afterRemoval.eligible );
		CHECK( afterRemoval.records[0] == baseOnly.records[0] );

		const std::array<uint32_t, 12> overlayPixels = {
			0xA1, 0xB2, 0xC3, 0xD4,
			0xE5, 0xF6, 0x17, 0x28,
			0x39, 0x4A, 0x5B, 0x6C,
		};
		const std::array<uint32_t, 12> expected90 = {
			0x39, 0xE5, 0xA1, 0x4A, 0xF6, 0xB2,
			0x5B, 0x17, 0xC3, 0x6C, 0x28, 0xD4,
		};
		const std::array<uint32_t, 12> expected270 = {
			0xD4, 0x28, 0x6C, 0xC3, 0x17, 0x5B,
			0xB2, 0xF6, 0x4A, 0xA1, 0xE5, 0x39,
		};
		if ( transform == Transform::Rotate90 )
			CHECK_IMAGE( transformImage( overlayPixels, Extent{ 4, 3 }, transform ), expected90 );
		else
			CHECK_IMAGE( transformImage( overlayPixels, Extent{ 4, 3 }, transform ), expected270 );
	}
}

void test_client_transform_proof()
{
	for ( Transform transform : { Transform::Rotate90, Transform::Rotate270 } )
	{
		DirectScanoutInput input = eligibleDirectInput( transform );
		CHECK( directScanoutDecision( input ).eligible );

		// The Vulkan/Xwayland path is distinct: normal wl_surface metadata plus
		// a matching committed VkSwapchain preTransform proves the native image.
		input.layers[0].client = {
			.clientClass = ClientClass::Xwayland,
			.waylandBufferTransform = ClientTransform::Normal,
			.vulkanPreTransform = matchingClientTransform( transform ),
		};
		CHECK( directScanoutDecision( input ).eligible );

		input = eligibleDirectInput( transform );
		input.layers[0].client.waylandBufferTransform = ClientTransform::Normal;
		expectRejected( input, DirectScanoutRejection::ClientTransformNotProven );

		input = eligibleDirectInput( transform );
		input.layers[0].client.waylandBufferTransform = wrongClientTransform( transform );
		expectRejected( input, DirectScanoutRejection::ClientTransformNotProven );

		input = eligibleDirectInput( transform );
		input.layers[0].client.waylandBufferTransform = ClientTransform::Unknown;
		expectRejected( input, DirectScanoutRejection::ClientTransformNotProven );

		input = eligibleDirectInput( transform );
		input.layers[0].client.vulkanPreTransform = matchingClientTransform( transform );
		expectRejected( input, DirectScanoutRejection::AmbiguousClientTransform );

		input = eligibleDirectInput( transform );
		input.layers[0].client = {
			.clientClass = ClientClass::Xwayland,
			.waylandBufferTransform = ClientTransform::Normal,
			.vulkanPreTransform = wrongClientTransform( transform ),
		};
		expectRejected( input, DirectScanoutRejection::ClientTransformNotProven );

		input = eligibleDirectInput( transform );
		input.layers[0].client = {
			.clientClass = ClientClass::Xwayland,
			.waylandBufferTransform = ClientTransform::Normal,
			.vulkanPreTransform = ClientTransform::Normal,
		};
		expectRejected( input, DirectScanoutRejection::ClientTransformNotProven );

		input = eligibleDirectInput( transform );
		input.layers[0].bufferExtent = { 1280, 720 };
		input.layers[0].contentExtent = { 1280, 720 };
		input.layers[0].client.waylandBufferTransform = ClientTransform::Normal;
		expectRejected( input, DirectScanoutRejection::NonNativeExtent );

		input = eligibleDirectInput( transform );
		input.normalKmsTransform = false;
		expectRejected( input, DirectScanoutRejection::KmsTransformNotNormal );
	}
}

void test_native_plane_policy_fails_closed()
{
	DirectScanoutInput input = eligibleDirectInput();
	input.outputTransform = Transform::Normal;
	expectRejected( input, DirectScanoutRejection::NoSoftwareRotation );

	input = eligibleDirectInput();
	input.layerCount = 3;
	expectRejected( input, DirectScanoutRejection::UnsupportedLayerCount );

	input = eligibleDirectInput();
	input.layers[0].role = LayerRole::Unknown;
	expectRejected( input, DirectScanoutRejection::UnclassifiedLayer );

	input = eligibleDirectInput();
	input.layers[0].opacity = 0.5f;
	expectRejected( input, DirectScanoutRejection::NotOpaqueBaseLayer );

	input = eligibleDirectInput();
	input.layers[0].bufferExtent = { 1280, 720 };
	expectRejected( input, DirectScanoutRejection::NonNativeExtent );

	input = eligibleDirectInput();
	input.layers[0].contentExtent = { 700, 1280 };
	expectRejected( input, DirectScanoutRejection::ContentExtentMismatch );

	input = eligibleDirectInput();
	input.layers[0].formatCompatible = false;
	expectRejected( input, DirectScanoutRejection::IncompatibleFormat );

	input = eligibleDirectInput();
	input.layers[0].explicitModifier = false;
	expectRejected( input, DirectScanoutRejection::AmbiguousModifier );

	input = eligibleDirectInput();
	input.layers[0].modifierCompatible = false;
	expectRejected( input, DirectScanoutRejection::IncompatibleModifier );

	input = eligibleDirectInput();
	input.layers[0].framebufferImportable = false;
	expectRejected( input, DirectScanoutRejection::FramebufferNotImportable );

	input = eligibleDirectInput();
	input.layerCount = 2;
	input.layers[1] = eligibleLayer( LayerRole::Unknown,
		Rect{ 900, 500, 200, 100 }, Transform::Rotate90, 0x0A11 );
	expectRejected( input, DirectScanoutRejection::UnclassifiedLayer );

	input.layers[1].role = LayerRole::Cursor;
	expectRejected( input, DirectScanoutRejection::UnclassifiedLayer );

	input.layers[1].role = LayerRole::SystemOverlay;
	input.layers[1].logicalRect = { 1200, 700, 200, 100 };
	expectRejected( input, DirectScanoutRejection::InvalidLogicalRectangle );

	input = eligibleDirectInput();
	input.layerCount = 2;
	input.layers[1] = eligibleLayer( LayerRole::SystemOverlay,
		Rect{ 900, 500, 200, 100 }, Transform::Rotate90, 0x0A11 );
	input.layers[1].bufferExtent = { 200, 100 };
	expectRejected( input, DirectScanoutRejection::NonNativeExtent );

	input.layers[1].bufferExtent = { 100, 200 };
	input.layers[1].contentExtent = { 100, 190 };
	expectRejected( input, DirectScanoutRejection::ContentExtentMismatch );

	input.layers[1].contentExtent = { 100, 200 };
	input.layers[1].client.waylandBufferTransform = ClientTransform::Rotate270;
	expectRejected( input, DirectScanoutRejection::ClientTransformNotProven );

	input.layers[1].client.waylandBufferTransform = ClientTransform::Rotate90;
	input.layers[1].client.vulkanPreTransform = ClientTransform::Rotate90;
	expectRejected( input, DirectScanoutRejection::AmbiguousClientTransform );

	input.layers[1].client.vulkanPreTransform = ClientTransform::Unknown;
	input.layers[1].formatCompatible = false;
	expectRejected( input, DirectScanoutRejection::IncompatibleFormat );

	input.layers[1].formatCompatible = true;
	input.layers[1].explicitModifier = false;
	expectRejected( input, DirectScanoutRejection::AmbiguousModifier );

	input.layers[1].explicitModifier = true;
	input.layers[1].modifierCompatible = false;
	expectRejected( input, DirectScanoutRejection::IncompatibleModifier );

	input.layers[1].modifierCompatible = true;
	input.layers[1].framebufferImportable = false;
	expectRejected( input, DirectScanoutRejection::FramebufferNotImportable );

	input.layers[1].framebufferImportable = true;
	input.layers[1].zpos = 0;
	expectRejected( input, DirectScanoutRejection::InvalidLayerOrder );

	input.layers[1].zpos = 3;
	input.layers[1].blendMode = PlaneBlendMode::Unsupported;
	expectRejected( input, DirectScanoutRejection::IncompatibleBlend );

	input.layers[1].blendMode = PlaneBlendMode::Coverage;
	input.layers[1].alphaCompatible = false;
	expectRejected( input, DirectScanoutRejection::IncompatibleAlpha );

	input.layers[1].alphaCompatible = true;
	input.layers[1].blendCompatible = false;
	expectRejected( input, DirectScanoutRejection::IncompatibleBlend );

	input.layers[1].blendCompatible = true;
	input.layers[1].zposCompatible = false;
	expectRejected( input, DirectScanoutRejection::IncompatibleZpos );

	input.layers[1].zposCompatible = true;
	input.layers[1].bufferIdentity = input.layers[0].bufferIdentity;
	expectRejected( input, DirectScanoutRejection::AmbiguousBufferIdentity );
}

} // namespace

int main()
{
	test_asymmetric_pixels_and_inverse_mappings();
	test_layout_stride_damage_cursor_and_overlay_coordinates();
	test_capture_and_scanout_contract();
	test_native_base_and_system_overlay_plane_records();
	test_client_transform_proof();
	test_native_plane_policy_fails_closed();
	return g_failures == 0 ? 0 : 1;
}
