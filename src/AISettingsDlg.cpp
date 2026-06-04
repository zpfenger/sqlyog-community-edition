/* Copyright (C) 2013 Webyog Inc

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA

*/

#include <windows.h>
#include <process.h>
#include "AISettingsDlg.h"
#include "AIService.h"
#include "Global.h"
#include "resource.h"
#include "commonhelper.h"

extern PGLOBALS		pGlobals;

#define UM_AI_TEST_RESULT (WM_USER + 360)

void AISettingsDlg::Show(HWND hparent)
{
    DialogBox(pGlobals->m_hinstance, MAKEINTRESOURCE(IDD_AISETTINGS),
              hparent, DlgProc);
}

INT_PTR CALLBACK AISettingsDlg::DlgProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        OnInitDialog(hwnd);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDOK:
            OnOK(hwnd);
            EndDialog(hwnd, IDOK);
            return TRUE;

        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;

        case IDC_AI_TESTBTN:
            OnTest(hwnd);
            return TRUE;
        }
        break;

    case UM_AI_TEST_RESULT:
    {
        // wparam: 1=success, 0=failure
        // lparam: pointer to error message (wyString*)
        wyString* perr = (wyString*)lparam;
        HWND hstatus = GetDlgItem(hwnd, IDC_AI_STATUS);
        if (wparam) {
            SetWindowText(hstatus, _(L"Connection successful"));
        } else if (perr) {
            wyWChar werr[512] = {0};
            wcsncpy(werr, perr->GetAsWideChar(), 511);
            SetWindowText(hstatus, werr);
            delete perr;
        } else {
            SetWindowText(hstatus, _(L"Connection failed"));
        }
        EnableWindow(GetDlgItem(hwnd, IDC_AI_TESTBTN), TRUE);
        return TRUE;
    }
    }

    return FALSE;
}

