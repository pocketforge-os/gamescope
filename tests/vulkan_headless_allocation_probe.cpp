// Standalone reproducer for gamescope's headless output-image allocation.
// Run with lavapipe; each case differs from "gamescope-exact" by one parameter.
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#define VK_STRUCTURE_TYPE_WSI_IMAGE_CREATE_INFO_MESA (VkStructureType)1000001002
#define VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA (VkStructureType)1000001003

struct wsi_image_create_info
{
	VkStructureType sType;
	const void *pNext;
	bool scanout;
	uint32_t modifier_count;
	const uint64_t *modifiers;
};

struct wsi_memory_allocate_info
{
	VkStructureType sType;
	const void *pNext;
	bool implicit_sync;
};

struct Case
{
	const char *name;
	bool imageScanout;
	bool externalImage;
	bool exportMemory;
	bool dedicatedMemory;
	bool wsiMemory;
};

static bool hasExtension( const std::vector<VkExtensionProperties> &extensions, const char *name )
{
	return std::any_of( extensions.begin(), extensions.end(), [name]( const auto &extension ) {
		return std::strcmp( extension.extensionName, name ) == 0;
	} );
}

static int findMemoryType( const VkPhysicalDeviceMemoryProperties &properties, uint32_t bits )
{
	for ( uint32_t i = 0; i < properties.memoryTypeCount; i++ )
	{
		if ( ( bits & ( 1u << i ) ) &&
			( properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) )
			return int( i );
	}
	return -1;
}

static VkResult runCase( VkDevice device, const VkPhysicalDeviceMemoryProperties &memoryProperties,
	const Case &testCase )
{
	wsi_image_create_info wsiImageInfo = {
		.sType = VK_STRUCTURE_TYPE_WSI_IMAGE_CREATE_INFO_MESA,
		.scanout = VK_TRUE,
	};
	VkExternalMemoryImageCreateInfo externalImageInfo = {
		.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
		.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
	};
	VkImageCreateInfo imageInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
		.extent = { 1280, 720, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	if ( testCase.imageScanout )
		wsiImageInfo.pNext = std::exchange( imageInfo.pNext, &wsiImageInfo );
	if ( testCase.externalImage )
		externalImageInfo.pNext = std::exchange( imageInfo.pNext, &externalImageInfo );

	VkImage image = VK_NULL_HANDLE;
	VkResult result = vkCreateImage( device, &imageInfo, nullptr, &image );
	if ( result != VK_SUCCESS )
	{
		std::printf( "PROBE case=%s stage=create_image result=%d\n", testCase.name, result );
		return result;
	}

	VkMemoryRequirements requirements = {};
	vkGetImageMemoryRequirements( device, image, &requirements );
	const int memoryType = findMemoryType( memoryProperties, requirements.memoryTypeBits );
	if ( memoryType < 0 )
	{
		std::printf( "PROBE case=%s stage=find_memory result=no_device_local bits=0x%x\n",
			testCase.name, requirements.memoryTypeBits );
		vkDestroyImage( device, image, nullptr );
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkMemoryAllocateInfo allocationInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = requirements.size,
		.memoryTypeIndex = uint32_t( memoryType ),
	};
	wsi_memory_allocate_info wsiMemoryInfo = {
		.sType = VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA,
	};
	VkMemoryDedicatedAllocateInfo dedicatedInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
		.image = image,
	};
	VkExportMemoryAllocateInfo exportInfo = {
		.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
		.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
	};
	// Match CVulkanTexture::BInit insertion order. The resulting traversal is
	// export -> dedicated -> wsi, because each insertion prepends the chain.
	if ( testCase.wsiMemory )
		wsiMemoryInfo.pNext = std::exchange( allocationInfo.pNext, &wsiMemoryInfo );
	if ( testCase.dedicatedMemory )
		dedicatedInfo.pNext = std::exchange( allocationInfo.pNext, &dedicatedInfo );
	if ( testCase.exportMemory )
		exportInfo.pNext = std::exchange( allocationInfo.pNext, &exportInfo );

	VkDeviceMemory memory = VK_NULL_HANDLE;
	result = vkAllocateMemory( device, &allocationInfo, nullptr, &memory );
	std::printf(
		"PROBE case=%s image_scanout=%d external_image=%d drm_modifier=none "
		"export_dma_buf=%d dedicated=%d wsi_memory=%d format=%d flags=0x%x "
		"usage=0x%x tiling=%d size=%llu type=%d bits=0x%x result=%d\n",
		testCase.name, testCase.imageScanout, testCase.externalImage,
		testCase.exportMemory, testCase.dedicatedMemory, testCase.wsiMemory,
		VK_FORMAT_A2B10G10R10_UNORM_PACK32, imageInfo.flags, imageInfo.usage,
		imageInfo.tiling, static_cast<unsigned long long>( requirements.size ),
		memoryType, requirements.memoryTypeBits, result );

	if ( memory != VK_NULL_HANDLE )
		vkFreeMemory( device, memory, nullptr );
	vkDestroyImage( device, image, nullptr );
	return result;
}

