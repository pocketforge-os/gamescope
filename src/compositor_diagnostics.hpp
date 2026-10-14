#pragma once

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace gamescope::compositor_diagnostics
{

enum class FramePath
{
	Direct,
	Composited,
};

inline bool enabled()
{
	const char *value = std::getenv( "GAMESCOPE_COMPOSITOR_DIAGNOSTICS" );
	return value && *value && std::atoi( value ) != 0;
}

constexpr const char *frame_kind( FramePath path )
{
	return path == FramePath::Direct ? "direct" : "composited";
}

constexpr const char *buffer_kind( bool dmabuf )
{
	return dmabuf ? "dmabuf" : "shm-or-cpu";
}

// A complete property tuple is useful on bring-up, but printing it for every
// 60 Hz commit obscures the events it is meant to diagnose. Eight successful
// assignments cover the initial modeset and the rotating output-image set.
constexpr bool should_log_atomic_properties( uint64_t frameId )
{
	return frameId >= 1 && frameId <= 8;
}

} // namespace gamescope::compositor_diagnostics

namespace gamescope::commit_trace
{

// This trace is deliberately a startup-only switch.  Call sites pay one
// predictable branch when it is disabled, with no formatting or counter work.
inline const bool enabled = [] {
	const char *value = std::getenv( "GAMESCOPE_COMMIT_TRACE" );
	return value && *value && std::atoi( value ) != 0;
}();

inline constexpr uint64_t max_events = 4096;
inline std::atomic<uint64_t> event_count = 0;

constexpr bool should_write( uint64_t sequence )
{
	return sequence <= max_events;
}

inline void log( const char *event, const char *format, ... )
{
	const uint64_t sequence = event_count.fetch_add( 1, std::memory_order_relaxed );
	if ( !should_write( sequence ) )
		return;

	flockfile( stderr );
	if ( sequence == max_events )
	{
		std::fprintf( stderr,
			"commit_trace seq=%lu event=rate_limit max_events=%lu\n",
			sequence, max_events );
	}
	else
	{
		std::fprintf( stderr, "commit_trace seq=%lu event=%s ", sequence, event );
		va_list args;
		va_start( args, format );
		std::vfprintf( stderr, format, args );
		va_end( args );
		std::fputc( '\n', stderr );
	}
	funlockfile( stderr );
}

} // namespace gamescope::commit_trace

#define GAMESCOPE_COMMIT_TRACE( event, ... ) \
	do \
	{ \
		if ( gamescope::commit_trace::enabled ) \
			gamescope::commit_trace::log( event, __VA_ARGS__ ); \
	} while ( 0 )
