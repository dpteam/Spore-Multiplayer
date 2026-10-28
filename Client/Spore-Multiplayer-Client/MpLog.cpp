/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpLog.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <share.h>

namespace SporeMP
{
	namespace
	{
		std::mutex g_logMutex;
		std::FILE* g_logFile = nullptr;
		std::string g_logPath;
		void (*g_consoleSink)(const char*) = nullptr;
		LogLevel g_logLevel = LogLevel::Info;

		bool EqualsIgnoreCase(const char* a, const char* b)
		{
			if (a == nullptr || b == nullptr) return false;

			while (*a != '\0' && *b != '\0')
			{
				char ca = *a;
				char cb = *b;
				if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
				if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
				if (ca != cb) return false;
				++a;
				++b;
			}

			return *a == '\0' && *b == '\0';
		}

		const char* LevelName(LogLevel level)
		{
			switch (level)
			{
			case LogLevel::Debug: return "DEBUG";
			case LogLevel::Info: return "INFO";
			case LogLevel::Warning: return "WARN";
			case LogLevel::Error: return "ERROR";
			default: return "?";
			}
		}
	}

	void InitializeLog(const std::string& directory)
	{
		std::lock_guard<std::mutex> lock(g_logMutex);

		if (g_logFile != nullptr) return;

		g_logPath = directory + "SporeMultiplayer.log";

		// _SH_DENYNO allows several game instances to write to the same log,
		// and lets other tools read it while the game is running.
		g_logFile = _fsopen(g_logPath.c_str(), "a", _SH_DENYNO);
		if (g_logFile == nullptr)
		{
			return;
		}

		SYSTEMTIME time;
		GetLocalTime(&time);
		std::fprintf(g_logFile, "\n===== Spore Multiplayer started %04d-%02d-%02d %02d:%02d:%02d (pid %lu) =====\n",
			time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
			static_cast<unsigned long>(GetCurrentProcessId()));
		std::fflush(g_logFile);
	}

	void ShutdownLog()
	{
		std::lock_guard<std::mutex> lock(g_logMutex);

		if (g_logFile != nullptr)
		{
			std::fprintf(g_logFile, "===== Spore Multiplayer stopped =====\n");
			std::fclose(g_logFile);
			g_logFile = nullptr;
		}
	}

	void SetLogLevel(LogLevel level)
	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		g_logLevel = level;
	}

	LogLevel GetLogLevel()
	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		return g_logLevel;
	}

	bool ParseLogLevel(const char* name, LogLevel& dst)
	{
		if (EqualsIgnoreCase(name, "debug")) { dst = LogLevel::Debug; return true; }
		if (EqualsIgnoreCase(name, "info")) { dst = LogLevel::Info; return true; }
		if (EqualsIgnoreCase(name, "warning") || EqualsIgnoreCase(name, "warn")) { dst = LogLevel::Warning; return true; }
		if (EqualsIgnoreCase(name, "error")) { dst = LogLevel::Error; return true; }
		return false;
	}

	void SetConsoleSink(void (*sink)(const char*))
	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		g_consoleSink = sink;
	}

	const std::string& GetLogPath()
	{
		return g_logPath;
	}

	void LogMessage(LogLevel level, const char* format, ...)
	{
		if (level < GetLogLevel()) return;

		char message[2048];
		va_list args;
		va_start(args, format);
		vsnprintf(message, sizeof(message), format, args);
		va_end(args);

		SYSTEMTIME time;
		GetLocalTime(&time);

		char line[2200];
		std::snprintf(line, sizeof(line), "[%02d:%02d:%02d] [%-5s] %s",
			time.wHour, time.wMinute, time.wSecond, LevelName(level), message);

		// The console sink is called outside the lock: it prints to stdout, which
		// must never block the log.
		void (*sink)(const char*) = nullptr;
		{
			std::lock_guard<std::mutex> lock(g_logMutex);
			if (g_logFile != nullptr)
			{
				std::fprintf(g_logFile, "%s\n", line);
				std::fflush(g_logFile);
			}
			sink = g_consoleSink;
		}

		if (sink != nullptr)
		{
			sink(line);
		}
	}
}
