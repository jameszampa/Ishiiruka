// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

// OpenGL Backend Documentation
/*

1.1 Display settings

Internal and fullscreen resolution: Since the only internal resolutions allowed
are also fullscreen resolution allowed by the system there is only need for one
resolution setting that applies to both the internal resolution and the
fullscreen resolution.  - Apparently no, someone else doesn't agree

Todo: Make the internal resolution option apply instantly, currently only the
native and 2x option applies instantly. To do this we need to be able to change
the reinitialize FramebufferManager:Init() while a game is running.

1.2 Screenshots


The screenshots should be taken from the internal representation of the picture
regardless of what the current window size is. Since AA and wireframe is
applied together with the picture resizing this rule is not currently applied
to AA or wireframe pictures, they are instead taken from whatever the window
size is.

Todo: Render AA and wireframe to a separate picture used for the screenshot in
addition to the one for display.

1.3 AA

Make AA apply instantly during gameplay if possible

*/

#include <memory>
#include <string>
#include <vector>

#include "Common/CommonPaths.h"
#include "Common/FileSearch.h"
#include "Common/GL/GLInterfaceBase.h"
#include "Common/GL/GLUtil.h"

#include "Core/ConfigManager.h"
#include "Core/Host.h"

#include "VideoBackends/OGL/BoundingBox.h"
#include "VideoBackends/OGL/PerfQuery.h"
#include "VideoBackends/OGL/ProgramShaderCache.h"
#include "VideoBackends/OGL/Render.h"
#include "VideoBackends/OGL/SamplerCache.h"
#include "VideoBackends/OGL/TextureCache.h"
#include "VideoBackends/OGL/TextureConverter.h"
#include "VideoBackends/OGL/VertexManager.h"
#include "VideoBackends/OGL/VideoBackend.h"

#include "VideoCommon/BPStructs.h"
#include "VideoCommon/CommandProcessor.h"
#include "VideoCommon/Fifo.h"
#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/IndexGenerator.h"
#include "VideoCommon/OnScreenDisplay.h"
#include "VideoCommon/OpcodeDecoding.h"
#include "VideoCommon/PixelEngine.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"

namespace OGL
{
// Draw messages on top of the screen
unsigned int VideoBackend::PeekMessages()
{
	return GLInterface->PeekMessages();
}

std::string VideoBackend::GetName() const
{
	return "OGL";
}

std::string VideoBackend::GetDisplayName() const
{
	if (GLInterface != nullptr && GLInterface->GetMode() == GLInterfaceMode::MODE_OPENGLES3)
		return "OpenGLES";
	else
		return "OpenGL";
}

void VideoBackend::InitBackendInfo()
{
	g_Config.backend_info.APIType = API_OPENGL;
	g_Config.backend_info.MaxTextureSize = 1024;
#ifdef _WIN32
	g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_BGRA32] = false;
	g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_RGBA32] = true;
	g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_I4_AS_I8] = false;
	g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_IA4_AS_IA8] = false;
	g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_I8] = false;
	g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_IA8] = false;
	g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_RGB565] = false;
	g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_DXT1] = true;
	g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_DXT3] = true;
	g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_DXT5] = true;
#endif
	g_Config.backend_info.bSupportsScaling = false;
	g_Config.backend_info.bSupportsExclusiveFullscreen = false;
	g_Config.backend_info.bSupportsOversizedViewports = true;
	g_Config.backend_info.bSupportsGeometryShaders = true;
	g_Config.backend_info.bSupports3DVision = false;
	g_Config.backend_info.bSupportsPostProcessing = true;
	g_Config.backend_info.bSupportsSSAA = true;
	g_Config.backend_info.bSupportsPixelLighting = true;
	g_Config.backend_info.bSupportsNormalMaps = true;
	g_Config.backend_info.bSupportsTessellation = false;
	g_Config.backend_info.bSupportsComputeShaders = false;
	g_Config.backend_info.bSupportsGPUTextureDecoding = true;
	g_Config.backend_info.bSupportsComputeTextureEncoding = false;
	g_Config.backend_info.bSupportsDepthClamp = true;
	g_Config.backend_info.bSupportsMultithreading = false;
	g_Config.backend_info.bSupportsValidationLayer = false;
	g_Config.backend_info.bSupportsReversedDepthRange = true;
	g_Config.backend_info.bSupportsInternalResolutionFrameDumps = true;
	g_Config.backend_info.bSupportsAsyncShaderCompilation = false;
	g_Config.backend_info.Adapters.clear();

	// aamodes - 1 is to stay consistent with D3D (means no AA)
	g_Config.backend_info.AAModes = { 1, 2, 4, 8 };
}

