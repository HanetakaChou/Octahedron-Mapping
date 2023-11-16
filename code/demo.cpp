//
// Copyright (C) Copyright (C) YuqiaoZhang(HanetakaChou)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include <DirectXPackedVector.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <assert.h>
#include "demo.h"
#include "support/utils_align_up.h"
#include "support/camera_controller.h"
#include "support/load_asset_input_stream.h"
#include "support/load_image_asset.h"
#include "../shaders/forward_shading_pipeline_layout.sli"
#include "../shaders/point_light_shadow_pipeline_layout.sli"
#include "../assets/cube.h"
#include "../assets/lunarg_dds.h"

static uint32_t const g_point_light_shadow_mapping_width_height_size = 1024U;

static inline uint32_t linear_allocate(uint32_t &buffer_current, uint32_t buffer_end, uint32_t size, uint32_t alignment);

static inline DirectX::XMMATRIX XM_CALLCONV DirectX_Math_Matrix_PerspectiveFovRH_ReversedZ(float FovAngleY, float AspectRatio, float NearZ, float FarZ);

Demo::Demo()
{
}

void Demo::init(pal_device const *device, pal_upload_ring_buffer const *global_upload_ring_buffer)
{
	// Descriptor Layout - Create
	pal_descriptor_set_layout *forward_shading_global_descriptor_set_layout = NULL;
	pal_descriptor_set_layout *material_descriptor_set_layout = NULL;
	pal_descriptor_set_layout *point_light_shadow_global_descriptor_set_layout = NULL;
	{
		PAL_DESCRIPTOR_SET_LAYOUT_BINDING forward_shading_global_descriptor_set_layout_bindings[] = {
			// main camera
			{0U, PAL_DESCRIPTOR_TYPE_DYNAMIC_UNIFORM_BUFFER, 1U},
			// object
			{1U, PAL_DESCRIPTOR_TYPE_DYNAMIC_UNIFORM_BUFFER, 1U},
			// sampler
			{2U, PAL_DESCRIPTOR_TYPE_SAMPLER, 2U},
			// point light shadow
			{3U, PAL_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1U}};
		forward_shading_global_descriptor_set_layout = device->create_descriptor_set_layout(sizeof(forward_shading_global_descriptor_set_layout_bindings) / sizeof(forward_shading_global_descriptor_set_layout_bindings[0]), forward_shading_global_descriptor_set_layout_bindings);

		PAL_DESCRIPTOR_SET_LAYOUT_BINDING material_descriptor_set_layout_bindings[] = {
			// material set emissive texture
			{0U, PAL_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1U},
		};
		material_descriptor_set_layout = device->create_descriptor_set_layout(sizeof(material_descriptor_set_layout_bindings) / sizeof(material_descriptor_set_layout_bindings[0]), material_descriptor_set_layout_bindings);

		PAL_DESCRIPTOR_SET_LAYOUT_BINDING point_light_shadow_global_descriptor_set_layout_bindings[] = {
			// light camera
			{0U, PAL_DESCRIPTOR_TYPE_DYNAMIC_UNIFORM_BUFFER, 1U},
			// object
			{1U, PAL_DESCRIPTOR_TYPE_DYNAMIC_UNIFORM_BUFFER, 1U}};
		point_light_shadow_global_descriptor_set_layout = device->create_descriptor_set_layout(sizeof(point_light_shadow_global_descriptor_set_layout_bindings) / sizeof(point_light_shadow_global_descriptor_set_layout_bindings[0]), point_light_shadow_global_descriptor_set_layout_bindings);

		pal_descriptor_set_layout *forward_shading_descriptor_set_layouts[2] = {forward_shading_global_descriptor_set_layout, material_descriptor_set_layout};
		this->m_forward_shading_pipeline_layout = device->create_pipeline_layout(sizeof(forward_shading_descriptor_set_layouts) / sizeof(forward_shading_descriptor_set_layouts[0]), forward_shading_descriptor_set_layouts);

		pal_descriptor_set_layout *point_light_shadow_descriptor_set_layouts[1] = {point_light_shadow_global_descriptor_set_layout};
		this->m_point_light_shadow_pipeline_layout = device->create_pipeline_layout(sizeof(point_light_shadow_descriptor_set_layouts) / sizeof(point_light_shadow_descriptor_set_layouts[0]), point_light_shadow_descriptor_set_layouts);
	}

	// Render Pass and Pipeline
	{
		this->m_swap_chain_image_format = PAL_COLOR_ATTACHMENT_FORMAT_B8G8R8A8_UNORM;
		this->m_forward_shading_render_pass = NULL;
		this->m_forward_shading_pipeline = NULL;
		this->create_render_pass_and_pipeline(device);

		// RenderPass
		{
			PAL_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT depth_stencil_attachment = {

				device->get_depth_attachment_image_format(),
				PAL_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_LOAD_OPERATION_CLEAR,
				PAL_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_STORE_OPERATION_FLUSH_FOR_SAMPLED_IMAGE

			};

			this->m_point_light_shadow_render_pass = device->create_render_pass(0U, NULL, &depth_stencil_attachment);
		}

		// Pipeline
		{
#if defined(USE_D3D12) && USE_D3D12
			typedef uint8_t BYTE;
#include "../dxbc/point_light_shadow_vertex.inl"
#include "../dxbc/point_light_shadow_fragment.inl"
#else
			constexpr uint32_t const point_light_shadow_vertex_shader_module_code[] = {
#include "../spirv/point_light_shadow_vertex.inl"
			};

			constexpr uint32_t const point_light_shadow_fragment_shader_module_code[] = {
#include "../spirv/point_light_shadow_fragment.inl"
			};
#endif

			PAL_GRAPHICS_PIPELINE_VERTEX_BINDING vertex_bindings[1] = {
				// Position
				{sizeof(float) * 3U}};

			PAL_GRAPHICS_PIPELINE_VERTEX_ATTRIBUTE vertex_attributes[1] = {
				// Position
				{
					0U,
					PAL_GRAPHICS_PIPELINE_VERTEX_ATTRIBUTE_FORMAT_R32G32B32_SFLOAT,
					0U}};

			this->m_point_light_shadow_pipeline = device->create_graphics_pipeline(this->m_point_light_shadow_render_pass, this->m_point_light_shadow_pipeline_layout, sizeof(point_light_shadow_vertex_shader_module_code), point_light_shadow_vertex_shader_module_code, sizeof(point_light_shadow_fragment_shader_module_code), point_light_shadow_fragment_shader_module_code, sizeof(vertex_bindings) / sizeof(vertex_bindings[0]), vertex_bindings, sizeof(vertex_attributes) / sizeof(vertex_attributes[0]), vertex_attributes, PAL_GRAPHICS_PIPELINE_COMPARE_OPERATION_LESS);
		}
	}

	// Point Light Shadow Image
	{
		this->m_point_light_shadow_image = device->create_depth_stencil_attachment_image(device->get_depth_attachment_image_format(), g_point_light_shadow_mapping_width_height_size, g_point_light_shadow_mapping_width_height_size, true);

		this->m_point_light_shadow_frame_buffer = device->create_frame_buffer(this->m_point_light_shadow_render_pass, g_point_light_shadow_mapping_width_height_size, g_point_light_shadow_mapping_width_height_size, 0U, NULL, this->m_point_light_shadow_image);
	}

	// Descriptor - Global
	{
		this->m_global_nearest_sampler = device->create_sampler(PAL_SAMPLER_FILTER_NEAREST);
		this->m_global_linear_sampler = device->create_sampler(PAL_SAMPLER_FILTER_LINEAR);

		this->m_forward_shading_global_descriptor_set = device->create_descriptor_set(forward_shading_global_descriptor_set_layout);

		constexpr uint32_t const forward_shading_dynamic_uniform_buffers_range[2] = {
			sizeof(forward_shading_layout_global_set_frame_uniform_buffer_binding),
			sizeof(forward_shading_layout_global_set_object_uniform_buffer_binding)};
		device->write_descriptor_set(this->m_forward_shading_global_descriptor_set, PAL_DESCRIPTOR_TYPE_DYNAMIC_UNIFORM_BUFFER, 0U, 0U, 1U, &global_upload_ring_buffer, &forward_shading_dynamic_uniform_buffers_range[0], NULL, NULL);
		device->write_descriptor_set(this->m_forward_shading_global_descriptor_set, PAL_DESCRIPTOR_TYPE_DYNAMIC_UNIFORM_BUFFER, 1U, 0U, 1U, &global_upload_ring_buffer, &forward_shading_dynamic_uniform_buffers_range[1], NULL, NULL);

		pal_sampler const *const forward_shading_samplers[2] = {
			this->m_global_nearest_sampler,
			this->m_global_linear_sampler};
		device->write_descriptor_set(this->m_forward_shading_global_descriptor_set, PAL_DESCRIPTOR_TYPE_SAMPLER, 2U, 0U, 2U, NULL, NULL, NULL, forward_shading_samplers);

		pal_sampled_image const *const forward_shading_sampled_images[1] = {this->m_point_light_shadow_image->get_sampled_image()};
		device->write_descriptor_set(this->m_forward_shading_global_descriptor_set, PAL_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 3U, 0U, 1U, NULL, NULL, forward_shading_sampled_images, NULL);

		this->m_point_light_shadow_global_descriptor_set = device->create_descriptor_set(point_light_shadow_global_descriptor_set_layout);

		constexpr uint32_t const point_light_shadow_dynamic_uniform_buffers_range[2] = {
			sizeof(point_light_shadow_layout_global_set_per_frame_uniform_buffer_binding),
			sizeof(point_light_shadow_layout_global_set_per_object_uniform_buffer_binding)};

		device->write_descriptor_set(this->m_point_light_shadow_global_descriptor_set, PAL_DESCRIPTOR_TYPE_DYNAMIC_UNIFORM_BUFFER, 0U, 0U, 1U, &global_upload_ring_buffer, &point_light_shadow_dynamic_uniform_buffers_range[0], NULL, NULL);
		device->write_descriptor_set(this->m_point_light_shadow_global_descriptor_set, PAL_DESCRIPTOR_TYPE_DYNAMIC_UNIFORM_BUFFER, 1U, 0U, 1U, &global_upload_ring_buffer, &point_light_shadow_dynamic_uniform_buffers_range[1], NULL, NULL);
	}

	// Assets
	{
		// NOTE: user can custum size
		uint32_t const staging_buffer_size = 320U * 1024U * 1024U;

		pal_staging_buffer *staging_buffer = device->create_staging_buffer(staging_buffer_size);

		void *staging_buffer_base = staging_buffer->get_memory_range_base();
		uint32_t staging_buffer_current = 0U;
		uint32_t staging_buffer_end = staging_buffer_size;

		pal_transfer_command_buffer *transfer_command_buffer = device->create_transfer_command_buffer();

		pal_graphics_command_buffer *graphics_command_buffer = device->create_graphics_command_buffer();

		device->reset_transfer_command_buffer(transfer_command_buffer);

		device->reset_graphics_command_buffer(graphics_command_buffer);

		transfer_command_buffer->begin();

		graphics_command_buffer->begin();

		// vertex position buffer
		{
			this->m_cube_vertex_position_buffer = device->create_asset_buffer(sizeof(g_cube_vertex_postion), PAL_ASSET_BUFFER_TYPE_VERTEX);

			uint32_t position_staging_buffer_offset = linear_allocate(staging_buffer_current, staging_buffer_end, sizeof(g_cube_vertex_postion), 1U);

			// write to staging buffer
			memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(staging_buffer_base) + position_staging_buffer_offset), g_cube_vertex_postion, sizeof(g_cube_vertex_postion));

			transfer_command_buffer->transfer_from_staging_buffer_to_asset_buffer(this->m_cube_vertex_position_buffer, PAL_ASSET_BUFFER_TYPE_VERTEX, 0U, staging_buffer, position_staging_buffer_offset, sizeof(g_cube_vertex_postion));

			graphics_command_buffer->acquire_asset_buffer(this->m_cube_vertex_position_buffer, PAL_ASSET_BUFFER_TYPE_VERTEX);
		}

		// vertex varying buffer
		{
			// convert
			uint32_t converted_vertex_uv[6U * 6U];

			for (int i = 0; i < (6U * 6U); ++i)
			{
				DirectX::PackedVector::XMUSHORTN2 vector_packed_output;

				DirectX::XMFLOAT2A vector_unpacked_input(g_cube_vertex_uv[2U * i], g_cube_vertex_uv[2U * i + 1U]);
				DirectX::PackedVector::XMStoreUShortN2(&vector_packed_output, DirectX::XMLoadFloat2A(&vector_unpacked_input));

				converted_vertex_uv[i] = vector_packed_output.v;
			}

			this->m_cube_vertex_varying_buffer = device->create_asset_buffer(sizeof(converted_vertex_uv), PAL_ASSET_BUFFER_TYPE_VERTEX);

			uint32_t varying_staging_buffer_offset = linear_allocate(staging_buffer_current, staging_buffer_end, sizeof(converted_vertex_uv), 1U);

			// write to staging buffer
			memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(staging_buffer_base) + varying_staging_buffer_offset), converted_vertex_uv, sizeof(converted_vertex_uv));

			transfer_command_buffer->transfer_from_staging_buffer_to_asset_buffer(this->m_cube_vertex_varying_buffer, PAL_ASSET_BUFFER_TYPE_VERTEX, 0U, staging_buffer, varying_staging_buffer_offset, sizeof(converted_vertex_uv));

			graphics_command_buffer->acquire_asset_buffer(this->m_cube_vertex_varying_buffer, PAL_ASSET_BUFFER_TYPE_VERTEX);
		}

		// emissive textrue
		{
			LOAD_IMAGE_ASSET_HEADER image_asset_header;
			std::vector<LOAD_IMAGE_ASSET_SUBRESOURCE_MEMCPY_DEST> subresource_memcpy_dests;
			{
				uint32_t const staging_buffer_offset_alignment = device->get_staging_buffer_offset_alignment();
				uint32_t const staging_buffer_row_pitch_alignment = device->get_staging_buffer_row_pitch_alignment();

				load_asset_memory_input_stream emissive_texture_input_stream;
				emissive_texture_input_stream.init(g_lunarg_dds, sizeof(g_lunarg_dds));

				size_t image_asset_data_offset;
				bool const res_load_dds_image_asset_header = load_dds_image_asset_header_from_input_stream(&emissive_texture_input_stream, &image_asset_header, &image_asset_data_offset);
				assert(res_load_dds_image_asset_header);

				uint32_t const subresource_count = image_asset_header.mip_levels;
				subresource_memcpy_dests.resize(subresource_count);

				size_t const total_bytes = load_image_asset_calculate_subresource_memcpy_dests(image_asset_header.format, image_asset_header.width, image_asset_header.height, image_asset_header.depth, image_asset_header.mip_levels, image_asset_header.array_layers, staging_buffer_current, staging_buffer_offset_alignment, staging_buffer_row_pitch_alignment, subresource_count, &subresource_memcpy_dests[0]);
				staging_buffer_current += static_cast<uint32_t>(total_bytes);
				assert(staging_buffer_current < staging_buffer_end);

				bool const load_dds_image_asset_data = load_dds_image_asset_data_from_input_stream(&emissive_texture_input_stream, &image_asset_header, image_asset_data_offset, staging_buffer_base, subresource_count, &subresource_memcpy_dests[0]);
				assert(load_dds_image_asset_data);

				emissive_texture_input_stream.uninit();
			}

			// TODO: support more image paramters
			assert(!image_asset_header.is_cube_map);
			assert(PAL_ASSET_IMAGE_TYPE_2D == image_asset_header.type);
			assert(1U == image_asset_header.depth);
			assert(1U == image_asset_header.array_layers);
			this->m_cube_emissive_texture = device->create_asset_image(image_asset_header.format, image_asset_header.width, image_asset_header.height, image_asset_header.mip_levels);

			for (uint32_t mip_level = 0U; mip_level < image_asset_header.mip_levels; ++mip_level)
			{
				transfer_command_buffer->transfer_from_staging_buffer_to_asset_image(this->m_cube_emissive_texture, image_asset_header.format, image_asset_header.width, image_asset_header.height, mip_level, staging_buffer, subresource_memcpy_dests[mip_level].staging_buffer_offset, subresource_memcpy_dests[mip_level].output_row_pitch, subresource_memcpy_dests[mip_level].output_row_count);

				graphics_command_buffer->acquire_asset_image(this->m_cube_emissive_texture, mip_level);
			}
		}

		transfer_command_buffer->end();

		graphics_command_buffer->end();

		pal_transfer_queue *transfer_queue = device->create_transfer_queue();

		pal_graphics_queue *graphics_queue = device->create_graphics_queue();

		pal_fence *fence = device->create_fence(true);

		transfer_queue->submit_and_signal(transfer_command_buffer);

		device->reset_fence(fence);

		graphics_queue->wait_and_submit(transfer_command_buffer, graphics_command_buffer, fence);

		device->wait_for_fence(fence);

		device->destroy_fence(fence);

		device->destroy_transfer_command_buffer(transfer_command_buffer);

		device->destroy_graphics_command_buffer(graphics_command_buffer);

		device->destroy_transfer_queue(transfer_queue);

		device->destroy_graphics_queue(graphics_queue);

		device->destroy_staging_buffer(staging_buffer);
	}

	// Descriptors - Assets
	{
		this->m_cube_material_set = device->create_descriptor_set(material_descriptor_set_layout);

		pal_sampled_image const *src_sampled_images[1] = {this->m_cube_emissive_texture->get_sampled_image()};
		device->write_descriptor_set(this->m_cube_material_set, PAL_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0U, 0U, 1U, NULL, NULL, src_sampled_images, NULL);
	}

	// Descriptor Layout - Destroy
	device->destroy_descriptor_set_layout(forward_shading_global_descriptor_set_layout);
	device->destroy_descriptor_set_layout(material_descriptor_set_layout);
	device->destroy_descriptor_set_layout(point_light_shadow_global_descriptor_set_layout);
	forward_shading_global_descriptor_set_layout = NULL;
	material_descriptor_set_layout = NULL;
	point_light_shadow_global_descriptor_set_layout = NULL;

	// Init Camera
	g_camera_controller.m_eye_position = DirectX::XMFLOAT3(0.0F, 6.0F, -10.0F);
	g_camera_controller.m_eye_direction = DirectX::XMFLOAT3(0.0F, -0.5F, 1.0F);
	g_camera_controller.m_up_direction = DirectX::XMFLOAT3(0.0F, 1.0F, 0.0F);

	// Init Box
	this->m_box_spin_angle = 0.0F;

	// Init Plane
	DirectX::XMStoreFloat4x4(&this->m_plane_model_transform, DirectX::XMMatrixMultiply(DirectX::XMMatrixScaling(25.0F, 1.0F, 25.0F), DirectX::XMMatrixTranslation(0.0F, -5.0F, 0.0F)));

	// Init Point Light
	this->m_point_light_position_and_radius.x = 0.0F;
	this->m_point_light_position_and_radius.y = 0.0F;
	this->m_point_light_position_and_radius.z = -5.0F;
	this->m_point_light_position_and_radius.w = 50.0F;
	this->m_point_light_shadow_bias = 0.000075F;

	// Init SwapChain Related
	this->m_swap_chain_image_width = 0U;
	this->m_swap_chain_image_height = 0U;
	this->m_depth_stencil_attachment_image = NULL;
	this->m_swap_chain_image_count = 0U;
	this->m_frame_buffers = NULL;
}

