#include "input_protocols.hpp"

#include "wlr_begin.hpp"
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_text_input_v3.h>
#include "wlr_end.hpp"

namespace gamescope::input_protocols
{

bool create_globals( wl_display *display )
{
	if ( !wlr_text_input_manager_v3_create( display ) )
		return false;

	// Advertise cursor-shape-v1 at version 2 so clients can use the
	// dnd-ask and all-resize shapes added in the version bump.
	if ( !wlr_cursor_shape_manager_v1_create( display, 2 ) )
		return false;

	return true;
}

}
