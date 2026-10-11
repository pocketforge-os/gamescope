#pragma once

#include <span>

namespace gamescope::xwayland_security
{

// Harden wlroots' already-created X11 listeners before its idle callback
// starts Xwayland. Returns false when the listener state cannot be verified.
bool harden_listeners( std::span<int> listener_fds );

} // namespace gamescope::xwayland_security
