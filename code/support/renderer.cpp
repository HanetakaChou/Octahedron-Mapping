//
// Copyright (C) YuqiaoZhang(HanetakaChou)
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

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <vector>

#include "renderer.h"
#include "frame_throttling.h"
#include "../../thirdparty/PAL/include/pal.h"
#include "../demo.h"

class renderer
{
	pal_device *m_device;

	pal_graphics_queue *m_graphics_queue;

	uint32_t m_frame_throtting_index;

	pal_graphics_command_buffer *m_command_buffers[FRAME_THROTTLING_COUNT];
	pal_fence *m_fences[FRAME_THROTTLING_COUNT];

	pal_upload_ring_buffer *m_upload_ring_buffer;
	void *m_upload_ring_buffer_base;
	uint32_t m_upload_ring_buffer_offset_alignment;
	uint32_t m_upload_ring_buffer_begin[FRAME_THROTTLING_COUNT];
	uint32_t m_upload_ring_buffer_end[FRAME_THROTTLING_COUNT];

	pal_surface *m_surface;
	pal_swap_chain *m_swap_chain;

	Demo m_demo;

public:
	renderer();

	~renderer();

	void init();

	void destroy();

	void attach_window(void *window);

	void dettach_window();

	void draw();
};

renderer::renderer() : m_surface(NULL), m_swap_chain(NULL)
{
}

renderer::~renderer()
{
	for (uint32_t frame_throtting_index = 0U; frame_throtting_index < FRAME_THROTTLING_COUNT; ++frame_throtting_index)
	{
		assert(NULL == this->m_command_buffers[frame_throtting_index]);
		assert(NULL == this->m_fences[frame_throtting_index]);
	}

	assert(NULL == this->m_upload_ring_buffer);

	assert(NULL == this->m_surface);
	assert(NULL == this->m_swap_chain);

	assert(NULL == this->m_device);
}

extern renderer *renderer_init()
{
	renderer *new_renderer = new (malloc(sizeof(renderer))) renderer{};
	new_renderer->init();
	return new_renderer;
}

extern void renderer_destroy(renderer *renderer)
{
	renderer->destroy();
	renderer->~renderer();
	free(renderer);
}

extern void renderer_attach_window(renderer *renderer, void *window)
{
	renderer->attach_window(window);
}

extern void renderer_dettach_window(renderer *renderer)
{
	renderer->dettach_window();
}

extern void renderer_draw(renderer *renderer)
{
	renderer->draw();
}

void renderer::init()
{
#if defined(USE_D3D12) && USE_D3D12
	this->m_device = pal_init_d3d12_device();
#else
	this->m_device = pal_init_vk_device();
#endif

	this->m_graphics_queue = this->m_device->create_graphics_queue();

	this->m_frame_throtting_index = 0U;

	// NVIDIA Driver 128 MB
	// \[Gruen 2015\] [Holger Gruen. "Constant Buffers without Constant Pain." NVIDIA GameWorks Blog 2015.](https://developer.nvidia.com/content/constant-buffers-without-constant-pain-0)
	// AMD Special Pool 256MB
	// \[Sawicki 2018\] [Adam Sawicki. "Memory Management in Vulkan and DX12." GDC 2018.](https://gpuopen.com/events/gdc-2018-presentations)
	uint32_t upload_ring_buffer_size = (224U * 1024U * 1024U); // 224MB
	this->m_upload_ring_buffer = this->m_device->create_upload_ring_buffer(upload_ring_buffer_size);
	this->m_upload_ring_buffer_base = this->m_upload_ring_buffer->get_memory_range_base();
	this->m_upload_ring_buffer_offset_alignment = this->m_device->get_upload_ring_buffer_offset_alignment();

	for (uint32_t frame_throtting_index = 0U; frame_throtting_index < FRAME_THROTTLING_COUNT; ++frame_throtting_index)
	{
		this->m_command_buffers[frame_throtting_index] = this->m_device->create_graphics_command_buffer();
		this->m_fences[frame_throtting_index] = this->m_device->create_fence(true);

		this->m_upload_ring_buffer_begin[frame_throtting_index] = upload_ring_buffer_size * frame_throtting_index / FRAME_THROTTLING_COUNT;
		this->m_upload_ring_buffer_end[frame_throtting_index] = upload_ring_buffer_size * (frame_throtting_index + 1U) / FRAME_THROTTLING_COUNT;
	}

	// Demo Init
	this->m_demo.init(this->m_device, this->m_upload_ring_buffer);
}

