#include "SlippiReplayComm.h"

#include <cctype>
#include <memory>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/Logging/LogManager.h"
#include "Core/ConfigManager.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

std::unique_ptr<SlippiReplayComm> g_replayComm;

// https://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring
// trim from start (in place)
static inline void ltrim(std::string &s)
{
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
}

// trim from end (in place)
static inline void rtrim(std::string &s)
{
	s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s)
{
	ltrim(s);
	rtrim(s);
}

SlippiReplayComm::SlippiReplayComm()
{
	fprintf(stderr, "[SLIPPI DEBUG] SlippiReplayComm constructor called\n");
	INFO_LOG(EXPANSIONINTERFACE, "SlippiReplayComm: Using playback config path: %s",
	         SConfig::GetInstance().m_strSlippiInput.c_str());
	configFilePath = SConfig::GetInstance().m_strSlippiInput.c_str();
	fprintf(stderr, "[SLIPPI DEBUG] SlippiReplayComm constructor completed, configFilePath: %s\n", configFilePath.c_str());
}

SlippiReplayComm::~SlippiReplayComm() {}

SlippiReplayComm::CommSettings SlippiReplayComm::getSettings()
{
	fprintf(stderr, "[SLIPPI DEBUG] getSettings() called\n");
	loadFile();
	fprintf(stderr, "[SLIPPI DEBUG] loadFile() completed successfully\n");
	return commFileSettings;
}

std::string SlippiReplayComm::getReplayPath()
{
	std::string replayFilePath = commFileSettings.replayPath;
	if (commFileSettings.mode == "queue")
	{
		// If we are in queue mode, let's grab the replay from the queue instead
		replayFilePath = commFileSettings.queue.empty() ? "" : commFileSettings.queue.front().path;
	}

	return replayFilePath;
}

bool SlippiReplayComm::isNewReplay()
{
	fprintf(stderr, "[SLIPPI DEBUG] isNewReplay() called\n");
	loadFile();
	std::string replayFilePath = getReplayPath();
	fprintf(stderr, "[SLIPPI DEBUG] Replay file path: %s\n", replayFilePath.c_str());

	bool hasPathChanged = replayFilePath != previousReplayLoaded;
	bool isReplay = !!replayFilePath.length();

	// The previous check is mostly good enough but it does not
	// work if someone tries to load the same replay twice in a row
	// the commandId was added to deal with this
	bool hasCommandChanged = commFileSettings.commandId != previousCommandId;

	// This checks if the queue index has changed, this is to fix the
	// issue where the same replay showing up twice in a row in a
	// queue would never cause this function to return true
	bool hasQueueIdxChanged = false;
	if (commFileSettings.mode == "queue" && !commFileSettings.queue.empty())
	{
		hasQueueIdxChanged = commFileSettings.queue.front().index != previousIndex;
	}

	bool isNewReplay = hasPathChanged || hasCommandChanged || hasQueueIdxChanged;
	
	fprintf(stderr, "[SLIPPI DEBUG] isNewReplay result: hasPathChanged=%s, hasCommandChanged=%s, hasQueueIdxChanged=%s, isReplay=%s, isNewReplay=%s\n",
		hasPathChanged ? "true" : "false",
		hasCommandChanged ? "true" : "false", 
		hasQueueIdxChanged ? "true" : "false",
		isReplay ? "true" : "false",
		isNewReplay ? "true" : "false");

	return isReplay && isNewReplay;
}

void SlippiReplayComm::nextReplay()
{
	if (commFileSettings.queue.empty())
	{
#ifdef IS_PLAYBACK
		if (!queueWasEmpty)
			std::cout << "[NO_GAME]" << std::endl;
		queueWasEmpty = true;
#endif
		return;
	}

	// Increment queue position
	commFileSettings.queue.pop();
}

std::unique_ptr<Slippi::SlippiGame> SlippiReplayComm::loadGame()
{
	fprintf(stderr, "[SLIPPI DEBUG] loadGame() called\n");
	auto replayFilePath = getReplayPath();
	fprintf(stderr, "[SLIPPI DEBUG] Attempting to load replay file: %s\n", replayFilePath.c_str());
	
	// Check if file exists
	if (!File::Exists(replayFilePath)) {
		fprintf(stderr, "[SLIPPI DEBUG] Replay file does not exist: %s\n", replayFilePath.c_str());
		return nullptr;
	}
	
	fprintf(stderr, "[SLIPPI DEBUG] Replay file exists, attempting to load\n");
	auto result = Slippi::SlippiGame::FromFile(replayFilePath);
	if (result)
	{
		fprintf(stderr, "[SLIPPI DEBUG] Replay file loaded successfully\n");
		// If we successfully loaded a SlippiGame, indicate as such so
		// that this game won't be considered new anymore. If the replay
		// file did not exist yet, result will be falsy, which will keep
		// the replay considered new so that the file will attempt to be
		// loaded again
		previousReplayLoaded = replayFilePath;
		previousCommandId = commFileSettings.commandId;
		if (commFileSettings.mode == "queue" && !commFileSettings.queue.empty())
		{
			previousIndex = commFileSettings.queue.front().index;
		}

		WatchSettings ws;
		ws.path = replayFilePath;
		ws.startFrame = commFileSettings.startFrame;
		ws.endFrame = commFileSettings.endFrame;
		if (commFileSettings.mode == "queue")
		{
			ws = commFileSettings.queue.front();
		}

		if (commFileSettings.outputOverlayFiles)
		{
			std::string dirpath = File::GetExeDirectory();
			File::WriteStringToFile(ws.gameStation, dirpath + DIR_SEP + "Slippi/out-station.txt");
			File::WriteStringToFile(ws.gameStartAt, dirpath + DIR_SEP + "Slippi/out-time.txt");
		}

		current = ws;
	}
	else
	{
		fprintf(stderr, "[SLIPPI DEBUG] Failed to load replay file\n");
	}

	return std::move(result);
}

