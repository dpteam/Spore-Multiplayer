/****************************************************************************
* Spore Multiplayer
*
* A real console window for the mod.
*
* Spore is a windowed DirectX application, so a console can be allocated with
* AllocConsole(). It mirrors the log file and accepts the same commands as the
* in-game console (Ctrl+Shift+C), which means the overlay can stay off and the
* game view stays clean.
*
* Commands are read on a small worker thread and queued, then executed from the
* game thread, so nothing touches the session from two threads at once.
****************************************************************************/
#pragma once

#include <string>
#include <vector>

namespace SporeMP
{
	class Console
	{
	public:
		static Console& Get();

		/// Allocates the console window and starts reading the input.
		void Initialize();

		/// Releases the console. Called from Dispose().
		void Shutdown();

		/// Prints a line to the console (and to the log file).
		void Print(const std::string& text);

		/// Executes the queued commands. Called from the game update loop.
		void ProcessCommands();

		bool IsOpen() const { return mOpen; }

	private:
		Console();

		void InputLoop();

		bool mOpen = false;
		bool mStopRequested = false;
		unsigned long mThreadId = 0;

		std::vector<std::string> mCommands;
	};
}
