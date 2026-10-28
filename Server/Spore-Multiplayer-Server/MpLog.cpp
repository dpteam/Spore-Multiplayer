/****************************************************************************
* Spore Multiplayer - Dedicated server
****************************************************************************/
#include "MpLog.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <share.h>
#include <string>

#include <windows.h>

namespace SporeMP
{
	namespace
	{
		std::mutex g_logMutex;
		LogLevel g_logLevel = LogLevel::Info;
		std::FILE* g_logFile = nullptr;
		std::string g_logPath;

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
	}

	void SetLogLevel(LogLevel level)
	{
		g_logLevel = level;
	}

	LogLevel GetLogLevel()
	{
		return g_logLevel;
	}

	void SetLogFile(const std::string& path)
	{
		std::lock_guard<std::mutex> lock(g_logMutex);

		if (g_logFile != nullptr)
		{
			std::fclose(g_logFile);
			g_logFile = nullptr;
		}

		g_logPath = path;
		if (path.empty()) return;

		// _SH_DENYNO: several servers on one machine, and other tools, can read it.
		g_logFile = _fsopen(path.c_str(), "a", _SH_DENYNO);
		if (g_logFile == nullptr)
		{
			g_logPath.clear();
			return;
		}

		SYSTEMTIME time;
		GetLocalTime(&time);
		std::fprintf(g_logFile, "\n===== Server started %04d-%02d-%02d %02d:%02d:%02d (pid %lu) =====\n",
			time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
			static_cast<unsigned long>(GetCurrentProcessId()));
		std::fflush(g_logFile);
	}

	const std::string& GetLogFile()
	{
		return g_logPath;
	}

	const char* LogLevelName(LogLevel level)
	{
		switch (level)
		{
		case LogLevel::Debug: return "debug";
		case LogLevel::Info: return "info";
		case LogLevel::Warning: return "warning";
		case LogLevel::Error: return "error";
		default: return "?";
		}
	}

	bool ParseLogLevel(const char* name, LogLevel& dst)
	{
		if (EqualsIgnoreCase(name, "debug")) { dst = LogLevel::Debug; return true; }
		if (EqualsIgnoreCase(name, "info")) { dst = LogLevel::Info; return true; }
		if (EqualsIgnoreCase(name, "warning") || EqualsIgnoreCase(name, "warn")) { dst = LogLevel::Warning; return true; }
		if (EqualsIgnoreCase(name, "error")) { dst = LogLevel::Error; return true; }
		return false;
	}

	void LogMessage(LogLevel level, const char* format, ...)
	{
		if (level < g_logLevel) return;

		char message[2048];
		va_list args;
		va_start(args, format);
		vsnprintf(message, sizeof(message), format, args);
		va_end(args);

		SYSTEMTIME time;
		GetLocalTime(&time);

		std::lock_guard<std::mutex> lock(g_logMutex);
		std::printf("[%02d:%02d:%02d] [%-7s] %s\n",
			time.wHour, time.wMinute, time.wSecond, LogLevelName(level), message);
		std::fflush(stdout);

		if (g_logFile != nullptr)
		{
			std::fprintf(g_logFile, "[%02d:%02d:%02d] [%-7s] %s\n",
				time.wHour, time.wMinute, time.wSecond, LogLevelName(level), message);
			std::fflush(g_logFile);
		}
	}
}
