/****************************************************************************
* Spore Multiplayer - testing helper
*
* Minimal DLL injector used to start a second Spore instance for local
* multiplayer testing.
*
* The Spore ModAPI Launcher refuses to start the game when another instance is
* already running ("GameAlreadyRunning"), which makes it impossible to test a
* session with two players on one computer. Spore itself allows it when it is
* started with the `-multipleInstances` flag, so this tool does what the
* launcher does: it loads the ModAPI DLL into an already running SporeApp.exe.
*
* Usage:
*     mp-inject.exe <process name or pid> <path to the DLL>
*
* Example:
*     mp-inject.exe SporeApp "C:\ProgramData\SPORE ModAPI Launcher Kit\SporeModAPI-steam_patched.dll"
*
* This is a development tool: it is not needed to play.
****************************************************************************/
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	DWORD FindProcessId(const char* name)
	{
		// A numeric argument is used directly as a process id.
		char* end = nullptr;
		const unsigned long asNumber = std::strtoul(name, &end, 10);
		if (end != nullptr && *end == '\0' && asNumber != 0)
		{
			return static_cast<DWORD>(asNumber);
		}

		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE) return 0;

		PROCESSENTRY32 entry;
		entry.dwSize = sizeof(entry);

		DWORD result = 0;
		if (Process32First(snapshot, &entry))
		{
			do
			{
				if (_stricmp(entry.szExeFile, name) == 0)
				{
					result = entry.th32ProcessID;
					break;
				}
			} while (Process32Next(snapshot, &entry));
		}

		CloseHandle(snapshot);
		return result;
	}

	bool Inject(DWORD processId, const std::string& dllPath)
	{
		HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
			PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, processId);

		if (process == nullptr)
		{
			std::printf("OpenProcess failed (%lu). Run this tool as administrator if Spore is elevated.\n",
				GetLastError());
			return false;
		}

		const size_t size = dllPath.size() + 1;
		void* remoteMemory = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (remoteMemory == nullptr)
		{
			std::printf("VirtualAllocEx failed (%lu)\n", GetLastError());
			CloseHandle(process);
			return false;
		}

		if (!WriteProcessMemory(process, remoteMemory, dllPath.c_str(), size, nullptr))
		{
			std::printf("WriteProcessMemory failed (%lu)\n", GetLastError());
			VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
			CloseHandle(process);
			return false;
		}

		// The address of LoadLibraryA is valid in the target process as well, because
		// kernel32.dll is mapped at the same base address in every 32 bit process.
		HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
		if (kernel32 == nullptr)
		{
			std::printf("Could not get kernel32.dll\n");
			VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
			CloseHandle(process);
			return false;
		}

		auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
			GetProcAddress(kernel32, "LoadLibraryA"));

		std::printf("  kernel32 base: %p\n", reinterpret_cast<void*>(kernel32));
		std::printf("  LoadLibraryA:  %p\n", reinterpret_cast<void*>(loadLibrary));
		std::printf("  remote string: %p\n", remoteMemory);

		// The base address of kernel32 is NOT guaranteed to be the same in both
		// processes (ASLR), so the target's own base is read and the offset of
		// LoadLibraryA is applied to it.
		HMODULE targetModules[256];
		DWORD needed = 0;
		void* targetLoadLibrary = reinterpret_cast<void*>(loadLibrary);

		if (EnumProcessModules(process, targetModules, sizeof(targetModules), &needed))
		{
			const DWORD count = needed / sizeof(HMODULE);
			for (DWORD i = 0; i < count; ++i)
			{
				char name[MAX_PATH] = { 0 };
				if (GetModuleBaseNameA(process, targetModules[i], name, MAX_PATH) == 0) continue;

				if (_stricmp(name, "kernel32.dll") != 0) continue;

				const uintptr_t localBase = reinterpret_cast<uintptr_t>(kernel32);
				const uintptr_t targetBase = reinterpret_cast<uintptr_t>(targetModules[i]);
				const uintptr_t offset = reinterpret_cast<uintptr_t>(loadLibrary) - localBase;

				targetLoadLibrary = reinterpret_cast<void*>(targetBase + offset);

				std::printf("  target kernel32 base: %p -> LoadLibraryA at %p\n",
					reinterpret_cast<void*>(targetBase), targetLoadLibrary);
				break;
			}
		}
		else
		{
			// ERROR_PARTIAL_COPY (299) is normal for a freshly created suspended
			// process: the module list is not ready yet. kernel32.dll is mapped at
			// the same address in every 32 bit process of the session, so the local
			// address is used as the fallback.
			std::printf("  EnumProcessModules failed (%lu), using the local address\n", GetLastError());
		}

		HANDLE thread = CreateRemoteThread(process, nullptr, 0,
			reinterpret_cast<LPTHREAD_START_ROUTINE>(targetLoadLibrary), remoteMemory, 0, nullptr);
		if (thread == nullptr)
		{
			std::printf("CreateRemoteThread failed (%lu)\n", GetLastError());
			VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
			CloseHandle(process);
			return false;
		}

		const DWORD waitResult = WaitForSingleObject(thread, 10000);
		std::printf("  wait result: %lu\n", waitResult);

		DWORD exitCode = 0;
		GetExitCodeThread(thread, &exitCode);

		std::printf("  thread exit code: %lu (0x%lx)\n", exitCode, exitCode);

		CloseHandle(thread);
		VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
		CloseHandle(process);

		if (exitCode == 0)
		{
			std::printf("LoadLibraryA returned NULL: the DLL was not loaded.\n");
			return false;
		}

		return true;
	}
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		std::printf(
			"Spore Multiplayer DLL injector\n"
			"\n"
			"Usage:\n"
			"  mp-inject.exe <process name or pid> <path to the DLL>\n"
			"  mp-inject.exe --launch <exe> <dll> [more dlls...] [-- args for the exe]\n"
			"\n"
			"The --launch form starts the executable suspended, injects every DLL and\n"
			"then resumes it. That is what the ModAPI Launcher does, and it is the only\n"
			"way to load the mod into a second Spore instance: injecting into an already\n"
			"running game loads the DLL but the ModAPI init has already happened.\n"
			"\n"
			"Example (second Spore window with an isolated UserData folder):\n"
			"  mp-inject.exe --launch SporeApp.exe \"...\\SporeModAPI-steam_patched.dll\" \"...\\Spore-Multiplayer-Client.dll\" -w -multipleInstances -devDirs\n");
		return 1;
	}

	const std::string first = argv[1];

	// ---- launch mode -------------------------------------------------------
	if (first == "--launch")
	{
		if (argc < 4)
		{
			std::printf("Usage: mp-inject.exe --launch <exe> <dll> [more dlls...] [-- args]\n");
			return 1;
		}

		const std::string executable = argv[2];

		std::vector<std::string> dlls;
		int index = 3;
		for (; index < argc; ++index)
		{
			const std::string value = argv[index];
			if (value == "--") { ++index; break; }
			dlls.push_back(value);
		}

		std::string arguments;
		for (; index < argc; ++index)
		{
			if (!arguments.empty()) arguments += ' ';
			arguments += argv[index];
		}

		// The executable must exist; a bare name is resolved through PATH.
		std::string executablePath = executable;
		if (GetFileAttributesA(executablePath.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			char found[MAX_PATH] = { 0 };
			if (SearchPathA(nullptr, executable.c_str(), ".exe", MAX_PATH, found, nullptr) == 0)
			{
				std::printf("Could not find %s\n", executable.c_str());
				return 1;
			}
			executablePath = found;
		}

		std::string workingDirectory = executablePath;
		const size_t separator = workingDirectory.find_last_of("\\/");
		workingDirectory = (separator == std::string::npos) ? std::string(".") : workingDirectory.substr(0, separator);

		std::string commandLine = "\"" + executablePath + "\"";
		if (!arguments.empty()) commandLine += " " + arguments;

		std::vector<char> buffer(commandLine.begin(), commandLine.end());
		buffer.push_back('\0');

		STARTUPINFOA startupInfo;
		std::memset(&startupInfo, 0, sizeof(startupInfo));
		startupInfo.cb = sizeof(startupInfo);

		PROCESS_INFORMATION processInfo;
		std::memset(&processInfo, 0, sizeof(processInfo));

		// CREATE_SUSPENDED: nothing of the game runs before the DLLs are in place.
		if (!CreateProcessA(nullptr, buffer.data(), nullptr, nullptr, FALSE,
			CREATE_SUSPENDED, nullptr, workingDirectory.c_str(), &startupInfo, &processInfo))
		{
			std::printf("CreateProcess failed (%lu)\n", GetLastError());
			return 1;
		}

		std::printf("Started %s (pid %lu, suspended)\n", executablePath.c_str(), processInfo.dwProcessId);

		bool ok = true;
		for (const std::string& dll : dlls)
		{
			std::printf("Injecting %s...\n", dll.c_str());
			if (!Inject(processInfo.dwProcessId, dll))
			{
				ok = false;
				break;
			}
		}

		if (!ok)
		{
			std::printf("Injection failed, killing the process\n");
			TerminateProcess(processInfo.hProcess, 1);
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			return 1;
		}

		ResumeThread(processInfo.hThread);

		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);

		std::printf("Resumed, the game should start with the mods loaded.\n");
		return 0;
	}

	// ---- inject into a running process -------------------------------------
	const char* target = argv[1];
	const std::string dllPath = argv[2];

	if (GetFileAttributesA(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		std::printf("The DLL does not exist: %s\n", dllPath.c_str());
		return 1;
	}

	const DWORD processId = FindProcessId(target);
	if (processId == 0)
	{
		std::printf("Could not find a process named '%s'\n", target);
		return 1;
	}

	std::printf("Injecting %s into pid %lu...\n", dllPath.c_str(), processId);

	if (!Inject(processId, dllPath))
	{
		return 1;
	}

	std::printf("Injected successfully.\n");
	return 0;
}
