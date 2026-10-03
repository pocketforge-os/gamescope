#include <vulkan/vulkan.h>

#include "cs_output_rotate.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace
{

constexpr uint32_t kLogicalWidth = 1280;
constexpr uint32_t kLogicalHeight = 720;
constexpr uint32_t kNativeWidth = kLogicalHeight;
constexpr uint32_t kNativeHeight = kLogicalWidth;
constexpr uint32_t kSamplerSlots = 16;

uint32_t sourcePixel( uint32_t x, uint32_t y )
{
	// Both coordinates fit without overlap. Every source pixel is unique, and
	// the fixed high byte keeps the byte lanes asymmetric after RGBA sampling.
	return 0xA5000000u | ( y << 11 ) | x;
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

struct Context
{
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t queueFamily = UINT32_MAX;
	VkPhysicalDeviceMemoryProperties memoryProperties = {};
	VkPhysicalDeviceProperties properties = {};

	~Context()
	{
		if ( device )
		{
			vkDeviceWaitIdle( device );
			vkDestroyDevice( device, nullptr );
		}
		if ( instance )
			vkDestroyInstance( instance, nullptr );
	}

	bool init()
	{
		const VkApplicationInfo applicationInfo = {
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "gamescope-output-rotation-test",
			.apiVersion = VK_API_VERSION_1_1,
		};
		const VkInstanceCreateInfo instanceInfo = {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &applicationInfo,
		};
		if ( vkCreateInstance( &instanceInfo, nullptr, &instance ) != VK_SUCCESS )
			return false;

		uint32_t physicalDeviceCount = 0;
		vkEnumeratePhysicalDevices( instance, &physicalDeviceCount, nullptr );
		std::vector<VkPhysicalDevice> physicalDevices( physicalDeviceCount );
		vkEnumeratePhysicalDevices( instance, &physicalDeviceCount, physicalDevices.data() );
		for ( VkPhysicalDevice candidate : physicalDevices )
		{
			VkPhysicalDeviceProperties candidateProperties = {};
			vkGetPhysicalDeviceProperties( candidate, &candidateProperties );
			if ( isSoftwareDevice( candidateProperties ) )
			{
				physicalDevice = candidate;
				properties = candidateProperties;
				break;
			}
		}
		if ( !physicalDevice )
			return false;

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &queueFamilyCount, nullptr );
		std::vector<VkQueueFamilyProperties> queues( queueFamilyCount );
		vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &queueFamilyCount, queues.data() );
		for ( uint32_t i = 0; i < queueFamilyCount; i++ )
		{
			if ( ( queues[i].queueFlags & ( VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT ) ) ==
				( VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT ) )
			{
				queueFamily = i;
				break;
			}
		}
		if ( queueFamily == UINT32_MAX )
			return false;

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
			return false;

		vkGetDeviceQueue( device, queueFamily, 0, &queue );
		vkGetPhysicalDeviceMemoryProperties( physicalDevice, &memoryProperties );
		return true;
	}

	uint32_t findMemoryType( uint32_t allowed, VkMemoryPropertyFlags required ) const
	{
		for ( uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++ )
		{
			if ( ( allowed & ( 1u << i ) ) &&
				( memoryProperties.memoryTypes[i].propertyFlags & required ) == required )
				return i;
		}
		return UINT32_MAX;
	}
};

struct Image
{
	Context *context = nullptr;
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkSubresourceLayout layout = {};

	~Image()
	{
		if ( !context || !context->device )
			return;
		if ( view ) vkDestroyImageView( context->device, view, nullptr );
		if ( image ) vkDestroyImage( context->device, image, nullptr );
		if ( memory ) vkFreeMemory( context->device, memory, nullptr );
	}
};

struct Buffer
{
	Context *context = nullptr;
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;

	~Buffer()
	{
		if ( !context || !context->device )
			return;
		if ( buffer ) vkDestroyBuffer( context->device, buffer, nullptr );
		if ( memory ) vkFreeMemory( context->device, memory, nullptr );
	}
};

struct Commands
{
	Context *context = nullptr;
	VkCommandPool pool = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;

	~Commands()
	{
		if ( !context || !context->device )
			return;
		if ( fence ) vkDestroyFence( context->device, fence, nullptr );
		if ( pool ) vkDestroyCommandPool( context->device, pool, nullptr );
	}
};

struct Pipeline
{
	Context *context = nullptr;
	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkShaderModule shaderModule = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;

