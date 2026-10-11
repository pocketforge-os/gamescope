#pragma once

#include <wayland-server-core.h>

namespace gamescope::input_protocols
{

// Creates the Wayland globals for the input protocols gamescope exposes
// directly through wlroots: zwp_text_input_manager_v3 (version 2) and
// wp_cursor_shape_manager_v1 (version 2). Returns false if either global
// could not be created.
bool create_globals( wl_display *display );

}
