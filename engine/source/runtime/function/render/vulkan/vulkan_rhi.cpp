#include "vulkan_rhi.h"
#include "runtime/function/render/window_system.h"

#include <array>
#include <algorithm>

#define ENABLE_VALIDATION_LAYER DEBUG
#define MAX_FRAMES_IN_FLIGHT 2

namespace Bamboo
{
	void VulkanRHI::init()
	{
		createInstance();
#if ENABLE_VALIDATION_LAYER
		createDebugging();
#endif
		createSurface();
		pickPhysicalDevice();
		createLogicDevice();
		getDeviceQueues();
		createVmaAllocator();

		createSwapchain();
		createSwapchainObjects();
		createCommandPools();
		createCommandBuffers();
		createSynchronizationPrimitives();
	}

	void VulkanRHI::destroy()
	{
		vkDestroyPipelineCache(m_device, m_pipeline_cache, nullptr);
		vkDestroyRenderPass(m_device, m_render_pass, nullptr);
		for (VkFramebuffer framebuffer : m_framebuffers)
		{
			vkDestroyFramebuffer(m_device, framebuffer, nullptr);
		}
		for (VkSemaphore image_avaliable_semaphore : m_image_avaliable_semaphores)
		{
			vkDestroySemaphore(m_device, image_avaliable_semaphore, nullptr);
		}
		for (VkSemaphore render_finished_semaphore : m_render_finished_semaphores)
		{
			vkDestroySemaphore(m_device, render_finished_semaphore, nullptr);
		}
		for (VkFence flight_fence : m_flight_fences)
		{
			vkDestroyFence(m_device, flight_fence, nullptr);
		}

		vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
		destroySwapchainObjects();
		vkDestroyCommandPool(m_device, m_transient_command_pool, nullptr);
		vkDestroyCommandPool(m_device, m_command_pool, nullptr);

#if ENABLE_VALIDATION_LAYER
		destroyDebugging();
#endif
		vmaDestroyAllocator(m_vma_alloc);
		vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
		vkDestroyDevice(m_device, nullptr);
		vkDestroyInstance(m_instance, nullptr);
	}

	void VulkanRHI::createInstance()
	{
		// set vulkan application info
		VkApplicationInfo app_info{};
		app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app_info.pApplicationName = APP_NAME;
		app_info.pEngineName = APP_NAME;
		app_info.apiVersion = VK_API_VERSION_1_3;
		app_info.applicationVersion = VK_MAKE_API_VERSION(0, APP_MAJOR_VERSION, APP_MINOR_VERSION, APP_PATCH_VERSION);
		app_info.engineVersion = app_info.applicationVersion;

		// set instance create info
		m_required_instance_extensions = getRequiredInstanceExtensions();
		m_required_instance_layers = getRequiredInstanceLayers();

		VkInstanceCreateInfo instance_ci{};
		instance_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instance_ci.pApplicationInfo = &app_info;
		instance_ci.enabledExtensionCount = static_cast<uint32_t>(m_required_instance_extensions.size());
		instance_ci.ppEnabledExtensionNames = m_required_instance_extensions.data();
		instance_ci.enabledLayerCount = static_cast<uint32_t>(m_required_instance_layers.size());
		instance_ci.ppEnabledLayerNames = m_required_instance_layers.data();

		// create instance
		VkResult result = vkCreateInstance(&instance_ci, nullptr, &m_instance);
		CHECK_VULKAN_RESULT(result, "create instance");
	}