void Demo::destroy(pal_device const *device)
{
	this->destroy_render_pass_and_pipeline(device);

	device->destroy_render_pass(this->m_point_light_shadow_render_pass);
	device->destroy_graphics_pipeline(this->m_point_light_shadow_pipeline);

	device->destroy_pipeline_layout(this->m_forward_shading_pipeline_layout);
	device->destroy_pipeline_layout(this->m_point_light_shadow_pipeline_layout);

	device->destroy_descriptor_set(this->m_forward_shading_global_descriptor_set);
	device->destroy_descriptor_set(this->m_point_light_shadow_global_descriptor_set);

	device->destroy_frame_buffer(this->m_point_light_shadow_frame_buffer);
	device->destroy_depth_stencil_attachment_image(this->m_point_light_shadow_image);

	device->destroy_sampler(this->m_global_nearest_sampler);
	device->destroy_sampler(this->m_global_linear_sampler);

	device->destroy_asset_image(this->m_cube_emissive_texture);
	device->destroy_asset_buffer(this->m_cube_vertex_position_buffer);
	device->destroy_asset_buffer(this->m_cube_vertex_varying_buffer);

	device->destroy_descriptor_set(this->m_cube_material_set);
}

void Demo::create_render_pass_and_pipeline(pal_device const *device)
{
	assert(NULL == this->m_forward_shading_render_pass);
	assert(NULL == this->m_forward_shading_pipeline);

	// Render Pass
	{
		PAL_RENDER_PASS_COLOR_ATTACHMENT color_attachments[1] = {
			{this->m_swap_chain_image_format,
			 PAL_RENDER_PASS_COLOR_ATTACHMENT_LOAD_OPERATION_CLEAR,
			 PAL_RENDER_PASS_COLOR_ATTACHMENT_STORE_OPERATION_FLUSH_FOR_PRESENT}};

		PAL_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT depth_stencil_attachment = {

			device->get_depth_attachment_image_format(),
			PAL_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_LOAD_OPERATION_CLEAR,
			PAL_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_STORE_OPERATION_DONT_CARE

		};

		this->m_forward_shading_render_pass = device->create_render_pass(sizeof(color_attachments) / sizeof(color_attachments[0]), color_attachments, &depth_stencil_attachment);
	}

	// Pipeline
	{
#if defined(USE_D3D12) && USE_D3D12
		typedef uint8_t BYTE;
#include "../dxbc/forward_shading_vertex.inl"
#include "../dxbc/forward_shading_fragment.inl"
#else
		constexpr uint32_t const forward_shading_vertex_shader_module_code[] = {
#include "../spirv/forward_shading_vertex.inl"
		};

		constexpr uint32_t const forward_shading_fragment_shader_module_code[] = {
#include "../spirv/forward_shading_fragment.inl"
		};
#endif

		PAL_GRAPHICS_PIPELINE_VERTEX_BINDING vertex_bindings[2] = {
			// Position
			{sizeof(float) * 3U},
			// Varying
			{sizeof(uint16_t) * 2U}};

		PAL_GRAPHICS_PIPELINE_VERTEX_ATTRIBUTE vertex_attributes[2] = {
			// Position
			{
				0U,
				PAL_GRAPHICS_PIPELINE_VERTEX_ATTRIBUTE_FORMAT_R32G32B32_SFLOAT,
				0U},
			// UV
			{
				1U,
				PAL_GRAPHICS_PIPELINE_VERTEX_ATTRIBUTE_FORMAT_R16G16_UNORM,
				0U}};

		this->m_forward_shading_pipeline = device->create_graphics_pipeline(this->m_forward_shading_render_pass, this->m_forward_shading_pipeline_layout, sizeof(forward_shading_vertex_shader_module_code), forward_shading_vertex_shader_module_code, sizeof(forward_shading_fragment_shader_module_code), forward_shading_fragment_shader_module_code, sizeof(vertex_bindings) / sizeof(vertex_bindings[0]), vertex_bindings, sizeof(vertex_attributes) / sizeof(vertex_attributes[0]), vertex_attributes, PAL_GRAPHICS_PIPELINE_COMPARE_OPERATION_GREATER);
	}
}

