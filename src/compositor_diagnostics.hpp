#pragma once

#include <cstdint>
#include <cstdlib>

namespace gamescope::compositor_diagnostics
{

// Must match compositedebug_ConstantRed in shaders/descriptor_set.h and
// CompositeDebugFlag::ConstantRed in rendervulkan.hpp.
inline constexpr uint32_t ConstantRedDebugBit = 1u << 6;

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

inline bool constant_red_enabled()
{
	const char *value = std::getenv( "GAMESCOPE_COMPOSITOR_CONSTANT_RED" );
	return value && *value && std::atoi( value ) != 0;
}

constexpr uint32_t with_constant_red_debug( uint32_t debug )
{
	return debug | ConstantRedDebugBit;
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
