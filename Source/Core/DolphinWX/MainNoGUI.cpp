// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <signal.h>
#include <string>
#include <thread>
#include <unistd.h>

#include "Common/Common.h"
#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/CPUDetect.h"
#include "Common/FileUtil.h"
#include "Common/Logging/LogManager.h"
#include "Common/MsgHandler.h"
#include "Common/Thread.h"
#include "Common/Timer.h"

#include "Core/Analytics.h"
#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/CPU.h"
#include "Core/HW/DVDInterface.h"
#include "Core/HW/EXI_DeviceSlippi.h"
#include "Core/HW/SystemTimers.h"
#include "Core/Movie.h"
#include "Core/NetPlayClient.h"
#include "Core/NetPlayServer.h"
#include "Core/PatchEngine.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/State.h"
#include "Core/VideoBackends/Headless/VideoBackend.h"

#include "Core/Slippi/SlippiPlayback.h"
#include "Core/Slippi/SlippiReplayComm.h"

#include "UICommon/UICommon.h"

#include "VideoCommon/RenderBase.h"
#include "VideoCommon/VideoBackendBase.h"

// External declarations for Slippi replay system
extern std::unique_ptr<SlippiPlaybackStatus> g_playbackStatus;
extern std::unique_ptr<SlippiReplayComm> g_replayComm;

static bool rendererHasFocus = true;
static bool rendererIsFullscreen = false;
static Common::Flag s_running{ true };
static Common::Flag s_shutdown_requested{ false };
static Common::Flag s_tried_graceful_shutdown{ false };

static void signal_handler(int)
{
	const char message[] = "A signal was received. A second signal will force Dolphin to stop.\n";
	if (write(STDERR_FILENO, message, sizeof(message)) < 0)
	{
	}
	s_shutdown_requested.Set();
}

namespace ProcessorInterface
{
void PowerButton_Tap();
}

class Platform
{
public:
	virtual void Init() {}
	virtual void SetTitle(const std::string& title) {}
	virtual void MainLoop()
	{
		fprintf(stderr, "[DEBUG] MainLoop: Starting main loop\n");
		int loopCount = 0;
		while (s_running.IsSet())
		{
			fprintf(stderr, "[DEBUG] MainLoop: About to call Core::HostDispatchJobs()\n");
			Core::HostDispatchJobs();
			fprintf(stderr, "[DEBUG] MainLoop: Core::HostDispatchJobs() completed\n");
			
			// Add some debugging every 100 iterations (10 seconds)
			loopCount++;
			if (loopCount % 100 == 0)
			{
				fprintf(stderr, "[DEBUG] MainLoop: Still running, iteration %d\n", loopCount);
				fprintf(stderr, "[DEBUG] MainLoop: Core state: %d, IsRunning: %s\n", 
					Core::GetState(), Core::IsRunning() ? "true" : "false");
				
				// Check if Slippi replay system is active
				if (g_replayComm)
				{
					fprintf(stderr, "[DEBUG] MainLoop: SlippiReplayComm exists\n");
					auto settings = g_replayComm->getSettings();
					fprintf(stderr, "[DEBUG] MainLoop: Replay mode: %s\n", settings.mode.c_str());
					fprintf(stderr, "[DEBUG] MainLoop: Replay path: %s\n", settings.replayPath.c_str());
					fprintf(stderr, "[DEBUG] MainLoop: Is new replay: %s\n", g_replayComm->isNewReplay() ? "true" : "false");
					
					// Try to load a game to see if it works
					auto testGame = g_replayComm->loadGame();
					if (testGame)
					{
						fprintf(stderr, "[DEBUG] MainLoop: Test game load successful, latest frame: %d\n", testGame->GetLatestIndex());
					}
					else
					{
						fprintf(stderr, "[DEBUG] MainLoop: Test game load failed\n");
					}
				}
				else
				{
					fprintf(stderr, "[DEBUG] MainLoop: SlippiReplayComm is null\n");
				}
			}
			
			// Check if core is still running
			if (!Core::IsRunning())
			{
				fprintf(stderr, "[DEBUG] MainLoop: Core stopped running, breaking loop\n");
				fprintf(stderr, "[DEBUG] MainLoop: Core state when stopped: %d\n", Core::GetState());
				break;
			}
			
			fprintf(stderr, "[DEBUG] MainLoop: About to sleep for 100ms\n");
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			fprintf(stderr, "[DEBUG] MainLoop: Sleep completed\n");
		}
		fprintf(stderr, "[DEBUG] MainLoop: Main loop ended\n");
	}
	virtual void Shutdown() {}
	virtual ~Platform() {}
};

