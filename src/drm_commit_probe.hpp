#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace gamescope::drm_commit_probe
{

enum class Phase : uint8_t
{
	Master,
	Submit,
	WaitForPageFlip,
	PageFlipHandler,
	Complete,
};

// The fault under investigation is the first modeset/page flip. Keep later
// logging bounded so an accidentally long diagnostic session cannot fill tmpfs.
constexpr uint64_t kMaxCapturedCommits = 256;

inline bool enabled()
{
	const char *value = std::getenv( "GAMESCOPE_DRM_COMMIT_DIAGNOSTICS" );
	return value && std::strcmp( value, "1" ) == 0;
}

constexpr bool withinCapture( uint64_t commitId )
{
	return commitId >= 1 && commitId <= kMaxCapturedCommits;
}

inline bool shouldLog( uint64_t commitId )
{
	return enabled() && withinCapture( commitId );
}

constexpr const char *phaseName( Phase phase )
{
	switch ( phase )
	{
		case Phase::Master:
			return "master";
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
