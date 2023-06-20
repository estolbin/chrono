#include "Event.h"
#include "SQLiteConnectionPool.h"
#include "TimeHelper.h"
#include "resource.h"
#include <commctrl.h>
#include <string>
#include <tchar.h>
#include <vector>
#include <windows.h>


#define WIN32_LEAN_AND_MEAN

static HINSTANCE g_hInst;
const TCHAR g_szTitle[] = _T("20 min chronolog");
const TCHAR g_szWindowClass[] = _T("20minChronolog");

static const char *const CREATE_TABLE =
    "CREATE TABLE IF NOT EXISTS mytable (id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "start_date text, end_date text, day text, description TEXT);";
static const char *const INSERT_DATA =
    "INSERT INTO mytable (day, start_date, end_date, description) VALUES (?, "
    "?, ?, ?);";
static const char *const SELECT_DAY =
    "SELECT id, start_date, end_date, description FROM mytable WHERE day = ?;";
static const char *const SELECT_ALL =
    "SELECT id,start_date, end_date, description FROM mytable;";

class Event;
static std::vector<Event> events = {};
static Event *current_event = nullptr;
static int TIMER_TIMEOUT = 20 * 60 * 1000;
static int TIMER_INTERVAL = 60 * 1000;
static int GLOBAL_TIMER = 480 * 60 * 1000; // 8 hours timer.
static int ELAPSE_TIME = 20;
static bool global_enabled = false;
static HWND hList;
static HWND hStatusBar;
static sqlite3 *db = NULL;
static char *errmsg = NULL;
static WorkTime workTime = {8, 0};

HFONT g_hFont = CreateFont(12, 0, 0, 0, 0, 0, 0, 0, ANSI_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH, _T("Arial"));
LRESULT CALLBACK DialogProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

static std::string GetCurrTime();
static std::string GetCurrDay();

static std::string CurrDay;

void RegisterDialogClass(HWND);
void CreateDialogBox(HWND);

static LoggingRunningState logging_running_state =
    LoggingRunningState::NotRunning;
static SQLiteConnectionPool pool("Cronolog.db");

std::string GetStatus() {
  std::string status;
  switch (logging_running_state) {
  case LoggingRunningState::NotRunning:
    status = "Not running";
    break;
  case LoggingRunningState::Running:
    status = "Running";
    break;
  }
  return status;
}

void display_event() {

  ListView_DeleteAllItems(hList);
  if(events.size() != 0) {
    events.clear();
  }

  if (CurrDay.empty()) {
    CurrDay = GetCurrDay();
  }

  sqlite3 *db = pool.acquire();
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, SELECT_DAY, -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, CurrDay.c_str(), -1, SQLITE_TRANSIENT);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Event e((const char *)sqlite3_column_text(stmt, 3),
            (const char *)sqlite3_column_text(stmt, 1),
            (const char *)sqlite3_column_text(stmt, 2),
            sqlite3_column_int(stmt, 0));
    events.push_back(e);
  }

  sqlite3_finalize(stmt);
  pool.release(db);

  int i = 0;
  if (events.size() == 0) {
    return;
  }

  for (auto &ev : events) {
      LVITEM item = {};
      item.pszText = (TCHAR *)ev.get_id();
      item.iSubItem = 0;
      item.iItem = i;
      ListView_InsertItem(hList, &item);
      ListView_SetItemText(hList, i, 1, (TCHAR *)ev.get_start_date().c_str());
      ListView_SetItemText(hList, i, 2, (TCHAR *)ev.get_end_date().c_str());
      ListView_SetItemText(hList, i, 3, (TCHAR *)ev.get_description().c_str());
      i++;
  }
}

void AddCol(HWND hwnd, int ColWidth, TCHAR *Text, int iSubItem) {
  LV_COLUMN p;
  p.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
  p.fmt = LVCFMT_LEFT;
  p.cx = ColWidth;
  p.pszText = Text;
  p.cchTextMax = 2;
  p.iSubItem = iSubItem;

  ListView_InsertColumn(hwnd, 0, &p);
}

