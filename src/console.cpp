#include "console.h"
#ifdef WIN32
#include <direct.h>
#define getcwd _getcwd // avoid warning C4996
#else
#include <stdarg.h>
#include <unistd.h>
#endif
#ifdef USE_GUI
#include "gui/igui.h"
#endif

#define MAX_DATE_LEN 9

using namespace std;

CConsole::CConsole()
{
	char cwd[MAX_PATH];
	if (!getcwd(cwd, sizeof(cwd)))
	{
		printf("getcwd failed: %s", strerror(errno));
	}

	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/Logs", cwd);

#ifdef WIN32
	_mkdir(path);
#else 
	mkdir(path, 0755);
#endif

	time_t currTime = time(NULL);
	struct tm* pTime = localtime(&currTime);

	snprintf(m_szServerLogPath, sizeof(m_szServerLogPath), "%s/Server_%d-%.2d-%.2d %.2d-%.2d-%.2d.log", path,
		pTime->tm_year + 1900,	/* Year less 2000 */
		pTime->tm_mon + 1,		/* month (0 - 11 : 0 = January) */
		pTime->tm_mday,			/* day of month (1 - 31) */
		pTime->tm_hour,			/* hour (0 - 23) */
		pTime->tm_min,		    /* minutes (0 - 59) */
		pTime->tm_sec		    /* seconds (0 - 59) */
	);

	FILE* file = fopen(m_szServerLogPath, "w");
	if (file)
	{
		fclose(file);
	}

#ifdef WIN32
	m_Output = GetStdHandle(STD_OUTPUT_HANDLE);

	char windowName[512];
#ifdef _DEBUG
	snprintf(windowName, sizeof(windowName), OBFUSCATE("CSN:Z Server %s %s DEBUG BUILD"), __DATE__, __TIME__);
#else
	snprintf(windowName, sizeof(windowName), OBFUSCATE("CSN:Z Server %s %s"), __DATE__, __TIME__);
#endif
	SetWindowTextA(GetConsoleWindow(), windowName);
#endif

	m_nLogLevel = LOG_INFO | LOG_WARN | LOG_ERROR;
	m_szLastPacket = "none";

	printf("\n");
}

CConsole::~CConsole()
{
	SetTextColor(CON_CYAN);
	printf("Console destroyed\n");
	SetTextColor(CON_WHITE);
}

const char* CConsole::GetCurrTime()
{
	static char buf[21]; // 32 should be enough
	char szTime[MAX_DATE_LEN];
	char szDate[MAX_DATE_LEN];

#ifdef WIN32
	_strtime_s(szTime);
	_strdate_s(szDate);
#else
	time_t myTime = time(NULL);
	strftime(szTime, MAX_DATE_LEN, "%T", localtime(&myTime));
	strftime(szDate, MAX_DATE_LEN, "%D", localtime(&myTime));
#endif

	sprintf(buf, "[%s %s]", szDate, szTime);

	return buf;
}

void CConsole::SetTextColor(TextColor color)
{
#ifdef WIN32
	SetConsoleTextAttribute(m_Output, color);
#else
	switch (color)
	{
	case CON_YELLOW:
		printf("\033[0;33m");
		break;
	case CON_RED:
		printf("\033[0;31m");
		break;
	case CON_WHITE:
		printf("\033[0;37m");
		break;
	case CON_GREEN:
		printf("\033[0;32m");
		break;
	case CON_CYAN:
		printf("\033[0;36m");
		break;
	}
#endif
}

void CConsole::Write(OutMode mode, const char* msg)
{
	TextColor color;
	switch (mode)
	{
	case CON_WARNING:
		color = CON_YELLOW;
		break;
	case CON_ERROR:
	case CON_FATAL_ERROR:
		color = CON_RED;
		break;
	case CON_LOG:
		color = CON_WHITE;
		break;
	default:
		color = CON_WHITE;
		break;
	}

	SetTextColor(color);
	WriteToConsole(mode, msg);
	SetTextColor(CON_WHITE); //восстанавливаем цвет
}

