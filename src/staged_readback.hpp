#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

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

struct MappedLayout
{
	size_t allocationSize = 0;
	size_t offset = 0;
	size_t rowPitch = 0;
	uint32_t width = 0;
	uint32_t height = 0;
};

Summary summarize( const uint8_t *data, size_t rowPitch, uint32_t width,
	uint32_t height, PixelOrder order );

std::optional<Summary> summarizeMapped( const uint8_t *allocation,
	const MappedLayout &layout, PixelOrder order );

bool enabled();
bool should_sample( uint64_t frame );
std::optional<uint64_t> next_eligible_frame( std::atomic<uint64_t> &counter,
	bool eligible );

} // namespace gamescope::staged_readback