bool VideoBackend::Initialize(void* window_handle)
{
	fprintf(stderr, "[OGL DEBUG] VideoBackend::Initialize called with window_handle: %p\n", window_handle);
	if (window_handle == nullptr)
	{
		fprintf(stderr, "[OGL DEBUG] window_handle is nullptr, returning false\n");
		return false;
	}

	fprintf(stderr, "[OGL DEBUG] Calling InitializeShared()\n");
	InitializeShared();
	fprintf(stderr, "[OGL DEBUG] Calling InitBackendInfo()\n");
	InitBackendInfo();

	fprintf(stderr, "[OGL DEBUG] Calling InitInterface()\n");
	InitInterface();
	fprintf(stderr, "[OGL DEBUG] Setting GLInterface mode to MODE_DETECT\n");
	GLInterface->SetMode(GLInterfaceMode::MODE_DETECT);
	fprintf(stderr, "[OGL DEBUG] About to call GLInterface->Create()\n");
	if (!GLInterface->Create(window_handle))
	{
		fprintf(stderr, "[OGL DEBUG] GLInterface->Create() failed\n");
		return false;
	}
	fprintf(stderr, "[OGL DEBUG] GLInterface->Create() succeeded\n");

	return true;
}

// This is called after Initialize() from the Core
// Run from the graphics thread
void VideoBackend::Video_Prepare()
{
	fprintf(stderr, "[OGL DEBUG] VideoBackend::Video_Prepare called\n");
	fprintf(stderr, "[OGL DEBUG] Calling GLInterface->MakeCurrent()\n");
	GLInterface->MakeCurrent();

	fprintf(stderr, "[OGL DEBUG] Creating Renderer\n");
	g_renderer = std::make_unique<Renderer>();

	fprintf(stderr, "[OGL DEBUG] Creating VertexManager\n");
	g_vertex_manager = std::make_unique<VertexManager>();
	fprintf(stderr, "[OGL DEBUG] Getting PerfQuery\n");
	g_perf_query = GetPerfQuery();
	fprintf(stderr, "[OGL DEBUG] Initializing ProgramShaderCache\n");
	ProgramShaderCache::Init();
	fprintf(stderr, "[OGL DEBUG] Creating TextureCache\n");
	g_texture_cache = std::make_unique<TextureCache>();
	fprintf(stderr, "[OGL DEBUG] Creating SamplerCache\n");
	g_sampler_cache = std::make_unique<SamplerCache>();
	fprintf(stderr, "[OGL DEBUG] Calling g_renderer->Init()\n");
	g_renderer->Init();
	fprintf(stderr, "[OGL DEBUG] Initializing TextureConverter\n");
	TextureConverter::Init();
	fprintf(stderr, "[OGL DEBUG] Initializing BBox\n");
	BBox::Init();
	fprintf(stderr, "[OGL DEBUG] Video_Prepare completed successfully\n");
}

void VideoBackend::Shutdown()
{
	GLInterface->Shutdown();
	GLInterface.reset();
	ShutdownShared();
}

void VideoBackend::Video_Cleanup()
{
	// The following calls are NOT Thread Safe
	// And need to be called from the video thread
	CleanupShared();
	static_cast<Renderer*>(g_renderer.get())->Shutdown();
	BBox::Shutdown();
	TextureConverter::Shutdown();
	g_sampler_cache.reset();
	g_texture_cache.reset();
	ProgramShaderCache::Shutdown();
	g_perf_query.reset();
	g_vertex_manager.reset();
	g_renderer.reset();
	GLInterface->ClearCurrent();
}

void VideoBackend::PrepareWindow(void* window_handle) {

}
}
