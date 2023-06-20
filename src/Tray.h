#pragma once

#include <Windows.h>
#include <shellapi.h>
#include "resource.h"

extern HINSTANCE g_hInst;

void TrayDrawIcon(HWND hWnd) {
	NOTIFYICONDATA nid;
	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = hWnd;
	nid.uID = TRAY_ICON;
	nid.uVersion = NOTIFYICON_VERSION;
    nid.uCallbackMessage = WM_TRAYMESSAGE;
	nid.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_ICON));
    strcpy_s( nid.szTip, sizeof( nid.szTip ), IDS_APP_TITLE);
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	Shell_NotifyIcon(NIM_ADD, &nid);    
}

void TrayDeleteIcon(HWND hWnd) {
	NOTIFYICONDATA nid;
	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = hWnd;
	nid.uID = TRAY_ICON;
	Shell_NotifyIcon(NIM_DELETE, &nid);
}