static Platform* platform;

void Host_NotifyMapLoaded()
{
}
void Host_RefreshDSPDebuggerWindow()
{
}

static Common::Event updateMainFrameEvent;
void Host_Message(int Id)
{
	if (Id == WM_USER_STOP)
	{
		s_running.Clear();
		updateMainFrameEvent.Set();
	}
}

static void* s_window_handle = nullptr;
void* Host_GetRenderHandle()
{
	return s_window_handle;
}

void Host_UpdateTitle(const std::string& title)
{
	platform->SetTitle(title);
}

void Host_UpdateDisasmDialog()
{
}

void Host_UpdateMainFrame()
{
	updateMainFrameEvent.Set();
}

void Host_RequestRenderWindowSize(int width, int height)
{
}

void Host_SetStartupDebuggingParameters()
{
	SConfig& StartUp = SConfig::GetInstance();
	StartUp.bEnableDebugging = false;
	StartUp.bBootToPause = false;
}

bool Host_UIHasFocus()
{
	return false;
}

bool Host_RendererHasFocus()
{
	return rendererHasFocus;
}

bool Host_RendererIsFullscreen()
{
	return rendererIsFullscreen;
}

void Host_ConnectWiimote(int wm_idx, bool connect)
{
	if (Core::IsRunning() && SConfig::GetInstance().bWii &&
		!SConfig::GetInstance().m_bt_passthrough_enabled)
	{
		Core::QueueHostJob([=] {
			bool was_unpaused = Core::PauseAndLock(true);
			GetUsbPointer()->AccessWiiMote(wm_idx | 0x100)->Activate(connect);
			Host_UpdateMainFrame();
			Core::PauseAndLock(false, was_unpaused);
		});
	}
}

void Host_SetWiiMoteConnectionState(int _State)
{
}

void Host_ShowVideoConfig(void*, const std::string&)
{
}

void Host_YieldToUI()
{
}

#if HAVE_X11
#include <X11/keysym.h>
#include "DolphinWX/X11Utils.h"

class PlatformX11 : public Platform
{
	Display* dpy;
	Window win;
	Cursor blankCursor = None;
#if defined(HAVE_XRANDR) && HAVE_XRANDR
	X11Utils::XRRConfiguration* XRRConfig;
#endif

	void Init() override
	{
		XInitThreads();
		dpy = XOpenDisplay(nullptr);
		if (!dpy)
		{
			PanicAlert("No X11 display found");
			exit(1);
		}

		win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), SConfig::GetInstance().iRenderWindowXPos,
			SConfig::GetInstance().iRenderWindowYPos,
			SConfig::GetInstance().iRenderWindowWidth,
			SConfig::GetInstance().iRenderWindowHeight, 0, 0, BlackPixel(dpy, 0));
		XSelectInput(dpy, win, StructureNotifyMask | KeyPressMask | FocusChangeMask);
		Atom wmProtocols[1];
		wmProtocols[0] = XInternAtom(dpy, "WM_DELETE_WINDOW", True);
		XSetWMProtocols(dpy, win, wmProtocols, 1);
		XMapRaised(dpy, win);
		XFlush(dpy);
		s_window_handle = (void*)win;

		if (SConfig::GetInstance().bDisableScreenSaver)
			X11Utils::InhibitScreensaver(dpy, win, true);

