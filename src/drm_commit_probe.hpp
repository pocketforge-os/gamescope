#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace gamescope::drm_commit_probe
{

enum class Phase : uint8_t
{
	Submit,
	WaitForPageFlip,
	PageFlipHandler,
	Complete,
};

// This covers a little over 34 seconds at 60 Hz, including the r32 30-second
// checkpoint, without leaving an unbounded log stream on a long-running device.
constexpr uint64_t kMaxCapturedEvents = 2'048;

inline bool enabled()
{
	const char *value = std::getenv( "GAMESCOPE_COMPOSITOR_DIAGNOSTICS" );
	return value && *value && std::atoi( value ) != 0;
}

constexpr bool withinCapture( uint64_t eventId )
{
	return eventId >= 1 && eventId <= kMaxCapturedEvents;
}

inline bool shouldLog( uint64_t eventId )
{
	return enabled() && withinCapture( eventId );
}

inline uint64_t nextCapturedEvent( std::atomic<uint64_t> &counter )
{
	if ( !enabled() )
		return 0;

	const uint64_t eventId = counter.fetch_add( 1, std::memory_order_relaxed ) + 1;
	return withinCapture( eventId ) ? eventId : 0;
}

constexpr const char *phaseName( Phase phase )
{
	switch ( phase )
	{
		case Phase::Submit:
			return "submit";
		case Phase::WaitForPageFlip:
			return "wait-page-flip";
		case Phase::PageFlipHandler:
			return "page-flip-handler";
		case Phase::Complete:
			return "complete";
	}

	return "unknown";
}

} // namespace gamescope::drm_commit_probe
