#pragma once

#include <cstdlib>

namespace gamescope::compositor_diagnostics
{

inline bool enabled()
{
	const char *value = std::getenv( "GAMESCOPE_COMPOSITOR_DIAGNOSTICS" );
	return value && *value && std::atoi( value ) != 0;
}

constexpr const char *frame_kind( bool nativeOutput )
{
	return nativeOutput ? "direct" : "composited";
}

constexpr const char *buffer_kind( bool dmabuf )
{
	return dmabuf ? "dmabuf" : "shm-or-cpu";
}

} // namespace gamescope::compositor_diagnostics
