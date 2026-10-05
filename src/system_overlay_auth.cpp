#include "system_overlay_auth.hpp"

namespace gamescope::system_overlay_auth
{

bool is_authenticated_peer(pid_t peer_pid, pid_t launcher_pid,
	const ParentPidLookup &parent_pid_lookup)
{
	if (peer_pid <= 0 || launcher_pid <= 0 || !parent_pid_lookup)
		return false;

	const std::optional<pid_t> parent_pid = parent_pid_lookup(peer_pid);
	return parent_pid.has_value() && *parent_pid == launcher_pid;
}

} // namespace gamescope::system_overlay_auth
