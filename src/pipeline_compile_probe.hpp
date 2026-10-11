#pragma once

#include <cstdlib>
#include <cstdint>

namespace gamescope::pipeline_compile_probe
{

enum class Source : uint8_t
{
	Precompile,
	Demand,
};

inline bool enabled()
{
	const char *value = std::getenv( "GAMESCOPE_PIPELINE_COMPILE_LOG" );
	return value && *value && std::atoi( value ) != 0;
}

constexpr const char *source_name( Source source )
{
	switch ( source )
	{
		case Source::Precompile:
			return "precompile";
		case Source::Demand:
			return "demand";
	}

	return "unknown";
}

constexpr const char *shader_name( uint32_t shader_type )
{
	switch ( shader_type )
	{
		case 0: return "BLIT";
		case 1: return "BLUR";
		case 2: return "BLUR_COND";
		case 3: return "BLUR_FIRST_PASS";
		case 4: return "EASU";
		case 5: return "RCAS";
		case 6: return "NIS";
		case 7: return "RGB_TO_NV12";
		case 8: return "OUTPUT_ROTATE";
	}

	return "UNKNOWN";
}

} // namespace gamescope::pipeline_compile_probe
