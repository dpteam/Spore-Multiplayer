// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

#include "MpAvatars.h"
#include "MpCheats.h"
#include "MpConfig.h"
#include "MpConsole.h"
#include "MpHost.h"
#include "MpLog.h"
#include "MpOverlay.h"
#include "MpSession.h"

namespace
{
	/// Directory of the mod DLL, with a trailing backslash. It is resolved from the
	/// module handle, because GetModuleFileName(nullptr) would return the path of
	/// SporeApp.exe instead of the path of this DLL.
	std::string g_moduleDirectory;

	void ResolveModuleDirectory(HMODULE module)
	{
		char path[MAX_PATH] = { 0 };
		const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
		if (length == 0) return;

		std::string fullPath(path, length);
		const size_t separator = fullPath.find_last_of("\\/");
		if (separator == std::string::npos) return;
		g_moduleDirectory = fullPath.substr(0, separator + 1);
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

	// The console replaces the overlay: same information, no screen clutter.
	if (SporeMP::Session::Get().Config().console)
	{
		SporeMP::Console::Get().Initialize();
	}

	SporeMP::Overlay::Get().Initialize();
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
