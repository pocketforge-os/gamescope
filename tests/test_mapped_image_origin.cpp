#include "mapped_image_origin.hpp"

#include <array>
#include <cassert>
#include <cstdint>

int main()
{
	std::array<uint8_t, 64> allocation = {};
	allocation[24] = 0x11;
	allocation[40] = 0x22;

	uint8_t *color = gamescope::mapped_image_origin( allocation.data(), 8u, false );
	uint8_t *planes = gamescope::mapped_image_origin( allocation.data(), 8u, true );
	assert( color == allocation.data() + 8u );
	assert( planes == allocation.data() );

	// NV12 plane offsets are allocation-relative. Starting at the color
	// subresource offset would add that offset twice.
	assert( planes[24] == 0x11 );
	assert( planes[40] == 0x22 );
	assert( color[24] != 0x11 );
	assert( gamescope::mapped_image_origin( nullptr, 8u, true ) == nullptr );
}