	~Pipeline()
	{
		if ( !context || !context->device )
			return;
		if ( sampler ) vkDestroySampler( context->device, sampler, nullptr );
		if ( pipeline ) vkDestroyPipeline( context->device, pipeline, nullptr );
		if ( shaderModule ) vkDestroyShaderModule( context->device, shaderModule, nullptr );
		if ( pipelineLayout ) vkDestroyPipelineLayout( context->device, pipelineLayout, nullptr );
		if ( descriptorPool ) vkDestroyDescriptorPool( context->device, descriptorPool, nullptr );
		if ( descriptorSetLayout ) vkDestroyDescriptorSetLayout( context->device, descriptorSetLayout, nullptr );
	}
};

bool makeImage( Context &context, uint32_t width, uint32_t height,
	VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags memoryFlags,
	Image *result )
{
	result->context = &context;
	const VkImageCreateInfo imageInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = { width, height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = tiling == VK_IMAGE_TILING_LINEAR
			? VK_IMAGE_LAYOUT_PREINITIALIZED : VK_IMAGE_LAYOUT_UNDEFINED,
	};
	if ( vkCreateImage( context.device, &imageInfo, nullptr, &result->image ) != VK_SUCCESS )
		return false;

	VkMemoryRequirements requirements = {};
	vkGetImageMemoryRequirements( context.device, result->image, &requirements );
	const uint32_t memoryType = context.findMemoryType( requirements.memoryTypeBits, memoryFlags );
	if ( memoryType == UINT32_MAX )
		return false;
	const VkMemoryAllocateInfo allocationInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = requirements.size,
		.memoryTypeIndex = memoryType,
	};
	if ( vkAllocateMemory( context.device, &allocationInfo, nullptr, &result->memory ) != VK_SUCCESS ||
		vkBindImageMemory( context.device, result->image, result->memory, 0 ) != VK_SUCCESS )
		return false;

	const VkImageViewCreateInfo viewInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = result->image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
	};
	if ( vkCreateImageView( context.device, &viewInfo, nullptr, &result->view ) != VK_SUCCESS )
		return false;
	if ( tiling == VK_IMAGE_TILING_LINEAR )
	{
		const VkImageSubresource subresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
		vkGetImageSubresourceLayout( context.device, result->image, &subresource, &result->layout );
	}
	return true;
}

bool makeUniformBuffer( Context &context, Buffer *result )
{
	result->context = &context;
	const VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = sizeof( uint32_t ),
		.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	if ( vkCreateBuffer( context.device, &bufferInfo, nullptr, &result->buffer ) != VK_SUCCESS )
		return false;
	VkMemoryRequirements requirements = {};
	vkGetBufferMemoryRequirements( context.device, result->buffer, &requirements );
	const uint32_t memoryType = context.findMemoryType(
		requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT );
	if ( memoryType == UINT32_MAX )
		return false;
	const VkMemoryAllocateInfo allocationInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = requirements.size,
		.memoryTypeIndex = memoryType,
	};
	return vkAllocateMemory( context.device, &allocationInfo, nullptr, &result->memory ) == VK_SUCCESS &&
		vkBindBufferMemory( context.device, result->buffer, result->memory, 0 ) == VK_SUCCESS;
}

bool writeMapped( Context &context, VkDeviceMemory memory, const void *data, size_t size )
{
	void *mapped = nullptr;
	if ( vkMapMemory( context.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped ) != VK_SUCCESS )
		return false;
	std::memcpy( mapped, data, size );
	const VkMappedMemoryRange range = {
		.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
		.memory = memory,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
	};
	const bool success = vkFlushMappedMemoryRanges( context.device, 1, &range ) == VK_SUCCESS;
	vkUnmapMemory( context.device, memory );
	return success;
}

bool writeSourceImage( Context &context, Image &source )
{
	void *mapped = nullptr;
	if ( vkMapMemory( context.device, source.memory, 0, VK_WHOLE_SIZE, 0, &mapped ) != VK_SUCCESS )
		return false;
	for ( uint32_t y = 0; y < kLogicalHeight; y++ )
	{
		uint32_t *row = reinterpret_cast<uint32_t *>(
			static_cast<uint8_t *>( mapped ) + source.layout.offset + y * source.layout.rowPitch );
		for ( uint32_t x = 0; x < kLogicalWidth; x++ )
			row[x] = sourcePixel( x, y );
	}
	const VkMappedMemoryRange range = {
		.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
		.memory = source.memory,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
	};
	const bool success = vkFlushMappedMemoryRanges( context.device, 1, &range ) == VK_SUCCESS;
	vkUnmapMemory( context.device, source.memory );
	return success;
}

