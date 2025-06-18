// Copyright 2024 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>

#include "Common/CommonTypes.h"
#include "VideoCommon/RenderBase.h"
#include "VideoCommon/AVIDump.h"

// Forward declarations
class EFBRectangle;
struct EFBCopyFormat;
class PostProcessor;
class XFBSourceBase;

namespace Headless
{

class HeadlessRenderer : public Renderer
{
public:
	HeadlessRenderer();
	~HeadlessRenderer();

	void Init() override;
	void Shutdown() override;

	// State management methods (no-op in headless mode)
	void SetColorMask() override {}
	void SetBlendMode(bool forceUpdate) override {}
	void SetScissorRect(const EFBRectangle& rc) override {}
	void SetGenerationMode() override {}
	void SetDepthMode() override {}
	void SetLogicOpMode() override {}
	void SetSamplerState(int stage, int texindex, bool custom_tex) override {}
	void SetInterlacingMode() override {}
	void SetViewport() override {}
	void SetFullscreen(bool enable_fullscreen) override {}
	bool IsFullscreen() const override { return false; }
	void RestoreState() override {}
	void ResetAPIState() override {}
	void RestoreAPIState() override {}
	void ChangeSurface(void* new_surface_handle) override {}

	// Required virtual methods
	void RenderText(const std::string& pstr, int left, int top, u32 color) override;
	u32 AccessEFB(EFBAccessType type, u32 x, u32 y, u32 poke_data) override;
	void PokeEFB(EFBAccessType type, const EfbPokeData* points, size_t num_points) override;
	u16 BBoxRead(int index) override;
	void BBoxWrite(int index, u16 value) override;
	TargetRectangle ConvertEFBRectangle(const EFBRectangle& rc) override;
	void SwapImpl(u32 xfbAddr, u32 fbWidth, u32 fbStride, u32 fbHeight, const EFBRectangle& rc, u64 ticks, float Gamma) override;
	void ClearScreen(const EFBRectangle& rc, bool colorEnable, bool alphaEnable, bool zEnable, u32 color, u32 z) override;
	void ReinterpretPixelData(unsigned int convtype) override;
	void ApplyState(bool bUseDstAlpha) override;

	// Frame dumping
	void DumpFrame(const EFBRectangle& source_rc, u32 xfb_addr,
		const XFBSourceBase* const* xfb_sources, u32 xfb_count, u32 fb_width,
		u32 fb_stride, u32 fb_height, u64 ticks);

	// Static methods for EFB access
	static void CopyEFB(u8* dst, const EFBCopyFormat& format, u32 native_width, u32 bytes_per_row,
		u32 num_blocks_y, u32 memory_stride, bool is_depth_copy,
		const EFBRectangle& src_rect, bool scale_by_half);
	static void CopyToRealXFB(u32 xfbAddr, u32 fbStride, u32 fbHeight, const EFBRectangle& sourceRc, float Gamma);
	static unsigned int GetTargetWidth();
	static unsigned int GetTargetHeight();

private:
	// Static member variables
	static unsigned int s_target_width;
	static unsigned int s_target_height;
	static u8* s_efb_color_buffer;
	static u8* s_efb_depth_buffer;
};

} // namespace Headless 