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
#include <cstdio>
#include <cstdlib>
#include <string>

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

		// The address of LoadLibraryW is valid in the target process as well, because
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
			GetProcAddress(kernel32, "LoadLibraryW"));

		HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remoteMemory, 0, nullptr);
		if (thread == nullptr)
		{
			std::printf("CreateRemoteThread failed (%lu)\n", GetLastError());
			VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
			CloseHandle(process);
			return false;
		}

		WaitForSingleObject(thread, 10000);

		DWORD exitCode = 0;
		GetExitCodeThread(thread, &exitCode);

		CloseHandle(thread);
		VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
		CloseHandle(process);

		if (exitCode == 0)
		{
			std::printf("LoadLibraryW returned NULL: the DLL was not loaded.\n");
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
			"Usage: mp-inject.exe <process name or pid> <path to the DLL>\n"
			"\n"
			"Example:\n"
			"  mp-inject.exe SporeApp \"C:\\ProgramData\\SPORE ModAPI Launcher Kit\\SporeModAPI-steam_patched.dll\"\n");
		return 1;
	}

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
