#include "vulkan_present_features.h"

constexpr VulkanPresentCapabilities noPresentCapabilities = {};
constexpr VulkanPresentCapabilities extensionsWithoutFeatures = {
	.presentIdExtension = true,
	.presentWaitExtension = true,
};
constexpr VulkanPresentCapabilities fullPresentCapabilities = {
	.presentIdExtension = true,
	.presentWaitExtension = true,
	.presentIdFeature = true,
	.presentWaitFeature = true,
};

constexpr VulkanPresentFeatureSelection directDrmSelection =
	selectVulkanPresentFeatures( false, noPresentCapabilities );
static_assert( directDrmSelection.supported );
static_assert( !directDrmSelection.enablePresentId );
static_assert( !directDrmSelection.enablePresentWait );

constexpr VulkanPresentFeatureSelection missingExtensionSwapchainSelection =
	selectVulkanPresentFeatures( true, noPresentCapabilities );
static_assert( !missingExtensionSwapchainSelection.supported );
static_assert( !missingExtensionSwapchainSelection.enablePresentId );
static_assert( !missingExtensionSwapchainSelection.enablePresentWait );

constexpr VulkanPresentFeatureSelection unsupportedSwapchainSelection =
	selectVulkanPresentFeatures( true, extensionsWithoutFeatures );
static_assert( !unsupportedSwapchainSelection.supported );
static_assert( !unsupportedSwapchainSelection.enablePresentId );
static_assert( !unsupportedSwapchainSelection.enablePresentWait );

constexpr VulkanPresentFeatureSelection supportedSwapchainSelection =
	selectVulkanPresentFeatures( true, fullPresentCapabilities );
static_assert( supportedSwapchainSelection.supported );
static_assert( supportedSwapchainSelection.enablePresentId );
static_assert( supportedSwapchainSelection.enablePresentWait );

int main()
{
	return 0;
}