void Demo::destroy_render_pass_and_pipeline(pal_device const *device)
{
	device->destroy_render_pass(this->m_forward_shading_render_pass);
	device->destroy_graphics_pipeline(this->m_forward_shading_pipeline);

	this->m_forward_shading_render_pass = NULL;
	this->m_forward_shading_pipeline = NULL;
}

void Demo::attach_swap_chain(pal_device const *device, pal_swap_chain const *swap_chain)
{
	if (this->m_swap_chain_image_format != swap_chain->get_image_format())
	{
		this->m_swap_chain_image_format = swap_chain->get_image_format();
		this->destroy_render_pass_and_pipeline(device);
		this->create_render_pass_and_pipeline(device);
	}

	assert(0 == this->m_swap_chain_image_width);
	assert(0 == this->m_swap_chain_image_height);
	this->m_swap_chain_image_width = swap_chain->get_image_width();
	this->m_swap_chain_image_height = swap_chain->get_image_height();

	assert(NULL == this->m_depth_stencil_attachment_image);
	this->m_depth_stencil_attachment_image = device->create_depth_stencil_attachment_image(device->get_depth_attachment_image_format(), this->m_swap_chain_image_width, this->m_swap_chain_image_height, false);

	assert(0 == this->m_swap_chain_image_count);
	this->m_swap_chain_image_count = swap_chain->get_image_count();

	assert(NULL == this->m_frame_buffers);
	this->m_frame_buffers = static_cast<pal_frame_buffer **>(malloc(sizeof(pal_frame_buffer *) * this->m_swap_chain_image_count));
	for (uint32_t swap_chain_image_index = 0U; swap_chain_image_index < this->m_swap_chain_image_count; ++swap_chain_image_index)
	{
		pal_color_attachment_image const *color_attachments[1] = {swap_chain->get_image(swap_chain_image_index)};

		this->m_frame_buffers[swap_chain_image_index] = device->create_frame_buffer(this->m_forward_shading_render_pass, this->m_swap_chain_image_width, this->m_swap_chain_image_height, sizeof(color_attachments) / sizeof(color_attachments[0]), color_attachments, this->m_depth_stencil_attachment_image);
	}
}

