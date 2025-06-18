// Copyright 2024 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <atomic>
#include <memory>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/Logging/LogManager.h"

#include "Core/Host.h"

#include "VideoBackends/Headless/VideoBackend.h"
#include "VideoBackends/Headless/HeadlessRenderer.h"
#include "VideoBackends/Headless/HeadlessVertexLoader.h"

#include "VideoCommon/BPStructs.h"
#include "VideoCommon/CommandProcessor.h"
#include "VideoCommon/CPMemory.h"
#include "VideoCommon/Fifo.h"
#include "VideoCommon/FramebufferManagerBase.h"
#include "VideoCommon/IndexGenerator.h"
#include "VideoCommon/OnScreenDisplay.h"
#include "VideoCommon/OpcodeDecoding.h"
#include "VideoCommon/PixelEngine.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/TextureCacheBase.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoBackends/Software/DebugUtil.h"

namespace Headless
{

class PerfQuery: public PerfQueryBase
{
public:
	PerfQuery() {}
	~PerfQuery() {}

	void EnableQuery(PerfQueryGroup type) override {}
	void DisableQuery(PerfQueryGroup type) override {}
	void ResetQuery() override {}
	u32 GetQueryResult(PerfQueryType type) override { return 0; }
	void FlushResults() override {}
	bool IsFlushed() const override { return true; }
};

class TextureCache: public TextureCacheBase
{
public:
	virtual PC_TexFormat GetNativeTextureFormat(const s32 texformat,
		const TlutFormat tlutfmt, u32 width, u32 height) override
	{
		return PC_TexFormat::PC_TEX_FMT_RGBA32;
	}
	bool CompileShaders() override { return true; }
	void DeleteShaders() override {}
	bool Palettize(TCacheEntryBase* entry, const TCacheEntryBase* base_entry) override
	{
		return false;
	}
	void CopyEFB(u8* dst, const EFBCopyFormat& format, u32 native_width, u32 bytes_per_row,
		u32 num_blocks_y, u32 memory_stride, bool is_depth_copy,
		const EFBRectangle& src_rect, bool scale_by_half) override
	{
		// Copy EFB data for frame dumping
		HeadlessRenderer::CopyEFB(dst, format, native_width, bytes_per_row, num_blocks_y, memory_stride, is_depth_copy, src_rect, scale_by_half);
	}
	void LoadLut(u32 lutFmt, void* addr, u32 size) override {}

private:
	struct TCacheEntry: TCacheEntryBase
	{
		TCacheEntry(const TCacheEntryConfig& _config): TCacheEntryBase(_config) {}
		~TCacheEntry() {}

		void Load(const u8* src, u32 width, u32 height,
			u32 expanded_width, u32 level) override {}
		bool SupportsMaterialMap() const override { return false; }

		void FromRenderTarget(bool is_depth_copy, const EFBRectangle& srcRect,
			bool scaleByHalf, unsigned int cbufid, const float *colmat, u32 width, u32 height) override
		{
			HeadlessRenderer::CopyEFB(nullptr, EFBCopyFormat{}, width, width * 4, height, width * 4, is_depth_copy, srcRect, scaleByHalf);
		}

		void CopyRectangleFromTexture(
			const TCacheEntryBase* source,
			const MathUtil::Rectangle<int>& srcrect,
			const MathUtil::Rectangle<int>& dstrect) override {}

		void Bind(u32 stage) override {}

		bool Save(const std::string& filename, u32 level) override { return false; }

		uintptr_t GetInternalObject() override { return 0; }
	};

