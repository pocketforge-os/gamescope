#pragma once

#include <cstdint>

namespace gamescope::drm_commit_probe
{

enum class Phase : uint8_t
{
	Submit,
	WaitForPageFlip,
	PageFlipHandler,
	Complete,
};

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
