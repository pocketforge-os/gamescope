#pragma once

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

} // namespace gamescope::compositor_diagnostics
