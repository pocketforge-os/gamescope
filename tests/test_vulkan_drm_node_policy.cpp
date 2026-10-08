#include "vulkan_drm_node_policy.hpp"

#include <cassert>

int main()
{
	using gamescope::vulkan_drm_node_policy::requires_primary;

	// Standalone DRM presents without a swapchain and owns a session.
	assert( requires_primary( false, true ) );

	// Headless has neither a swapchain nor a DRM session, so a render node is sufficient.
	assert( !requires_primary( false, false ) );

	// Nested swapchain backends never require a DRM primary node from the Vulkan device.
	assert( !requires_primary( true, false ) );
	assert( !requires_primary( true, true ) );
}
