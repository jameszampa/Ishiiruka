// Copyright 2024 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/Headless/HeadlessVertexLoader.h"
#include "Common/Logging/Log.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/CPMemory.h"

namespace Headless
{

HeadlessVertexLoader::HeadlessVertexLoader(const TVtxDesc& vtx_desc, const VAT& vtx_attr)
	: VertexLoaderBase(vtx_desc, vtx_attr)
{
	InitializeVertexData();
}

HeadlessVertexLoader::~HeadlessVertexLoader()
{
}

s32 HeadlessVertexLoader::RunVertices(const VertexLoaderParameters& parameters)
{
	// In headless mode, we don't actually process vertices
	// Just return the count to indicate success
	m_numLoadedVertices += parameters.count;
	return parameters.count;
}

void HeadlessVertexLoader::InitializeVertexData()
{
	// Initialize vertex data for headless mode
	m_native_components = 0;
	m_VertexSize = 0;
	m_native_stride = 0;
	
	// Set up basic vertex declaration
	memset(&m_native_vtx_decl, 0, sizeof(m_native_vtx_decl));
	
	// Position component
	m_native_vtx_decl.position.components = 3;
	m_native_vtx_decl.position.enable = true;
	m_native_vtx_decl.position.offset = 0;
	m_native_vtx_decl.position.type = FORMAT_FLOAT;
	
	// Calculate basic vertex size and stride
	m_VertexSize = 12; // 3 floats for position
	m_native_stride = 12;
	m_native_vtx_decl.stride = m_native_stride;
	
	INFO_LOG(VIDEO, "Headless vertex loader initialized with vertex size %d, stride %d", m_VertexSize, m_native_stride);
}

} // namespace Headless 