void SetStatusBar() {
  TCHAR buf[256];
  if (logging_running_state == LoggingRunningState::Running)
    wsprintf(buf, _T("%s. Elapse time: %d"), GetStatus().c_str(), ELAPSE_TIME);
  else
    wsprintf(buf, _T("%s."), GetStatus().c_str());
  SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)buf);

if(global_enabled)
    wsprintf(buf, _T("Work time left: %s"), GetWorkTimeStr(workTime).c_str());
else
    wsprintf(buf, _T("Work timer not running"));

  SendMessage(hStatusBar, SB_SETTEXT, 1, (LPARAM)buf);
}

bool CALLBACK SetFont(HWND child, LPARAM font) {
  SendMessage(child, WM_SETFONT, font, true);
  return true;
}

INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, INT nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  HWND hWnd;
  MSG msg;
  WNDCLASSEX wc;

  g_hInst = hInstance;

  INITCOMMONCONTROLSEX iccex;
  iccex.dwSize = sizeof(iccex);
  iccex.dwICC = ICC_WIN95_CLASSES;
  InitCommonControlsEx(&iccex);

  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = hInstance;
  wc.hIcon = LoadIcon(NULL, MAKEINTRESOURCE(IDI_ICON));
  wc.hIconSm = (HICON)LoadImage(
      GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, 16, 16, 0);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
  wc.lpszMenuName = NULL;
  wc.lpszClassName = g_szWindowClass;
  wc.cbSize = sizeof(WNDCLASSEX);

  RegisterClassEx(&wc);

  hWnd = CreateWindow(g_szWindowClass, g_szTitle, WS_OVERLAPPEDWINDOW,
                      CW_USEDEFAULT, CW_USEDEFAULT, 630, 430, NULL, NULL,
                      hInstance, NULL);

  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);

  sqlite3 *db = pool.acquire();
  sqlite3_exec(db, CREATE_TABLE, NULL, NULL, &errmsg);
  pool.release(db);

  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  return msg.wParam;
}

void StartLogging(HWND hWnd) {
  if (current_event == nullptr) {
    current_event = new Event(GetCurrTime().c_str());
  }

  KillTimer(hWnd, ID_TIMER);
  KillTimer(hWnd, IDD_STATUS_TIMER);
  ELAPSE_TIME = 20;

  if(!global_enabled) {
    global_enabled = true;
    SetTimer(hWnd, IDD_WORK_TIMER, GLOBAL_TIMER, NULL);
  }
  SetTimer(hWnd, ID_TIMER, TIMER_TIMEOUT, NULL);
  SetTimer(hWnd, IDD_STATUS_TIMER, 60 * 1000, NULL);
  logging_running_state = LoggingRunningState::Running;
  SetStatusBar();
}

void StopLogging(HWND hWnd) {
  CreateDialogBox(hWnd);
  KillTimer(hWnd, ID_TIMER);
  KillTimer(hWnd, IDD_STATUS_TIMER);
  logging_running_state = LoggingRunningState::NotRunning;
  SetStatusBar();
}