void Demo::dettach_swap_chain(pal_device const *device, pal_swap_chain const *swap_chain)
{
	assert(swap_chain->get_image_count() == this->m_swap_chain_image_count);

	for (uint32_t swap_chain_image_index = 0U; swap_chain_image_index < this->m_swap_chain_image_count; ++swap_chain_image_index)
	{
		device->destroy_frame_buffer(this->m_frame_buffers[swap_chain_image_index]);
	}

	free(this->m_frame_buffers);

	this->m_frame_buffers = NULL;
	this->m_swap_chain_image_count = 0U;

	device->destroy_depth_stencil_attachment_image(this->m_depth_stencil_attachment_image);

	this->m_depth_stencil_attachment_image = NULL;

	this->m_swap_chain_image_width = 0U;
	this->m_swap_chain_image_height = 0U;
}

void Demo::tick(pal_graphics_command_buffer *command_buffer, uint32_t swap_chain_image_index, void *upload_ring_buffer_base, uint32_t upload_ring_buffer_current, uint32_t upload_ring_buffer_end, uint32_t upload_ring_buffer_offset_alignment)
{
	// Update box and plane
	DirectX::XMFLOAT4X4 box_model_transform;
	{
		this->m_box_spin_angle += (0.7F / 180.0F * DirectX::XM_PI);

		DirectX::XMStoreFloat4x4(&box_model_transform, DirectX::XMMatrixRotationY(this->m_box_spin_angle));
	}

	// Point Light Shadow Pass
	{
		command_buffer->begin_debug_utils_label("Point Light Shadow Pass");

		float depth_clear_value = 1.0F;
		command_buffer->begin_render_pass(this->m_point_light_shadow_render_pass, this->m_point_light_shadow_frame_buffer, g_point_light_shadow_mapping_width_height_size, g_point_light_shadow_mapping_width_height_size, 0U, NULL, &depth_clear_value, NULL);

		command_buffer->bind_graphics_pipeline(this->m_point_light_shadow_pipeline);

		command_buffer->set_view_port(g_point_light_shadow_mapping_width_height_size, g_point_light_shadow_mapping_width_height_size);

		command_buffer->set_scissor(g_point_light_shadow_mapping_width_height_size, g_point_light_shadow_mapping_width_height_size);

		// update constant buffer
		uint32_t global_set_frame_binding_offset = linear_allocate(upload_ring_buffer_current, upload_ring_buffer_end, sizeof(point_light_shadow_layout_global_set_per_frame_uniform_buffer_binding), upload_ring_buffer_offset_alignment);
		{
			point_light_shadow_layout_global_set_per_frame_uniform_buffer_binding *global_set_frame_binding = reinterpret_cast<point_light_shadow_layout_global_set_per_frame_uniform_buffer_binding *>(reinterpret_cast<uintptr_t>(upload_ring_buffer_base) + global_set_frame_binding_offset);
			global_set_frame_binding->g_point_light_position_and_radius = this->m_point_light_position_and_radius;
		}

		// update constant buffer
		uint32_t global_set_box_object_binding_offset = linear_allocate(upload_ring_buffer_current, upload_ring_buffer_end, sizeof(point_light_shadow_layout_global_set_per_object_uniform_buffer_binding), upload_ring_buffer_offset_alignment);
		{
			point_light_shadow_layout_global_set_per_object_uniform_buffer_binding *global_set_box_object_binding = reinterpret_cast<point_light_shadow_layout_global_set_per_object_uniform_buffer_binding *>(reinterpret_cast<uintptr_t>(upload_ring_buffer_base) + global_set_box_object_binding_offset);
			global_set_box_object_binding->g_model_transform = box_model_transform;
		}

		// update constant buffer
		uint32_t global_set_plane_object_binding_offset = linear_allocate(upload_ring_buffer_current, upload_ring_buffer_end, sizeof(point_light_shadow_layout_global_set_per_object_uniform_buffer_binding), upload_ring_buffer_offset_alignment);
		{
			point_light_shadow_layout_global_set_per_object_uniform_buffer_binding *global_set_plane_object_binding = reinterpret_cast<point_light_shadow_layout_global_set_per_object_uniform_buffer_binding *>(reinterpret_cast<uintptr_t>(upload_ring_buffer_base) + global_set_plane_object_binding_offset);
			global_set_plane_object_binding->g_model_transform = this->m_plane_model_transform;
		}

		// draw box
		{
			pal_descriptor_set *descritor_sets[1] = {this->m_point_light_shadow_global_descriptor_set};
			uint32_t dynamic_offsets[2] = {global_set_frame_binding_offset, global_set_box_object_binding_offset};
			command_buffer->bind_descriptor_sets(this->m_point_light_shadow_pipeline_layout, sizeof(descritor_sets) / sizeof(descritor_sets[0]), descritor_sets, sizeof(dynamic_offsets) / sizeof(dynamic_offsets[0]), dynamic_offsets);

			pal_asset_buffer *vertex_buffers[1] = {this->m_cube_vertex_position_buffer};
			command_buffer->bind_vertex_buffers(sizeof(vertex_buffers) / sizeof(vertex_buffers[0]), vertex_buffers);

			command_buffer->draw(g_cube_vertex_count);
		}

		// draw plane
		{
			pal_descriptor_set *descritor_sets[1] = {this->m_point_light_shadow_global_descriptor_set};
			uint32_t dynamic_offsets[2] = {global_set_frame_binding_offset, global_set_box_object_binding_offset};
			command_buffer->bind_descriptor_sets(this->m_point_light_shadow_pipeline_layout, sizeof(descritor_sets) / sizeof(descritor_sets[0]), descritor_sets, sizeof(dynamic_offsets) / sizeof(dynamic_offsets[0]), dynamic_offsets);

			pal_asset_buffer *vertex_buffers[1] = {this->m_cube_vertex_position_buffer};
			command_buffer->bind_vertex_buffers(sizeof(vertex_buffers) / sizeof(vertex_buffers[0]), vertex_buffers);

			command_buffer->draw(g_cube_vertex_count);
		}

		command_buffer->end_render_pass();

		command_buffer->end_debug_utils_label();
	}

	// Main Camera Pass
	{
		command_buffer->begin_debug_utils_label("Main Camera Pass");

		pal_frame_buffer *frame_buffer = this->m_frame_buffers[swap_chain_image_index];

		float color_clear_values[4] = {0.0F, 0.0F, 0.0F, 0.0F};
		float depth_clear_value = 0.0F;
		command_buffer->begin_render_pass(this->m_forward_shading_render_pass, frame_buffer, this->m_swap_chain_image_width, this->m_swap_chain_image_height, 1U, &color_clear_values, &depth_clear_value, NULL);

		command_buffer->bind_graphics_pipeline(this->m_forward_shading_pipeline);

		command_buffer->set_view_port(this->m_swap_chain_image_width, this->m_swap_chain_image_height);

		command_buffer->set_scissor(this->m_swap_chain_image_width, this->m_swap_chain_image_height);

		// update constant buffer
		uint32_t global_set_frame_binding_offset = linear_allocate(upload_ring_buffer_current, upload_ring_buffer_end, sizeof(forward_shading_layout_global_set_frame_uniform_buffer_binding), upload_ring_buffer_offset_alignment);
		{
			forward_shading_layout_global_set_frame_uniform_buffer_binding *global_set_frame_binding = reinterpret_cast<forward_shading_layout_global_set_frame_uniform_buffer_binding *>(reinterpret_cast<uintptr_t>(upload_ring_buffer_base) + global_set_frame_binding_offset);

			// update camera
			DirectX::XMFLOAT3 eye_position = g_camera_controller.m_eye_position;
			DirectX::XMFLOAT3 eye_direction = g_camera_controller.m_eye_direction;
			DirectX::XMFLOAT3 up_direction = g_camera_controller.m_up_direction;

			DirectX::XMFLOAT4X4 view_transform;
			DirectX::XMStoreFloat4x4(&view_transform, DirectX::XMMatrixLookToRH(DirectX::XMLoadFloat3(&eye_position), DirectX::XMLoadFloat3(&eye_direction), DirectX::XMLoadFloat3(&up_direction)));

			DirectX::XMFLOAT4X4 projection_transform;
			{
				DirectX::XMMATRIX real_projection_transfrom = DirectX_Math_Matrix_PerspectiveFovRH_ReversedZ(0.785F, static_cast<float>(this->m_swap_chain_image_width) / static_cast<float>(this->m_swap_chain_image_height), 0.1F, 100.0F);

				// vulkan viewport flip y
				DirectX::XMFLOAT4X4 vulkan_viewport_filp_y_transfrom;
				{
					vulkan_viewport_filp_y_transfrom.m[0][0] = 1.0F;
					vulkan_viewport_filp_y_transfrom.m[0][1] = 0.0F;
					vulkan_viewport_filp_y_transfrom.m[0][2] = 0.0F;
					vulkan_viewport_filp_y_transfrom.m[0][3] = 0.0F;
					vulkan_viewport_filp_y_transfrom.m[1][0] = 0.0F;
					vulkan_viewport_filp_y_transfrom.m[1][1] = -1.0F;
					vulkan_viewport_filp_y_transfrom.m[1][2] = 0.0F;
					vulkan_viewport_filp_y_transfrom.m[1][3] = 0.0F;
					vulkan_viewport_filp_y_transfrom.m[2][0] = 0.0F;
					vulkan_viewport_filp_y_transfrom.m[2][1] = 0.0F;
					vulkan_viewport_filp_y_transfrom.m[2][2] = 1.0F;
					vulkan_viewport_filp_y_transfrom.m[2][3] = 0.0F;
					vulkan_viewport_filp_y_transfrom.m[3][0] = 0.0F;
					vulkan_viewport_filp_y_transfrom.m[3][1] = 0.0F;
					vulkan_viewport_filp_y_transfrom.m[3][2] = 0.0F;
					vulkan_viewport_filp_y_transfrom.m[3][3] = 1.0F;
				}

#if defined(USE_D3D12) && USE_D3D12
				DirectX::XMStoreFloat4x4(&projection_transform, real_projection_transfrom);
#elif defined(USE_AGC) && USE_AGC
				DirectX::XMStoreFloat4x4(&projection_transform, real_projection_transfrom);
#else
				DirectX::XMStoreFloat4x4(&projection_transform, DirectX::XMMatrixMultiply(real_projection_transfrom, DirectX::XMLoadFloat4x4(&vulkan_viewport_filp_y_transfrom)));
#endif
			}

			global_set_frame_binding->g_view_transform = view_transform;
			global_set_frame_binding->g_projection_transform = projection_transform;
			global_set_frame_binding->g_point_light_position_and_radius = this->m_point_light_position_and_radius;
			global_set_frame_binding->g_point_light_shadow_bias = this->m_point_light_shadow_bias;
		}

		// update constant buffer
		uint32_t global_set_box_object_binding_offset = linear_allocate(upload_ring_buffer_current, upload_ring_buffer_end, sizeof(forward_shading_layout_global_set_object_uniform_buffer_binding), upload_ring_buffer_offset_alignment);
		{
			forward_shading_layout_global_set_object_uniform_buffer_binding *global_set_box_object_binding = reinterpret_cast<forward_shading_layout_global_set_object_uniform_buffer_binding *>(reinterpret_cast<uintptr_t>(upload_ring_buffer_base) + global_set_box_object_binding_offset);
			global_set_box_object_binding->g_model_transform = box_model_transform;
		}

		// update constant buffer
		uint32_t global_set_plane_object_binding_offset = linear_allocate(upload_ring_buffer_current, upload_ring_buffer_end, sizeof(forward_shading_layout_global_set_object_uniform_buffer_binding), upload_ring_buffer_offset_alignment);
		{
			forward_shading_layout_global_set_object_uniform_buffer_binding *global_set_plane_object_binding = reinterpret_cast<forward_shading_layout_global_set_object_uniform_buffer_binding *>(reinterpret_cast<uintptr_t>(upload_ring_buffer_base) + global_set_plane_object_binding_offset);
			global_set_plane_object_binding->g_model_transform = this->m_plane_model_transform;
		}

		// draw box
		{
			pal_descriptor_set *descritor_sets[2] = {this->m_forward_shading_global_descriptor_set, this->m_cube_material_set};
			uint32_t dynamic_offsets[2] = {global_set_frame_binding_offset, global_set_box_object_binding_offset};
			command_buffer->bind_descriptor_sets(this->m_forward_shading_pipeline_layout, sizeof(descritor_sets) / sizeof(descritor_sets[0]), descritor_sets, sizeof(dynamic_offsets) / sizeof(dynamic_offsets[0]), dynamic_offsets);

			pal_asset_buffer *vertex_buffers[2] = {this->m_cube_vertex_position_buffer, this->m_cube_vertex_varying_buffer};
			command_buffer->bind_vertex_buffers(sizeof(vertex_buffers) / sizeof(vertex_buffers[0]), vertex_buffers);

			command_buffer->draw(g_cube_vertex_count);
		}

		// draw plane
		{
			pal_descriptor_set *descritor_sets[2] = {this->m_forward_shading_global_descriptor_set, this->m_cube_material_set};
			uint32_t dynamic_offsets[2] = {global_set_frame_binding_offset, global_set_plane_object_binding_offset};
			command_buffer->bind_descriptor_sets(this->m_forward_shading_pipeline_layout, sizeof(descritor_sets) / sizeof(descritor_sets[0]), descritor_sets, sizeof(dynamic_offsets) / sizeof(dynamic_offsets[0]), dynamic_offsets);

			pal_asset_buffer *vertex_buffers[2] = {this->m_cube_vertex_position_buffer, this->m_cube_vertex_varying_buffer};
			command_buffer->bind_vertex_buffers(sizeof(vertex_buffers) / sizeof(vertex_buffers[0]), vertex_buffers);

			command_buffer->draw(g_cube_vertex_count);
		}

		command_buffer->end_render_pass();

		command_buffer->end_debug_utils_label();
	}
}