bool makePipeline( Context &context, Image &source, Image &rotated, Buffer &uniform,
	Pipeline *result, VkDescriptorSet *descriptorSet )
{
	result->context = &context;
	const std::array<VkDescriptorSetLayoutBinding, 7> bindings = {{
		{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSamplerSlots, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSamplerSlots, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
	}};
	const VkDescriptorSetLayoutCreateInfo setLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = uint32_t( bindings.size() ),
		.pBindings = bindings.data(),
	};
	if ( vkCreateDescriptorSetLayout( context.device, &setLayoutInfo, nullptr,
		&result->descriptorSetLayout ) != VK_SUCCESS )
		return false;

	const VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &result->descriptorSetLayout,
	};
	if ( vkCreatePipelineLayout( context.device, &pipelineLayoutInfo, nullptr,
		&result->pipelineLayout ) != VK_SUCCESS )
		return false;

	const VkShaderModuleCreateInfo shaderInfo = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = sizeof( cs_output_rotate ),
		.pCode = cs_output_rotate,
	};
	if ( vkCreateShaderModule( context.device, &shaderInfo, nullptr,
		&result->shaderModule ) != VK_SUCCESS )
		return false;
	const VkComputePipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = result->shaderModule,
			.pName = "main",
		},
		.layout = result->pipelineLayout,
	};
	if ( vkCreateComputePipelines( context.device, VK_NULL_HANDLE, 1, &pipelineInfo,
		nullptr, &result->pipeline ) != VK_SUCCESS )
		return false;

	const VkSamplerCreateInfo samplerInfo = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	};
	if ( vkCreateSampler( context.device, &samplerInfo, nullptr, &result->sampler ) != VK_SUCCESS )
		return false;

	const std::array<VkDescriptorPoolSize, 3> poolSizes = {{
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSamplerSlots * 2 + 4 },
	}};
	const VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = uint32_t( poolSizes.size() ),
		.pPoolSizes = poolSizes.data(),
	};
	if ( vkCreateDescriptorPool( context.device, &poolInfo, nullptr,
		&result->descriptorPool ) != VK_SUCCESS )
		return false;
	const VkDescriptorSetAllocateInfo allocateInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = result->descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &result->descriptorSetLayout,
	};
	if ( vkAllocateDescriptorSets( context.device, &allocateInfo, descriptorSet ) != VK_SUCCESS )
		return false;

	const VkDescriptorBufferInfo bufferDescriptor = { uniform.buffer, 0, sizeof( uint32_t ) };
	const VkDescriptorImageInfo targetDescriptor = {
		.imageView = rotated.view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};
	std::array<VkDescriptorImageInfo, kSamplerSlots> sourceDescriptors = {};
	for ( VkDescriptorImageInfo &descriptor : sourceDescriptors )
	{
		descriptor.sampler = result->sampler;
		descriptor.imageView = source.view;
		descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	}
	const std::array<VkWriteDescriptorSet, 3> writes = {{
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = *descriptorSet,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &bufferDescriptor,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = *descriptorSet,
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &targetDescriptor,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = *descriptorSet,
			.dstBinding = 3,
			.descriptorCount = uint32_t( sourceDescriptors.size() ),
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = sourceDescriptors.data(),
		},
	}};
	vkUpdateDescriptorSets( context.device, uint32_t( writes.size() ), writes.data(), 0, nullptr );
	return true;
}

bool recordAndSubmit( Context &context, Image &source, Image &rotated, Image &scanout,
	Pipeline &pipeline, VkDescriptorSet descriptorSet )
{
	Commands commands{ .context = &context };
	const VkCommandPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = context.queueFamily,
	};
	if ( vkCreateCommandPool( context.device, &poolInfo, nullptr, &commands.pool ) != VK_SUCCESS )
		return false;
	const VkCommandBufferAllocateInfo allocationInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = commands.pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	if ( vkAllocateCommandBuffers( context.device, &allocationInfo, &commandBuffer ) != VK_SUCCESS )
		return false;
	const VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	if ( vkBeginCommandBuffer( commandBuffer, &beginInfo ) != VK_SUCCESS )
		return false;

	const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	const std::array<VkImageMemoryBarrier, 3> initialBarriers = {{
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = source.image,
			.subresourceRange = range,
		},
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = rotated.image,
			.subresourceRange = range,
		},
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = scanout.image,
			.subresourceRange = range,
		},
	}};
	vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_HOST_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, uint32_t( initialBarriers.size() ), initialBarriers.data() );

	vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline );
	vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline.pipelineLayout, 0, 1, &descriptorSet, 0, nullptr );
	vkCmdDispatch( commandBuffer, ( kNativeWidth + 7 ) / 8,
		( kNativeHeight + 7 ) / 8, 1 );

	const VkImageMemoryBarrier rotationBarrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = rotated.image,
		.subresourceRange = range,
	};
	vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &rotationBarrier );
	const VkImageCopy copy = {
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.extent = { kNativeWidth, kNativeHeight, 1 },
	};
	vkCmdCopyImage( commandBuffer, rotated.image, VK_IMAGE_LAYOUT_GENERAL,
		scanout.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy );
	const VkImageMemoryBarrier hostBarrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_HOST_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = scanout.image,
		.subresourceRange = range,
	};
	vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 0, nullptr, 1, &hostBarrier );
	if ( vkEndCommandBuffer( commandBuffer ) != VK_SUCCESS )
		return false;

	const VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	if ( vkCreateFence( context.device, &fenceInfo, nullptr, &commands.fence ) != VK_SUCCESS )
		return false;
	const VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &commandBuffer,
	};
	return vkQueueSubmit( context.queue, 1, &submitInfo, commands.fence ) == VK_SUCCESS &&
		vkWaitForFences( context.device, 1, &commands.fence, VK_TRUE, UINT64_MAX ) == VK_SUCCESS;
}

