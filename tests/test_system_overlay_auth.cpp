#include "system_overlay_auth.hpp"

#include <cassert>
#include <map>
#include <string>

struct FakeProcess
{
	pid_t parent_pid;
	std::string comm;
};

int main()
{
	std::map<pid_t, FakeProcess> processes = {
		{ 100, { 1, "gamescopereaper" } },
		{ 200, { 100, "mangoapp" } },
		// This process can spoof the mutable name but is not the launched peer.
		{ 301, { 400, "mangoapp" } },
	};

	const gamescope::system_overlay_auth::ParentPidLookup parent_pid_lookup =
		[&processes](pid_t pid) -> std::optional<pid_t>
		{
			auto it = processes.find(pid);
			if (it == processes.end())
				return std::nullopt;
			return it->second.parent_pid;
		};

	assert(gamescope::system_overlay_auth::is_authenticated_peer(200, 100,
		parent_pid_lookup));
	assert(!gamescope::system_overlay_auth::is_authenticated_peer(301, 100,
		parent_pid_lookup));

	// Mutating the spoofed process identity cannot turn it into the trusted peer.
	processes.at(301).comm = "renamed-mangoapp";
	assert(!gamescope::system_overlay_auth::is_authenticated_peer(301, 100,
		parent_pid_lookup));

	assert(!gamescope::system_overlay_auth::is_authenticated_peer(0, 100,
		parent_pid_lookup));
	assert(!gamescope::system_overlay_auth::is_authenticated_peer(200, 0,
		parent_pid_lookup));
	return 0;
}
