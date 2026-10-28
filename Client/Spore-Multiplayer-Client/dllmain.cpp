// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

#include "MpAvatars.h"
#include "MpCheats.h"
#include "MpConfig.h"
#include "MpConsole.h"
#include "MpHost.h"
#include "MpLog.h"
#include "MpMenu.h"
#include "MpOverlay.h"
#include "MpSession.h"

#include <cstring>
#include <vector>

#pragma comment(lib, "version.lib")

namespace
{
	/// Directory of the mod DLL, with a trailing backslash. It is resolved from the
	/// module handle, because GetModuleFileName(nullptr) would return the path of
	/// SporeApp.exe instead of the path of this DLL.
	std::string g_moduleDirectory;
	HMODULE g_moduleHandle = nullptr;

	void ResolveModuleDirectory(HMODULE module)
	{
		g_moduleHandle = module;

		char path[MAX_PATH] = { 0 };
		const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
		if (length == 0) return;

		std::string fullPath(path, length);
		const size_t separator = fullPath.find_last_of("\\/");
		if (separator == std::string::npos) return;
		g_moduleDirectory = fullPath.substr(0, separator + 1);
	}

	/// Logs everything needed to diagnose a report: paths, versions, OS and the
	/// effective configuration. Runs once at startup and again from `mp_diag`.
	void LogEnvironment()
	{
		// OS version. GetVersionEx lies unless the app is manifested for Windows 10,
		// so RtlGetVersion is used instead.
		MP_LOG_INFO("--- environment ---");

		typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

		HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		RtlGetVersionPtr rtlGetVersion = ntdll != nullptr
			? reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(ntdll, "RtlGetVersion"))
			: nullptr;