#if defined(HAVE_XRANDR) && HAVE_XRANDR
		XRRConfig = new X11Utils::XRRConfiguration(dpy, win);
#endif

		if (SConfig::GetInstance().bHideCursor)
		{
			// make a blank cursor
			Pixmap Blank;
			XColor DummyColor;
			char ZeroData[1] = { 0 };
			Blank = XCreateBitmapFromData(dpy, win, ZeroData, 1, 1);
			blankCursor = XCreatePixmapCursor(dpy, Blank, Blank, &DummyColor, &DummyColor, 0, 0);
			XFreePixmap(dpy, Blank);
			XDefineCursor(dpy, win, blankCursor);
		}
	}

	void SetTitle(const std::string& string) override { XStoreName(dpy, win, string.c_str()); }
	void MainLoop() override
	{
		bool fullscreen = SConfig::GetInstance().bFullscreen;
		int last_window_width = SConfig::GetInstance().iRenderWindowWidth;
		int last_window_height = SConfig::GetInstance().iRenderWindowHeight;
		if (fullscreen)
		{
			rendererIsFullscreen = X11Utils::ToggleFullscreen(dpy, win);
#if defined(HAVE_XRANDR) && HAVE_XRANDR
			XRRConfig->ToggleDisplayMode(True);
#endif
		}

		// The actual loop
		while (s_running.IsSet())
		{
			if (s_shutdown_requested.TestAndClear())
			{
				const auto& stm = WII_IPC_HLE_Interface::GetDeviceByName("/dev/stm/eventhook");
				if (!s_tried_graceful_shutdown.IsSet() && stm &&
					std::static_pointer_cast<CWII_IPC_HLE_Device_stm_eventhook>(stm)->HasHookInstalled())
				{
					ProcessorInterface::PowerButton_Tap();
					s_tried_graceful_shutdown.Set();
				}
				else
				{
					s_running.Clear();
				}
			}

			XEvent event;
			KeySym key;
			for (int num_events = XPending(dpy); num_events > 0; num_events--)
			{
				XNextEvent(dpy, &event);
				switch (event.type)
				{
				case KeyPress:
					key = XLookupKeysym((XKeyEvent*)&event, 0);
					if (key == XK_Escape)
					{
						if (Core::GetState() == Core::CORE_RUN)
						{
							if (SConfig::GetInstance().bHideCursor)
								XUndefineCursor(dpy, win);
							Core::SetState(Core::CORE_PAUSE);
						}
						else
						{
							if (SConfig::GetInstance().bHideCursor)
								XDefineCursor(dpy, win, blankCursor);
							Core::SetState(Core::CORE_RUN);
						}
					}
					else if ((key == XK_Return) && (event.xkey.state & Mod1Mask))
					{
						fullscreen = !fullscreen;
						X11Utils::ToggleFullscreen(dpy, win);
#if defined(HAVE_XRANDR) && HAVE_XRANDR
						XRRConfig->ToggleDisplayMode(fullscreen);
#endif
					}
					else if (key >= XK_F1 && key <= XK_F8)
					{
						int slot_number = key - XK_F1 + 1;
						if (event.xkey.state & ShiftMask)
							State::Save(slot_number);
						else
							State::Load(slot_number);
					}
					else if (key == XK_F9)
						Core::SaveScreenShot();
					else if (key == XK_F11)
						State::LoadLastSaved();
					else if (key == XK_F12)
					{
						if (event.xkey.state & ShiftMask)
							State::UndoLoadState();
						else
							State::UndoSaveState();
					}
					break;
				case FocusIn:
					rendererHasFocus = true;
					if (SConfig::GetInstance().bHideCursor && Core::GetState() != Core::CORE_PAUSE)
						XDefineCursor(dpy, win, blankCursor);
					break;
				case FocusOut:
					rendererHasFocus = false;
					if (SConfig::GetInstance().bHideCursor)
						XUndefineCursor(dpy, win);
					break;
				case ClientMessage:
					if ((unsigned long)event.xclient.data.l[0] == XInternAtom(dpy, "WM_DELETE_WINDOW", False))
						s_shutdown_requested.Set();
					break;
				case ConfigureNotify:
				{
					if (last_window_width != event.xconfigure.width ||
						last_window_height != event.xconfigure.height)
					{
						last_window_width = event.xconfigure.width;
						last_window_height = event.xconfigure.height;

						// We call Renderer::ChangeSurface here to indicate the size has changed,
						// but pass the same window handle. This is needed for the Vulkan backend,
						// otherwise it cannot tell that the window has been resized on some drivers.
						if (g_renderer)
							g_renderer->ChangeSurface(s_window_handle);
					}
				}
				break;
				}
			}
			if (!fullscreen)
			{
				Window winDummy;
				unsigned int borderDummy, depthDummy;
				XGetGeometry(dpy, win, &winDummy, &SConfig::GetInstance().iRenderWindowXPos,
					&SConfig::GetInstance().iRenderWindowYPos,
					(unsigned int*)&SConfig::GetInstance().iRenderWindowWidth,
					(unsigned int*)&SConfig::GetInstance().iRenderWindowHeight, &borderDummy,
					&depthDummy);
				rendererIsFullscreen = false;
			}
			Core::HostDispatchJobs();
			usleep(100000);
		}
	}

	void Shutdown() override
	{
#if defined(HAVE_XRANDR) && HAVE_XRANDR
		delete XRRConfig;
#endif

		if (SConfig::GetInstance().bHideCursor)
			XFreeCursor(dpy, blankCursor);

		XCloseDisplay(dpy);
	}
};
#endif