	TCacheEntryBase* CreateTexture(const TCacheEntryConfig& config) override
	{
		return new TCacheEntry(config);
	}
};

class XFBSource: public XFBSourceBase
{
	void DecodeToTexture(u32 xfbAddr, u32 fbWidth, u32 fbHeight) override {}
	void CopyEFB(float Gamma) override {}
};

class FramebufferManager: public FramebufferManagerBase
{
	std::unique_ptr<XFBSourceBase> CreateXFBSource(unsigned int target_width, unsigned int target_height, unsigned int layers) override
	{
		return std::make_unique<XFBSource>();
	}
	void GetTargetSize(unsigned int* width, unsigned int* height) override
	{
		*width = HeadlessRenderer::GetTargetWidth();
		*height = HeadlessRenderer::GetTargetHeight();
	}
	void CopyToRealXFB(u32 xfbAddr, u32 fbStride, u32 fbHeight, const EFBRectangle& sourceRc, float Gamma = 1.0f) override
	{
		HeadlessRenderer::CopyToRealXFB(xfbAddr, fbStride, fbHeight, sourceRc, Gamma);
	}
};

std::string VideoBackend::GetName() const
{
	return "Headless";
}

std::string VideoBackend::GetDisplayName() const
{
	return "Headless (File Output)";
}

void VideoBackend::InitBackendInfo()
{
	g_Config.backend_info.APIType = API_NONE;
	g_Config.backend_info.bSupports3DVision = false;
	g_Config.backend_info.bSupportsDualSourceBlend = true;
	g_Config.backend_info.bSupportsEarlyZ = true;
	g_Config.backend_info.bSupportsOversizedViewports = true;
	g_Config.backend_info.bSupportsPostProcessing = true;
	g_Config.backend_info.bSupportsGeometryShaders = false;
	g_Config.backend_info.bSupportsComputeShaders = false;
	g_Config.backend_info.bSupportsExclusiveFullscreen = false;
	g_Config.backend_info.bSupportsBBox = true;
	g_Config.backend_info.bSupportsGSInstancing = false;
	g_Config.backend_info.bSupportsPaletteConversion = true;
	g_Config.backend_info.bSupportsClipControl = true;
	g_Config.backend_info.bSupportsSSAA = false;
	g_Config.backend_info.bSupportsTessellation = false;
	g_Config.backend_info.bSupportsScaling = true;
	g_Config.backend_info.bSupportsDepthClamp = true;
	g_Config.backend_info.bSupportsGPUTextureDecoding = false;
	g_Config.backend_info.bSupportsComputeTextureEncoding = false;
	g_Config.backend_info.bSupportsMultithreading = false;
	g_Config.backend_info.bSupportsValidationLayer = false;
	g_Config.backend_info.bSupportsReversedDepthRange = false;
	g_Config.backend_info.bSupportsInternalResolutionFrameDumps = true;
	g_Config.backend_info.bSupportsAsyncShaderCompilation = false;

	// aamodes
	g_Config.backend_info.AAModes = {1};
}

bool VideoBackend::Initialize(void *window_handle)
{
	InitializeShared();
	InitBackendInfo();

	g_Config.Load((File::GetUserPath(D_CONFIG_IDX) + "gfx_headless.ini").c_str());
	g_Config.GameIniLoad();
	g_Config.UpdateProjectionHack();
	g_Config.VerifyValidity();
	UpdateActiveConfig();

	PixelEngine::Init();
	DebugUtil::Init();

	// Do our OSD callbacks
	OSD::DoCallbacks(OSD::CallbackType::Initialization);

	m_initialized = true;

	return true;
}

void VideoBackend::Shutdown()
{
	m_initialized = false;

	// Do our OSD callbacks
	OSD::DoCallbacks(OSD::CallbackType::Shutdown);

	ShutdownShared();
}

void VideoBackend::Video_Prepare()
{
	// Create renderer instance
	g_renderer = std::make_unique<HeadlessRenderer>();

	CommandProcessor::Init();
	PixelEngine::Init();

	BPInit();
	
	// Create vertex manager
	g_vertex_manager = std::make_unique<HeadlessVertexManager>();
	
	g_perf_query = std::make_unique<PerfQuery>();
	Fifo::Init(); // must be done before OpcodeDecoder_Init()
	OpcodeDecoder::Init();
	IndexGenerator::Init();
	VertexShaderManager::Init();
	PixelShaderManager::Init(true);
	g_texture_cache = std::make_unique<TextureCache>();
	g_renderer->Init();
	VertexLoaderManager::Init();
	g_framebuffer_manager = std::make_unique<FramebufferManager>();

	// Notify the core that the video backend is ready
	Host_Message(WM_USER_CREATE);

	INFO_LOG(VIDEO, "Headless video backend initialized.");
}

void VideoBackend::Video_Cleanup()
{
	if (g_renderer)
	{
		Fifo::Shutdown();
		g_renderer->Shutdown();
		DebugUtil::Shutdown();
		// The following calls are NOT Thread Safe
		// And need to be called from the video thread
		g_renderer->Shutdown();
		VertexLoaderManager::Shutdown();
		g_framebuffer_manager.reset();
		g_texture_cache.reset();
		g_perf_query.reset();
		g_vertex_manager.reset();
		g_renderer.reset();
	}
}

unsigned int VideoBackend::PeekMessages()
{
	// No window messages to process in headless mode
	return 0;
}

void VideoBackend::PrepareWindow(void* window_handle)
{
	// No window preparation needed in headless mode
}

} // namespace Headless 