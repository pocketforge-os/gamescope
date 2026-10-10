#include "staged_readback.hpp"

#include <array>
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

	assert( should_sample( 1 ) );
	assert( !should_sample( 2 ) );
	assert( should_sample( 60 ) );
	assert( should_sample( 600 ) );
	assert( !should_sample( 601 ) );

	unsetenv( "GAMESCOPE_STAGED_READBACK_DIAGNOSTICS" );
	assert( !enabled() );
	setenv( "GAMESCOPE_STAGED_READBACK_DIAGNOSTICS", "1", 1 );
	assert( enabled() );
	setenv( "GAMESCOPE_STAGED_READBACK_DIAGNOSTICS", "0", 1 );
	assert( !enabled() );
}
