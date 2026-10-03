#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace
{

constexpr uint32_t kWidth = 8;
constexpr uint32_t kHeight = 8;

uint32_t findMemoryType( const VkPhysicalDeviceMemoryProperties &properties,
	uint32_t allowed, VkMemoryPropertyFlags required )
{
	for ( uint32_t i = 0; i < properties.memoryTypeCount; i++ )
	{
		if ( ( allowed & ( 1u << i ) ) &&
			( properties.memoryTypes[i].propertyFlags & required ) == required )
			return i;
	}
	return UINT32_MAX;
}

bool isSoftwareDevice( const VkPhysicalDeviceProperties &properties )
{
	if ( properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU )
		return true;

	const std::string_view name{ properties.deviceName };
	return name.find( "lavapipe" ) != std::string_view::npos ||
		name.find( "llvmpipe" ) != std::string_view::npos ||
		name.find( "SwiftShader" ) != std::string_view::npos;
}

} // namespace

int main()
{
	VkInstance instance = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;
	VkImage source = VK_NULL_HANDLE;
	VkImage destination = VK_NULL_HANDLE;
	VkDeviceMemory sourceMemory = VK_NULL_HANDLE;
	VkDeviceMemory destinationMemory = VK_NULL_HANDLE;

	const VkApplicationInfo appInfo = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "gamescope-output-staging-test",
		.apiVersion = VK_API_VERSION_1_1,
	};
	const VkInstanceCreateInfo instanceInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
	};
	if ( vkCreateInstance( &instanceInfo, nullptr, &instance ) != VK_SUCCESS )
		return 77;

	uint32_t physicalDeviceCount = 0;
	vkEnumeratePhysicalDevices( instance, &physicalDeviceCount, nullptr );
	std::vector<VkPhysicalDevice> physicalDevices( physicalDeviceCount );
	vkEnumeratePhysicalDevices( instance, &physicalDeviceCount, physicalDevices.data() );

	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties deviceProperties = {};
	for ( VkPhysicalDevice candidate : physicalDevices )
	{
		VkPhysicalDeviceProperties candidateProperties = {};
		vkGetPhysicalDeviceProperties( candidate, &candidateProperties );
		if ( isSoftwareDevice( candidateProperties ) )
		{
			physicalDevice = candidate;
			deviceProperties = candidateProperties;
			break;
		}
	}
	if ( physicalDevice == VK_NULL_HANDLE )
	{
		std::fprintf( stderr, "SKIP: no software Vulkan device\n" );
		vkDestroyInstance( instance, nullptr );
		return 77;
	}

	VkFormatProperties formatProperties = {};
	vkGetPhysicalDeviceFormatProperties( physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProperties );
	const VkFormatFeatureFlags optimalNeeded =
		VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if ( ( formatProperties.optimalTilingFeatures & optimalNeeded ) != optimalNeeded ||
		!( formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT ) )
	{
		std::fprintf( stderr, "SKIP: software Vulkan device lacks required transfer features\n" );
		vkDestroyInstance( instance, nullptr );
		return 77;
	}

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &queueFamilyCount, nullptr );
	std::vector<VkQueueFamilyProperties> queueProperties( queueFamilyCount );
	vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &queueFamilyCount, queueProperties.data() );
	uint32_t queueFamily = UINT32_MAX;
	for ( uint32_t i = 0; i < queueFamilyCount; i++ )
	{
		if ( queueProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT )
		{
			queueFamily = i;
			break;
		}
	}
	if ( queueFamily == UINT32_MAX )
	{
		vkDestroyInstance( instance, nullptr );
		return 77;
	}

	const float priority = 1.0f;
	const VkDeviceQueueCreateInfo queueInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = queueFamily,
		.queueCount = 1,
		.pQueuePriorities = &priority,
	};
	const VkDeviceCreateInfo deviceInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueInfo,
	};
	if ( vkCreateDevice( physicalDevice, &deviceInfo, nullptr, &device ) != VK_SUCCESS )
	{
		vkDestroyInstance( instance, nullptr );
		return 77;
	}

	VkPhysicalDeviceMemoryProperties memoryProperties = {};
	vkGetPhysicalDeviceMemoryProperties( physicalDevice, &memoryProperties );

	auto makeImage = [&]( VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags memoryFlags,
		VkImage *image, VkDeviceMemory *memory ) -> bool {
		const VkImageCreateInfo imageInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = VK_FORMAT_R8G8B8A8_UNORM,
			.extent = { kWidth, kHeight, 1 },
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = tiling,
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};
		if ( vkCreateImage( device, &imageInfo, nullptr, image ) != VK_SUCCESS )
			return false;
		VkMemoryRequirements requirements = {};
		vkGetImageMemoryRequirements( device, *image, &requirements );
		const uint32_t memoryType = findMemoryType( memoryProperties, requirements.memoryTypeBits, memoryFlags );
		if ( memoryType == UINT32_MAX )
			return false;
		const VkMemoryAllocateInfo allocateInfo = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = requirements.size,
			.memoryTypeIndex = memoryType,
		};
		return vkAllocateMemory( device, &allocateInfo, nullptr, memory ) == VK_SUCCESS &&
			vkBindImageMemory( device, *image, *memory, 0 ) == VK_SUCCESS;
	};

	if ( !makeImage( VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &source, &sourceMemory ) ||
		!makeImage( VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &destination, &destinationMemory ) )
	{
		std::fprintf( stderr, "SKIP: software Vulkan image allocation unsupported\n" );
		goto cleanup_skip;
	}

	{
		const VkCommandPoolCreateInfo poolInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = queueFamily,
		};
		if ( vkCreateCommandPool( device, &poolInfo, nullptr, &commandPool ) != VK_SUCCESS )
			goto cleanup_fail;
		const VkCommandBufferAllocateInfo commandInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = commandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		if ( vkAllocateCommandBuffers( device, &commandInfo, &commandBuffer ) != VK_SUCCESS )
			goto cleanup_fail;
		const VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		vkBeginCommandBuffer( commandBuffer, &beginInfo );

		std::array<VkImageMemoryBarrier, 2> initialBarriers = {};
		for ( size_t i = 0; i < initialBarriers.size(); i++ )
		{
			initialBarriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			initialBarriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			initialBarriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			initialBarriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			initialBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			initialBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			initialBarriers[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		}
		initialBarriers[0].image = source;
		initialBarriers[1].image = destination;
		vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
			uint32_t( initialBarriers.size() ), initialBarriers.data() );

		const VkClearColorValue clearColor = {
			.float32 = { 17.0f / 255.0f, 34.0f / 255.0f, 51.0f / 255.0f, 68.0f / 255.0f },
		};
		const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdClearColorImage( commandBuffer, source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			&clearColor, 1, &range );

		const VkImageMemoryBarrier sourceBarrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = source,
			.subresourceRange = range,
		};
		vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &sourceBarrier );

		const VkImageCopy copy = {
			.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
			.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
			.extent = { kWidth, kHeight, 1 },
		};
		vkCmdCopyImage( commandBuffer, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );

		const VkImageMemoryBarrier hostBarrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_HOST_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = destination,
			.subresourceRange = range,
		};
		vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 0, nullptr, 1, &hostBarrier );
		vkEndCommandBuffer( commandBuffer );

		VkQueue queue = VK_NULL_HANDLE;
		vkGetDeviceQueue( device, queueFamily, 0, &queue );
		const VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		vkCreateFence( device, &fenceInfo, nullptr, &fence );
		const VkSubmitInfo submitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &commandBuffer,
		};
		if ( vkQueueSubmit( queue, 1, &submitInfo, fence ) != VK_SUCCESS ||
			vkWaitForFences( device, 1, &fence, VK_TRUE, UINT64_MAX ) != VK_SUCCESS )
			goto cleanup_fail;
	}

	{
		const VkImageSubresource subresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
		VkSubresourceLayout layout = {};
		vkGetImageSubresourceLayout( device, destination, &subresource, &layout );
		void *mapped = nullptr;
		if ( vkMapMemory( device, destinationMemory, 0, VK_WHOLE_SIZE, 0, &mapped ) != VK_SUCCESS )
			goto cleanup_fail;
		const VkMappedMemoryRange mappedRange = {
			.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
			.memory = destinationMemory,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		};
		vkInvalidateMappedMemoryRanges( device, 1, &mappedRange );
		const std::array<uint8_t, 4> expected = { 0x11, 0x22, 0x33, 0x44 };
		const auto *pixel = static_cast<const uint8_t *>( mapped ) + layout.offset;
		const bool matches = std::memcmp( pixel, expected.data(), expected.size() ) == 0;
		vkUnmapMemory( device, destinationMemory );
		if ( !matches )
		{
			std::fprintf( stderr, "FAIL: software Vulkan optimal-to-linear copy changed pixels\n" );
			goto cleanup_fail;
		}
	}

	std::fprintf( stderr, "PASS: %s optimal-to-linear same-format copy\n", deviceProperties.deviceName );
	vkDeviceWaitIdle( device );
	if ( fence ) vkDestroyFence( device, fence, nullptr );
	if ( commandPool ) vkDestroyCommandPool( device, commandPool, nullptr );
	if ( destination ) vkDestroyImage( device, destination, nullptr );
	if ( destinationMemory ) vkFreeMemory( device, destinationMemory, nullptr );
	if ( source ) vkDestroyImage( device, source, nullptr );
	if ( sourceMemory ) vkFreeMemory( device, sourceMemory, nullptr );
	vkDestroyDevice( device, nullptr );
	vkDestroyInstance( instance, nullptr );
	return 0;

cleanup_fail:
	vkDeviceWaitIdle( device );
	if ( fence ) vkDestroyFence( device, fence, nullptr );
	if ( commandPool ) vkDestroyCommandPool( device, commandPool, nullptr );
	if ( destination ) vkDestroyImage( device, destination, nullptr );
	if ( destinationMemory ) vkFreeMemory( device, destinationMemory, nullptr );
	if ( source ) vkDestroyImage( device, source, nullptr );
	if ( sourceMemory ) vkFreeMemory( device, sourceMemory, nullptr );
	vkDestroyDevice( device, nullptr );
	vkDestroyInstance( instance, nullptr );
	return 1;

cleanup_skip:
	vkDeviceWaitIdle( device );
	if ( destination ) vkDestroyImage( device, destination, nullptr );
	if ( destinationMemory ) vkFreeMemory( device, destinationMemory, nullptr );
	if ( source ) vkDestroyImage( device, source, nullptr );
	if ( sourceMemory ) vkFreeMemory( device, sourceMemory, nullptr );
	vkDestroyDevice( device, nullptr );
	vkDestroyInstance( instance, nullptr );
	return 77;
}
