// Copyright 2024 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/Headless/HeadlessVertexLoader.h"

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

#include "VideoCommon/IndexGenerator.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VideoConfig.h"

namespace Headless
{

HeadlessVertexFormat::HeadlessVertexFormat(const PortableVertexDeclaration& vtx_decl_param)
{
	vtx_decl = vtx_decl_param;
}

HeadlessVertexFormat::~HeadlessVertexFormat()
{
}

void HeadlessVertexFormat::SetupVertexPointers()
{
	// No-op in headless mode
}

HeadlessVertexManager::HeadlessVertexManager()
	: m_cpu_vertex_buffer(MAXVBUFFERSIZE), m_cpu_index_buffer(MAXIBUFFERSIZE)
{
	CreateDeviceObjects();
}

HeadlessVertexManager::~HeadlessVertexManager()
{
	DestroyDeviceObjects();
}

std::unique_ptr<NativeVertexFormat> HeadlessVertexManager::CreateNativeVertexFormat(const PortableVertexDeclaration& vtx_decl)
{
	return std::make_unique<HeadlessVertexFormat>(vtx_decl);
}

void HeadlessVertexManager::CreateDeviceObjects()
{
	// No device objects needed in headless mode
}

void HeadlessVertexManager::DestroyDeviceObjects()
{
	// No device objects to destroy in headless mode
}

void HeadlessVertexManager::PrepareShaders(PrimitiveType primitive, u32 components, const XFMemory& xfr, const BPMemory& bpm, bool ongputhread)
{
	// No shader preparation needed in headless mode
}

void HeadlessVertexManager::ResetBuffer(u32 stride)
{
	m_pCurBufferPointer = m_pBaseBufferPointer = m_cpu_vertex_buffer.data();
	m_pEndBufferPointer = m_pBaseBufferPointer + m_cpu_vertex_buffer.size();
	IndexGenerator::Start(GetIndexBuffer());
}

u16* HeadlessVertexManager::GetIndexBuffer()
{
	return m_cpu_index_buffer.data();
}

void HeadlessVertexManager::vFlush(bool useDstAlpha)
{
	// In headless mode, we don't actually render anything
	// Just update statistics
	INCSTAT(stats.thisFrame.numDrawCalls);
}

void HeadlessVertexManager::PrepareDrawBuffers(u32 stride)
{
	// No buffer preparation needed in headless mode
}

} // namespace Headless 