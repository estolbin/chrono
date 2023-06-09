#include <tchar.h>
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "resource.h"
#include "sqlite3.h"

#include <queue>
#include <mutex>
#include <condition_variable>

#define WIN32_LEAN_AND_MEAN

static HINSTANCE g_hInst;
const TCHAR g_szTitle[] = _T("My Window");
const TCHAR g_szWindowClass[] = _T("MyWindowClass");

static const char *const CREATE_TABLE = "CREATE TABLE IF NOT EXISTS mytable (id INTEGER PRIMARY KEY AUTOINCREMENT, start_date text, end_date text, day text, description TEXT);";
static const char *const INSERT_DATA = "INSERT INTO mytable (day, start_date, end_date, description) VALUES (?, ?, ?, ?);";
static const char *const SELECT_DAY = "SELECT id, start_date, end_date, description FROM mytable WHERE day = ?;";
static const char *const SELECT_ALL = "SELECT id,start_date, end_date, description FROM mytable;";

class Event;
static std::vector<Event> events = {};
static Event *current_event = nullptr;

static int TIMER_TIMEOUT = 20 * 60 * 1000;

static HWND hList;
static HWND hStatusBar;

static sqlite3 *db = NULL;
static char *errmsg = NULL;

SYSTEMTIME g_sysTime;

HFONT g_hFont = CreateFont(12, 0, 0, 0, 0, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, _T("Arial"));

LRESULT CALLBACK DialogProc(HWND, UINT, WPARAM, LPARAM);

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

static std::string GetCurrTime();
static std::string GetCurrDay();

static std::string CurrDay;

void RegisterDialogClass(HWND);
void CreateDialogBox(HWND);

enum class LoggingRunningState
{
    NotRunning,
    Running
};

static LoggingRunningState logging_running_state = LoggingRunningState::NotRunning;

std::string GetStatus()
{
    std::string status;
    switch (logging_running_state)
    {
    case LoggingRunningState::NotRunning:
        status = "Not running";
        break;
    case LoggingRunningState::Running:
        status = "Running";
        break;
    }
    return status;
}

class Event
{
private:
    std::string description;
    std::string start_date;
    std::string end_date;
    int id;

public:
    Event(const char *description, const char *start_date, const char *end_date, int id) : description(description), start_date(start_date), end_date(end_date), id(id) {}
    Event(const char *description, const char *start_date) : description(description), start_date(start_date) {}
    Event(const char *start_date) : start_date(start_date) {}
    std::string get_description() { return description; }
    std::string get_start_date() { return start_date; }
    std::string get_end_date() { return end_date; }
    int get_id() { return id; }

    void set_description(const TCHAR *description) { this->description = reinterpret_cast<const char *>(description); }
    void set_end_date(const char *end_date) { this->end_date = end_date; }
    void set_id(int id) { this->id = id; }
};

class SQLiteConnectionPool
{
public:
    SQLiteConnectionPool(const std::string &filename, int pool_size = 5)
        : filename_(filename)
    {
        for (int i = 0; i < pool_size; i++)
        {
            sqlite3 *db;
            sqlite3_open(filename.c_str(), &db);
            pool_.push(db);
        }
    }

    sqlite3 *acquire()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (pool_.empty())
        {
            cv_.wait(lock);
        }

        sqlite3 *db = pool_.front();
        pool_.pop();
        return db;
    }

    void release(sqlite3 *db)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pool_.push(db);
        cv_.notify_one();
    }

private:
    std::string filename_;
    std::queue<sqlite3 *> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

static SQLiteConnectionPool pool("test.db");