void CopyToClipboard(HWND hWnd) {
  
  std::string str;
  char buf[256] = {0};
  str = "Date: " + CurrDay + "\n";
  wsprintf(buf, "%5s\t%5s\t%50s", "Start", "End", "Description");
  str += buf;
  str += "\n";
  str += "------------------------------------------------------------------\n";
  for(auto &ev : events) {
    wsprintf(buf, "%5s\t%5s\t%50s", ev.get_start_date().c_str(),
             ev.get_end_date().c_str(), ev.get_description().c_str());
    str += buf;
    str += "\n";
  }

  const size_t len = strlen(str.c_str()) + 1;
  HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
  memcpy(GlobalLock(hMem), str.c_str(), len);
  GlobalUnlock(hMem);
  OpenClipboard(0);
  EmptyClipboard();
  SetClipboardData(CF_TEXT, hMem);
  CloseClipboard();

}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
  RECT rc;
  switch (message) {
  case WM_CREATE: {
    GetClientRect(hWnd, &rc);

    RegisterDialogClass(hWnd);
    InitCommonControls();

    HWND hDate = CreateWindowEx(0, DATETIMEPICK_CLASS, _T("DateTime"),
                                WS_BORDER | WS_CHILD | WS_VISIBLE, 5, 5, 80, 25,
                                hWnd, (HMENU)IDD_DATE, g_hInst, NULL);
    HWND hBtnStart = CreateWindow(_T("BUTTON"), _T("Start"),
                                  WS_BORDER | WS_CHILD | WS_VISIBLE | WS_EX_STATICEDGE, 85, 5, 80,
                                  25, hWnd, (HMENU)ID_START, g_hInst, NULL);
    HWND hBtnPause = CreateWindow(_T("BUTTON"), _T("Stop"),
                                  WS_BORDER | WS_CHILD | WS_VISIBLE | WS_EX_STATICEDGE, 170, 5, 80,
                                  25, hWnd, (HMENU)ID_PAUSE, g_hInst, NULL);
    HWND hBtnStop = CreateWindow(_T("BUTTON"), _T("Clipboard"),
                                  WS_BORDER | WS_CHILD | WS_VISIBLE | WS_EX_STATICEDGE, 255, 5, 80,
                                  25, hWnd, (HMENU)ID_COPY, g_hInst, NULL);


    hList = CreateWindow(
        WC_LISTVIEW, _T(""),
        WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT | LVS_EDITLABELS, 5, 35,
        rc.right - 10, rc.bottom - 60, hWnd, (HMENU)ID_LIST, g_hInst, NULL);
    float width = ((rc.right - rc.left) - 10) / 5;
    AddCol(hList, width * 3, _T("Description"), 3);
    AddCol(hList, width, _T("End"), 2);
    AddCol(hList, width, _T("Start"), 1);
    AddCol(hList, 0, _T("ID"), 0);

    display_event();

    hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, _T(GetStatus().c_str()),
                                SBARS_SIZEGRIP | WS_CHILD | WS_VISIBLE, 0, 0, 0,
                                0, hWnd, (HMENU)950, g_hInst, NULL);
    int widths[2] = {200, -1};
    SendMessage(hStatusBar,SB_SETPARTS,2,(LPARAM)widths);
    EnumChildWindows(hWnd, (WNDENUMPROC)SetFont,
                     (LPARAM)GetStockObject(DEFAULT_GUI_FONT));
  } break;
  case WM_COMMAND: {
    switch (LOWORD(wParam)) {
    case ID_COPY: {
      CopyToClipboard(hWnd);
    } break;
    case ID_START: {
      StartLogging(hWnd);
    } break;

    case ID_PAUSE: {
      StopLogging(hWnd);
    } break;
    }
  } break;
  case WM_TIMER: {
    switch (wParam) {
    case ID_TIMER: {
      CreateDialogBox(hWnd);
      StartLogging(hWnd);
    } break;
    case IDD_STATUS_TIMER: {
      ELAPSE_TIME--;
      DecreaseWorkTime(workTime, global_enabled);
      SetStatusBar();
    } break;
    }

  } break;
  case WM_SIZE: {
    GetClientRect(hWnd, &rc);
    SetWindowPos(hList, NULL, 5, 35, rc.right - 10, rc.bottom - 60,
                 SWP_NOZORDER);
    float width = ((rc.right - rc.left) - 10) / 5;
    ListView_SetColumnWidth(hList, 0, 0);
    ListView_SetColumnWidth(hList, 1, width);
    ListView_SetColumnWidth(hList, 2, width);
    ListView_SetColumnWidth(hList, 3, width * 3);
    SendMessage(hStatusBar, WM_SIZE, 0, 0);
  } break;
  case WM_GETMINMAXINFO: {
    LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
    lpMMI->ptMinTrackSize.x = 630;
    lpMMI->ptMinTrackSize.y = 430;
  } break;
  case WM_NOTIFY: {
    LPNMHDR hdr = (LPNMHDR)lParam;
    switch (hdr->code) {
    case DTN_DATETIMECHANGE: {
      LPNMDATETIMECHANGE lpChange = (LPNMDATETIMECHANGE)lParam;

      char dt[10] = {};
      wsprintf(dt, "%04d-%02d-%02d", lpChange->st.wYear, lpChange->st.wMonth,
               lpChange->st.wDay);
      CurrDay = std::string(dt);
      display_event();
    } break;
    }
  } break;
  case WM_DESTROY:
    KillTimer(hWnd, ID_TIMER);
    KillTimer(hWnd, IDD_STATUS_TIMER);
    KillTimer(hWnd, IDD_WORK_TIMER);
    PostQuitMessage(0);
    break;

  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {

  case WM_CREATE: {

    CreateWindowW(L"Static", L"What?", WS_VISIBLE | WS_CHILD, 15, 10, 50, 30,
                  hwnd, (HMENU)-1, NULL, NULL);
    HWND edit = CreateWindowW(L"edit", L"", WS_VISIBLE | WS_CHILD | WS_BORDER,
                              55, 10, 250, 30, hwnd, (HMENU)600, NULL, NULL);
    CreateWindowW(L"button", L"Ok", WS_VISIBLE | WS_CHILD, 200, 50, 80, 25,
                  hwnd, (HMENU)1, NULL, NULL);

    EnumChildWindows(hwnd, (WNDENUMPROC)SetFont,
                     (LPARAM)GetStockObject(DEFAULT_GUI_FONT));

    current_event->set_end_date(GetCurrTime().c_str());

    if (SetForegroundWindow(hwnd))
      SetFocus(edit);
  } break;

  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case 1: {

      TCHAR s[255];
      GetDlgItemText(hwnd, 600, s, 255);

      current_event->set_description(s);
      // current_event->set_end_date(GetCurrTime().c_str());

      events.push_back(*current_event);
      // (day, start_date, end_date, description, id)

      sqlite3 *db = pool.acquire();
      sqlite3_stmt *stmt;
      sqlite3_prepare_v2(db, INSERT_DATA, -1, &stmt, nullptr);
      sqlite3_bind_text(stmt, 1, GetCurrDay().c_str(), -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 2, current_event->get_start_date().c_str(), -1,
                        SQLITE_STATIC);
      sqlite3_bind_text(stmt, 3, current_event->get_end_date().c_str(), -1,
                        SQLITE_STATIC);
      sqlite3_bind_text(stmt, 4, current_event->get_description().c_str(), -1,
                        SQLITE_STATIC);
      int rc = sqlite3_step(stmt);
      if (rc != SQLITE_DONE) {
        std::string m = "Error: " + std::string(sqlite3_errmsg(db));
        MessageBox(NULL, m.c_str(), _T("Error"), MB_OK);
      }
      rc = sqlite3_finalize(stmt);

      pool.release(db);
      display_event();

      if (logging_running_state == LoggingRunningState::Running) {
        current_event = new Event(GetCurrTime().c_str());
      } else {
        current_event = nullptr;
      }

      DestroyWindow(hwnd);
    } break;
    }
    break;
  case WM_CLOSE:
    DestroyWindow(hwnd);
    break;
  }

  return (DefWindowProcW(hwnd, msg, wParam, lParam));
}

void RegisterDialogClass(HWND hWnd) {
  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = (WNDPROC)DialogProc;
  wc.hInstance = g_hInst;
  wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
  wc.lpszClassName = L"DialogClass";
  RegisterClassExW(&wc);
}

void CreateDialogBox(HWND hwnd) {

  CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"DialogClass",
                  L"Dialog Box", WS_VISIBLE | WS_SYSMENU | WS_CAPTION, 100, 150,
                  320, 120, NULL, NULL, g_hInst, NULL);
}
