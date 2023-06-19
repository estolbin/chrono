
#include <Windows.h>

SYSTEMTIME g_sysTime;

static std::string GetCurrTime()
{
    GetLocalTime(&g_sysTime);
    std::string t = std::to_string(g_sysTime.wHour) + ":" + std::to_string(g_sysTime.wMinute);
    return t;
}

static std::string GetCurrDay()
{
    GetLocalTime(&g_sysTime);
    char dt[10];
    wsprintf(dt, "%04d-%02d-%02d", g_sysTime.wYear, g_sysTime.wMonth, g_sysTime.wDay);
    return std::string(dt);
}