/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpConsole.h"
#include "MpLog.h"
#include "MpSession.h"

#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>

namespace SporeMP
{
	namespace
	{
		std::mutex g_commandMutex;

		/// Receives every log line so the console shows the same messages as the
		/// log file. It must never call the logger itself, or it would recurse.
		void ConsoleLogSink(const char* line)
		{
			Console::Get().Print(line);
		}
	}

	Console::Console()
	{
	}

	Console& Console::Get()
	{
		static Console instance;
		return instance;
	}

	void Console::Initialize()
	{
		if (mOpen) return;

		if (!AllocConsole())
		{
			MP_LOG_WARN("Could not allocate the console window (error %lu)", GetLastError());
			return;
		}

		// Make the standard streams point at the new console, so printf/iostream
		// work as usual.
		FILE* stream = nullptr;
		freopen_s(&stream, "CONOUT$", "w", stdout);
		freopen_s(&stream, "CONOUT$", "w", stderr);
		freopen_s(&stream, "CONIN$", "r", stdin);

		SetConsoleTitleA("Spore Multiplayer");

		// The console window is a separate OS window; give it a readable size.
		HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
		if (output != INVALID_HANDLE_VALUE)
		{
			CONSOLE_SCREEN_BUFFER_INFO info;
			if (GetConsoleScreenBufferInfo(output, &info))
			{
				SMALL_RECT window = info.srWindow;
				window.Right = window.Left + 99;
				window.Bottom = window.Top + 34;
				SetConsoleWindowInfo(output, TRUE, &window);
			}
		}

		mOpen = true;
		mStopRequested = false;

		std::printf("Spore Multiplayer console\n");
		std::printf("Type 'mp_help' for the list of commands. The game keeps running in the background.\n");
		std::printf("Short version: mp_host to host, mp_servers / mp_join to join.\n\n");
		std::fflush(stdout);

		std::thread inputThread(&Console::InputLoop, this);
		inputThread.detach();

		mThreadId = GetCurrentThreadId();

		// Mirror the log into the console window.
		SetConsoleSink(ConsoleLogSink);

		MP_LOG_INFO("Console window opened");
	}

	void Console::Shutdown()
	{
		if (!mOpen) return;

		SetConsoleSink(nullptr);

		mStopRequested = true;
		mOpen = false;

		// Closing the console while the input thread is blocked on stdin would
		// leave a dangling thread, so the thread is simply detached and the console
		// is freed; the process is shutting down anyway.
		FreeConsole();
	}

	void Console::Print(const std::string& text)
	{
		if (!mOpen) return;

		std::printf("%s\n", text.c_str());
		std::fflush(stdout);
	}

	void Console::InputLoop()
	{
		std::string line;
		while (!mStopRequested && std::getline(std::cin, line))
		{
			if (line.empty()) continue;

			std::lock_guard<std::mutex> lock(g_commandMutex);
			mCommands.push_back(line);
		}
	}

	void Console::ProcessCommands()
	{
		std::vector<std::string> commands;
		{
			std::lock_guard<std::mutex> lock(g_commandMutex);
			commands.swap(mCommands);
		}

		for (const std::string& command : commands)
		{
			Session::Get().ExecuteConsoleCommand(command);
		}
	}
}
