#pragma once

#include <cstddef>
#include <cstdint>

namespace gamescope
{

// A color-subresource consumer starts at its reported offset. Multi-planar
// offsets are already allocation-relative and must instead start at the
// allocation base.
inline uint8_t *mapped_image_origin( uint8_t *allocation, size_t colorOffset,
	bool absolutePlaneOffsets )
{
	if ( !allocation )
		return nullptr;
	return allocation + ( absolutePlaneOffsets ? 0u : colorOffset );
}

} // namespace gamescope