void display_event()
{
    ListView_DeleteAllItems(hList);

    events.clear();

    if(CurrDay.empty())
    {
        CurrDay = GetCurrDay();
    }

    sqlite3 *db = pool.acquire();
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, SELECT_DAY, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, CurrDay.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        Event e((const char *)sqlite3_column_text(stmt, 3), (const char *)sqlite3_column_text(stmt, 1), (const char *)sqlite3_column_text(stmt, 2), sqlite3_column_int(stmt, 0));
        events.push_back(e);
    }
    sqlite3_finalize(stmt);
    pool.release(db);

    int i = 0;
    for (auto &ev : events)
    {
        LVITEM item;
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

void AddCol(HWND hwnd, int ColWidth, TCHAR *Text, int iSubItem)
{
    LV_COLUMN p;
    p.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    p.fmt = LVCFMT_LEFT;
    p.cx = ColWidth;
    p.pszText = Text;
    p.cchTextMax = 2;
    p.iSubItem = iSubItem;

    ListView_InsertColumn(hwnd, 0, &p);
}

bool CALLBACK SetFont(HWND child, LPARAM font)
{
    SendMessage(child, WM_SETFONT, font, true);
    return true;
}

INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, INT nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    HWND hWnd;
    MSG msg;
    WNDCLASS wc;

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
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = g_szWindowClass;

    RegisterClass(&wc);

    hWnd = CreateWindow(g_szWindowClass, g_szTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 630, 430, NULL, NULL, hInstance, NULL);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);


    sqlite3 *db = pool.acquire();
    sqlite3_exec(db, CREATE_TABLE, NULL, NULL, &errmsg);
    pool.release(db);

    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    RECT rc;
    switch (message)
    {
    case WM_CREATE:
    {
        GetClientRect(hWnd, &rc);


        RegisterDialogClass(hWnd);
        InitCommonControls();

        HWND hDate = CreateWindowEx(0, DATETIMEPICK_CLASS, _T("DateTime"), WS_BORDER | WS_CHILD | WS_VISIBLE, 5, 5, 80, 25, hWnd, (HMENU)ID_DATE, g_hInst, NULL);
        HWND hBtnStart = CreateWindow(_T("BUTTON"), _T("Start"), WS_BORDER | WS_CHILD | WS_VISIBLE, 85, 5, 80, 25, hWnd, (HMENU)ID_START, g_hInst, NULL);
        HWND hBtnPause = CreateWindow(_T("BUTTON"), _T("Stop"), WS_BORDER | WS_CHILD | WS_VISIBLE, 165, 5, 80, 25, hWnd, (HMENU)ID_PAUSE, g_hInst, NULL);

        hList = CreateWindow(WC_LISTVIEW, _T(""), WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT | LVS_EDITLABELS, 5, 35, rc.right-10, rc.bottom-60, hWnd, (HMENU)ID_LIST, g_hInst, NULL);
        float width = ((rc.right - rc.left) - 10) / 5;
        AddCol(hList, width * 3, _T("Description"), 3);
        AddCol(hList, width, _T("End"), 2);
        AddCol(hList, width, _T("Start"), 1);
        AddCol(hList, 0, _T("ID"), 0);

        display_event();

        hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, _T(GetStatus().c_str()), SBARS_SIZEGRIP | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)950, g_hInst, NULL);
        EnumChildWindows(hWnd, (WNDENUMPROC)SetFont, (LPARAM)GetStockObject(DEFAULT_GUI_FONT));
    }
    break;
    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case ID_START:
            {
                current_event = new Event(GetCurrTime().c_str());
                try
                {
                    KillTimer(hWnd, ID_TIMER);
                }
                catch (...)
                {
                    // do nothing
                }
                SetTimer(hWnd, ID_TIMER, TIMER_TIMEOUT, NULL);
                logging_running_state = LoggingRunningState::Running;
                SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)_T(GetStatus().c_str()));

            }
            break;

        case ID_PAUSE:
            CreateDialogBox(hWnd);
            KillTimer(hWnd, ID_TIMER);
            logging_running_state = LoggingRunningState::NotRunning;
            SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)_T(GetStatus().c_str()));

            break;
        }
    }
    break;
    case WM_TIMER:
    {
        CreateDialogBox(hWnd);
        KillTimer(hWnd, ID_TIMER);
        SetTimer(hWnd, ID_TIMER, TIMER_TIMEOUT, NULL);
        logging_running_state = LoggingRunningState::Running;
        SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)_T(GetStatus().c_str()));

    }
    break;
    case WM_SIZE:
    {
        GetClientRect(hWnd, &rc);
        SetWindowPos(hList, NULL, 5, 35, rc.right-10, rc.bottom-60, SWP_NOZORDER);
        float width = ((rc.right - rc.left) - 10) / 5;
        ListView_SetColumnWidth(hList, 0, 0);
        ListView_SetColumnWidth(hList, 1, width);
        ListView_SetColumnWidth(hList, 2, width);
        ListView_SetColumnWidth(hList, 3, width * 3);
        SendMessage(hStatusBar, WM_SIZE, 0, 0);
    }    
    break;
    case WM_GETMINMAXINFO:
    {
        LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
        lpMMI->ptMinTrackSize.x = 630;
        lpMMI->ptMinTrackSize.y = 430;
    }
    case WM_NOTIFY:
    {
        LPNMHDR hdr = (LPNMHDR)lParam;
        switch (hdr->code)
        {
            case DTN_DATETIMECHANGE:
            {
                LPNMDATETIMECHANGE lpChange = (LPNMDATETIMECHANGE)lParam;

                char dt[10];
                wsprintf(dt, "%04d-%02d-%02d", lpChange->st.wYear, lpChange->st.wMonth, lpChange->st.wDay);
                CurrDay = std::string(dt);
                events.clear();
                display_event();
            }
        }
    }
    break;
    case WM_DESTROY:
        KillTimer(hWnd, ID_TIMER);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {

    case WM_CREATE:
    {

        CreateWindowW(L"Static", L"What?", WS_VISIBLE | WS_CHILD, 15, 10, 50, 30, hwnd, (HMENU)-1, NULL, NULL);
        CreateWindowW(L"edit", L"", WS_VISIBLE | WS_CHILD | WS_BORDER, 55, 10, 250, 30, hwnd, (HMENU)600, NULL, NULL);
        CreateWindowW(L"button", L"Ok", WS_VISIBLE | WS_CHILD, 200, 50, 80, 25, hwnd, (HMENU)1, NULL, NULL);

        EnumChildWindows(hwnd, (WNDENUMPROC)SetFont, (LPARAM)GetStockObject(DEFAULT_GUI_FONT));
    }
    break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case 1:
        {

            TCHAR s[255];
            GetDlgItemText(hwnd, 600, s, 255);

            current_event->set_description(s);
            current_event->set_end_date(GetCurrTime().c_str());

            events.push_back(*current_event);
            // (day, start_date, end_date, description, id)

            sqlite3 *db = pool.acquire();
            sqlite3_stmt *stmt;
            sqlite3_prepare_v2(db, INSERT_DATA, -1, &stmt, nullptr);
            sqlite3_bind_text(stmt, 1, GetCurrDay().c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, current_event->get_start_date().c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, current_event->get_end_date().c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, current_event->get_description().c_str(), -1, SQLITE_STATIC);
            //sqlite3_bind_int(stmt,  5, current_event->get_id());
            int rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE)
            {
                std::string m = "Error: " + std::string(sqlite3_errmsg(db));
                MessageBox(NULL, m.c_str(), _T("Error"), MB_OK);
            }
            rc = sqlite3_finalize(stmt);

            pool.release(db);

            display_event();

            current_event = nullptr;

            DestroyWindow(hwnd);
        }
        break;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;
    }

    return (DefWindowProcW(hwnd, msg, wParam, lParam));
}

void RegisterDialogClass(HWND hWnd)
{
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = (WNDPROC)DialogProc;
    wc.hInstance = g_hInst;
    wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
    wc.lpszClassName = L"DialogClass";
    RegisterClassExW(&wc);
}

void CreateDialogBox(HWND hwnd)
{

    CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"DialogClass", L"Dialog Box",
                    WS_VISIBLE | WS_SYSMENU | WS_CAPTION, 100, 150, 320, 120,
                    NULL, NULL, g_hInst, NULL);
}

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