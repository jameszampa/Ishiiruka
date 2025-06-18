// Copyright 2024 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"
#include "Common/Logging/Log.h"

#include "Core/Core.h"
#include "Core/HW/Memmap.h"

#include "VideoBackends/Headless/HeadlessRenderer.h"

#include "VideoCommon/BoundingBox.h"
#include "VideoCommon/Fifo.h"
#include "VideoCommon/ImageWrite.h"
#include "VideoCommon/OnScreenDisplay.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/FramebufferManagerBase.h"

namespace Headless
{

// Static member initialization
unsigned int HeadlessRenderer::s_target_width = 640;
unsigned int HeadlessRenderer::s_target_height = 480;
u8* HeadlessRenderer::s_efb_color_buffer = nullptr;
u8* HeadlessRenderer::s_efb_depth_buffer = nullptr;

HeadlessRenderer::HeadlessRenderer()
{
}

HeadlessRenderer::~HeadlessRenderer()
{
}

void HeadlessRenderer::Init()
{
	s_target_width = EFB_WIDTH;
	s_target_height = EFB_HEIGHT;
	
	// Allocate EFB buffers
	if (!s_efb_color_buffer)
		s_efb_color_buffer = new u8[s_target_width * s_target_height * 4];
	if (!s_efb_depth_buffer)
		s_efb_depth_buffer = new u8[s_target_width * s_target_height * 4];
	
	// Clear buffers
	memset(s_efb_color_buffer, 0, s_target_width * s_target_height * 4);
	memset(s_efb_depth_buffer, 0, s_target_width * s_target_height * 4);
}

void HeadlessRenderer::Shutdown()
{
	if (s_efb_color_buffer)
	{
		delete[] s_efb_color_buffer;
		s_efb_color_buffer = nullptr;
	}
	if (s_efb_depth_buffer)
	{
		delete[] s_efb_depth_buffer;
		s_efb_depth_buffer = nullptr;
	}
}

void HeadlessRenderer::RenderText(const std::string& pstr, int left, int top, u32 color)
{
	// No text rendering in headless mode
}

u32 HeadlessRenderer::AccessEFB(EFBAccessType type, u32 x, u32 y, u32 poke_data)
{
	u32 value = 0;

	switch (type)
	{
	case EFBAccessType::PeekZ:
	{
		if (x < s_target_width && y < s_target_height)
		{
			u32 offset = (y * s_target_width + x) * 4;
			memcpy(&value, &s_efb_depth_buffer[offset], 4);
		}
		break;
	}
	case EFBAccessType::PeekColor:
	{
		if (x < s_target_width && y < s_target_height)
		{
			u32 offset = (y * s_target_width + x) * 4;
			memcpy(&value, &s_efb_color_buffer[offset], 4);
		}
		break;
	}
	default:
		break;
	}

	return value;
}

void HeadlessRenderer::PokeEFB(EFBAccessType type, const EfbPokeData* points, size_t num_points)
{
	for (size_t i = 0; i < num_points; i++)
	{
		u32 x = points[i].x;
		u32 y = points[i].y;
		u32 data = points[i].data;

		if (x >= s_target_width || y >= s_target_height)
			continue;

		u32 offset = (y * s_target_width + x) * 4;

		switch (type)
		{
		case EFBAccessType::PokeZ:
			memcpy(&s_efb_depth_buffer[offset], &data, 4);
			break;
		case EFBAccessType::PokeColor:
			memcpy(&s_efb_color_buffer[offset], &data, 4);
			break;
		default:
			break;
		}
	}
}

u16 HeadlessRenderer::BBoxRead(int index)
{
	return BoundingBox::coords[index];
}

void HeadlessRenderer::BBoxWrite(int index, u16 value)
{
	BoundingBox::coords[index] = value;
}

TargetRectangle HeadlessRenderer::ConvertEFBRectangle(const EFBRectangle& rc)
{
	TargetRectangle result;
	result.left = rc.left;
	result.top = rc.top;
	result.right = rc.right;
	result.bottom = rc.bottom;
	return result;
}

void HeadlessRenderer::SwapImpl(u32 xfbAddr, u32 fbWidth, u32 fbStride, u32 fbHeight, const EFBRectangle& rc, u64 ticks, float Gamma)
{
	if ((!m_xfb_written && !g_ActiveConfig.RealXFBEnabled()) || !fbWidth || !fbHeight)
	{
		Core::Callback_VideoCopiedToXFB(false);
		return;
	}

	u32 xfbCount = 0;
	const XFBSourceBase* const* xfbSourceList = FramebufferManagerBase::GetXFBSource(xfbAddr, fbStride, fbHeight, &xfbCount);
	if (g_ActiveConfig.VirtualXFBEnabled() && (!xfbSourceList || xfbCount == 0))
	{
		Core::Callback_VideoCopiedToXFB(false);
		return;
	}

	if (!g_ActiveConfig.bUseXFB)
		m_post_processor->OnEndFrame();

	UpdateDrawRectangle();
	TargetRectangle targetRc = GetTargetRectangle();

	// Dump frames if enabled
	if (IsFrameDumping())
	{
		DumpFrame(rc, xfbAddr, xfbSourceList, xfbCount, fbWidth, fbStride, fbHeight, ticks);
	}

	// Update active config
	UpdateActiveConfig();

	// Call the callback to notify that a frame was rendered
	Core::Callback_VideoCopiedToXFB(m_xfb_written || (g_ActiveConfig.bUseXFB && g_ActiveConfig.bUseRealXFB));
}

void HeadlessRenderer::ClearScreen(const EFBRectangle& rc, bool colorEnable, bool alphaEnable, bool zEnable, u32 color, u32 z)
{
	// Clear the EFB buffers
	if (colorEnable)
	{
		for (int y = rc.top; y < rc.bottom; y++)
		{
			for (int x = rc.left; x < rc.right; x++)
			{
				if (x >= 0 && x < (int)s_target_width && y >= 0 && y < (int)s_target_height)
				{
					u32 offset = (y * s_target_width + x) * 4;
					memcpy(&s_efb_color_buffer[offset], &color, 4);
				}
			}
		}
	}

	if (zEnable)
	{
		for (int y = rc.top; y < rc.bottom; y++)
		{
			for (int x = rc.left; x < rc.right; x++)
			{
				if (x >= 0 && x < (int)s_target_width && y >= 0 && y < (int)s_target_height)
				{
					u32 offset = (y * s_target_width + x) * 4;
					memcpy(&s_efb_depth_buffer[offset], &z, 4);
				}
			}
		}
	}
}

void HeadlessRenderer::ReinterpretPixelData(unsigned int convtype)
{
	// No pixel reinterpretation needed in headless mode
}

void HeadlessRenderer::ApplyState(bool bUseDstAlpha)
{
	// No state application needed in headless mode
}

void HeadlessRenderer::DumpFrame(const EFBRectangle& source_rc, u32 xfb_addr,
	const XFBSourceBase* const* xfb_sources, u32 xfb_count, u32 fb_width,
	u32 fb_stride, u32 fb_height, u64 ticks)
{
	// Get the frame data from EFB
	u32 frame_width = source_rc.GetWidth();
	u32 frame_height = source_rc.GetHeight();
	
	if (frame_width == 0 || frame_height == 0)
		return;

	// Allocate buffer for frame data
	u8* frame_data = new u8[frame_width * frame_height * 4];
	
	// Copy EFB data to frame buffer
	for (u32 y = 0; y < frame_height; y++)
	{
		for (u32 x = 0; x < frame_width; x++)
		{
			u32 src_x = source_rc.left + x;
			u32 src_y = source_rc.top + y;
			
			if (src_x < s_target_width && src_y < s_target_height)
			{
				u32 src_offset = (src_y * s_target_width + src_x) * 4;
				u32 dst_offset = (y * frame_width + x) * 4;
				memcpy(&frame_data[dst_offset], &s_efb_color_buffer[src_offset], 4);
			}
		}
	}

	// Create frame state for AVI dump
	AVIDump::Frame frame_state = AVIDump::FetchState(ticks);
	
	// Dump the frame data
	DumpFrameData(frame_data, frame_width, frame_height, frame_width * 4, frame_state, false, false);
	
	delete[] frame_data;
}

// Static method implementations
void HeadlessRenderer::CopyEFB(u8* dst, const EFBCopyFormat& format, u32 native_width, u32 bytes_per_row,
	u32 num_blocks_y, u32 memory_stride, bool is_depth_copy,
	const EFBRectangle& src_rect, bool scale_by_half)
{
	if (!dst)
		return;

	u8* src_buffer = is_depth_copy ? s_efb_depth_buffer : s_efb_color_buffer;
	
	for (u32 y = 0; y < num_blocks_y; y++)
	{
		for (u32 x = 0; x < native_width; x++)
		{
			u32 src_x = src_rect.left + x;
			u32 src_y = src_rect.top + y;
			
			if (src_x < s_target_width && src_y < s_target_height)
			{
				u32 src_offset = (src_y * s_target_width + src_x) * 4;
				u32 dst_offset = y * memory_stride + x * 4;
				
				if (dst_offset + 3 < native_width * num_blocks_y * 4)
				{
					memcpy(&dst[dst_offset], &src_buffer[src_offset], 4);
				}
			}
		}
	}
}

void HeadlessRenderer::CopyToRealXFB(u32 xfbAddr, u32 fbStride, u32 fbHeight, const EFBRectangle& sourceRc, float Gamma)
{
	// Copy EFB data to XFB memory
	u8* xfb_ptr = Memory::GetPointer(xfbAddr);
	if (!xfb_ptr)
		return;

	u32 frame_width = sourceRc.GetWidth();
	u32 frame_height = sourceRc.GetHeight();
	
	for (u32 y = 0; y < frame_height; y++)
	{
		for (u32 x = 0; x < frame_width; x++)
		{
			u32 src_x = sourceRc.left + x;
			u32 src_y = sourceRc.top + y;
			
			if (src_x < s_target_width && src_y < s_target_height)
			{
				u32 src_offset = (src_y * s_target_width + src_x) * 4;
				u32 dst_offset = y * fbStride + x * 4;
				
				if (dst_offset + 3 < fbStride * fbHeight)
				{
					memcpy(&xfb_ptr[dst_offset], &s_efb_color_buffer[src_offset], 4);
				}
			}
		}
	}
}

unsigned int HeadlessRenderer::GetTargetWidth()
{
	return s_target_width;
}

unsigned int HeadlessRenderer::GetTargetHeight()
{
	return s_target_height;
}

} 