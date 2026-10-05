#pragma once

#include <functional>
#include <optional>
#include <sys/types.h>

namespace gamescope::system_overlay_auth
{

using ParentPidLookup = std::function<std::optional<pid_t>(pid_t)>;

// Native-plane admission is tied to the compositor-registered launcher peer,
// not to mutable process metadata such as /proc/<pid>/stat's comm field.
bool is_authenticated_peer(pid_t peer_pid, pid_t launcher_pid,
	const ParentPidLookup &parent_pid_lookup);

} // namespace gamescope::system_overlay_auth