void renderer::destroy()
{
	for (uint32_t frame_throtting_index = 0U; frame_throtting_index < FRAME_THROTTLING_COUNT; ++frame_throtting_index)
	{
		this->m_device->wait_for_fence(this->m_fences[frame_throtting_index]);
	}

	this->m_demo.destroy(this->m_device);

	for (uint32_t frame_throtting_index = 0U; frame_throtting_index < FRAME_THROTTLING_COUNT; ++frame_throtting_index)
	{
		this->m_device->destroy_graphics_command_buffer(this->m_command_buffers[frame_throtting_index]);
		this->m_command_buffers[frame_throtting_index] = NULL;
		this->m_device->destroy_fence(this->m_fences[frame_throtting_index]);
		this->m_fences[frame_throtting_index] = NULL;
	}

	this->m_device->destroy_upload_ring_buffer(this->m_upload_ring_buffer);
	this->m_upload_ring_buffer = NULL;

#if defined(USE_D3D12) && USE_D3D12
	pal_destroy_d3d12_device(this->m_device);
#else
	pal_destroy_vk_device(this->m_device);
#endif
	this->m_device = NULL;
}

void renderer::attach_window(void *window)
{
	assert(NULL == this->m_surface);
	assert(NULL == this->m_swap_chain);

	this->m_surface = this->m_device->create_surface(window);
	this->m_swap_chain = this->m_device->create_swap_chain(this->m_surface);
	this->m_demo.attach_swap_chain(this->m_device, this->m_swap_chain);
}

void renderer::dettach_window()
{
	for (uint32_t frame_throtting_index = 0U; frame_throtting_index < FRAME_THROTTLING_COUNT; ++frame_throtting_index)
	{
		this->m_device->wait_for_fence(this->m_fences[frame_throtting_index]);
	}

	this->m_demo.dettach_swap_chain(this->m_device, this->m_swap_chain);
	this->m_device->destroy_swap_chain(this->m_swap_chain);
	this->m_device->destroy_surface(this->m_surface);

	this->m_surface = NULL;
	this->m_swap_chain = NULL;
}

void renderer::draw()
{
	if (NULL == this->m_surface)
	{
		// skip this frame
		return;
	}

	assert(NULL != this->m_swap_chain);

	this->m_device->wait_for_fence(this->m_fences[this->m_frame_throtting_index]);

	this->m_device->reset_graphics_command_buffer(this->m_command_buffers[this->m_frame_throtting_index]);

	uint32_t swap_chain_image_index = -1;
	bool acquire_next_image_not_out_of_date = this->m_device->acquire_next_image(this->m_command_buffers[this->m_frame_throtting_index], this->m_swap_chain, &swap_chain_image_index);
	if (!acquire_next_image_not_out_of_date)
	{
		for (uint32_t frame_throtting_index = 0U; frame_throtting_index < FRAME_THROTTLING_COUNT; ++frame_throtting_index)
		{
			this->m_device->wait_for_fence(this->m_fences[frame_throtting_index]);
		}

		this->m_demo.dettach_swap_chain(this->m_device, this->m_swap_chain);
		this->m_device->destroy_swap_chain(this->m_swap_chain);
		this->m_swap_chain = this->m_device->create_swap_chain(this->m_surface);
		this->m_demo.attach_swap_chain(this->m_device, this->m_swap_chain);

		// skip this frame
		this->m_command_buffers[this->m_frame_throtting_index]->end();
		return;
	}

	this->m_command_buffers[this->m_frame_throtting_index]->begin();

	this->m_demo.tick(this->m_command_buffers[this->m_frame_throtting_index], swap_chain_image_index, this->m_upload_ring_buffer_base, this->m_upload_ring_buffer_begin[this->m_frame_throtting_index], this->m_upload_ring_buffer_end[this->m_frame_throtting_index], this->m_upload_ring_buffer_offset_alignment);

	this->m_command_buffers[this->m_frame_throtting_index]->end();

	this->m_device->reset_fence(this->m_fences[this->m_frame_throtting_index]);

	bool present_not_out_of_date = this->m_graphics_queue->submit_and_present(this->m_command_buffers[this->m_frame_throtting_index], this->m_swap_chain, swap_chain_image_index, this->m_fences[this->m_frame_throtting_index]);
	if (!present_not_out_of_date)
	{
		for (uint32_t frame_throtting_index = 0U; frame_throtting_index < FRAME_THROTTLING_COUNT; ++frame_throtting_index)
		{
			this->m_device->wait_for_fence(this->m_fences[frame_throtting_index]);
		}

		this->m_demo.dettach_swap_chain(this->m_device, this->m_swap_chain);
		this->m_device->destroy_swap_chain(this->m_swap_chain);
		this->m_swap_chain = this->m_device->create_swap_chain(this->m_surface);
		this->m_demo.attach_swap_chain(this->m_device, this->m_swap_chain);

		// continue this frame
	}

	++this->m_frame_throtting_index;
	this->m_frame_throtting_index %= FRAME_THROTTLING_COUNT;
}
