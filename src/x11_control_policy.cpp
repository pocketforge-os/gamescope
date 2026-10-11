#include "x11_control_policy.hpp"

#include <algorithm>

namespace gamescope::x11_control_policy
{

bool dispatches_effect( std::string_view atom, bool legacy_controls_enabled )
{
	return legacy_controls_enabled &&
		std::ranges::find( kLegacyControlAtoms, atom ) != kLegacyControlAtoms.end();
}

} // namespace gamescope::x11_control_policy
