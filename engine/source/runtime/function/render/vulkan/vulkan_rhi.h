#pragma once

#include "vulkan_texture.h"

#include <string>
#include <vector>

namespace Bamboo
{
	class VulkanRHI
	{
	public:
		void init();
		void destroy();

	private:
		struct QueueFamilyIndices
		{
			uint32_t graphics;
			uint32_t compute;
			uint32_t transfer;
		};

		struct SwapchainSupportDetails
		{
			VkSurfaceCapabilitiesKHR capabilities;
			std::vector<VkSurfaceFormatKHR> formats;
			std::vector<VkPresentModeKHR> present_modes;
		};

		void createInstance();
		void createDebugging();
		void destroyDebugging();
		void createSurface();
		void pickPhysicalDevice();
		void createLogicDevice();
		void getDeviceQueues();
		void createVmaAllocator();
		void createSwapchain();
		void destroySwapchainObjects();
		void createSwapchainObjects();
		void createCommandPools();
		void createCommandBuffers();
		void createSynchronizationPrimitives();
		void createRenderPass();
		void createPipelineCache();

		void waitFrame();
		void submitFrame();
		void presentFrame();
		void recreateSwapchain();

		std::vector<const char*> getRequiredInstanceExtensions();
		std::vector<const char*> getRequiredInstanceLayers();
		std::vector<const char*> getRequiredDeviceExtensions();
		VkPhysicalDeviceFeatures getRequiredDeviceFeatures();

		QueueFamilyIndices getQueueFamilyIndices(std::vector<VkDeviceQueueCreateInfo>& queue_cis);
		uint32_t getQueueFamilyIndex(VkQueueFlags queue_flags);

		SwapchainSupportDetails getSwapchainSupportDetails();
		VkSurfaceFormatKHR getProperSwapchainSurfaceFormat(const SwapchainSupportDetails& details);
		VkPresentModeKHR getProperSwapchainSurfacePresentMode(const SwapchainSupportDetails& details);
		VkExtent2D getProperSwapchainSurfaceExtent(const SwapchainSupportDetails& details);
		VkImageUsageFlags getProperSwapchainSurfaceImageUsage(const SwapchainSupportDetails& details);
		VkFormat getProperImageFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

		static VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData);

		// vulkan objects
		VkInstance m_instance;
		VkPhysicalDevice m_physical_device;
		VkDevice m_device;
		VkQueue m_graphics_queue;
		VkSurfaceKHR m_surface;
		VmaAllocator m_vma_alloc;
		VkCommandPool m_command_pool;
		VkCommandPool m_transient_command_pool;
		VkSwapchainKHR m_swapchain;
		VkRenderPass m_render_pass;
		VkPipelineCache m_pipeline_cache;

		// debug functions
		PFN_vkCreateDebugUtilsMessengerEXT m_vk_create_debug_func;
		PFN_vkDestroyDebugUtilsMessengerEXT m_vk_destroy_debug_func;
		VkDebugUtilsMessengerEXT m_debug_utils_messenger;

		// required extensions/layers/features of instance/device
		std::vector<const char*> m_required_instance_extensions;
		std::vector<const char*> m_required_instance_layers;
		std::vector<const char*> m_required_device_extensions;
		VkPhysicalDeviceFeatures m_required_device_features;

		// queue families
		QueueFamilyIndices m_queue_family_indices;
		std::vector<VkQueueFamilyProperties> m_queue_family_propertiess;

		// swapchain objects
		VkSurfaceFormatKHR m_surface_format;
		VkPresentModeKHR m_present_mode;
		VkExtent2D m_extent;
		VkFormat m_depth_format;

		uint32_t m_swapchain_image_count;
		std::vector<VkImageView> m_swapchain_image_views;
		VmaImageView m_depth_stencil_image_view;
		std::vector<VkFramebuffer> m_framebuffers;

		// synchronization primitives
		uint32_t m_flight_index;
		uint32_t m_image_index;
		std::vector<VkSemaphore> m_image_avaliable_semaphores;
		std::vector<VkSemaphore> m_render_finished_semaphores;
		std::vector<VkFence> m_flight_fences;
		std::vector<VkCommandBuffer> m_command_buffers;
	};
}