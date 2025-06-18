// Copyright 2024 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "VideoCommon/VertexLoaderBase.h"

namespace Headless
{

class HeadlessVertexLoader : public VertexLoaderBase
{
public:
	HeadlessVertexLoader(const TVtxDesc& vtx_desc, const VAT& vtx_attr);
	~HeadlessVertexLoader();

	// Required methods from VertexLoaderBase
	s32 RunVertices(const VertexLoaderParameters& parameters) override;
	bool IsInitialized() override { return true; }

private:
	void InitializeVertexData();
};

} // namespace Headless 