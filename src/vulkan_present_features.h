#pragma once

struct VulkanPresentCapabilities
{
	bool presentIdExtension = false;
	bool presentWaitExtension = false;
	bool presentIdFeature = false;
	bool presentWaitFeature = false;
};

struct VulkanPresentFeatureSelection
{
	bool supported;
	bool enablePresentId;
	bool enablePresentWait;
};

constexpr VulkanPresentFeatureSelection selectVulkanPresentFeatures(
	bool usesVulkanSwapchain,
	const VulkanPresentCapabilities &capabilities )
{
	if ( !usesVulkanSwapchain )
	{
		return {
			.supported = true,
			.enablePresentId = false,
			.enablePresentWait = false,
		};
	}

	const bool supported =
		capabilities.presentIdExtension &&
		capabilities.presentWaitExtension &&
		capabilities.presentIdFeature &&
		capabilities.presentWaitFeature;

	return {
		.supported = supported,
		.enablePresentId = supported,
		.enablePresentWait = supported,
	};
}