bool checkScanout( Context &context, Image &scanout, uint32_t transform )
{
	if ( scanout.layout.rowPitch < kNativeWidth * sizeof( uint32_t ) )
		return false;
	void *mapped = nullptr;
	if ( vkMapMemory( context.device, scanout.memory, 0, VK_WHOLE_SIZE, 0, &mapped ) != VK_SUCCESS )
		return false;
	const VkMappedMemoryRange range = {
		.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
		.memory = scanout.memory,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
	};
	bool matches = vkInvalidateMappedMemoryRanges( context.device, 1, &range ) == VK_SUCCESS;
	for ( uint32_t y = 0; matches && y < kNativeHeight; y++ )
	{
		const uint32_t *row = reinterpret_cast<const uint32_t *>(
			static_cast<const uint8_t *>( mapped ) +
			scanout.layout.offset + y * scanout.layout.rowPitch );
		for ( uint32_t x = 0; matches && x < kNativeWidth; x++ )
		{
			const uint32_t logicalX = transform == 1 ? y : kLogicalWidth - 1 - y;
			const uint32_t logicalY = transform == 1 ? kLogicalHeight - 1 - x : x;
			matches = row[x] == sourcePixel( logicalX, logicalY );
		}
	}
	vkUnmapMemory( context.device, scanout.memory );
	return matches;
}

bool runTransform( Context &context, uint32_t transform )
{
	Image source;
	Image rotated;
	Image scanout;
	Buffer uniform;
	if ( !makeImage( context, kLogicalWidth, kLogicalHeight, VK_IMAGE_TILING_LINEAR,
			VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &source ) ||
		!makeImage( context, kNativeWidth, kNativeHeight, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &rotated ) ||
		!makeImage( context, kNativeWidth, kNativeHeight, VK_IMAGE_TILING_LINEAR,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &scanout ) ||
		!makeUniformBuffer( context, &uniform ) ||
		!writeSourceImage( context, source ) ||
		!writeMapped( context, uniform.memory, &transform, sizeof( transform ) ) )
		return false;

	Pipeline pipeline;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	if ( !makePipeline( context, source, rotated, uniform, &pipeline, &descriptorSet ) ||
		!recordAndSubmit( context, source, rotated, scanout, pipeline, descriptorSet ) )
		return false;
	return checkScanout( context, scanout, transform );
}

} // namespace

int main()
{
	Context context;
	if ( !context.init() )
	{
		std::fprintf( stderr, "SKIP: no software Vulkan compute device\n" );
		return 77;
	}

	VkFormatProperties formatProperties = {};
	vkGetPhysicalDeviceFormatProperties( context.physicalDevice,
		VK_FORMAT_R8G8B8A8_UNORM, &formatProperties );
	const VkFormatFeatureFlags optimalNeeded =
		VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
	const VkFormatFeatureFlags linearNeeded =
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if ( ( formatProperties.optimalTilingFeatures & optimalNeeded ) != optimalNeeded ||
		( formatProperties.linearTilingFeatures & linearNeeded ) != linearNeeded )
	{
		std::fprintf( stderr, "SKIP: software Vulkan device lacks rotation/staging image features\n" );
		return 77;
	}

	if ( !runTransform( context, 1 ) )
	{
		std::fprintf( stderr, "FAIL: Rotate90 shader/staging output mismatch\n" );
		return 1;
	}
	if ( !runTransform( context, 2 ) )
	{
		std::fprintf( stderr, "FAIL: Rotate270 shader/staging output mismatch\n" );
		return 1;
	}

	std::fprintf( stderr,
		"PASS: %s pixel-correct 1280x720 to 720x1280 90/270 rotation and linear staging\n",
		context.properties.deviceName );
	return 0;
}
