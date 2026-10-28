/****************************************************************************
* Spore Multiplayer
*
* Logging for the client mod. Messages always go to `SporeMultiplayer.log`
* (next to the DLL).
*
* The file is opened in shared mode, so it can be read (for example with
* `Get-Content` or a tail tool) while the game is running.
****************************************************************************/
#pragma once

#include <string>

namespace SporeMP
{
	enum class LogLevel
	{
		Debug = 0,
		Info = 1,
		Warning = 2,
		Error = 3,
	};

	/// Prepares the log file. Must be called from Initialize().
	void InitializeLog(const std::string& directory);

	/// Sets the minimum level that is written. Defaults to Info.
	void SetLogLevel(LogLevel level);

	LogLevel GetLogLevel();

	/// Parses "debug", "info", "warning", "error". Returns false if unknown.
	bool ParseLogLevel(const char* name, LogLevel& dst);

	/// Closes the log file. Must be called from Dispose().
	void ShutdownLog();

		void LogMessage(LogLevel level, const char* format, ...);

	/// Registers a callback that receives every log line (used by the console
	/// window). Pass nullptr to remove it.
	void SetConsoleSink(void (*sink)(const char*));

	/// Returns the full path of the log file.
	const std::string& GetLogPath();
}

#define MP_LOG_DEBUG(...) ::SporeMP::LogMessage(::SporeMP::LogLevel::Debug, __VA_ARGS__)
#define MP_LOG_INFO(...) ::SporeMP::LogMessage(::SporeMP::LogLevel::Info, __VA_ARGS__)
#define MP_LOG_WARN(...) ::SporeMP::LogMessage(::SporeMP::LogLevel::Warning, __VA_ARGS__)
#define MP_LOG_ERROR(...) ::SporeMP::LogMessage(::SporeMP::LogLevel::Error, __VA_ARGS__)
