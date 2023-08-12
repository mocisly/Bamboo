#include "gbuffer_pass.h"
#include "runtime/core/vulkan/vulkan_rhi.h"
#include "runtime/resource/shader/shader_manager.h"
#include "runtime/resource/asset/asset_manager.h"
#include "runtime/platform/timer/timer.h"
#include "runtime/resource/asset/base/mesh.h"
#include "runtime/function/render/render_data.h"

#include <array>

namespace Bamboo
{

	GBufferPass::GBufferPass()
	{
		m_formats = {
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_FORMAT_R8G8B8A8_UNORM,
			VulkanRHI::get().getDepthFormat()
		};
	}

	void GBufferPass::render()
	{
		static bool is_rendered = false;
		if (is_rendered)
		{
			return;
		}
		is_rendered = true;
		StopWatch stop_watch;
		stop_watch.start();

// 		VkCommandBuffer command_buffer = VulkanRHI::get().getCommandBuffer();
// 		uint32_t flight_index = VulkanRHI::get().getFlightIndex();

		VkCommandBuffer command_buffer = VulkanUtil::beginInstantCommands();
		uint32_t flight_index = 0;

		VkRenderPassBeginInfo render_pass_bi{};
		render_pass_bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_bi.renderPass = m_render_pass;
		render_pass_bi.framebuffer = m_framebuffer;
		render_pass_bi.renderArea.offset = { 0, 0 };
		render_pass_bi.renderArea.extent = { m_width, m_height };

		std::array<VkClearValue, 6> clear_values{};
		clear_values[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clear_values[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clear_values[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clear_values[3].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clear_values[4].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clear_values[5].depthStencil = { 1.0f, 0 };
		render_pass_bi.clearValueCount = static_cast<uint32_t>(clear_values.size());
		render_pass_bi.pClearValues = clear_values.data();

		vkCmdBeginRenderPass(command_buffer, &render_pass_bi, VK_SUBPASS_CONTENTS_INLINE);

		// 1.set viewport
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(m_width);
		viewport.height = static_cast<float>(m_height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);

		// 2.set scissor
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = { m_width, m_height };
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);

		// 3.bind states and render
		for (auto& render_data : m_render_datas)
		{
			std::shared_ptr<SkeletalMeshRenderData> skeletal_mesh_render_data = nullptr;
			std::shared_ptr<MeshRenderData> mesh_render_data = std::static_pointer_cast<MeshRenderData>(render_data);
			EMeshType mesh_type = mesh_render_data->mesh_type;
			if (mesh_type == EMeshType::Skeletal)
			{
				skeletal_mesh_render_data = std::static_pointer_cast<SkeletalMeshRenderData>(render_data);
			}

			VkPipeline pipeline = m_pipelines[(uint32_t)mesh_type];
			VkPipelineLayout pipeline_layout = m_pipeline_layouts[(uint32_t)mesh_type];

			// bind pipeline
			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

			// bind vertex and index buffer
			VkBuffer vertexBuffers[] = { mesh_render_data->vertex_buffer.buffer };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(command_buffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(command_buffer, mesh_render_data->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			// render all sub meshes
			std::vector<uint32_t>& index_counts = mesh_render_data->index_counts;
			std::vector<uint32_t>& index_offsets = mesh_render_data->index_offsets;
			size_t sub_mesh_count = index_counts.size();
			for (size_t i = 0; i < sub_mesh_count; ++i)
			{
				// push constants
				const void* pcos[] = { &mesh_render_data->transform_pco, &mesh_render_data->material_pcos[i] };
				for (size_t c = 0; c < m_push_constant_ranges.size(); ++c)
				{
					const VkPushConstantRange& pushConstantRange = m_push_constant_ranges[c];
					vkCmdPushConstants(command_buffer, pipeline_layout, pushConstantRange.stageFlags, pushConstantRange.offset, pushConstantRange.size, pcos[c]);
				}

				// update(push) sub mesh descriptors
				std::vector<VkWriteDescriptorSet> desc_writes;

				// bone matrix ubo
				if (mesh_type == EMeshType::Skeletal)
				{
					VkDescriptorBufferInfo desc_buffer_info{};
					desc_buffer_info.buffer = skeletal_mesh_render_data->bone_ubs[flight_index].buffer;
					desc_buffer_info.offset = 0;
					desc_buffer_info.range = sizeof(BoneUBO);

					VkWriteDescriptorSet desc_write{};
					desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					desc_write.dstSet = 0;
					desc_write.dstBinding = 0;
					desc_write.dstArrayElement = 0;
					desc_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
					desc_write.descriptorCount = 1;
					desc_write.pBufferInfo = &desc_buffer_info;
					desc_writes.push_back(desc_write);
				}

				// image sampler
				std::vector<VmaImageViewSampler> pbr_textures = {
					mesh_render_data->pbr_textures[i].base_color_texure,
					mesh_render_data->pbr_textures[i].metallic_roughness_texure,
					mesh_render_data->pbr_textures[i].normal_texure,
					mesh_render_data->pbr_textures[i].occlusion_texure,
					mesh_render_data->pbr_textures[i].emissive_texure,
				};
				std::vector<VkDescriptorImageInfo> desc_image_infos(pbr_textures.size(), VkDescriptorImageInfo{});
				for (size_t t = 0; t < pbr_textures.size(); ++t)
				{
					desc_image_infos[t].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					desc_image_infos[t].imageView = pbr_textures[t].view;
					desc_image_infos[t].sampler = pbr_textures[t].sampler;

					VkWriteDescriptorSet desc_write{};
					desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					desc_write.dstSet = 0;
					desc_write.dstBinding = static_cast<uint32_t>(t + 1);
					desc_write.dstArrayElement = 0;
					desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					desc_write.descriptorCount = 1;
					desc_write.pImageInfo = &desc_image_infos[t];
					desc_writes.push_back(desc_write);
				}

				VulkanRHI::get().getVkCmdPushDescriptorSetKHR()(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					pipeline_layout, 0, static_cast<uint32_t>(desc_writes.size()), desc_writes.data());

				// render sub mesh
				vkCmdDrawIndexed(command_buffer, index_counts[i], 1, index_offsets[i], 0, 0);
			}
		}

		vkCmdEndRenderPass(command_buffer);
		VulkanUtil::endInstantCommands(command_buffer);

		// save gbuffer to files
// 		std::vector<VmaImageViewSampler> texture_samplers = {
// 			m_position_texture_sampler,
// 			m_normal_texture_sampler,
// 			m_base_color_texture_sampler,
// 			m_emissive_texture_sampler,
// 			m_metallic_roughness_occlusion_texture_sampler,
// 			m_depth_stencil_texture_sampler
// 		};
// 
// 		for (size_t i = 0; i < texture_samplers.size(); ++i)
// 		{
// 			VulkanUtil::saveImage(texture_samplers[i].image(), m_width, m_height, m_formats[i], 
// 				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, StringUtil::format("D:/Test/gbuffer_%d.bin", i));
// 		}
		LOG_INFO("gbuffer pass elapsed time: {}ms", stop_watch.stop());
	}

	void GBufferPass::createRenderPass()
	{
		// attachments
		std::array<VkAttachmentDescription, 6> attachments = {};
		std::array<VkAttachmentReference, 6> references = {};
		for (uint32_t i = 0; i < 6; ++i)
		{
			attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
			attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			attachments[i].format = m_formats[i];

			references[i].attachment = i;
			references[i].layout = i == 5 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		// subpass
		VkSubpassDescription subpass_desc{};
		subpass_desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass_desc.colorAttachmentCount = static_cast<uint32_t>(references.size() - 1);
		subpass_desc.pColorAttachments = references.data();
		subpass_desc.pDepthStencilAttachment = &references[references.size() - 1];

		// subpass dependencies
		std::array<VkSubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// create render pass
		VkRenderPassCreateInfo render_pass_ci{};
		render_pass_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_ci.attachmentCount = static_cast<uint32_t>(attachments.size());
		render_pass_ci.pAttachments = attachments.data();
		render_pass_ci.subpassCount = 1;
		render_pass_ci.pSubpasses = &subpass_desc;
		render_pass_ci.dependencyCount = static_cast<uint32_t>(dependencies.size());
		render_pass_ci.pDependencies = dependencies.data();

		VkResult result = vkCreateRenderPass(VulkanRHI::get().getDevice(), &render_pass_ci, nullptr, &m_render_pass);
		CHECK_VULKAN_RESULT(result, "create render pass");
	}

	void GBufferPass::createDescriptorSetLayouts()
	{
		std::vector<VkDescriptorSetLayoutBinding> desc_set_layout_bindings = {
			{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
			{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
			{3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
			{4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
			{5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		};

		VkDescriptorSetLayoutCreateInfo desc_set_layout_ci{};
		desc_set_layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		desc_set_layout_ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
		desc_set_layout_ci.bindingCount = static_cast<uint32_t>(desc_set_layout_bindings.size());
		desc_set_layout_ci.pBindings = desc_set_layout_bindings.data();

		m_desc_set_layouts.resize(2);
		VkResult result = vkCreateDescriptorSetLayout(VulkanRHI::get().getDevice(), &desc_set_layout_ci, nullptr, &m_desc_set_layouts[0]);

		desc_set_layout_bindings.insert(desc_set_layout_bindings.begin(), { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr });
		desc_set_layout_ci.bindingCount = static_cast<uint32_t>(desc_set_layout_bindings.size());
		desc_set_layout_ci.pBindings = desc_set_layout_bindings.data();
		result = vkCreateDescriptorSetLayout(VulkanRHI::get().getDevice(), &desc_set_layout_ci, nullptr, &m_desc_set_layouts[1]);
		CHECK_VULKAN_RESULT(result, "create descriptor set layout");
	}

	void GBufferPass::createPipelineLayouts()
	{
		VkPipelineLayoutCreateInfo pipeline_layout_ci{};
		pipeline_layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_ci.setLayoutCount = 1;
		pipeline_layout_ci.pSetLayouts = &m_desc_set_layouts[0];

		m_push_constant_ranges =
		{
			{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TransformPCO) },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(TransformPCO), sizeof(MaterialPCO) }
		};

		pipeline_layout_ci.pushConstantRangeCount = static_cast<uint32_t>(m_push_constant_ranges.size());
		pipeline_layout_ci.pPushConstantRanges = m_push_constant_ranges.data();

		m_pipeline_layouts.resize(2);
		VkResult result = vkCreatePipelineLayout(VulkanRHI::get().getDevice(), &pipeline_layout_ci, nullptr, &m_pipeline_layouts[0]);
		
		pipeline_layout_ci.pSetLayouts = &m_desc_set_layouts[1];
		result = vkCreatePipelineLayout(VulkanRHI::get().getDevice(), &pipeline_layout_ci, nullptr, &m_pipeline_layouts[1]);
		CHECK_VULKAN_RESULT(result, "create pipeline layout");
	}

	void GBufferPass::createPipelines()
	{
		// color blending
		for (int i = 0; i < 4; ++i)
		{
			m_color_blend_attachments.push_back(m_color_blend_attachments.front());
		}
		m_color_blend_ci.attachmentCount = static_cast<uint32_t>(m_color_blend_attachments.size());
		m_color_blend_ci.pAttachments = m_color_blend_attachments.data();

		// vertex input
		// vertex bindings
		std::vector<VkVertexInputBindingDescription> vertex_input_binding_descriptions;
		vertex_input_binding_descriptions.resize(1, VkVertexInputBindingDescription{});
		vertex_input_binding_descriptions[0].binding = 0;
		vertex_input_binding_descriptions[0].stride = sizeof(StaticVertex);
		vertex_input_binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		// vertex attributes
		std::vector<VkVertexInputAttributeDescription> vertex_input_attribute_descriptions;
		vertex_input_attribute_descriptions.resize(3, VkVertexInputAttributeDescription{});

		vertex_input_attribute_descriptions[0].binding = 0;
		vertex_input_attribute_descriptions[0].location = 0;
		vertex_input_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertex_input_attribute_descriptions[0].offset = offsetof(StaticVertex, m_position);

		vertex_input_attribute_descriptions[1].binding = 0;
		vertex_input_attribute_descriptions[1].location = 1;
		vertex_input_attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
		vertex_input_attribute_descriptions[1].offset = offsetof(StaticVertex, m_tex_coord);

		vertex_input_attribute_descriptions[2].binding = 0;
		vertex_input_attribute_descriptions[2].location = 2;
		vertex_input_attribute_descriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertex_input_attribute_descriptions[2].offset = offsetof(StaticVertex, m_normal);

		VkPipelineVertexInputStateCreateInfo vertex_input_ci{};
		vertex_input_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input_ci.vertexBindingDescriptionCount = static_cast<uint32_t>(vertex_input_binding_descriptions.size());
		vertex_input_ci.pVertexBindingDescriptions = vertex_input_binding_descriptions.data();
		vertex_input_ci.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_input_attribute_descriptions.size());
		vertex_input_ci.pVertexAttributeDescriptions = vertex_input_attribute_descriptions.data();

		// shader stages
		const auto& shader_manager = g_runtime_context.shaderManager();
		std::vector<VkPipelineShaderStageCreateInfo> shader_stage_cis = {
			shader_manager->getShaderStageCI("blinn_phong_static_mesh.vert", VK_SHADER_STAGE_VERTEX_BIT),
			shader_manager->getShaderStageCI("gbuffer.frag", VK_SHADER_STAGE_FRAGMENT_BIT)
		};

		// create graphics pipeline
		m_pipeline_ci.stageCount = static_cast<uint32_t>(shader_stage_cis.size());
		m_pipeline_ci.pStages = shader_stage_cis.data();
		m_pipeline_ci.pVertexInputState = &vertex_input_ci;
		m_pipeline_ci.layout = m_pipeline_layouts[0];
		m_pipeline_ci.renderPass = m_render_pass;
		m_pipeline_ci.subpass = 0;

		m_pipelines.resize(2);

		// create static mesh pipeline
		VkResult result = vkCreateGraphicsPipelines(VulkanRHI::get().getDevice(), m_pipeline_cache, 1, &m_pipeline_ci, nullptr, &m_pipelines[0]);
		CHECK_VULKAN_RESULT(result, "create static mesh graphics pipeline");

		// create skeletal mesh pipeline
		shader_stage_cis[0] = shader_manager->getShaderStageCI("blinn_phong_skeletal_mesh.vert", VK_SHADER_STAGE_VERTEX_BIT);

		vertex_input_binding_descriptions[0].stride = sizeof(SkeletalVertex);

		vertex_input_attribute_descriptions.resize(5);
		vertex_input_attribute_descriptions[3].binding = 0;
		vertex_input_attribute_descriptions[3].location = 3;
		vertex_input_attribute_descriptions[3].format = VK_FORMAT_R32G32B32A32_SINT;
		vertex_input_attribute_descriptions[3].offset = offsetof(SkeletalVertex, m_bones);

		vertex_input_attribute_descriptions[4].binding = 0;
		vertex_input_attribute_descriptions[4].location = 4;
		vertex_input_attribute_descriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		vertex_input_attribute_descriptions[4].offset = offsetof(SkeletalVertex, m_weights);

		vertex_input_ci.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_input_attribute_descriptions.size());
		vertex_input_ci.pVertexAttributeDescriptions = vertex_input_attribute_descriptions.data();

		m_pipeline_ci.layout = m_pipeline_layouts[1];
		result = vkCreateGraphicsPipelines(VulkanRHI::get().getDevice(), m_pipeline_cache, 1, &m_pipeline_ci, nullptr, &m_pipelines[1]);
		CHECK_VULKAN_RESULT(result, "create skeletal mesh graphics pipeline");
	}

	void GBufferPass::createFramebuffer()
	{
		// 1.create color images and view
		VulkanUtil::createImageViewSampler(m_width, m_height, nullptr, 1, 1, m_formats[0], VK_FILTER_NEAREST, VK_FILTER_NEAREST, 
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, m_position_texture_sampler, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
		VulkanUtil::createImageViewSampler(m_width, m_height, nullptr, 1, 1, m_formats[1], VK_FILTER_NEAREST, VK_FILTER_NEAREST, 
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, m_normal_texture_sampler, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
		VulkanUtil::createImageViewSampler(m_width, m_height, nullptr, 1, 1, m_formats[2], VK_FILTER_NEAREST, VK_FILTER_NEAREST, 
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, m_base_color_texture_sampler, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
		VulkanUtil::createImageViewSampler(m_width, m_height, nullptr, 1, 1, m_formats[3], VK_FILTER_NEAREST, VK_FILTER_NEAREST, 
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, m_emissive_texture_sampler, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
		VulkanUtil::createImageViewSampler(m_width, m_height, nullptr, 1, 1, m_formats[4], VK_FILTER_NEAREST, VK_FILTER_NEAREST, 
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, m_metallic_roughness_occlusion_texture_sampler, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
		VulkanUtil::createImageViewSampler(m_width, m_height, nullptr, 1, 1, m_formats[5], VK_FILTER_NEAREST, VK_FILTER_NEAREST, 
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, m_depth_stencil_texture_sampler,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

		// 2.create framebuffer
		std::array<VkImageView, 6> attachments = {
			m_position_texture_sampler.view,
			m_normal_texture_sampler.view,
			m_base_color_texture_sampler.view,
			m_emissive_texture_sampler.view,
			m_metallic_roughness_occlusion_texture_sampler.view,
			m_depth_stencil_texture_sampler.view
		};

		VkFramebufferCreateInfo framebuffer_ci{};
		framebuffer_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_ci.renderPass = m_render_pass;
		framebuffer_ci.attachmentCount = static_cast<uint32_t>(attachments.size());
		framebuffer_ci.pAttachments = attachments.data();
		framebuffer_ci.width = m_width;
		framebuffer_ci.height = m_height;
		framebuffer_ci.layers = 1;

		VkResult result = vkCreateFramebuffer(VulkanRHI::get().getDevice(), &framebuffer_ci, nullptr, &m_framebuffer);
		CHECK_VULKAN_RESULT(result, "create brdf lut graphics pipeline");
	}

	void GBufferPass::destroyResizableObjects()
	{
		m_position_texture_sampler.destroy();
		m_normal_texture_sampler.destroy();
		m_base_color_texture_sampler.destroy();
		m_emissive_texture_sampler.destroy();
		m_metallic_roughness_occlusion_texture_sampler.destroy();
		m_depth_stencil_texture_sampler.destroy();

		RenderPass::destroyResizableObjects();
	}

}