void SlippiReplayComm::loadFile()
{
	fprintf(stderr, "[SLIPPI DEBUG] loadFile() called, configFilePath: %s\n", configFilePath.c_str());
	
	// TODO: Consider even only checking file mod time every 250 ms or something? Not sure
	// TODO: what the perf impact is atm

	u64 modTime = File::GetFileModTime(configFilePath);
	fprintf(stderr, "[SLIPPI DEBUG] File mod time: %lu, last load mod time: %lu\n", modTime, configLastLoadModTime);
	if (modTime != 0 && modTime == configLastLoadModTime)
	{
		fprintf(stderr, "[SLIPPI DEBUG] File hasn't changed, returning early\n");
		// TODO: Maybe be smarter than just using mod time? Look for other things that would
		// TODO: indicate that file has changed and needs to be reloaded?
		return;
	}

	WARN_LOG(EXPANSIONINTERFACE, "File change detected in comm file: %s", configFilePath.c_str());
	configLastLoadModTime = modTime;

	// TODO: Maybe load file in a more intelligent way to save
	// TODO: file operations
	fprintf(stderr, "[SLIPPI DEBUG] About to read file contents\n");
	std::string commFileContents;
	File::ReadFileToString(configFilePath, commFileContents);
	fprintf(stderr, "[SLIPPI DEBUG] File contents read, length: %zu\n", commFileContents.length());

	fprintf(stderr, "[SLIPPI DEBUG] About to parse JSON\n");
	auto res = json::parse(commFileContents, nullptr, false);
	fprintf(stderr, "[SLIPPI DEBUG] JSON parsing completed\n");
	if (res.is_discarded() || !res.is_object())
	{
		fprintf(stderr, "[SLIPPI DEBUG] JSON parsing failed or not an object\n");
		// Happens if there is a parse error, I think?
		commFileSettings.mode = "normal";
		commFileSettings.replayPath = "";
		commFileSettings.startFrame = Slippi::GAME_FIRST_FRAME;
		commFileSettings.endFrame = INT_MAX;
		commFileSettings.commandId = "";
		commFileSettings.outputOverlayFiles = false;
		commFileSettings.isRealTimeMode = false;
		commFileSettings.shouldResync = true;
		commFileSettings.rollbackDisplayMethod = "off";
		commFileSettings.gameStation = "";

		if (res.is_string())
		{
			// If we have a string, let's use that as the replayPath
			// This is really only here because when developing it might be easier
			// to just throw in a string instead of an object

			commFileSettings.replayPath = res.get<std::string>();
		}
		else
		{
			WARN_LOG(EXPANSIONINTERFACE, "Comm file load error detected. Check file format");

			// Reset in the case of read error. this fixes a race condition where file mod time changes but
			// the file is not readable yet?
			configLastLoadModTime = 0;
		}

		fprintf(stderr, "[SLIPPI DEBUG] Using default settings due to parsing error\n");
		return;
	}

	fprintf(stderr, "[SLIPPI DEBUG] About to extract settings from JSON\n");
	// TODO: Support file with only path string
	commFileSettings.mode = res.value("mode", "normal");
	commFileSettings.replayPath = res.value("replay", "");
	commFileSettings.startFrame = res.value("startFrame", Slippi::GAME_FIRST_FRAME);
	commFileSettings.endFrame = res.value("endFrame", INT_MAX);
	commFileSettings.commandId = res.value("commandId", "");
	commFileSettings.outputOverlayFiles = res.value("outputOverlayFiles", false);
	commFileSettings.isRealTimeMode = res.value("isRealTimeMode", false);
	commFileSettings.shouldResync = res.value("shouldResync", true);
	commFileSettings.rollbackDisplayMethod = res.value("rollbackDisplayMethod", "off");
	commFileSettings.gameStation = res.value("gameStation", "");

	if (commFileSettings.mode == "queue")
	{
		fprintf(stderr, "[SLIPPI DEBUG] Processing queue mode\n");
		auto queue = res["queue"];
		if (queue.is_array())
		{
			std::queue<WatchSettings>().swap(commFileSettings.queue);
			int index = 0;
			for (json::iterator it = queue.begin(); it != queue.end(); ++it)
			{
				json el = *it;
				WatchSettings w = {};
				w.path = el.value("path", "");
				w.startFrame = el.value("startFrame", Slippi::GAME_FIRST_FRAME);
				w.endFrame = el.value("endFrame", INT_MAX);
				w.gameStartAt = el.value("gameStartAt", "");
				w.gameStation = el.value("gameStation", "");
				w.index = index++;

				commFileSettings.queue.push(w);
			};

			queueWasEmpty = false;
		}
	}
	
	fprintf(stderr, "[SLIPPI DEBUG] Settings loaded successfully: mode=%s, replayPath=%s\n", 
		commFileSettings.mode.c_str(), commFileSettings.replayPath.c_str());
}
