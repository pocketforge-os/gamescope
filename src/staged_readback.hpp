#pragma once

#include <cstddef>
#include <cstdint>

namespace gamescope::staged_readback
{

enum class PixelOrder
{
	RGBA,
	BGRA,
};

struct Summary
{
	uint64_t hash = 0;
	uint64_t pixels = 0;
	uint64_t red = 0;
	uint64_t black = 0;
};

Summary summarize( const uint8_t *data, size_t rowPitch, uint32_t width,
	uint32_t height, PixelOrder order );

bool enabled();
bool should_sample( uint64_t frame );

} // namespace gamescope::staged_readback