		if (rtlGetVersion != nullptr)
		{
			RTL_OSVERSIONINFOW info;
			std::memset(&info, 0, sizeof(info));
			info.dwOSVersionInfoSize = sizeof(info);

			if (rtlGetVersion(&info) == 0)
			{
				MP_LOG_INFO("OS: %lu.%lu build %lu",
					info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
			}
		}

		// This DLL and the game executable.
		char modulePath[MAX_PATH] = { 0 };
		if (GetModuleFileNameA(g_moduleHandle, modulePath, MAX_PATH) != 0)
		{
			MP_LOG_INFO("Mod DLL: %s", modulePath);
		}

		char gamePath[MAX_PATH] = { 0 };
		if (GetModuleFileNameA(nullptr, gamePath, MAX_PATH) != 0)
		{
			MP_LOG_INFO("Game exe: %s", gamePath);

			// Spore's own version, which matters because the SDK targets it.
			DWORD ignored = 0;
			const DWORD size = GetFileVersionInfoSizeA(gamePath, &ignored);
			if (size > 0)
			{
				std::vector<uint8_t> data(size);
				if (GetFileVersionInfoA(gamePath, 0, size, data.data()))
				{
					VS_FIXEDFILEINFO* info = nullptr;
					UINT infoSize = 0;
					if (VerQueryValueA(data.data(), "\\", reinterpret_cast<void**>(&info), &infoSize) && info != nullptr)
					{
						MP_LOG_INFO("Spore version: %u.%u.%u.%u",
							HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
							HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
					}
				}
			}
		}

		MP_LOG_INFO("Log file: %s", SporeMP::GetLogPath().c_str());
		MP_LOG_INFO("Process id: %lu", static_cast<unsigned long>(GetCurrentProcessId()));

		// Effective configuration, secrets aside.
		const SporeMP::ClientConfig& config = SporeMP::Session::Get().Config();
		MP_LOG_INFO("--- configuration ---");
		MP_LOG_INFO("host=%s port=%u name='%s' auto_connect=%d",
			config.host.c_str(), config.port, config.name.c_str(), config.autoConnect ? 1 : 0);
		MP_LOG_INFO("master=%s relay=%s",
			config.masterUrl.c_str(), config.relayUrl.c_str());
		MP_LOG_INFO("hud=%d console=%d send_rate=%d avatars=%d tags=%d interpolation=%d log_level=%s",
			config.hud ? 1 : 0, config.console ? 1 : 0, config.sendRate,
			config.showAvatars ? 1 : 0, config.nameTags ? 1 : 0,
			config.interpolation ? 1 : 0, config.logLevel.c_str());
		MP_LOG_INFO("--- end environment ---");
	}

	/// Called once every game frame by the App update message.
	void Update()
	{
		static ULONGLONG lastTick = GetTickCount64();
		static ULONGLONG lastHeartbeat = GetTickCount64();
		static unsigned long long frameCount = 0;

		const ULONGLONG now = GetTickCount64();
		float deltaTime = static_cast<float>(now - lastTick) / 1000.0f;
		lastTick = now;

		// A paused or alt-tabbed game can produce huge deltas; clamp them so the
		// interpolation does not jump.
		if (deltaTime < 0.0f) deltaTime = 0.0f;
		if (deltaTime > 0.5f) deltaTime = 0.5f;

		// An exception must never escape into the game message loop: it would
		// abort the dispatch of the update message and the mod would stop working.
		try
		{
			SporeMP::Console::Get().ProcessCommands();
			SporeMP::Session::Get().Update(deltaTime);
			SporeMP::Menu::Get().Update(deltaTime);
			SporeMP::Overlay::Get().Update(deltaTime);
		}
		catch (const std::exception& exception)
		{
			MP_LOG_ERROR("Exception in the update loop: %s", exception.what());
		}
		catch (...)
		{
			MP_LOG_ERROR("Unknown exception in the update loop");
		}

		++frameCount;

		// Heartbeat: proves that the update message keeps arriving.
		if (now - lastHeartbeat >= 15000)
		{
			lastHeartbeat = now;
			MP_LOG_DEBUG("Heartbeat: %llu frames, %s", frameCount,
				SporeMP::Session::Get().GetStatusText().c_str());
		}
	}
}

void Initialize()
{
	// This method is executed when the game starts, before the user interface is shown

	const std::string directory = SporeMP::ResolveDataDirectory(g_moduleDirectory);

	SporeMP::InitializeLog(directory);
	MP_LOG_INFO("Spore Multiplayer client starting (data directory: %s)", directory.c_str());

	SporeMP::Session::Get().Initialize(directory);
	LogEnvironment();

	// The console replaces the overlay: same information, no screen clutter.
	if (SporeMP::Session::Get().Config().console)
	{
		SporeMP::Console::Get().Initialize();
	}

	SporeMP::Overlay::Get().Initialize();
	SporeMP::Menu::Get().Initialize();
	SporeMP::AddCheats();

	// Hosting looks for the server next to the mod DLL, unless the configuration
	// specifies an explicit path.
	SporeMP::Host::Get().Initialize(directory);
	if (!SporeMP::Session::Get().Config().serverExe.empty())
	{
		SporeMP::Host::Get().SetServerPath(SporeMP::Session::Get().Config().serverExe);
	}

	App::AddUpdateFunction(Update);

	MP_LOG_INFO("Spore Multiplayer client ready (log: %s)", SporeMP::GetLogPath().c_str());
}

void Dispose()
{
	// This method is called when the game is closing

	MP_LOG_INFO("Spore Multiplayer client stopping");

	SporeMP::Overlay::Get().Dispose();
	SporeMP::Menu::Get().Dispose();
	SporeMP::Session::Get().Dispose();
	SporeMP::Console::Get().Shutdown();
	SporeMP::ShutdownLog();
}

void AttachDetours()
{
	// Call the attach() method on any detours you want to add
	// For example: cViewer_SetRenderType_detour::attach(GetAddress(cViewer, SetRenderType));
}


// Generally, you don't need to touch any code here
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		ResolveModuleDirectory(hModule);

		ModAPI::AddPostInitFunction(Initialize);
		ModAPI::AddDisposeFunction(Dispose);

		PrepareDetours(hModule);
		AttachDetours();
		CommitDetours();
		break;

	case DLL_PROCESS_DETACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}
