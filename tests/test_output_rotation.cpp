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
	CHECK( !logicalToNative( Rect{ 1200, 700, 100, 30 }, logical, Transform::Rotate90 ) );

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

DirectScanoutInput eligibleDirectInput()
{
	return {
		.outputTransform = Transform::Rotate90,
		.layerCount = 1,
		.logicalExtent = { 1280, 720 },
		.bufferExtent = { 720, 1280 },
		.contentExtent = { 720, 1280 },
		.baseLayer = true,
		.opaque = true,
		.normalClientTransform = true,
		.normalKmsTransform = true,
		.formatCompatible = true,
		.explicitModifier = true,
		.modifierCompatible = true,
		.framebufferImportable = true,
	};
}

void expectRejected( DirectScanoutInput input, DirectScanoutRejection expected )
{
	const DirectScanoutDecision decision = directScanoutDecision( input );
	CHECK( !decision.eligible );
	CHECK( decision.rejection == expected );
}

void test_direct_scanout_is_native_only_and_fail_closed()
{
	DirectScanoutInput input = eligibleDirectInput();
	DirectScanoutDecision decision = directScanoutDecision( input );
	CHECK( decision.eligible );
	CHECK( decision.rejection == DirectScanoutRejection::Accepted );
	CHECK( decision.kmsTransform == Transform::Normal );

	input.outputTransform = Transform::Rotate270;
	decision = directScanoutDecision( input );
	CHECK( decision.eligible );
	CHECK( decision.kmsTransform == Transform::Normal );

	input = eligibleDirectInput();
	input.layerCount = 2;
	expectRejected( input, DirectScanoutRejection::MultipleLayers );
	input = eligibleDirectInput();
	input.baseLayer = false;
	expectRejected( input, DirectScanoutRejection::NotOpaqueBaseLayer );
	input = eligibleDirectInput();
	input.opaque = false;
	expectRejected( input, DirectScanoutRejection::NotOpaqueBaseLayer );
	input = eligibleDirectInput();
	input.bufferExtent = { 1280, 720 };
	expectRejected( input, DirectScanoutRejection::NonNativeExtent );
	input = eligibleDirectInput();
	input.contentExtent = { 700, 1280 };
	expectRejected( input, DirectScanoutRejection::ContentExtentMismatch );
	input = eligibleDirectInput();
	input.normalClientTransform = false;
	expectRejected( input, DirectScanoutRejection::ClientTransformNotNormal );
	input = eligibleDirectInput();
	input.normalKmsTransform = false;
	expectRejected( input, DirectScanoutRejection::KmsTransformNotNormal );
	input = eligibleDirectInput();
	input.formatCompatible = false;
	expectRejected( input, DirectScanoutRejection::IncompatibleFormat );
	input = eligibleDirectInput();
	input.explicitModifier = false;
	expectRejected( input, DirectScanoutRejection::AmbiguousModifier );
	input = eligibleDirectInput();
	input.modifierCompatible = false;
	expectRejected( input, DirectScanoutRejection::IncompatibleModifier );
	input = eligibleDirectInput();
	input.framebufferImportable = false;
	expectRejected( input, DirectScanoutRejection::FramebufferNotImportable );
	input = eligibleDirectInput();
	input.outputTransform = Transform::Normal;
	expectRejected( input, DirectScanoutRejection::NoSoftwareRotation );
}

} // namespace

int main()
{
	test_asymmetric_pixels_and_inverse_mappings();
	test_layout_stride_damage_cursor_and_overlay_coordinates();
	test_capture_and_scanout_contract();
	test_direct_scanout_is_native_only_and_fail_closed();
	return g_failures == 0 ? 0 : 1;
}