int main()
{
	VkApplicationInfo applicationInfo = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "gamescope-headless-allocation-probe",
		.applicationVersion = VK_MAKE_VERSION( 1, 0, 0 ),
		.pEngineName = "hopefully not just some code",
		.engineVersion = VK_MAKE_VERSION( 1, 0, 0 ),
		.apiVersion = VK_API_VERSION_1_3,
	};
	VkInstanceCreateInfo instanceInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &applicationInfo,
	};
	VkInstance instance = VK_NULL_HANDLE;
	VkResult result = vkCreateInstance( &instanceInfo, nullptr, &instance );
	if ( result != VK_SUCCESS )
	{
		std::fprintf( stderr, "vkCreateInstance failed: %d\n", result );
		return 2;
	}

	uint32_t physicalDeviceCount = 0;
	vkEnumeratePhysicalDevices( instance, &physicalDeviceCount, nullptr );
	std::vector<VkPhysicalDevice> physicalDevices( physicalDeviceCount );
	vkEnumeratePhysicalDevices( instance, &physicalDeviceCount, physicalDevices.data() );
	if ( physicalDevices.empty() )
	{
		std::fprintf( stderr, "no Vulkan physical device\n" );
		vkDestroyInstance( instance, nullptr );
		return 2;
	}
	VkPhysicalDevice physicalDevice = physicalDevices.front();

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &queueFamilyCount, nullptr );
	std::vector<VkQueueFamilyProperties> queueFamilies( queueFamilyCount );
	vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &queueFamilyCount, queueFamilies.data() );
	uint32_t queueFamily = UINT32_MAX;
	for ( uint32_t i = 0; i < queueFamilyCount; i++ )
	{
		if ( ( queueFamilies[i].queueFlags & ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT ) ) ==
			( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT ) )
		{
			queueFamily = i;
			break;
		}
	}
	if ( queueFamily == UINT32_MAX )
	{
		std::fprintf( stderr, "no general Vulkan queue\n" );
		vkDestroyInstance( instance, nullptr );
		return 2;
	}

	uint32_t extensionCount = 0;
	vkEnumerateDeviceExtensionProperties( physicalDevice, nullptr, &extensionCount, nullptr );
	std::vector<VkExtensionProperties> extensions( extensionCount );
	vkEnumerateDeviceExtensionProperties( physicalDevice, nullptr, &extensionCount, extensions.data() );
	std::vector<const char *> enabledExtensions = {
		VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
		VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
	};
	if ( hasExtension( extensions, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME ) )
		enabledExtensions.push_back( VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME );
	for ( const char *extension : enabledExtensions )
	{
		if ( !hasExtension( extensions, extension ) )
		{
			std::fprintf( stderr, "required device extension missing: %s\n", extension );
			vkDestroyInstance( instance, nullptr );
			return 2;
		}
	}

	VkPhysicalDeviceVulkan12Features supported12 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
	};
	VkPhysicalDeviceFeatures2 supported = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &supported12,
	};
	vkGetPhysicalDeviceFeatures2( physicalDevice, &supported );
	const VkBool32 fp16 = supported12.shaderFloat16 && supported.features.shaderInt16;

	VkPhysicalDeviceVulkan13Features features13 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.dynamicRendering = VK_TRUE,
	};
	VkPhysicalDeviceVulkan12Features features12 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &features13,
		.shaderFloat16 = fp16,
		.scalarBlockLayout = VK_TRUE,
		.timelineSemaphore = VK_TRUE,
	};
	VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcrFeatures = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,
		.pNext = &features12,
		.samplerYcbcrConversion = VK_TRUE,
	};
	VkPhysicalDeviceRobustness2FeaturesEXT robustnessFeatures = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
		.pNext = &ycbcrFeatures,
		.nullDescriptor = VK_TRUE,
	};
	VkPhysicalDeviceFeatures2 features2 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &robustnessFeatures,
		.features = { .shaderInt16 = fp16 },
	};
	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = queueFamily,
		.queueCount = 1,
		.pQueuePriorities = &priority,
	};
	VkDeviceCreateInfo deviceInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &features2,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueInfo,
		.enabledExtensionCount = uint32_t( enabledExtensions.size() ),
		.ppEnabledExtensionNames = enabledExtensions.data(),
	};
	VkDevice device = VK_NULL_HANDLE;
	result = vkCreateDevice( physicalDevice, &deviceInfo, nullptr, &device );
	if ( result != VK_SUCCESS )
	{
		std::fprintf( stderr, "vkCreateDevice failed: %d\n", result );
		vkDestroyInstance( instance, nullptr );
		return 2;
	}

	VkPhysicalDeviceMemoryProperties memoryProperties = {};
	vkGetPhysicalDeviceMemoryProperties( physicalDevice, &memoryProperties );
	const std::array<Case, 7> cases = {{
		{ "gamescope-exact", true, false, true, true, true },
		{ "no-image-scanout", false, false, true, true, true },
		{ "external-image-declared", true, true, true, true, true },
		{ "no-export-memory", true, false, false, true, true },
		{ "no-dedicated-memory", true, false, true, false, true },
		{ "no-wsi-memory", true, false, true, true, false },
		{ "plain-device-local", false, false, false, false, false },
	}};
	for ( const Case &testCase : cases )
		runCase( device, memoryProperties, testCase );

	vkDestroyDevice( device, nullptr );
	vkDestroyInstance( instance, nullptr );
	return 0;
}