	void VulkanRHI::createDebugging()
	{
		m_vk_create_debug_func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
		m_vk_destroy_debug_func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));

		VkDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_ci{};
		debug_utils_messenger_ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debug_utils_messenger_ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debug_utils_messenger_ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;
		debug_utils_messenger_ci.pfnUserCallback = debugUtilsMessengerCallback;
		VkResult result = m_vk_create_debug_func(m_instance, &debug_utils_messenger_ci, nullptr, &m_debug_utils_messenger);
		CHECK_VULKAN_RESULT(result, "create debug utils messenger");
	}

	void VulkanRHI::destroyDebugging()
	{
		m_vk_destroy_debug_func(m_instance, m_debug_utils_messenger, nullptr);
	}

	void VulkanRHI::createSurface()
	{
		GLFWwindow* window = g_runtime_context.windowSystem()->getWindow();
		VkResult result = glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface);
		CHECK_VULKAN_RESULT(result, "create window surface");
	}

	void VulkanRHI::pickPhysicalDevice()
	{
		uint32_t gpu_count = 0;
		vkEnumeratePhysicalDevices(m_instance, &gpu_count, nullptr);
		ASSERT(gpu_count > 0, "failed to find a vulkan compatiable physical device");

		std::vector<VkPhysicalDevice> physical_devices(gpu_count);
		vkEnumeratePhysicalDevices(m_instance, &gpu_count, physical_devices.data());

		std::vector<VkPhysicalDevice> discrete_physical_devices;
		for (uint32_t i = 0; i < gpu_count; ++i)
		{
			VkPhysicalDeviceProperties physical_device_properties;
			vkGetPhysicalDeviceProperties(physical_devices[i], &physical_device_properties);
			LOG_INFO("device[{}]: {} {} {}.{}.{}", 
				i, physical_device_properties.deviceName, 
				vkPhysicalDeviceTypeString(physical_device_properties.deviceType),
				physical_device_properties.apiVersion >> 22,
				(physical_device_properties.apiVersion >> 12) & 0x3ff,
				physical_device_properties.apiVersion & 0xfff);

			// only use discrete gpu, for best performance
			if (physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				discrete_physical_devices.push_back(physical_devices[i]);
			}
		}

		// set the selected device index
		uint32_t selected_device_index = 0;
		if (selected_device_index >= discrete_physical_devices.size())
		{
			LOG_FATAL("selected device index {} is out of range {}", selected_device_index, discrete_physical_devices.size());
		}
		m_physical_device = discrete_physical_devices[selected_device_index];
	}

	void VulkanRHI::createLogicDevice()
	{
		m_required_device_extensions = getRequiredDeviceExtensions();
		m_required_device_features = getRequiredDeviceFeatures();
		std::vector<VkDeviceQueueCreateInfo> queue_cis;
		m_queue_family_indices = getQueueFamilyIndices(queue_cis);

		VkDeviceCreateInfo device_ci{};
		device_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		device_ci.queueCreateInfoCount = static_cast<uint32_t>(queue_cis.size());
		device_ci.pQueueCreateInfos = queue_cis.data();
		device_ci.pEnabledFeatures = &m_required_device_features;
		device_ci.enabledExtensionCount = static_cast<uint32_t>(m_required_device_extensions.size());
		device_ci.ppEnabledExtensionNames = m_required_device_extensions.data();

		VkResult result = vkCreateDevice(m_physical_device, &device_ci, nullptr, &m_device);
		CHECK_VULKAN_RESULT(result, "create device");
	}

	void VulkanRHI::getDeviceQueues()
	{
		// get graphics queue
		vkGetDeviceQueue(m_device, m_queue_family_indices.graphics, 0, &m_graphics_queue);
	}

	void VulkanRHI::createVmaAllocator()
	{
		VmaAllocatorCreateInfo vma_alloc_ci{};
		vma_alloc_ci.vulkanApiVersion = VK_API_VERSION_1_3;
		vma_alloc_ci.instance = m_instance;
		vma_alloc_ci.physicalDevice = m_physical_device;
		vma_alloc_ci.device = m_device;
		vma_alloc_ci.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;

		VkResult result = vmaCreateAllocator(&vma_alloc_ci, &m_vma_alloc);
		CHECK_VULKAN_RESULT(result, "create vma allocator");
	}

	void VulkanRHI::createSwapchain()
	{
		SwapchainSupportDetails swapchain_support_details = getSwapchainSupportDetails();
		m_surface_format = getProperSwapchainSurfaceFormat(swapchain_support_details);
		m_present_mode = getProperSwapchainSurfacePresentMode(swapchain_support_details);
		m_extent = getProperSwapchainSurfaceExtent(swapchain_support_details);
		VkImageUsageFlags image_usage = getProperSwapchainSurfaceImageUsage(swapchain_support_details);

		uint32_t image_count = std::min(swapchain_support_details.capabilities.minImageCount + 1, 
			swapchain_support_details.capabilities.maxImageCount);

		VkSwapchainCreateInfoKHR swapchain_ci{};
		swapchain_ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapchain_ci.surface = m_surface;
		swapchain_ci.minImageCount = image_count;
		swapchain_ci.imageFormat = m_surface_format.format;
		swapchain_ci.imageColorSpace = m_surface_format.colorSpace;
		swapchain_ci.imageExtent = m_extent;
		swapchain_ci.imageArrayLayers = 1;
		swapchain_ci.imageUsage = image_usage;
		swapchain_ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapchain_ci.preTransform = swapchain_support_details.capabilities.currentTransform;
		swapchain_ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		swapchain_ci.presentMode = m_present_mode;
		swapchain_ci.clipped = VK_TRUE;
		swapchain_ci.oldSwapchain = m_swapchain;

		VkResult result = vkCreateSwapchainKHR(m_device, &swapchain_ci, nullptr, &m_swapchain);
		CHECK_VULKAN_RESULT(result, "create swapchain");
	}

	void VulkanRHI::destroySwapchainObjects()
	{
		// 1.destroy swapchain image views
		for (VkImageView swapchain_image_view : m_swapchain_image_views)
		{
			vkDestroyImageView(m_device, swapchain_image_view, nullptr);
		}

		// 2.destroy depth stencil image and view
		m_depth_stencil_image_view.destroy(m_device, m_vma_alloc);

		// 3.destroy framebuffers
		for (VkFramebuffer framebuffer : m_framebuffers)
		{
			vkDestroyFramebuffer(m_device, framebuffer, nullptr);
		}
	}

	void VulkanRHI::createSwapchainObjects()
	{
		// 1.get swapchain images
		uint32_t last_swapchain_image_count = m_swapchain_image_count;
		vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_swapchain_image_count, nullptr);
		ASSERT(last_swapchain_image_count == 0 || last_swapchain_image_count == m_swapchain_image_count,
			"swapchain image count shouldn't change");

		std::vector<VkImage> swapchain_images(m_swapchain_image_count);
		vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_swapchain_image_count, swapchain_images.data());

		// create swapchain image views
		m_swapchain_image_views.resize(m_swapchain_image_count);
		for (uint32_t i = 0; i < m_swapchain_image_count; ++i)
		{
			m_swapchain_image_views[i] = createImageView(m_device, swapchain_images[i], m_surface_format.format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
		}

		// 2.create depth stencil image and view
		std::vector<VkFormat> depth_format_candidates = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
		VkImageTiling depth_tiling = VK_IMAGE_TILING_OPTIMAL;
		VkFormatFeatureFlags depth_features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
		VkImageUsageFlags depth_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		m_depth_format = getProperImageFormat(depth_format_candidates, depth_tiling, depth_features);
		createImageAndView(m_device, m_vma_alloc, m_extent.width, m_extent.height, 1, VK_SAMPLE_COUNT_1_BIT, m_depth_format, depth_tiling, depth_usage,
			VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_IMAGE_ASPECT_DEPTH_BIT, m_depth_stencil_image_view);

		// 3.create framebuffers
		std::array<VkImageView, 2> attachments{};

		// depth stenci attachment is same for all framebuffers
		attachments[1] = m_depth_stencil_image_view.view;

		VkFramebufferCreateInfo framebuffer_ci{};
		framebuffer_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_ci.renderPass = m_render_pass;
		framebuffer_ci.attachmentCount = static_cast<uint32_t>(attachments.size());
		framebuffer_ci.pAttachments = attachments.data();
		framebuffer_ci.width = m_extent.width;
		framebuffer_ci.height = m_extent.height;
		framebuffer_ci.layers = 1;

		// create frame buffer for each swap chain image
		m_framebuffers.resize(m_swapchain_image_count);
		for (uint32_t i = 0; i < m_swapchain_image_count; ++i)
		{
			attachments[0] = m_swapchain_image_views[i];
			vkCreateFramebuffer(m_device, &framebuffer_ci, nullptr, &m_framebuffers[i]);
		}
	}

	void VulkanRHI::createCommandPools()
	{
		VkCommandPoolCreateInfo command_pool_ci = {};
		command_pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		command_pool_ci.queueFamilyIndex = m_queue_family_indices.graphics;
		command_pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

		vkCreateCommandPool(m_device, &command_pool_ci, nullptr, &m_command_pool);

		command_pool_ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		vkCreateCommandPool(m_device, &command_pool_ci, nullptr, &m_transient_command_pool);
	}

	void VulkanRHI::createCommandBuffers()
	{
		m_command_buffers.resize(MAX_FRAMES_IN_FLIGHT);

		VkCommandBufferAllocateInfo command_buffer_ai{};
		command_buffer_ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		command_buffer_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		command_buffer_ai.commandPool = m_command_pool;
		command_buffer_ai.commandBufferCount = static_cast<uint32_t>(m_command_buffers.size());

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			VkCommandBuffer command_buffer;
			vkAllocateCommandBuffers(m_device, &command_buffer_ai, m_command_buffers.data());
		}
	}

	void VulkanRHI::createSynchronizationPrimitives()
	{
		m_flight_index = 0;

		// semaphore: GPU-GPU
		m_image_avaliable_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
		m_render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);

		// fence: CPU-GPU
		m_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

		VkSemaphoreCreateInfo semaphore_ci{};
		semaphore_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fence_ci{};
		fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkCreateSemaphore(m_device, &semaphore_ci, nullptr, &m_image_avaliable_semaphores[i]);
			vkCreateSemaphore(m_device, &semaphore_ci, nullptr, &m_render_finished_semaphores[i]);
			vkCreateFence(m_device, &fence_ci, nullptr, &m_flight_fences[i]);
		}
	}

	void VulkanRHI::createRenderPass()
	{
		std::array<VkAttachmentDescription, 2> attachments{};

		// color attachment
		attachments[0].format = m_surface_format.format;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// depth attachment
		attachments[1].format = m_depth_format;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference color_reference{};
		color_reference.attachment = 0;
		color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depth_reference{};
		depth_reference.attachment = 1;
		depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		// subpass
		VkSubpassDescription subpass_desc{};
		subpass_desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass_desc.colorAttachmentCount = 1;
		subpass_desc.pColorAttachments = &color_reference;
		subpass_desc.pDepthStencilAttachment = &depth_reference;
		subpass_desc.inputAttachmentCount = 0;
		subpass_desc.pInputAttachments = nullptr;
		subpass_desc.preserveAttachmentCount = 0;
		subpass_desc.pPreserveAttachments = nullptr;
		subpass_desc.pResolveAttachments = nullptr;

		// subpass dependencies
		std::array<VkSubpassDependency, 2> dependencies{};
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		dependencies[0].dependencyFlags = 0;

		dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].dstSubpass = 0;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].srcAccessMask = 0;
		dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		dependencies[1].dependencyFlags = 0;

		// create render pass
		VkRenderPassCreateInfo render_pass_ci{};
		render_pass_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_ci.attachmentCount = static_cast<uint32_t>(attachments.size());
		render_pass_ci.pAttachments = attachments.data();
		render_pass_ci.subpassCount = 1;
		render_pass_ci.pSubpasses = &subpass_desc;
		render_pass_ci.dependencyCount = static_cast<uint32_t>(dependencies.size());
		render_pass_ci.pDependencies = dependencies.data();

		vkCreateRenderPass(m_device, &render_pass_ci, nullptr, &m_render_pass);
	}

	void VulkanRHI::createPipelineCache()
	{
		VkPipelineCacheCreateInfo pipeline_cache_ci{};
		pipeline_cache_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		vkCreatePipelineCache(m_device, &pipeline_cache_ci, nullptr, &m_pipeline_cache);
	}

	void VulkanRHI::waitFrame()
	{
		// wait sumbitted command buffer finished
		vkWaitForFences(m_device, 1, &m_flight_fences[m_flight_index], VK_TRUE, UINT64_MAX);

		// get free swapchain image
		VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_image_avaliable_semaphores[m_flight_index], VK_NULL_HANDLE, &m_image_index);
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			recreateSwapchain();
			return;
		}
		ASSERT(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR, "failed to acquire swapchain image!");
	}

	void VulkanRHI::submitFrame()
	{
		VkSubmitInfo submit_info{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &m_image_avaliable_semaphores[m_flight_index];
		VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submit_info.pWaitDstStageMask = wait_stages;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &m_command_buffers[m_flight_index];
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &m_render_finished_semaphores[m_flight_index];

		VkResult result = vkQueueSubmit(m_graphics_queue, 1, &submit_info, m_flight_fences[m_flight_index]);
		CHECK_VULKAN_RESULT(result, "submit queue");
	}

	void VulkanRHI::presentFrame()
	{
		VkPresentInfoKHR present_info{};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = &m_render_finished_semaphores[m_flight_index];
		present_info.swapchainCount = 1;
		present_info.pSwapchains = &m_swapchain;
		present_info.pImageIndices = &m_image_index;

		VkResult result = vkQueuePresentKHR(m_graphics_queue, &present_info);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		{
			recreateSwapchain();
		}
		else
		{
			CHECK_VULKAN_RESULT(result, "present swapchain image");
		}

		m_flight_index = (m_flight_index + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	void VulkanRHI::recreateSwapchain()
	{
		// handle the window minimization corner case
		int width = 0;
		int height = 0;
		GLFWwindow* window = g_runtime_context.windowSystem()->getWindow();
		glfwGetFramebufferSize(window, &width, &height);
		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(window, &width, &height);
			glfwWaitEvents();
		}

		// ensure all device operations have done
		vkDeviceWaitIdle(m_device);

		VkSwapchainKHR oldSwapchain = m_swapchain;
		createSwapchain();
		vkDestroySwapchainKHR(m_device, oldSwapchain, nullptr);

		destroySwapchainObjects();
		createSwapchainObjects();
	}

	std::vector<const char*> VulkanRHI::getRequiredInstanceExtensions()
	{
		// find all supported instance extensions
		uint32_t supported_instance_extension_count = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &supported_instance_extension_count, nullptr);
		std::vector<VkExtensionProperties> supported_extension_propertiess(supported_instance_extension_count);
		vkEnumerateInstanceExtensionProperties(nullptr, &supported_instance_extension_count, supported_extension_propertiess.data());
		std::vector<std::string> supported_instance_extensions;
		for (const VkExtensionProperties& extension_properties : supported_extension_propertiess)
		{
			supported_instance_extensions.push_back(extension_properties.extensionName);
		}

		// find glfw instance extensions
		uint32_t glfw_instance_extension_count = 0;
		const char** glfw_instance_extensions = glfwGetRequiredInstanceExtensions(&glfw_instance_extension_count);
		std::vector<const char*> required_instance_extensions(glfw_instance_extensions, glfw_instance_extensions + glfw_instance_extension_count);

		// if enable validation layer, add some extra debug extension
#if ENABLE_VALIDATION_LAYER
		required_instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
		// add additional extensions
		// TODO

		// check if each required instance extension is supported
		for (const char* required_instance_extension : required_instance_extensions)
		{
			if (std::find(supported_instance_extensions.begin(), supported_instance_extensions.end(),
				required_instance_extension) == supported_instance_extensions.end())
			{
				LOG_FATAL("required instance extension {} is not supported", required_instance_extension);
			}
		}

		return required_instance_extensions;
	}

	std::vector<const char*> VulkanRHI::getRequiredInstanceLayers()
	{
		// find all supported instance layers
		uint32_t supported_instance_layer_count = 0;
		vkEnumerateInstanceLayerProperties(&supported_instance_layer_count, nullptr);
		std::vector<VkLayerProperties> supported_layer_propertiess(supported_instance_layer_count);
		vkEnumerateInstanceLayerProperties(&supported_instance_layer_count, supported_layer_propertiess.data());
		std::vector<std::string> supported_instance_layers;
		for (const VkLayerProperties& supported_layer_properties : supported_layer_propertiess)
		{
			supported_instance_layers.push_back(supported_layer_properties.layerName);
		}

		// set required instance layer
		std::vector<const char*> required_instance_layers;
#if ENABLE_VALIDATION_LAYER
		required_instance_layers.push_back("VK_LAYER_KHRONOS_validation");
#endif
	
		// check if each required instance layer is supported
		for (const char* required_instance_layer : required_instance_layers)
		{
			if (std::find(supported_instance_layers.begin(), supported_instance_layers.end(),
				required_instance_layer) == supported_instance_layers.end())
			{
				LOG_FATAL("required instance layer {} is not supported", required_instance_layer);
			}
		}

		return required_instance_layers;
	}

	std::vector<const char*> VulkanRHI::getRequiredDeviceExtensions()
	{
		// find all supported device extensions
		uint32_t supported_device_extension_count = 0;
		vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &supported_device_extension_count, nullptr);
		std::vector<VkExtensionProperties> supported_extension_propertiess(supported_device_extension_count);
		vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &supported_device_extension_count, supported_extension_propertiess.data());
		std::vector<std::string> supported_device_extensions;
		for (const VkExtensionProperties& extension_properties : supported_extension_propertiess)
		{
			supported_device_extensions.push_back(extension_properties.extensionName);
		}

		// set required device extensions
		std::vector<const char*> required_device_extensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		};

		// check if each required device extension is supported
		for (const char* required_device_extension : required_device_extensions)
		{
			if (std::find(supported_device_extensions.begin(), supported_device_extensions.end(),
				required_device_extension) == supported_device_extensions.end())
			{
				LOG_FATAL("required device extension {} is not supported", required_device_extension);
			}
		}

		return required_device_extensions;
	}

	VkPhysicalDeviceFeatures VulkanRHI::getRequiredDeviceFeatures()
	{
		VkPhysicalDeviceFeatures supported_device_features{};
		vkGetPhysicalDeviceFeatures(m_physical_device, &supported_device_features);

		// set required device features
		VkPhysicalDeviceFeatures required_device_features{};
		if (supported_device_features.sampleRateShading)
		{
			required_device_features.sampleRateShading = VK_TRUE;
		}

		if (supported_device_features.samplerAnisotropy)
		{
			required_device_features.samplerAnisotropy = VK_TRUE;
		}

		if (supported_device_features.geometryShader)
		{
			required_device_features.geometryShader = VK_TRUE;
		}

		if (supported_device_features.fillModeNonSolid)
		{
			required_device_features.fillModeNonSolid = VK_TRUE;
		}

		return required_device_features;
	}

	VulkanRHI::QueueFamilyIndices VulkanRHI::getQueueFamilyIndices(std::vector<VkDeviceQueueCreateInfo>& queue_cis)
	{
		// get physical device queue family properties
		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_count, nullptr);
		ASSERT(queue_family_count > 0, "no supported physical device queue family");
		m_queue_family_propertiess.resize(queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_count, m_queue_family_propertiess.data());

		// create device queue create infos
		VulkanRHI::QueueFamilyIndices queue_family_indices{};
		VkQueueFlags required_queue_types = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
		const float k_default_queue_priority = 0.0f;
		queue_cis.clear();

		// graphics queue
		if (required_queue_types & VK_QUEUE_GRAPHICS_BIT)
		{
			queue_family_indices.graphics = getQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT);
			VkDeviceQueueCreateInfo queue_ci{};
			queue_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_ci.queueFamilyIndex = queue_family_indices.graphics;
			queue_ci.queueCount = 1;
			queue_ci.pQueuePriorities = &k_default_queue_priority;
			queue_cis.push_back(queue_ci);

			// ensure the graphic queue family must support presentation
			VkBool32 is_present_support = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, queue_family_indices.graphics, m_surface, &is_present_support);
			ASSERT(is_present_support, "graphic queue family doesn't support presentation");
		}
		else
		{
			queue_family_indices.graphics = 0;
		}

		// dedicated compute queue
		if (required_queue_types & VK_QUEUE_COMPUTE_BIT)
		{
			queue_family_indices.compute = getQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT);
			if (queue_family_indices.compute != queue_family_indices.graphics)
			{
				// if compute family index differs, we need an additional queue create info for the compute queue
				VkDeviceQueueCreateInfo queue_ci{};
				queue_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queue_ci.queueFamilyIndex = queue_family_indices.compute;
				queue_ci.queueCount = 1;
				queue_ci.pQueuePriorities = &k_default_queue_priority;
				queue_cis.push_back(queue_ci);
			}
		}
		else
		{
			// else we use the same queue
			queue_family_indices.compute = queue_family_indices.graphics;
		}

		// dedicated transfer queue
		if (required_queue_types & VK_QUEUE_TRANSFER_BIT)
		{
			queue_family_indices.transfer = getQueueFamilyIndex(VK_QUEUE_TRANSFER_BIT);
			if ((queue_family_indices.transfer != queue_family_indices.graphics) && (queue_family_indices.transfer != queue_family_indices.compute))
			{
				// if transfer family index differs, we need an additional queue create info for the transfer queue
				VkDeviceQueueCreateInfo queue_ci{};
				queue_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queue_ci.queueFamilyIndex = queue_family_indices.transfer;
				queue_ci.queueCount = 1;
				queue_ci.pQueuePriorities = &k_default_queue_priority;
				queue_cis.push_back(queue_ci);
			}
		}
		else
		{
			// else we use the same queue
			queue_family_indices.transfer = queue_family_indices.graphics;
		}

		return queue_family_indices;
	}

	uint32_t VulkanRHI::getQueueFamilyIndex(VkQueueFlags queue_flags)
	{
		// find a queue only for compute, not for graphics
		if ((queue_flags & VK_QUEUE_COMPUTE_BIT) == queue_flags)
		{
			for (size_t i = 0; i < m_queue_family_propertiess.size(); ++i)
			{
				if ((m_queue_family_propertiess[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
					!(m_queue_family_propertiess[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
				{
					return i;
				}
			}
		}

		// find a queue only for transfer, not for graphics and compute
		if ((queue_flags & VK_QUEUE_TRANSFER_BIT) == queue_flags)
		{
			for (size_t i = 0; i < m_queue_family_propertiess.size(); ++i)
			{
				if ((m_queue_family_propertiess[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
					!(m_queue_family_propertiess[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
					!(m_queue_family_propertiess[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
				{
					return i;
				}
			}
		}

		// for other queue types, return the first one that support the requested flags
		for (size_t i = 0; i < m_queue_family_propertiess.size(); ++i)
		{
			if ((m_queue_family_propertiess[i].queueFlags & queue_flags) == queue_flags)
			{
				return i;
			}
		}

		LOG_FATAL("failed to find a matching queue family index");
		return -1;
	}

	VulkanRHI::SwapchainSupportDetails VulkanRHI::getSwapchainSupportDetails()
	{
		VulkanRHI::SwapchainSupportDetails details;

		// surface capabilities
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &details.capabilities);

		// surface formats
		uint32_t format_count;
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &format_count, nullptr);
		ASSERT(format_count > 0, "no supported surface formats");
		details.formats.resize(format_count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &format_count, details.formats.data());

		// surface present modes
		uint32_t present_mode_count;
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical_device, m_surface, &present_mode_count, nullptr);
		ASSERT(present_mode_count > 0, "no supported surface present modes");
		details.present_modes.resize(present_mode_count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical_device, m_surface, &present_mode_count, details.present_modes.data());

		return details;
	}

	VkSurfaceFormatKHR VulkanRHI::getProperSwapchainSurfaceFormat(const SwapchainSupportDetails& details)
	{
		for (VkSurfaceFormatKHR format : details.formats)
		{
			if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
				format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				return format;
			}
		}
		LOG_FATAL("no supported swapchain surface format: VK_FORMAT_B8G8R8A8_SRGB");
		return details.formats.front();
	}

	VkPresentModeKHR VulkanRHI::getProperSwapchainSurfacePresentMode(const SwapchainSupportDetails& details)
	{
		for (VkPresentModeKHR present_mode : details.present_modes)
		{
			if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				return present_mode;
			}
		}
		LOG_FATAL("no supported swapchain surface present mode: VK_PRESENT_MODE_MAILBOX_KHR");
		return details.present_modes.front();
	}

	VkExtent2D VulkanRHI::getProperSwapchainSurfaceExtent(const SwapchainSupportDetails& details)
	{
		if (details.capabilities.currentExtent.width != UINT32_MAX)
		{
			return details.capabilities.currentExtent;
		}

		int width, height;
		GLFWwindow* window = g_runtime_context.windowSystem()->getWindow();
		glfwGetFramebufferSize(window, &width, &height);

		VkExtent2D actual_extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
		actual_extent.width = std::clamp(actual_extent.width, details.capabilities.minImageExtent.width, details.capabilities.maxImageExtent.width);
		actual_extent.height = std::clamp(actual_extent.height, details.capabilities.minImageExtent.height, details.capabilities.maxImageExtent.height);

		return actual_extent;
	}

	VkImageUsageFlags VulkanRHI::getProperSwapchainSurfaceImageUsage(const SwapchainSupportDetails& details)
	{
		ASSERT(details.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "swapchain doesn't support VK_IMAGE_USAGE_TRANSFER_SRC_BIT");
		ASSERT(details.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT, "swapchain doesn't support VK_IMAGE_USAGE_TRANSFER_DST_BIT");
		return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}

	VkFormat VulkanRHI::getProperImageFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
	{
		for (VkFormat format : candidates)
		{
			VkFormatProperties format_properties;
			vkGetPhysicalDeviceFormatProperties(m_physical_device, format, &format_properties);

			if ((tiling == VK_IMAGE_TILING_LINEAR && (format_properties.linearTilingFeatures & features) == features) ||
				(tiling == VK_IMAGE_TILING_OPTIMAL && (format_properties.optimalTilingFeatures & features) == features))
			{
				return format;
			}
		}

		LOG_FATAL("failed to find a proper image format");
		return VK_FORMAT_UNDEFINED;
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRHI::debugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* p_user_data)
	{
		if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		{
			LOG_WARNING("vulkan validation layer: {}", p_callback_data->pMessage);
		}
		else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		{
			LOG_ERROR("vulkan validation layer: {}", p_callback_data->pMessage);
		}

		return VK_FALSE;
	}
}