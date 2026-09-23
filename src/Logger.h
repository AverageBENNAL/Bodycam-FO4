#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <string>

// Tiny dependency-free file logger. Writes Bodycam.log next to this DLL
// (i.e. Data\F4SE\Plugins\Bodycam.log), flushing every line so the log survives a
// crash - important since we're validating unverified addresses.
class Logger
{
public:
	static Logger& Get()
	{
		static Logger instance;
		return instance;
	}

	void Init(HMODULE selfModule)
	{
		char path[MAX_PATH]{};
		GetModuleFileNameA(selfModule, path, MAX_PATH);
		std::string p(path);
		auto slash = p.find_last_of("\\/");
		p = (slash == std::string::npos ? "" : p.substr(0, slash + 1)) + "Bodycam.log";

		std::lock_guard<std::mutex> lock(m_mutex);
		m_file = fopen(p.c_str(), "w");
	}

	void Log(const char* fmt, ...)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_file)
			return;

		SYSTEMTIME st;
		GetLocalTime(&st);
		fprintf(m_file, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

		va_list args;
		va_start(args, fmt);
		vfprintf(m_file, fmt, args);
		va_end(args);

		fprintf(m_file, "\n");

		// Flushing every line costs a disk write on the game thread (slow through MO2's virtual
		// file system). Flush at most once per second; a crash loses at most ~1 s of log.
		ULONGLONG nowMs = GetTickCount64();
		if (nowMs - m_lastFlushMs >= 1000)
		{
			fflush(m_file);
			m_lastFlushMs = nowMs;
		}
	}

private:
	Logger() = default;
	FILE* m_file = nullptr;
	ULONGLONG m_lastFlushMs = 0;
	std::mutex m_mutex;
};

#define CP_LOG(...) Logger::Get().Log(__VA_ARGS__)