static inline uint32_t linear_allocate(uint32_t &buffer_current, uint32_t buffer_end, uint32_t size, uint32_t alignment)
{
	uint32_t buffer_offset = utils_align_up(buffer_current, alignment);
	buffer_current = (buffer_offset + size);
	assert(buffer_current < buffer_end);
	return buffer_offset;
}

static inline DirectX::XMMATRIX XM_CALLCONV DirectX_Math_Matrix_PerspectiveFovRH_ReversedZ(float FovAngleY, float AspectRatio, float NearZ, float FarZ)
{
	// [Reversed-Z](https://developer.nvidia.com/content/depth-precision-visualized)
	//
	// _  0  0  0
	// 0  _  0  0
	// 0  0  b -1
	// 0  0  a  0
	//
	// _  0  0  0
	// 0  _  0  0
	// 0  0 zb  -z
	// 0  0  a
	//
	// z' = -b - a/z
	//
	// Standard
	// 0 = -b + a/nearz // z=-nearz
	// 1 = -b + a/farz  // z=-farz
	// a = farz*nearz/(nearz - farz)
	// b = farz/(nearz - farz)
	//
	// Reversed-Z
	// 1 = -b + a/nearz // z=-nearz
	// 0 = -b + a/farz  // z=-farz
	// a = farz*nearz/(farz - nearz)
	// b = nearz/(farz - nearz)

	// __m128 _mm_shuffle_ps(__m128 lo,__m128 hi, _MM_SHUFFLE(hi3,hi2,lo1,lo0))
	// Interleave inputs into low 2 floats and high 2 floats of output. Basically
	// out[0]=lo[lo0];
	// out[1]=lo[lo1];
	// out[2]=hi[hi2];
	// out[3]=hi[hi3];

	// DirectX::XMMatrixPerspectiveFovRH

	float SinFov;
	float CosFov;
	DirectX::XMScalarSinCos(&SinFov, &CosFov, 0.5F * FovAngleY);

	float Height = CosFov / SinFov;
	float Width = Height / AspectRatio;
	float b = NearZ / (FarZ - NearZ);
	float a = (FarZ / (FarZ - NearZ)) * NearZ;
#if defined(_XM_SSE_INTRINSICS_)
	// Note: This is recorded on the stack
	DirectX::XMVECTOR rMem = {
		Width,
		Height,
		b,
		a};

	// Copy from memory to SSE register
	DirectX::XMVECTOR vValues = rMem;
	DirectX::XMVECTOR vTemp = _mm_setzero_ps();
	// Copy x only
	vTemp = _mm_move_ss(vTemp, vValues);
	// CosFov / SinFov,0,0,0
	DirectX::XMMATRIX M;
	M.r[0] = vTemp;
	// 0,Height / AspectRatio,0,0
	vTemp = vValues;
	vTemp = _mm_and_ps(vTemp, DirectX::g_XMMaskY);
	M.r[1] = vTemp;
	// x=b,y=a,0,-1.0f
	vTemp = _mm_setzero_ps();
	vValues = _mm_shuffle_ps(vValues, DirectX::g_XMNegIdentityR3, _MM_SHUFFLE(3, 2, 3, 2));
	// 0,0,b,-1.0f
	vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(3, 0, 0, 0));
	M.r[2] = vTemp;
	// 0,0,a,0.0f
	vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(2, 1, 0, 0));
	M.r[3] = vTemp;
	return M;
#elif defined(_XM_ARM_NEON_INTRINSICS_)
	const float32x4_t Zero = vdupq_n_f32(0);

	DirectX::XMMATRIX M;
	M.r[0] = vsetq_lane_f32(Width, Zero, 0);
	M.r[1] = vsetq_lane_f32(Height, Zero, 1);
	M.r[2] = vsetq_lane_f32(b, DirectX::g_XMNegIdentityR3.v, 2);
	M.r[3] = vsetq_lane_f32(a, Zero, 2);
	return M;
#endif
}
