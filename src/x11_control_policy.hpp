#pragma once

#include <array>
#include <string_view>

#ifndef HAVE_LEGACY_X11_CONTROL_ATOMS
#define HAVE_LEGACY_X11_CONTROL_ATOMS 0
#endif

namespace gamescope::x11_control_policy
{

inline constexpr std::string_view kSteamOverlay = "STEAM_OVERLAY";
inline constexpr std::string_view kExternalOverlay = "GAMESCOPE_EXTERNAL_OVERLAY";
inline constexpr std::string_view kSteamInputFocus = "STEAM_INPUT_FOCUS";
inline constexpr std::string_view kBaselayerAppId = "GAMESCOPECTRL_BASELAYER_APPID";
inline constexpr std::string_view kBaselayerWindow = "GAMESCOPECTRL_BASELAYER_WINDOW";
inline constexpr std::string_view kRequestScreenshot = "GAMESCOPECTRL_REQUEST_SCREENSHOT";
inline constexpr std::string_view kDebugRequestScreenshot = "GAMESCOPECTRL_DEBUG_REQUEST_SCREENSHOT";

inline constexpr std::array kLegacyControlAtoms = {
	kSteamOverlay,
	kExternalOverlay,
	kSteamInputFocus,
	kBaselayerAppId,
	kBaselayerWindow,
	kRequestScreenshot,
	kDebugRequestScreenshot,
};

bool dispatches_effect( std::string_view atom, bool legacy_controls_enabled );
inline bool dispatches_effect( std::string_view atom )
{
	return dispatches_effect( atom, HAVE_LEGACY_X11_CONTROL_ATOMS );
}

} // namespace gamescope::x11_control_policy
