#include "staged_readback.hpp"

#include <cstdlib>
#include <limits>

namespace gamescope::staged_readback
{

Summary summarize( const uint8_t *data, size_t rowPitch, uint32_t width,
	uint32_t height, PixelOrder order )
{
	constexpr uint64_t fnvOffset = UINT64_C( 14695981039346656037 );
	constexpr uint64_t fnvPrime = UINT64_C( 1099511628211 );
	Summary result = { .hash = fnvOffset };

	for ( uint32_t y = 0; y < height; y++ )
	{
		const uint8_t *row = data + size_t( y ) * rowPitch;
		for ( uint32_t x = 0; x < width; x++ )
		{
			const uint8_t *pixel = row + size_t( x ) * 4;
			for ( uint32_t channel = 0; channel < 4; channel++ )
			{
				result.hash ^= pixel[channel];
				result.hash *= fnvPrime;
			}

			const uint8_t red = pixel[order == PixelOrder::RGBA ? 0 : 2];
			const uint8_t green = pixel[1];
			const uint8_t blue = pixel[order == PixelOrder::RGBA ? 2 : 0];
			result.red += red == 255 && green == 0 && blue == 0;
			result.black += red == 0 && green == 0 && blue == 0;
			result.pixels++;
		}
	}

	return result;
}

std::optional<Summary> summarizeMapped( const uint8_t *allocation,
	const MappedLayout &layout, PixelOrder order )
{
	if ( !allocation || layout.width == 0 || layout.height == 0 )
		return std::nullopt;

	constexpr size_t bytesPerPixel = 4;
	if ( layout.width > std::numeric_limits<size_t>::max() / bytesPerPixel )
		return std::nullopt;
	const size_t rowBytes = size_t( layout.width ) * bytesPerPixel;
	if ( layout.rowPitch < rowBytes || layout.offset > layout.allocationSize )
		return std::nullopt;

	const size_t remaining = layout.allocationSize - layout.offset;
	if ( rowBytes > remaining )
		return std::nullopt;
	const size_t precedingRows = size_t( layout.height - 1 );
	if ( precedingRows > ( remaining - rowBytes ) / layout.rowPitch )
		return std::nullopt;

	return summarize( allocation + layout.offset, layout.rowPitch,
		layout.width, layout.height, order );
}

bool enabled()
{
	const char *value = std::getenv( "GAMESCOPE_STAGED_READBACK_DIAGNOSTICS" );
	return value && *value && std::atoi( value ) != 0;
}

bool should_sample( uint64_t frame )
{
	return frame == 1 || ( frame <= 600 && frame % 60 == 0 );
}

std::optional<uint64_t> next_eligible_frame( std::atomic<uint64_t> &counter,
	bool eligible )
{
	if ( !eligible )
		return std::nullopt;
	return counter.fetch_add( 1, std::memory_order_relaxed ) + 1;
}

} // namespace gamescope::staged_readback
