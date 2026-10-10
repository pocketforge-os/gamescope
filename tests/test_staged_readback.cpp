#include "staged_readback.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>

int main()
{
	using namespace gamescope::staged_readback;

	// XB24 is R, G, B, X in byte order. Include row padding to ensure the
	// summary follows the image's reported pitch rather than assuming packed rows.
	const std::array<uint8_t, 24> xb24 = {
		255, 0, 0, 255, 255, 0, 0, 255, 0xaa, 0xaa, 0xaa, 0xaa,
		0, 0, 0, 255, 12, 34, 56, 255, 0xbb, 0xbb, 0xbb, 0xbb,
	};
	const Summary xb24Summary = summarize( xb24.data(), 12, 2, 2, PixelOrder::RGBA );
	assert( xb24Summary.pixels == 4 );
	assert( xb24Summary.red == 2 );
	assert( xb24Summary.black == 1 );
	assert( xb24Summary.hash == UINT64_C( 0xc39db315d310602f ) );

	// XR24 is B, G, R, X in byte order.
	const std::array<uint8_t, 8> xr24 = { 0, 0, 255, 0, 0, 0, 0, 0 };
	const Summary xr24Summary = summarize( xr24.data(), 8, 2, 1, PixelOrder::BGRA );
	assert( xr24Summary.pixels == 2 );
	assert( xr24Summary.red == 1 );
	assert( xr24Summary.black == 1 );

	// Model a portrait (rotated-output) VkImage subresource whose pixels start
	// after the allocation base and whose rows are wider than packed pixels.
	// Passing allocation.data() directly to summarize(), as the old call site
	// did, reads the poison prefix instead of the image subresource.
	std::array<uint8_t, 64> allocation = {};
	allocation.fill( 0x5a );
	constexpr MappedLayout portraitLayout = {
		.allocationSize = allocation.size(),
		.offset = 16,
		.rowPitch = 12,
		.width = 2,
		.height = 3,
	};
	for ( uint32_t y = 0; y < portraitLayout.height; y++ )
	{
		uint8_t *row = allocation.data() + portraitLayout.offset + y * portraitLayout.rowPitch;
		for ( uint32_t x = 0; x < portraitLayout.width; x++ )
		{
			row[x * 4 + 0] = 255;
			row[x * 4 + 1] = 0;
			row[x * 4 + 2] = 0;
			row[x * 4 + 3] = 255;
		}
	}
	const Summary oldBasePointerSummary = summarize( allocation.data(),
		portraitLayout.rowPitch, portraitLayout.width, portraitLayout.height,
		PixelOrder::RGBA );
	assert( oldBasePointerSummary.red != 6 );
	const std::optional<Summary> portraitSummary = summarizeMapped(
		allocation.data(), portraitLayout, PixelOrder::RGBA );
	assert( portraitSummary );
	assert( portraitSummary->pixels == 6 );
	assert( portraitSummary->red == 6 );
	assert( portraitSummary->black == 0 );

	MappedLayout truncated = portraitLayout;
	truncated.allocationSize = portraitLayout.offset +
		( portraitLayout.height - 1 ) * portraitLayout.rowPitch +
		portraitLayout.width * 4 - 1;
	assert( !summarizeMapped( allocation.data(), truncated, PixelOrder::RGBA ) );
	MappedLayout shortPitch = portraitLayout;
	shortPitch.rowPitch = portraitLayout.width * 4 - 1;
	assert( !summarizeMapped( allocation.data(), shortPitch, PixelOrder::RGBA ) );

	assert( should_sample( 1 ) );
	assert( !should_sample( 2 ) );
	assert( should_sample( 60 ) );
	assert( should_sample( 600 ) );
	assert( !should_sample( 601 ) );

	// Partial-overlay composites are not eligible for paired readback and must
	// not consume the full-frame sampling schedule.
	std::atomic<uint64_t> eligibleFrames = 0;
	assert( !next_eligible_frame( eligibleFrames, false ) );
	assert( eligibleFrames.load() == 0 );
	const std::optional<uint64_t> firstEligible = next_eligible_frame( eligibleFrames, true );
	assert( firstEligible && *firstEligible == 1 );
	assert( should_sample( *firstEligible ) );

	unsetenv( "GAMESCOPE_STAGED_READBACK_DIAGNOSTICS" );
	assert( !enabled() );
	setenv( "GAMESCOPE_STAGED_READBACK_DIAGNOSTICS", "1", 1 );
	assert( enabled() );
	setenv( "GAMESCOPE_STAGED_READBACK_DIAGNOSTICS", "0", 1 );
	assert( !enabled() );
}
