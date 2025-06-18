// Copyright 2024 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Common/MemoryUtil.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/NativeVertexFormat.h"

namespace Headless
{

class HeadlessVertexFormat : public NativeVertexFormat
{
public:
	HeadlessVertexFormat(const PortableVertexDeclaration& vtx_decl);
	~HeadlessVertexFormat();

	void SetupVertexPointers() override;
};

class HeadlessVertexManager : public VertexManagerBase
{
public:
	HeadlessVertexManager();
	~HeadlessVertexManager();

	std::unique_ptr<NativeVertexFormat> CreateNativeVertexFormat(const PortableVertexDeclaration& vtx_decl) override;
	void CreateDeviceObjects() override;
	void DestroyDeviceObjects() override;
	void PrepareShaders(PrimitiveType primitive, u32 components, const XFMemory& xfr, const BPMemory& bpm, bool ongputhread) override;

protected:
	void ResetBuffer(u32 stride) override;
	u16* GetIndexBuffer() override;

private:
	void vFlush(bool useDstAlpha) override;
	void PrepareDrawBuffers(u32 stride);

	std::vector<u8, Common::aligned_allocator<u8, 256>> m_cpu_vertex_buffer;
	std::vector<u16, Common::aligned_allocator<u16, 256>> m_cpu_index_buffer;
};

} // namespace Headless 