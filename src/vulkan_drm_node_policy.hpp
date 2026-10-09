#pragma once

namespace gamescope::vulkan_drm_node_policy
{

constexpr bool requires_primary( bool uses_vulkan_swapchain, bool is_session_based )
{
	return !uses_vulkan_swapchain && is_session_based;
}

} // namespace gamescope::vulkan_drm_node_policy
