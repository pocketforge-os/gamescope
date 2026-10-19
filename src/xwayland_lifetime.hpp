#pragma once

namespace gamescope::xwayland_lifetime
{

// wlroots starts Xwayland with -terminate. Without a delay, Xwayland can
// terminate on a slow SoC before gamescope's own X connection (opened when
// wlroots reports ready, wlserver.cpp on_xwayland_ready) exists, which leaves
// wlserver_init() waiting forever for a server that is gone. The delay gives
// that first connection time to arrive; once gamescope's window manager is
// connected the server lives as long as gamescope does. wlroots' own lazy
// mode uses 10 s (xwayland/xwayland.c).
inline constexpr int terminate_delay_seconds = 10;

} // namespace gamescope::xwayland_lifetime
