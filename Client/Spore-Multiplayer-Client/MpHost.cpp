/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpHost.h"
#include "MpLog.h"

#include <vector>

namespace SporeMP
{
	namespace
	{
		const char* kServerExeName = "Spore-Multiplayer-Server.exe";

		bool FileExists(const std::string& path)
		{
			const DWORD attributes = GetFileAttributesA(path.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}
	}

	Host::Host()
	{
	}

	Host& Host::Get()
	{
		static Host instance;
		return instance;
	}

	bool Host::LocateServer()
	{
		if (!mServerPath.empty() && FileExists(mServerPath)) return true;

		// The mod DLL lives in the launcher "mLibs" folder; the server is usually
		// next to it or in the build output folder of the repository.
		std::string directory = mDirectory;
		if (directory.empty())
		{
			MP_LOG_WARN("The mod directory is unknown, hosting is not available");
			return false;
		}

		const std::string candidates[] = {
			directory + kServerExeName,
			directory + "..\\" + kServerExeName,
			directory + "..\\..\\" + kServerExeName,
			directory + "..\\..\\..\\bin\\Release\\" + kServerExeName,
			directory + "..\\..\\..\\bin\\Debug\\" + kServerExeName,
			directory + "..\\..\\..\\..\\bin\\Release\\" + kServerExeName,
			directory + "..\\..\\..\\..\\bin\\Debug\\" + kServerExeName,
		};

		for (const std::string& candidate : candidates)
		{
			char fullPath[MAX_PATH] = { 0 };
			if (GetFullPathNameA(candidate.c_str(), MAX_PATH, fullPath, nullptr) == 0) continue;

			if (FileExists(fullPath))
			{
				mServerPath = fullPath;
				MP_LOG_INFO("Found the dedicated server at %s", mServerPath.c_str());
				return true;
			}
		}

		MP_LOG_WARN("Could not find %s, hosting is not available", kServerExeName);
		return false;
	}

	bool Host::StartServer(uint16_t port, const std::string& sessionName,
		const std::string& masterUrl, std::string& error)
	{
		if (IsHosting())
		{
			error = "A server is already running";
			return false;
		}

		if (!LocateServer())
		{
			error = std::string("Could not find ") + kServerExeName +
				". Put it next to the mod DLL or set server_exe in SporeMultiplayer.ini";
			return false;
		}

		// The server is a console application; it is started hidden so it does not
		// steal the focus from the game.
		std::string commandLine = "\"" + mServerPath + "\"";
		commandLine += " --port " + std::to_string(port);
		commandLine += " --name \"" + sessionName + "\"";

		// Register the session on the master so other players can find it.
		if (!masterUrl.empty())
		{
			commandLine += " --master \"" + masterUrl + "\"";
		}

		STARTUPINFOA startupInfo;
		std::memset(&startupInfo, 0, sizeof(startupInfo));
		startupInfo.cb = sizeof(startupInfo);
		startupInfo.dwFlags = STARTF_USESHOWWINDOW;
		startupInfo.wShowWindow = SW_HIDE;

		PROCESS_INFORMATION processInfo;
		std::memset(&processInfo, 0, sizeof(processInfo));

		// CreateProcessA modifies the command line buffer, so it needs a mutable copy.
		std::vector<char> buffer(commandLine.begin(), commandLine.end());
		buffer.push_back('\0');

		const std::string workingDirectory = mServerPath.substr(0, mServerPath.find_last_of("\\/"));

		const BOOL started = CreateProcessA(
			nullptr, buffer.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(),
			&startupInfo, &processInfo);

		if (!started)
		{
			error = "Could not start the server (error " + std::to_string(GetLastError()) + ")";
			return false;
		}

		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		mProcess = static_cast<uintptr_t>(processInfo.dwProcessId);

		MP_LOG_INFO("Started the dedicated server (pid %lu) on port %u: %s",
			processInfo.dwProcessId, port, commandLine.c_str());
		return true;
	}

	bool Host::IsHosting() const
	{
		if (mProcess == 0) return false;

		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(mProcess));
		if (process == nullptr) return false;

		DWORD exitCode = 0;
		const bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
		CloseHandle(process);

		return running;
	}

	void Host::StopServer()
	{
		if (mProcess == 0) return;

		HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(mProcess));
		if (process != nullptr)
		{
			TerminateProcess(process, 0);
			CloseHandle(process);
			MP_LOG_INFO("Stopped the dedicated server");
		}

		mProcess = 0;
	}
}
