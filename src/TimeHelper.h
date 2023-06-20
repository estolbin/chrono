#include <Windows.h>

SYSTEMTIME g_sysTime;

struct WorkTime {
  int hour;
  int minute;  
};

static std::string GetCurrTime() {
  GetLocalTime(&g_sysTime);
  std::string t = (g_sysTime.wHour < 10 ? "0" : "") +
                  std::to_string(g_sysTime.wHour) + ":" +
                  (g_sysTime.wMinute < 10 ? "0" : "") +
                  std::to_string(g_sysTime.wMinute);
  return t;
}

static std::string GetCurrDay() {
  GetLocalTime(&g_sysTime);
  char dt[10] = {0};
  wsprintf(dt, "%04d-%02d-%02d", g_sysTime.wYear, g_sysTime.wMonth,
           g_sysTime.wDay);
  return std::string(dt);
}

static void DecreaseWorkTime(WorkTime &workTime, bool &isWorkTime) {
  if(workTime.minute == 0) {
    if(workTime.hour == 0) {
      MessageBox(0, "Рабочий день закончился", "Рабочее время", MB_OK | MB_ICONINFORMATION);
      isWorkTime = false;
      return;
    }
    workTime.hour--;
    workTime.minute = 59; }
  else
    workTime.minute--;
}

static std::string GetWorkTimeStr(WorkTime workTime) {
  std::string t = (workTime.hour < 10 ? "0" : "") +
                  std::to_string(workTime.hour) + ":" +
                  (workTime.minute < 10 ? "0" : "") +
                  std::to_string(workTime.minute);
  return t;
}