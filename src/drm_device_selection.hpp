#pragma once

#include <string>
#include <string_view>

namespace gamescope::drm_device_selection
{

enum class DeviceSource
{
	Discovery,
	VulkanPrimary,
	Preferred,
};

struct DeviceSelection
{
	DeviceSource source = DeviceSource::Discovery;
	std::string path;
};

DeviceSelection selectDevice( std::string_view preferredDevice,
	std::string_view vulkanPrimaryDevice );

} // namespace gamescope::drm_device_selection
