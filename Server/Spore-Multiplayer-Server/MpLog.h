/****************************************************************************
* Spore Multiplayer - Dedicated server
*
* Minimal logging helpers. The server writes everything to stdout, which makes
* it usable both from a console window and from a redirected log file.
****************************************************************************/
#pragma once

#include <cstdarg>
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

	/// Changes the minimum level of messages that are printed.
	void SetLogLevel(LogLevel level);

	LogLevel GetLogLevel();

	/// Redirects the log to a file as well as stdout. Passing an empty path
	/// disables the file. The default is `<exe dir>\SporeMultiplayer-Server.log`,
	/// which matters because the server is often started hidden (mp_host) and its
	/// stdout is lost otherwise.
	void SetLogFile(const std::string& path);

	/// Returns the current log file path, empty if file logging is disabled.
	const std::string& GetLogFile();

	/// Returns "debug", "info", "warning" or "error".
	const char* LogLevelName(LogLevel level);

	/// Parses a log level name (case insensitive). Returns false if the name is unknown.
	bool ParseLogLevel(const char* name, LogLevel& dst);

	/// Prints a timestamped line to stdout if `level` is >= the current log level.
	void LogMessage(LogLevel level, const char* format, ...);
}

#define MP_LOG_DEBUG(...) ::SporeMP::LogMessage(::SporeMP::LogLevel::Debug, __VA_ARGS__)
#define MP_LOG_INFO(...) ::SporeMP::LogMessage(::SporeMP::LogLevel::Info, __VA_ARGS__)
#define MP_LOG_WARN(...) ::SporeMP::LogMessage(::SporeMP::LogLevel::Warning, __VA_ARGS__)
#define MP_LOG_ERROR(...) ::SporeMP::LogMessage(::SporeMP::LogLevel::Error, __VA_ARGS__)
