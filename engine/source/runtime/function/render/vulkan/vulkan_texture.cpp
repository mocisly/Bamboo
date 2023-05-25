#define VMA_IMPLEMENTATION
#include "vulkan_texture.h"

namespace Bamboo
{

	void createImageAndView(VkDevice device, VmaAllocator allocator, uint32_t width, uint32_t height, uint32_t mip_levels, VkSampleCountFlagBits num_samples, 
		VkFormat format, VkImageTiling tiling, VkImageUsageFlags image_usage, VmaMemoryUsage memory_usage, VkImageAspectFlags aspect_flags, VmaImageView& vma_image_view)
	{
		createImage(allocator, width, height, mip_levels, num_samples, format, tiling, image_usage, memory_usage, vma_image_view.vma_image);
		vma_image_view.view = createImageView(device, vma_image_view.vma_image.image, format, aspect_flags, mip_levels);
	}

	void createImage(VmaAllocator allocator, uint32_t width, uint32_t height, uint32_t mip_levels, VkSampleCountFlagBits num_samples,
		VkFormat format, VkImageTiling tiling, VkImageUsageFlags image_usage, VmaMemoryUsage memory_usage, VmaImage& image)
	{
		VkImageCreateInfo image_ci{};
		image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_ci.imageType = VK_IMAGE_TYPE_2D;
		image_ci.extent.width = width;
		image_ci.extent.height = height;
		image_ci.extent.depth = 1;
		image_ci.mipLevels = mip_levels;
		image_ci.arrayLayers = 1;
		image_ci.format = format;
		image_ci.tiling = tiling;
		image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_ci.usage = image_usage;
		image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_ci.samples = num_samples;
		image_ci.flags = 0;

		VmaAllocationCreateInfo vma_alloc_ci{};
		vma_alloc_ci.usage = memory_usage;

		image.mip_levels = mip_levels;
		VkResult result = vmaCreateImage(allocator, &image_ci, &vma_alloc_ci, &image.image, &image.allocation, nullptr);
		CHECK_VULKAN_RESULT(result, "vma create image");
	}

	VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels)
	{
		VkImageViewCreateInfo image_view_ci{};
		image_view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_ci.image = image;
		image_view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		image_view_ci.format = format;

		image_view_ci.subresourceRange.aspectMask = aspect_flags;
		image_view_ci.subresourceRange.baseMipLevel = 0;
		image_view_ci.subresourceRange.levelCount = mip_levels;
		image_view_ci.subresourceRange.baseArrayLayer = 0;
		image_view_ci.subresourceRange.layerCount = 1;

		VkImageView image_view;
		vkCreateImageView(device, &image_view_ci, nullptr, &image_view);

		return image_view;
	}

}