void AISettingsDlg::OnInitDialog(HWND hwnd)
{
    // Localize dialog strings - check Chinese language since L10n DB may not have these entries
    bool isChinese = false;
    {
        const char* langcode = GetL10nLangcode();
        isChinese = (langcode && (strcmp(langcode, "zh-cn") == 0 || strcmp(langcode, "zh") == 0));
    }

    if (isChinese) {
        SetWindowText(hwnd, L"AI \x8BBE\x7F6E");
        SetDlgItemText(hwnd, IDC_STATIC_URL_LABEL, L"API \x5730\x5740:");
        SetDlgItemText(hwnd, IDC_STATIC_KEY_LABEL, L"API \x5BC6\x94A5:");
        SetDlgItemText(hwnd, IDC_STATIC_MODEL_LABEL, L"\x6A21\x578B:");
        SetDlgItemText(hwnd, IDC_AI_TESTBTN, L"\x6D4B\x8BD5");
        SetDlgItemText(hwnd, IDOK, L"\x786E\x5B9A");
        SetDlgItemText(hwnd, IDCANCEL, L"\x53D6\x6D88");
    } else {
        SetWindowText(hwnd, _(L"AI Settings"));
        SetDlgItemText(hwnd, IDC_STATIC_URL_LABEL, _(L"API URL:"));
        SetDlgItemText(hwnd, IDC_STATIC_KEY_LABEL, _(L"API Key:"));
        SetDlgItemText(hwnd, IDC_STATIC_MODEL_LABEL, _(L"Model:"));
        SetDlgItemText(hwnd, IDC_AI_TESTBTN, _(L"Test"));
        SetDlgItemText(hwnd, IDOK, _(L"OK"));
        SetDlgItemText(hwnd, IDCANCEL, _(L"Cancel"));
    }

    // Load current config
    AIConfig config;
    AIService::LoadConfig(&config);

    // Set edit controls
    wyWChar wval[2048] = {0};

    wcsncpy(wval, config.url.GetAsWideChar(), 2047);
    SetDlgItemText(hwnd, IDC_AI_URL, wval);

    wcsncpy(wval, config.key.GetAsWideChar(), 2047);
    SetDlgItemText(hwnd, IDC_AI_KEY, wval);

    wcsncpy(wval, config.model.GetAsWideChar(), 2047);
    SetDlgItemText(hwnd, IDC_AI_MODEL, wval);

    // Center on parent
    RECT rc, prc;
    GetWindowRect(hwnd, &rc);
    GetWindowRect(GetParent(hwnd), &prc);
    int x = prc.left + (prc.right - prc.left - (rc.right - rc.left)) / 2;
    int y = prc.top + (prc.bottom - prc.top - (rc.bottom - rc.top)) / 2;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void AISettingsDlg::OnOK(HWND hwnd)
{
    wyWChar wurl[2048] = {0};
    wyWChar wkey[2048] = {0};
    wyWChar wmodel[2048] = {0};

    GetDlgItemText(hwnd, IDC_AI_URL, wurl, 2047);
    GetDlgItemText(hwnd, IDC_AI_KEY, wkey, 2047);
    GetDlgItemText(hwnd, IDC_AI_MODEL, wmodel, 2047);

    AIConfig config;
    config.url.SetAs(wurl);
    config.key.SetAs(wkey);
    config.model.SetAs(wmodel);

    // Trim trailing whitespace/newlines from URL
    wyChar* urlStr = (wyChar*)config.url.GetString();
    int len = config.url.GetLength();
    while (len > 0 && (urlStr[len-1] == ' ' || urlStr[len-1] == '\r' || urlStr[len-1] == '\n'))
        urlStr[--len] = 0;

    AIService::SaveConfig(&config);
}

void AISettingsDlg::OnTest(HWND hwnd)
{
    // Get current values from controls
    wyWChar wurl[2048] = {0};
    wyWChar wkey[2048] = {0};
    wyWChar wmodel[2048] = {0};

    GetDlgItemText(hwnd, IDC_AI_URL, wurl, 2047);
    GetDlgItemText(hwnd, IDC_AI_KEY, wkey, 2047);
    GetDlgItemText(hwnd, IDC_AI_MODEL, wmodel, 2047);

    // Validate
    if (wcslen(wurl) == 0 || wcslen(wkey) == 0 || wcslen(wmodel) == 0) {
        SetDlgItemText(hwnd, IDC_AI_STATUS, _(L"Please fill in all fields"));
        return;
    }

    // Disable button during test
    EnableWindow(GetDlgItem(hwnd, IDC_AI_TESTBTN), FALSE);
    SetDlgItemText(hwnd, IDC_AI_STATUS, _(L"Testing..."));

    // Launch test in background thread
    TestParam* param = new TestParam;
    param->hwnd = hwnd;
    param->url.SetAs(wurl);
    param->key.SetAs(wkey);
    param->model.SetAs(wmodel);

    unsigned threadid;
    _beginthreadex(NULL, 0, TestThreadProc, param, 0, &threadid);
}

// Test callback context
struct TestCtx {
    bool received;
};

// Test callback: stop after first token
static bool TestStreamCallback(const char* token, int len, void* ud)
{
    TestCtx* c = (TestCtx*)ud;
    c->received = true;
    return false;  // stop after first token
}

unsigned __stdcall AISettingsDlg::TestThreadProc(void* p)
{
    TestParam* param = (TestParam*)p;
    HWND hwnd = param->hwnd;

    AIConfig config;
    config.url.SetAs(param->url.GetString());
    config.key.SetAs(param->key.GetString());
    config.model.SetAs(param->model.GetString());

    // Build a simple test request
    wyString requestJson;
    AIService::BuildRequestJson("Hello, respond with 'OK' only.",
                                AIService::GetPromptChat(), NULL,
                                config.model.GetString(), &requestJson);

    wyString errorBuf;
    TestCtx ctx = { false };

    bool ok = AIService::SendRequestStreaming(&config, requestJson.GetString(),
                                              TestStreamCallback, &ctx, NULL, &errorBuf);

    // Post result to UI
    if (ok || ctx.received) {
        PostMessage(hwnd, UM_AI_TEST_RESULT, 1, 0);
    } else {
        wyString* perr = new wyString();
        perr->SetAs(errorBuf.GetString());
        PostMessage(hwnd, UM_AI_TEST_RESULT, 0, (LPARAM)perr);
    }

    delete param;
    return 0;
}
