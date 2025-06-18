// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <memory>

#include "Common/GL/GLInterfaceBase.h"

#if defined(__APPLE__)
#include "Common/GL/GLInterface/AGL.h"
#elif defined(_WIN32)
#include "Common/GL/GLInterface/WGL.h"
#elif HAVE_X11
#if defined(USE_EGL) && USE_EGL
#include "Common/GL/GLInterface/EGLX11.h"
#else
#include "Common/GL/GLInterface/GLX.h"
#endif
#elif defined(USE_EGL) && USE_EGL && defined(USE_HEADLESS)
#include "Common/GL/GLInterface/EGL.h"
#elif ANDROID
#include "Common/GL/GLInterface/EGLAndroid.h"
#elif defined(__HAIKU__)
#include "Common/GL/GLInterface/BGL.h"
#else
#error Platform doesnt have a GLInterface
#endif

std::unique_ptr<cInterfaceBase> HostGL_CreateGLInterface()
{
	fprintf(stderr, "[GL DEBUG] HostGL_CreateGLInterface() called\n");
#if defined(__APPLE__)
	fprintf(stderr, "[GL DEBUG] Creating AGL interface for macOS\n");
	return std::make_unique<cInterfaceAGL>();
#elif defined(_WIN32)
	fprintf(stderr, "[GL DEBUG] Creating WGL interface for Windows\n");
	return std::make_unique<cInterfaceWGL>();
#elif defined(USE_EGL) && defined(USE_HEADLESS)
	fprintf(stderr, "[GL DEBUG] Creating EGL interface for headless\n");
	return std::make_unique<cInterfaceEGL>();
#elif defined(HAVE_X11) && HAVE_X11
#if defined(USE_EGL) && USE_EGL
	fprintf(stderr, "[GL DEBUG] Creating EGLX11 interface for X11 with EGL\n");
	return std::make_unique<cInterfaceEGLX11>();
#else
	fprintf(stderr, "[GL DEBUG] Creating GLX interface for X11\n");
	return std::make_unique<cInterfaceGLX>();
#endif
#elif ANDROID
	fprintf(stderr, "[GL DEBUG] Creating EGLAndroid interface for Android\n");
	return std::make_unique<cInterfaceEGLAndroid>();
#elif defined(__HAIKU__)
	fprintf(stderr, "[GL DEBUG] Creating BGL interface for Haiku\n");
	return std::make_unique<cInterfaceBGL>();
#else
	fprintf(stderr, "[GL DEBUG] No GLInterface available for this platform\n");
	return nullptr;
#endif
}
