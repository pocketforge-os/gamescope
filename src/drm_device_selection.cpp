#include "drm_device_selection.hpp"

namespace gamescope::drm_device_selection
{

DeviceSelection selectDevice( std::string_view preferredDevice,
	std::string_view vulkanPrimaryDevice )
{
	if ( !preferredDevice.empty() )
	{
		if ( preferredDevice.front() == '/' )
			return { DeviceSource::Preferred, std::string{ preferredDevice } };

		return { DeviceSource::Preferred,
			std::string{ "/dev/dri/" } + std::string{ preferredDevice } };
	}

	if ( !vulkanPrimaryDevice.empty() )
		return { DeviceSource::VulkanPrimary, std::string{ vulkanPrimaryDevice } };

	return {};
}

} // namespace gamescope::drm_device_selection