void CConsole::WriteToConsole(OutMode mode, const char* msg)
{
	static char buffer[8192];
	snprintf(buffer, sizeof(buffer), "%s %s", GetCurrTime(), msg);

	// write to console
	printf("%s", buffer);

	FILE* file = fopen(m_szServerLogPath, "a+");
	if (file)
	{
		fprintf(file, "%s", buffer);
		fclose(file);
	}

#ifdef USE_GUI
	GUI()->LogMessage(mode, buffer);
#endif

	if (mode == CON_FATAL_ERROR)
	{
#ifdef USE_GUI
		GUI()->ShowMessageBox("Critical error", msg, true);
#elif WIN32
		MessageBox(NULL, msg, "Critical error", MB_ICONERROR);
#endif
	}

}

void CConsole::Error(const char* msg, ...)
{
	if ((m_nLogLevel & LOG_ERROR) == 0)
	{
		return;
	}

	va_list argptr;
	static char buffer[8192];

	va_start(argptr, msg);
	vsnprintf(buffer, sizeof(buffer), msg, argptr);
	va_end(argptr);

	Write(CON_ERROR, buffer);
}

void CConsole::FatalError(const char* msg, ...)
{
	if ((m_nLogLevel & LOG_ERROR) == 0)
	{
		return;
	}

	va_list argptr;
	static char buffer[8192];

	va_start(argptr, msg);
	vsnprintf(buffer, sizeof(buffer), msg, argptr);
	va_end(argptr);

	Write(CON_FATAL_ERROR, buffer);
}

void CConsole::Warn(const char* msg, ...)
{
	if ((m_nLogLevel & LOG_WARN) == 0)
	{
		return;
	}

	va_list argptr;
	static char buffer[8192];

	va_start(argptr, msg);
	vsnprintf(buffer, sizeof(buffer), msg, argptr);
	va_end(argptr);

	Write(CON_WARNING, buffer);
}

void CConsole::Log(const char* msg, ...)
{
	if ((m_nLogLevel & LOG_INFO) == 0)
	{
		return;
	}

	va_list argptr;
	static char buffer[8192];

	va_start(argptr, msg);
	vsnprintf(buffer, sizeof(buffer), msg, argptr);
	va_end(argptr);

	Write(CON_LOG, buffer);
}

void CConsole::Debug(const char* msg, ...)
{
	if ((m_nLogLevel & LOG_DEBUG_PACKET) == 0)
	{
		return;
	}

	va_list argptr;
	static char buffer[8192];

	va_start(argptr, msg);
	vsnprintf(buffer, sizeof(buffer), msg, argptr);
	va_end(argptr);

	Write(CON_LOG, buffer);
}

void CConsole::SetStatus(const char* status)
{
#ifdef WIN32
	strncpy(m_szStatusLine, status, sizeof(m_szStatusLine) - 1);
	m_szStatusLine[sizeof(m_szStatusLine) - 2] = '\0';
	UpdateStatus();
#endif
}

void CConsole::SetLogLevel(int logLevel)
{
	m_nLogLevel = logLevel;
}

void CConsole::SetLastPacket(const char* name)
{
	m_szLastPacket = name;
}

const char* CConsole::GetLastPacket()
{
	return m_szLastPacket;
}

void CConsole::UpdateStatus()
{
#ifdef WIN32
	COORD coord;
	DWORD dwWritten = 0;
	WORD wAttrib[80];

	for (int i = 0; i < 80; i++)
	{
		wAttrib[i] = FOREGROUND_GREEN; // FOREGROUND_GREEN | FOREGROUND_INTENSITY | BACKGROUND_INTENSITY;
	}

	coord.X = coord.Y = 0;

	WriteConsoleOutputAttribute(m_Output, wAttrib, 80, coord, &dwWritten);
	WriteConsoleOutputCharacter(m_Output, m_szStatusLine, 80, coord, &dwWritten);
#endif
}