static Platform* GetPlatform()
{
#if defined(USE_EGL) && defined(USE_HEADLESS)
	return new Platform();
#elif HAVE_X11
	return new PlatformX11();
#endif
	return nullptr;
}

int main(int argc, char* argv[])
{
	fprintf(stderr, "[DEBUG] MainNoGUI starting with %d arguments\n", argc);
	
	int help = 0;
	std::string exec_file;
	std::string output_directory;
	std::string output_filename_base;
	std::string video_backend;
#ifdef IS_PLAYBACK
	std::string slippi_input;
	bool hide_seekbar = false;
	bool enable_cout = false;
#endif

	struct option longopts[] = {
		{ "exec", required_argument, nullptr, 'e' },
		{ "help", no_argument, nullptr, 'h' },
		{ "version", no_argument, nullptr, 'v' },
		{ "output-directory", required_argument, nullptr, 'd' },
		{ "output-filename-base", required_argument, nullptr, 'o' },
		{ "video_backend", required_argument, nullptr, 'b' },
#ifdef IS_PLAYBACK
		{ "slippi-input", required_argument, nullptr, 'i' },
		{ "hide-seekbar", no_argument, nullptr, 1000 },
		{ "cout", no_argument, nullptr, 1001 },
#endif
		{ nullptr, 0, nullptr, 0 }
	};

	fprintf(stderr, "[DEBUG] Starting argument parsing\n");
	int opt;
	int longindex = 0;
	while ((opt = getopt_long(argc, argv, "e:hvd:o:b:" 
#ifdef IS_PLAYBACK
		"i:"
#endif
		, longopts, &longindex)) != -1)
	{
		fprintf(stderr, "[DEBUG] Parsed option: %c (optarg: %s)\n", opt, optarg ? optarg : "null");
		switch (opt)
		{
		case 'e':
			exec_file = optarg;
			fprintf(stderr, "[DEBUG] Set exec_file to: %s\n", exec_file.c_str());
			break;
		case 'd':
			output_directory = optarg;
			fprintf(stderr, "[DEBUG] Set output_directory to: %s\n", output_directory.c_str());
			break;
		case 'o':
			output_filename_base = optarg;
			fprintf(stderr, "[DEBUG] Set output_filename_base to: %s\n", output_filename_base.c_str());
			break;
		case 'b':
			video_backend = optarg;
			fprintf(stderr, "[DEBUG] Set video_backend to: %s\n", video_backend.c_str());
			break;
#ifdef IS_PLAYBACK
		case 'i':
			slippi_input = optarg;
			fprintf(stderr, "[DEBUG] Set slippi_input to: %s\n", slippi_input.c_str());
			break;
#endif
		case 'h':
		case '?':
			help = 1;
			fprintf(stderr, "[DEBUG] Help requested\n");
			break;
		case 'v':
			fprintf(stderr, "[DEBUG] Version requested\n");
			fprintf(stderr, "%s\n", scm_rev_str.c_str());
			return 1;
#ifdef IS_PLAYBACK
		case 1000: // --hide-seekbar
			hide_seekbar = true;
			fprintf(stderr, "[DEBUG] Hide seekbar enabled\n");
			break;
		case 1001: // --cout
			enable_cout = true;
			fprintf(stderr, "[DEBUG] Cout enabled\n");
			break;
#endif
		}
	}

	fprintf(stderr, "[DEBUG] Argument parsing complete. help=%d, exec_file='%s', optind=%d, argc=%d\n", 
		help, exec_file.c_str(), optind, argc);

	if (help == 1 || (exec_file.empty() && argc == optind))
	{
		fprintf(stderr, "[DEBUG] Showing help message\n");
		fprintf(stderr, "%s\n\n", scm_rev_str.c_str());
		fprintf(stderr, "A multi-platform GameCube/Wii emulator\n\n");
		fprintf(stderr, "Usage: %s [options] -e <file>\n", argv[0]);
		fprintf(stderr, "  -e, --exec <file>           Load the specified file\n");
		fprintf(stderr, "  -d, --output-directory DIR  Directory for dump files\n");
		fprintf(stderr, "  -o, --output-filename-base  Base name for dump files\n");
		fprintf(stderr, "  -b, --video_backend NAME    Video backend to use\n");
#ifdef IS_PLAYBACK
		fprintf(stderr, "  -i, --slippi-input FILE     Path to Slippi replay config file\n");
		fprintf(stderr, "      --hide-seekbar          Hide seekbar during playback\n");
		fprintf(stderr, "      --cout                  Enable cout during playback\n");
#endif
		fprintf(stderr, "  -h, --help                 Show this help message\n");
		fprintf(stderr, "  -v, --version              Print version and exit\n");
		return 1;
	}

	fprintf(stderr, "[DEBUG] Setting user directory\n");
	UICommon::SetUserDirectory("");  // Auto-detect user folder
	fprintf(stderr, "[DEBUG] User directory set, calling UICommon::Init()\n");
	UICommon::Init();
	fprintf(stderr, "[DEBUG] UICommon initialization complete\n");

	fprintf(stderr, "[DEBUG] Setting up configuration (after UICommon::Init)\n");
	if (!output_directory.empty()) {
		fprintf(stderr, "[DEBUG] Processing output_directory: %s\n", output_directory.c_str());
		if (output_directory.back() != '/' && output_directory.back() != '\\')
			output_directory += "/";
		fprintf(stderr, "[DEBUG] About to call SConfig::GetInstance()\n");
		SConfig& config = SConfig::GetInstance();
		fprintf(stderr, "[DEBUG] SConfig::GetInstance() completed successfully\n");
		fprintf(stderr, "[DEBUG] About to assign to m_strOutputDirectory\n");
		config.m_strOutputDirectory = output_directory;
		fprintf(stderr, "[DEBUG] Set SConfig output directory to: %s\n", output_directory.c_str());
	}
	if (!output_filename_base.empty()) {
		fprintf(stderr, "[DEBUG] About to set SConfig output filename base\n");
		fprintf(stderr, "[DEBUG] About to call SConfig::GetInstance() for filename base\n");
		SConfig& config = SConfig::GetInstance();
		fprintf(stderr, "[DEBUG] SConfig::GetInstance() for filename base completed\n");
		fprintf(stderr, "[DEBUG] About to assign to m_strOutputFilenameBase\n");
		config.m_strOutputFilenameBase = output_filename_base;
		fprintf(stderr, "[DEBUG] Set SConfig output filename base to: %s\n", output_filename_base.c_str());
	}
	if (!video_backend.empty()) {
		fprintf(stderr, "[DEBUG] About to set SConfig video backend\n");
		fprintf(stderr, "[DEBUG] About to call SConfig::GetInstance() for video backend\n");
		SConfig& config = SConfig::GetInstance();
		fprintf(stderr, "[DEBUG] SConfig::GetInstance() for video backend completed\n");
		fprintf(stderr, "[DEBUG] About to assign to m_strVideoBackend\n");
		config.m_strVideoBackend = video_backend;
		fprintf(stderr, "[DEBUG] Activating video backend: %s\n", video_backend.c_str());
		VideoBackendBase::ActivateBackend(video_backend);
		fprintf(stderr, "[DEBUG] Video backend activation complete\n");
	}
#ifdef IS_PLAYBACK
	fprintf(stderr, "[DEBUG] Processing Slippi configuration\n");
	if (!slippi_input.empty()) {
		fprintf(stderr, "[DEBUG] About to set Slippi input\n");
		fprintf(stderr, "[DEBUG] About to call SConfig::GetInstance() for Slippi input\n");
		SConfig& config = SConfig::GetInstance();
		fprintf(stderr, "[DEBUG] SConfig::GetInstance() for Slippi input completed\n");
		fprintf(stderr, "[DEBUG] About to assign to m_strSlippiInput\n");
		config.m_strSlippiInput = slippi_input;
		fprintf(stderr, "[DEBUG] Set Slippi input to: %s\n", slippi_input.c_str());
	} else {
		fprintf(stderr, "[DEBUG] About to set default Slippi input\n");
		fprintf(stderr, "[DEBUG] About to call SConfig::GetInstance() for default Slippi input\n");
		SConfig& config = SConfig::GetInstance();
		fprintf(stderr, "[DEBUG] SConfig::GetInstance() for default Slippi input completed\n");
		fprintf(stderr, "[DEBUG] About to assign to m_strSlippiInput (default)\n");
		config.m_strSlippiInput = "Slippi/playback.txt";
		fprintf(stderr, "[DEBUG] Using default Slippi input: Slippi/playback.txt\n");
	}
	
	// Enable replay regeneration for playback mode
	fprintf(stderr, "[DEBUG] About to enable Slippi replay regeneration\n");
	fprintf(stderr, "[DEBUG] About to call SConfig::GetInstance() for replay regeneration\n");
	SConfig& config = SConfig::GetInstance();
	fprintf(stderr, "[DEBUG] SConfig::GetInstance() for replay regeneration completed\n");
	fprintf(stderr, "[DEBUG] About to assign to m_slippiRegenerateReplays\n");
	config.m_slippiRegenerateReplays = true;
	fprintf(stderr, "[DEBUG] Slippi replay regeneration enabled\n");
	
	// Set the regenerate replay directory to the output directory
	fprintf(stderr, "[DEBUG] About to set Slippi regenerate replay directory\n");
	fprintf(stderr, "[DEBUG] About to assign to m_strSlippiRegenerateReplayDir\n");
	config.m_strSlippiRegenerateReplayDir = output_directory;
	fprintf(stderr, "[DEBUG] Set Slippi regenerate replay directory to: %s\n", output_directory.c_str());
	
	if (hide_seekbar) {
		fprintf(stderr, "[DEBUG] About to set hide seekbar config\n");
		fprintf(stderr, "[DEBUG] About to call SConfig::GetInstance() for hide seekbar\n");
		SConfig& config = SConfig::GetInstance();
		fprintf(stderr, "[DEBUG] SConfig::GetInstance() for hide seekbar completed\n");
		fprintf(stderr, "[DEBUG] About to assign to m_CLIHideSeekbar\n");
		config.m_CLIHideSeekbar = true;
		fprintf(stderr, "[DEBUG] Hide seekbar enabled in config\n");
	}
	if (enable_cout) {
		fprintf(stderr, "[DEBUG] About to set cout config\n");
		fprintf(stderr, "[DEBUG] About to call SConfig::GetInstance() for cout\n");
		SConfig& config = SConfig::GetInstance();
		fprintf(stderr, "[DEBUG] SConfig::GetInstance() for cout completed\n");
		fprintf(stderr, "[DEBUG] About to assign to m_coutEnabled\n");
		config.m_coutEnabled = true;
		fprintf(stderr, "[DEBUG] Cout enabled in config\n");
	}
	fprintf(stderr, "[DEBUG] Slippi configuration complete\n");
#endif

	// Configure audio for headless Docker environment
	fprintf(stderr, "[DEBUG] Configuring audio for headless environment\n");
	SConfig& audioConfig = SConfig::GetInstance();
	fprintf(stderr, "[DEBUG] Setting audio backend to null sound\n");
	audioConfig.sBackend = "No audio output";
	fprintf(stderr, "[DEBUG] Enabling audio dumping\n");
	audioConfig.m_DumpAudio = true;
	fprintf(stderr, "[DEBUG] Enabling silent audio dumping\n");
	audioConfig.m_DumpAudioSilent = true;
	fprintf(stderr, "[DEBUG] Audio configuration complete\n");

	fprintf(stderr, "[DEBUG] Getting platform\n");
	platform = GetPlatform();
	if (!platform)
	{
		fprintf(stderr, "[ERROR] No platform found\n");
		return 1;
	}
	fprintf(stderr, "[DEBUG] Platform obtained successfully\n");

	fprintf(stderr, "[DEBUG] Setting up core callbacks and initializing platform\n");
	Core::SetOnStoppedCallback([]() { s_running.Clear(); });
	platform->Init();
	fprintf(stderr, "[DEBUG] Platform initialization complete\n");

	// Shut down cleanly on SIGINT and SIGTERM
	struct sigaction sa;
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sa, nullptr);
	sigaction(SIGTERM, &sa, nullptr);
	fprintf(stderr, "[DEBUG] Signal handlers set up\n");

	fprintf(stderr, "[DEBUG] Reporting analytics\n");
	DolphinAnalytics::Instance()->ReportDolphinStart("nogui");

	// Use exec_file if provided, otherwise fallback to argv[optind]
	const char* boot_file = nullptr;
	if (!exec_file.empty())
		boot_file = exec_file.c_str();
	else if (argc > optind)
		boot_file = argv[optind];
	else
		boot_file = nullptr;

	fprintf(stderr, "[DEBUG] Boot file determined: %s\n", boot_file ? boot_file : "(none)");

	if (!boot_file || !BootManager::BootCore(boot_file))
	{
		fprintf(stderr, "[ERROR] Could not boot %s\n", boot_file ? boot_file : "(none)");
		return 1;
	}
	fprintf(stderr, "[DEBUG] BootCore completed successfully\n");

	fprintf(stderr, "[DEBUG] Waiting for core to start running\n");
	while (!Core::IsRunning() && s_running.IsSet())
	{
		Core::HostDispatchJobs();
		updateMainFrameEvent.Wait();
	}
	fprintf(stderr, "[DEBUG] Core is now running: %s\n", Core::IsRunning() ? "true" : "false");

	if (s_running.IsSet())
	{
		fprintf(stderr, "[DEBUG] Starting main loop\n");
		platform->MainLoop();
		fprintf(stderr, "[DEBUG] Main loop exited\n");
	}
	else
	{
		fprintf(stderr, "[DEBUG] Skipping main loop (not running)\n");
	}
	
	fprintf(stderr, "[DEBUG] Stopping core\n");
	Core::Stop();

	fprintf(stderr, "[DEBUG] Shutting down\n");
	Core::Shutdown();
	platform->Shutdown();
	UICommon::Shutdown();

	delete platform;
	fprintf(stderr, "[DEBUG] MainNoGUI exiting normally\n");

	return 0;
}
