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
#include <stdio.h>
#include "TabAI.h"
#include "AIService.h"
#include "Global.h"
#include "resource.h"
#include "commonhelper.h"
#include "MDIWindow.h"
#include "TabMgmt.h"
#include "TabModule.h"
#include "TabEditor.h"
#include "EditorBase.h"
#include "CustTab.h"
#include "jsoncpp.h"

// Scintilla message constants (if not already defined)
#ifndef SCI_SETREADONLY
#define SCI_SETREADONLY 2171
#endif
#ifndef SCI_APPENDTEXT
#define SCI_APPENDTEXT 2282
#endif
#ifndef SCI_SETTEXT
#define SCI_SETTEXT 2181
#endif
#ifndef SCI_GETTEXTLENGTH
#define SCI_GETTEXTLENGTH 2183
#endif
#ifndef SCI_GOTOPOS
#define SCI_GOTOPOS 2025
#endif
#ifndef SCI_SETCURRENTPOS
#define SCI_SETCURRENTPOS 2141
#endif
#ifndef SCI_SETSCROLLWIDTH
#define SCI_SETSCROLLWIDTH 2274
#endif
#ifndef SCI_SETMARGINWIDTHN
#define SCI_SETMARGINWIDTHN 2242
#endif
#ifndef SCI_STYLESETFORE
#define SCI_STYLESETFORE 2051
#endif
#ifndef SCI_STYLESETBOLD
#define SCI_STYLESETBOLD 2233
#endif
#ifndef SCI_STARTSTYLING
#define SCI_STARTSTYLING 2032
#endif
#ifndef SCI_SETSTYLING
#define SCI_SETSTYLING 2033
#endif
#ifndef STYLE_DEFAULT
#define STYLE_DEFAULT 32
#endif
#ifndef STYLE_LASTPREDEFINED
#define STYLE_LASTPREDEFINED 39
#endif
#ifndef SCI_STYLESETFONT
#define SCI_STYLESETFONT 2056
#endif
#ifndef SCI_SETCODEPAGE
#define SCI_SETCODEPAGE 2037
#endif
#ifndef SC_CP_UTF8
#define SC_CP_UTF8 65001
#endif
#ifndef SCI_STYLESETBACK
#define SCI_STYLESETBACK 2052
#endif
#ifndef SCI_STYLESETUNDERLINE
#define SCI_STYLESETUNDERLINE 2406
#endif
#ifndef SCI_GETTEXTRANGE
#define SCI_GETTEXTRANGE 2165
#endif

// Scintilla style indices for AI chat
#define STYLE_USER_MSG    1
#define STYLE_AI_MSG      2
#define STYLE_ERROR_MSG   3
#define STYLE_LOADING_MSG 9   // Loading state (gray)

// Markdown styles
#define STYLE_CODE_BLOCK  4   // Code block (light blue bg)
#define STYLE_CODE_INLINE 5   // Inline code (light gray bg)
#define STYLE_HEADING     6   // Heading (dark blue, bold)
#define STYLE_BOLD        7   // Bold text
#define STYLE_LINK        8   // Links (blue, underline)

TabAI::TabAI(MDIWindow* wnd, HWND hwndparent)
    : TabQueryTypes(wnd, hwndparent)
{
    m_hwnddisplay = NULL;
    m_hwndinput = NULL;
    m_hwndsend = NULL;
    m_hwndclear = NULL;
    m_hwndstatus = NULL;
    m_origdisplayproc = NULL;
    m_originputproc = NULL;
    InterlockedExchange(&m_istreaming, 0);
    InterlockedExchange(&m_stopstream, 0);
    m_hthread = NULL;
    m_isscintilla = FALSE;
    m_aiMsgStartPos = 0;
}

TabAI::~TabAI()
{
    // Cancel any active stream
    CancelStream();

    // Wait for thread to finish
    if (m_hthread) {
        WaitForSingleObject(m_hthread, 5000);
        CloseHandle(m_hthread);
        m_hthread = NULL;
    }

    // Cleanup history
    for (size_t i = 0; i < m_history.size(); i++) {
        ChatMessage* msg = m_history[i];
        if (msg) delete msg;
    }
    m_history.clear();

    // Cleanup pending PostMessage tokens (audit fix A4)
    MSG msg;
    while (PeekMessage(&msg, m_hwnddisplay, UM_AI_STREAM_TOKEN, UM_AI_STREAM_TOKEN, PM_REMOVE)) {
        wyString* ptoken = (wyString*)msg.lParam;
        if (ptoken) delete ptoken;
    }
}

wyBool TabAI::Create()
{
    HWND hwndparent = m_hwndparent;
    HINSTANCE hinst = pGlobals->m_hinstance;

    // Create Scintilla display control (read-only)
    // Check if Scintilla window class is registered (use NULL hInstance to search all classes)
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    m_isscintilla = GetClassInfoEx(NULL, L"Scintilla", &wc);

    if (!m_isscintilla) {
        // Try loading Scintilla DLL - it registers the class on load
        HMODULE hscidll = LoadLibrary(L"SciLexer.dll");
        if (hscidll) {
            m_isscintilla = GetClassInfoEx(NULL, L"Scintilla", &wc);
        }
    }

    if (m_isscintilla) {
        m_hwnddisplay = CreateWindowEx(0, L"Scintilla", L"",
            WS_CHILD | WS_VSCROLL | WS_TABSTOP,
            0, 0, 100, 100, hwndparent, (HMENU)IDC_AI_DISPLAY, hinst, NULL);
    } else {
        // Fallback: use multiline EDIT control
        m_hwnddisplay = CreateWindowEx(0, L"EDIT", L"",
            WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 100, 100, hwndparent, (HMENU)IDC_AI_DISPLAY, hinst, NULL);
    }

    if (!m_hwnddisplay)
        return wyFalse;

    if (m_isscintilla) {
        // Set Scintilla to read-only
        SendMessage(m_hwnddisplay, SCI_SETREADONLY, TRUE, 0);

        // Set UTF-8 code page for proper Chinese character display
        SendMessage(m_hwnddisplay, SCI_SETCODEPAGE, SC_CP_UTF8, 0);

        // Set margin width to 0 (no line numbers)
        SendMessage(m_hwnddisplay, SCI_SETMARGINWIDTHN, 0, 0);
        SendMessage(m_hwnddisplay, SCI_SETMARGINWIDTHN, 1, 0);

        // Setup styles for chat display
        // Set CJK-compatible font for all styles (supports Chinese characters)
        SendMessage(m_hwnddisplay, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)"Microsoft YaHei");
        SendMessage(m_hwnddisplay, SCI_STYLESETFONT, STYLE_USER_MSG, (LPARAM)"Microsoft YaHei");
        SendMessage(m_hwnddisplay, SCI_STYLESETFONT, STYLE_AI_MSG, (LPARAM)"Microsoft YaHei");
        SendMessage(m_hwnddisplay, SCI_STYLESETFONT, STYLE_ERROR_MSG, (LPARAM)"Microsoft YaHei");
        // User message: blue, bold
        SendMessage(m_hwnddisplay, SCI_STYLESETFORE, STYLE_USER_MSG, RGB(0, 0, 180));
        SendMessage(m_hwnddisplay, SCI_STYLESETBOLD, STYLE_USER_MSG, TRUE);
        // AI message: dark green
        SendMessage(m_hwnddisplay, SCI_STYLESETFORE, STYLE_AI_MSG, RGB(0, 128, 0));
        // Error: red
        SendMessage(m_hwnddisplay, SCI_STYLESETFORE, STYLE_ERROR_MSG, RGB(200, 0, 0));
        // Loading: gray
        SendMessage(m_hwnddisplay, SCI_STYLESETFONT, STYLE_LOADING_MSG, (LPARAM)"Microsoft YaHei");
        SendMessage(m_hwnddisplay, SCI_STYLESETFORE, STYLE_LOADING_MSG, RGB(128, 128, 128));

        // Markdown styles - light theme (white/blue)
        // Code block: light blue background
        SendMessage(m_hwnddisplay, SCI_STYLESETFONT, STYLE_CODE_BLOCK, (LPARAM)"Microsoft YaHei");
        SendMessage(m_hwnddisplay, SCI_STYLESETFORE, STYLE_CODE_BLOCK, RGB(50, 50, 50));
        SendMessage(m_hwnddisplay, SCI_STYLESETBACK, STYLE_CODE_BLOCK, RGB(230, 240, 255));
        // Inline code: light background, dark text
        SendMessage(m_hwnddisplay, SCI_STYLESETFONT, STYLE_CODE_INLINE, (LPARAM)"Microsoft YaHei");
        SendMessage(m_hwnddisplay, SCI_STYLESETFORE, STYLE_CODE_INLINE, RGB(50, 50, 50));
        SendMessage(m_hwnddisplay, SCI_STYLESETBACK, STYLE_CODE_INLINE, RGB(240, 240, 245));
        // Heading: dark blue, bold
        SendMessage(m_hwnddisplay, SCI_STYLESETFONT, STYLE_HEADING, (LPARAM)"Microsoft YaHei");
        SendMessage(m_hwnddisplay, SCI_STYLESETFORE, STYLE_HEADING, RGB(0, 70, 140));
        SendMessage(m_hwnddisplay, SCI_STYLESETBOLD, STYLE_HEADING, TRUE);
        // Bold: dark text, bold
        SendMessage(m_hwnddisplay, SCI_STYLESETFONT, STYLE_BOLD, (LPARAM)"Microsoft YaHei");
        SendMessage(m_hwnddisplay, SCI_STYLESETFORE, STYLE_BOLD, RGB(30, 30, 30));
        SendMessage(m_hwnddisplay, SCI_STYLESETBOLD, STYLE_BOLD, TRUE);
        // Link: blue, underline
        SendMessage(m_hwnddisplay, SCI_STYLESETFONT, STYLE_LINK, (LPARAM)"Microsoft YaHei");
        SendMessage(m_hwnddisplay, SCI_STYLESETFORE, STYLE_LINK, RGB(0, 100, 200));
        SendMessage(m_hwnddisplay, SCI_STYLESETUNDERLINE, STYLE_LINK, TRUE);
    } else {
        // EDIT fallback: set a CJK-compatible font
        HFONT hfallback = CreateFont(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        if (hfallback)
            SendMessage(m_hwnddisplay, WM_SETFONT, (WPARAM)hfallback, TRUE);
    }

    // Subclass display control
    SetWindowLongPtr(m_hwnddisplay, GWLP_USERDATA, (LONG_PTR)this);
    m_origdisplayproc = (WNDPROC)SetWindowLongPtr(m_hwnddisplay, GWLP_WNDPROC, (LONG_PTR)DisplayProc);

    // Create multiline input edit
    m_hwndinput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
        0, 0, 100, 100, hwndparent, (HMENU)IDC_AI_INPUT, hinst, NULL);

    // Subclass input control
    SetWindowLongPtr(m_hwndinput, GWLP_USERDATA, (LONG_PTR)this);
    m_originputproc = (WNDPROC)SetWindowLongPtr(m_hwndinput, GWLP_WNDPROC, (LONG_PTR)InputProc);

    // Create buttons (localized text)
    const char* langcode = GetL10nLangcode();
    bool isChinese = (langcode && (strcmp(langcode, "zh-cn") == 0 || strcmp(langcode, "zh") == 0));
    const wchar_t* sendText = isChinese ? L"\x53D1\x9001" : L"Send";      // 发送
    const wchar_t* clearText = isChinese ? L"\x6E05\x9664" : L"Clear";    // 清除

    m_hwndsend = CreateWindowEx(0, L"BUTTON", sendText,
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 55, 20, hwndparent, (HMENU)IDC_AI_SENDBTN, hinst, NULL);

    m_hwndclear = CreateWindowEx(0, L"BUTTON", clearText,
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 55, 20, hwndparent, (HMENU)IDC_AI_CLEARBTN, hinst, NULL);

    // Create status label
    m_hwndstatus = CreateWindowEx(0, L"STATIC", L"Ready",
        WS_CHILD | SS_LEFT,
        0, 0, 100, 16, hwndparent, (HMENU)IDC_AI_STATUS, hinst, NULL);

    // Set fonts - use CJK-compatible font for all controls
    HFONT hfont = CreateFont(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (!hfont)
        hfont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (hfont) {
        SendMessage(m_hwndinput, WM_SETFONT, (WPARAM)hfont, TRUE);
        SendMessage(m_hwndsend, WM_SETFONT, (WPARAM)hfont, TRUE);
        SendMessage(m_hwndclear, WM_SETFONT, (WPARAM)hfont, TRUE);
        SendMessage(m_hwndstatus, WM_SETFONT, (WPARAM)hfont, TRUE);
    }

    return wyTrue;
}

void TabAI::Resize()
{
    if (!m_hwnddisplay)
        return;

    RECT rc;
    GetClientRect(m_hwndparent, &rc);
    int tabH = CustomTab_GetTabHeight(m_hwndparent);
    int btnH = 22;
    int inputH = 60;
    int statusH = 18;
    int padding = 4;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int displayH = h - tabH - inputH - btnH - statusH - padding * 2;

    if (displayH < 50) displayH = 50;

    MoveWindow(m_hwnddisplay, padding, tabH + padding, w - padding * 2, displayH, TRUE);
    MoveWindow(m_hwndinput, padding, tabH + displayH + padding, w - 130, inputH, TRUE);
    MoveWindow(m_hwndsend, w - 120, tabH + displayH + padding, 55, btnH, TRUE);
    MoveWindow(m_hwndclear, w - 60, tabH + displayH + padding, 55, btnH, TRUE);
    MoveWindow(m_hwndstatus, padding, tabH + displayH + inputH + padding, w - padding * 2, statusH, TRUE);
}

void TabAI::OnTabSelChange(wyBool isselected)
{
    if (!m_hwnddisplay)
        return;

    int show = (isselected == wyTrue) ? SW_SHOW : SW_HIDE;
    ShowWindow(m_hwnddisplay, show);
    ShowWindow(m_hwndinput, show);
    ShowWindow(m_hwndsend, show);
    ShowWindow(m_hwndclear, show);
    ShowWindow(m_hwndstatus, show);

    if (isselected == wyTrue) {
        SetFocus(m_hwndinput);
    }
}

void TabAI::UpdateStatusBar(StatusBarMgmt* pmgmt)
{
    // No-op for AI tab
}

void TabAI::AddMessageToDisplay(const wyChar* role, const wyChar* content)
{
    if (!m_hwnddisplay || !content)
        return;

    // Build the message text
    wyString header;
    if (strcmp(role, "user") == 0) {
        header.Sprintf("%s", "You:");
    } else if (strcmp(role, "assistant") == 0) {
        header.Sprintf("%s", "AI:");
    } else if (strcmp(role, "error") == 0) {
        header.Sprintf("%s", "Error:");
    } else {
        header.Sprintf("%s:", role);
    }

    if (m_isscintilla) {
        // Scintilla path: use styled text
        SendMessage(m_hwnddisplay, SCI_SETREADONLY, FALSE, 0);

        int pos = (int)SendMessage(m_hwnddisplay, SCI_GETTEXTLENGTH, 0, 0);

        if (pos > 0) {
            const char* sep = "\n";
            SendMessage(m_hwnddisplay, SCI_APPENDTEXT, 1, (LPARAM)sep);
            pos++;
        }

        int style = STYLE_DEFAULT;
        if (strcmp(role, "user") == 0) style = STYLE_USER_MSG;
        else if (strcmp(role, "assistant") == 0) style = STYLE_AI_MSG;
        else if (strcmp(role, "error") == 0) style = STYLE_ERROR_MSG;

        int headerStart = pos;
        SendMessage(m_hwnddisplay, SCI_APPENDTEXT, header.GetLength(), (LPARAM)header.GetString());
        int headerEnd = pos + header.GetLength();
        SendMessage(m_hwnddisplay, SCI_STARTSTYLING, headerStart, 0x1F);
        SendMessage(m_hwnddisplay, SCI_SETSTYLING, headerEnd - headerStart, style);

        const char* nl = "\n";
        SendMessage(m_hwnddisplay, SCI_APPENDTEXT, 1, (LPARAM)nl);

        int contentStart = headerEnd + 1;
        int contentLen = strlen(content);
        SendMessage(m_hwnddisplay, SCI_APPENDTEXT, contentLen, (LPARAM)content);
        int contentEnd = contentStart + contentLen;

        SendMessage(m_hwnddisplay, SCI_STARTSTYLING, contentStart, 0x1F);
        SendMessage(m_hwnddisplay, SCI_SETSTYLING, contentEnd - contentStart, style);

        // Track AI message start position for markdown formatting
        if (strcmp(role, "assistant") == 0) {
            m_aiMsgStartPos = contentStart;
        }

        SendMessage(m_hwnddisplay, SCI_GOTOPOS, contentEnd, 0);
        SendMessage(m_hwnddisplay, SCI_SETREADONLY, TRUE, 0);
    } else {
        // EDIT fallback: append plain text
        wyString msg;
        if (strlen(content) > 0)
            msg.Sprintf("%s\n%s\n\n", header.GetString(), content);
        else
            msg.Sprintf("%s\n", header.GetString());

        // Get current text length
        int len = GetWindowTextLength(m_hwnddisplay);
        SendMessage(m_hwnddisplay, EM_SETSEL, len, len);
        wyWChar* wmsg = msg.GetAsWideChar();
        SendMessage(m_hwnddisplay, EM_REPLACESEL, FALSE, (LPARAM)wmsg);

        // Scroll to end
        SendMessage(m_hwnddisplay, EM_SCROLLCARET, 0, 0);
    }
}

void TabAI::AppendToLastMessage(const wyChar* token)
{
    if (!m_hwnddisplay || !token)
        return;

    if (m_isscintilla) {
        // Scintilla path: styled append
        SendMessage(m_hwnddisplay, SCI_SETREADONLY, FALSE, 0);

        int pos = (int)SendMessage(m_hwnddisplay, SCI_GETTEXTLENGTH, 0, 0);

        int tokenLen = strlen(token);
        SendMessage(m_hwnddisplay, SCI_APPENDTEXT, tokenLen, (LPARAM)token);

        // Apply AI style
        SendMessage(m_hwnddisplay, SCI_STARTSTYLING, pos, 0x1F);
        SendMessage(m_hwnddisplay, SCI_SETSTYLING, tokenLen, STYLE_AI_MSG);

        // Scroll to end
        SendMessage(m_hwnddisplay, SCI_GOTOPOS, pos + tokenLen, 0);

        // Make read-only again
        SendMessage(m_hwnddisplay, SCI_SETREADONLY, TRUE, 0);
    } else {
        // EDIT fallback: append plain text
        int len = GetWindowTextLength(m_hwnddisplay);
        SendMessage(m_hwnddisplay, EM_SETSEL, len, len);
        wyString wtoken;
        wtoken.SetAs(token);
        wyWChar* wstr = wtoken.GetAsWideChar();
        SendMessage(m_hwnddisplay, EM_REPLACESEL, FALSE, (LPARAM)wstr);

        // Scroll to end
        SendMessage(m_hwnddisplay, EM_SCROLLCARET, 0, 0);
    }
}

void TabAI::OnSend()
{
    // Prevent re-entry (audit fix: C4)
    if (InterlockedCompareExchange(&m_istreaming, 0, 0) != 0)
        return;

    // Get input text
    int len = GetWindowTextLength(m_hwndinput);
    if (len == 0)
        return;

    wyWChar* buf = new wyWChar[len + 1];
    GetWindowText(m_hwndinput, buf, len + 1);

    wyString prompt;
    prompt.SetAs(buf);
    delete[] buf;

    // Clear input
    SetWindowText(m_hwndinput, L"");

    // Add user message to display
    AddMessageToDisplay("user", prompt.GetString());

    // Prepare for streaming
    InterlockedExchange(&m_istreaming, 1);
    InterlockedExchange(&m_stopstream, 0);
    UpdateSendButton();

    // Add placeholder for AI response
    AddMessageToDisplay("assistant", "");
    m_streambuf.Clear();

    // Build request
    AIConfig config;
    AIService::LoadConfig(&config);

    // Show API info in status
    const char* langcode = GetL10nLangcode();
    bool isChinese = (langcode && (strcmp(langcode, "zh-cn") == 0 || strcmp(langcode, "zh") == 0));
    wyString statusText;
    statusText.Sprintf("%s: %s", isChinese ? "\xC8\xED\xBC\xFE" : "Model", config.model.GetString());  // 模型
    SetWindowText(m_hwndstatus, statusText.GetAsWideChar());

    // Set status
    SetWindowText(m_hwndstatus, isChinese ? L"\x8BF7\x6C42\x4E2D..." : _(L"Requesting..."));  // 请求中...

    wyString requestJson;
    AIService::BuildRequestJson(
        prompt.GetString(), AIService::GetPromptChat(), m_historyJson.GetString(), config.model.GetString(), &requestJson);

    // Add to history after building the request so the current prompt is not duplicated.
    ChatMessage* userMsg = new ChatMessage;
    userMsg->role.SetAs("user");
    userMsg->content.SetAs(prompt.GetString());
    m_history.push_back(userMsg);
    UpdateHistoryJson();

    // Launch thread
    StreamThreadParam* param = new StreamThreadParam;
    param->pthis = this;
    param->prompt.SetAs(requestJson.GetString());
    param->systemPrompt.SetAs(AIService::GetPromptChat());
    param->timeoutMs = 300000;

    if (m_hthread) {
        CloseHandle(m_hthread);
        m_hthread = NULL;
    }

    unsigned threadid;
    m_hthread = (HANDLE)_beginthreadex(NULL, 0, StreamThreadProc, param, 0, &threadid);
}

void TabAI::OnStop()
{
    CancelStream();
    const char* langcode = GetL10nLangcode();
    bool isChinese = (langcode && (strcmp(langcode, "zh-cn") == 0 || strcmp(langcode, "zh") == 0));
    SetWindowText(m_hwndstatus, isChinese ? L"\x505C\x6B62\x4E2D..." : _(L"Stopping..."));  // 停止中...
}

void TabAI::CancelStream()
{
    InterlockedExchange(&m_stopstream, 1);
}

void TabAI::UpdateSendButton()
{
    const char* langcode = GetL10nLangcode();
    bool isChinese = (langcode && (strcmp(langcode, "zh-cn") == 0 || strcmp(langcode, "zh") == 0));

    if (InterlockedCompareExchange(&m_istreaming, 0, 0) != 0) {
        SetWindowText(m_hwndsend, isChinese ? L"\x505C\x6B62" : _(L"Stop"));  // 停止
    } else {
        SetWindowText(m_hwndsend, isChinese ? L"\x53D1\x9001" : _(L"Send"));  // 发送
    }
}

void TabAI::UpdateHistoryJson()
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < m_history.size(); i++) {
        ChatMessage* msg = m_history[i];
        if (msg) {
            Json::Value obj;
            obj["role"] = msg->role.GetString();
            obj["content"] = msg->content.GetString();
            arr.append(obj);
        }
    }
    Json::FastWriter writer;
    m_historyJson.SetAs(writer.write(arr).c_str());
}

void TabAI::ConvertMarkdownToDisplay(const wyChar* markdown, wyString& displayText, MarkdownElement** elements, int& elementCount)
{
    if (!markdown) {
        displayText.Clear();
        *elements = NULL;
        elementCount = 0;
        return;
    }

    // Allocate elements array (max possible size)
    int maxElements = strlen(markdown) / 3;  // rough estimate
    MarkdownElement* elems = new MarkdownElement[maxElements];
    elementCount = 0;

    // Allocate buffer for display text
    int len = strlen(markdown);
    char* buf = new char[len + 1];
    int bufPos = 0;

    const char* p = markdown;
    while (*p) {
        // Code block: ``` ... ```
        if (p[0] == '`' && p[1] == '`' && p[2] == '`') {
            p += 3;
            // Skip language identifier
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            // Copy code content
            int elemStart = bufPos;
            while (*p) {
                if (p[0] == '`' && p[1] == '`' && p[2] == '`') {
                    p += 3;
                    break;
                }
                buf[bufPos++] = *p++;
            }
            // Record element
            if (bufPos > elemStart) {
                elems[elementCount].start = elemStart;
                elems[elementCount].len = bufPos - elemStart;
                elems[elementCount].style = STYLE_CODE_BLOCK;
                elementCount++;
            }
            if (*p == '\n') p++;
            continue;
        }

        // Inline code: `...`
        if (*p == '`') {
            p++;
            int elemStart = bufPos;
            while (*p && *p != '`') {
                buf[bufPos++] = *p++;
            }
            if (*p == '`') p++;
            // Record element
            if (bufPos > elemStart) {
                elems[elementCount].start = elemStart;
                elems[elementCount].len = bufPos - elemStart;
                elems[elementCount].style = STYLE_CODE_INLINE;
                elementCount++;
            }
            continue;
        }

        // Heading: # at start of line
        if (*p == '#' && (p == markdown || *(p-1) == '\n')) {
            while (*p == '#') p++;
            if (*p == ' ') p++;
            int elemStart = bufPos;
            while (*p && *p != '\n') {
                buf[bufPos++] = *p++;
            }
            // Record element
            if (bufPos > elemStart) {
                elems[elementCount].start = elemStart;
                elems[elementCount].len = bufPos - elemStart;
                elems[elementCount].style = STYLE_HEADING;
                elementCount++;
            }
            continue;
        }

        // Bold: **...**
        if (p[0] == '*' && p[1] == '*') {
            p += 2;
            int elemStart = bufPos;
            while (*p && !(p[0] == '*' && p[1] == '*')) {
                buf[bufPos++] = *p++;
            }
            if (p[0] == '*' && p[1] == '*') p += 2;
            // Record element
            if (bufPos > elemStart) {
                elems[elementCount].start = elemStart;
                elems[elementCount].len = bufPos - elemStart;
                elems[elementCount].style = STYLE_BOLD;
                elementCount++;
            }
            continue;
        }

        // Link: [text](url) -> show as "text"
        if (*p == '[') {
            p++;
            int elemStart = bufPos;
            while (*p && *p != ']') {
                buf[bufPos++] = *p++;
            }
            if (*p == ']') p++;
            // Skip (url)
            if (*p == '(') {
                while (*p && *p != ')') p++;
                if (*p == ')') p++;
            }
            // Record element
            if (bufPos > elemStart) {
                elems[elementCount].start = elemStart;
                elems[elementCount].len = bufPos - elemStart;
                elems[elementCount].style = STYLE_LINK;
                elementCount++;
            }
            continue;
        }

        // List item: - or * at start of line
        if ((*p == '-' || *p == '*') && (p == markdown || *(p-1) == '\n') && *(p+1) == ' ') {
            p += 2;
            buf[bufPos++] = ' ';
            buf[bufPos++] = ' ';
            continue;
        }

        // Regular character
        buf[bufPos++] = *p++;
    }

    buf[bufPos] = '\0';
    displayText.SetAs(buf, bufPos);
    delete[] buf;

    *elements = elems;
}

void TabAI::ApplyMarkdownFormatting(int startPos, int endPos, int baseStyle, MarkdownElement* elements, int elementCount)
{
    if (!m_isscintilla || startPos >= endPos)
        return;

    // First, set the entire range to base style
    int textLen = endPos - startPos;
    if (textLen <= 0) return;

    SendMessage(m_hwnddisplay, SCI_STARTSTYLING, startPos, 0x1F);
    SendMessage(m_hwnddisplay, SCI_SETSTYLING, textLen, baseStyle);

    // Apply styles from elements array
    for (int i = 0; i < elementCount; i++) {
        if (elements[i].len > 0) {
            SendMessage(m_hwnddisplay, SCI_STARTSTYLING, startPos + elements[i].start, 0x1F);
            SendMessage(m_hwnddisplay, SCI_SETSTYLING, elements[i].len, elements[i].style);
        }
    }
}

void TabAI::ClearConversation()
{
    // Clear history
    for (size_t i = 0; i < m_history.size(); i++) {
        ChatMessage* msg = m_history[i];
        if (msg) delete msg;
    }
    m_history.clear();
    m_historyJson.Clear();

    // Clear display
    if (m_hwnddisplay) {
        if (m_isscintilla) {
            SendMessage(m_hwnddisplay, SCI_SETREADONLY, FALSE, 0);
            SendMessage(m_hwnddisplay, SCI_SETTEXT, 0, (LPARAM)"");
            SendMessage(m_hwnddisplay, SCI_SETREADONLY, TRUE, 0);
        } else {
            SetWindowText(m_hwnddisplay, L"");
        }
    }

    const char* langcode = GetL10nLangcode();
    bool isChinese = (langcode && (strcmp(langcode, "zh-cn") == 0 || strcmp(langcode, "zh") == 0));
    SetWindowText(m_hwndstatus, isChinese ? L"\x5C31\x7EEA" : _(L"Ready"));  // 就绪
}

void TabAI::SendSQLToAI(const wyChar* sql, const wyChar* action)
{
    if (!sql || !sql[0])
        return;

    // Determine system prompt based on action
    const char* sysPrompt = AIService::GetPromptChat();
    if (action && strcmp(action, "analyze") == 0) {
        sysPrompt = AIService::GetPromptAnalyze();
    } else if (action && strcmp(action, "beautify") == 0) {
        sysPrompt = AIService::GetPromptBeautify();
    }

    // Prevent re-entry
    if (InterlockedCompareExchange(&m_istreaming, 0, 0) != 0)
        return;

    // Add user message to display (localized)
    const char* langcode = GetL10nLangcode();
    bool isChinese = (langcode && (strcmp(langcode, "zh-cn") == 0 || strcmp(langcode, "zh") == 0));
    wyString userMsg;
    if (isChinese) {
        if (action && strcmp(action, "analyze") == 0)
            userMsg.Sprintf("\xe8\xaf\xb7\xe5\x88\x86\xe6\x9e\x90\xe8\xbf\x99\xe6\x9d\xa1 SQL\xef\xbc\x9a\n%s", sql);  // 请分析这条SQL：
        else if (action && strcmp(action, "beautify") == 0)
            userMsg.Sprintf("\xe8\xaf\xb7\xe7\xbe\x8e\xe5\x8c\x96\xe8\xbf\x99\xe6\x9d\xa1 SQL\xef\xbc\x9a\n%s", sql);  // 请美化这条SQL：
        else
            userMsg.Sprintf("\xe8\xaf\xb7\xe5\xa4\x84\xe7\x90\x86\xe8\xbf\x99\xe6\x9d\xa1 SQL\xef\xbc\x9a\n%s", sql);  // 请处理这条SQL：
    } else {
        userMsg.Sprintf("Please %s this SQL:\n%s", action ? action : "analyze", sql);
    }
    AddMessageToDisplay("user", userMsg.GetString());

    // Prepare for streaming
    InterlockedExchange(&m_istreaming, 1);
    InterlockedExchange(&m_stopstream, 0);
    UpdateSendButton();

    // Add placeholder for AI response
    AddMessageToDisplay("assistant", "");
    m_streambuf.Clear();

    SetWindowText(m_hwndstatus, isChinese ? L"\x8BF7\x6C42\x4E2D..." : _(L"Requesting..."));  // 请求中...

    // Build request
    AIConfig config;
    AIService::LoadConfig(&config);

    wyString requestJson;
    AIService::BuildRequestJson(userMsg.GetString(), sysPrompt, m_historyJson.GetString(), config.model.GetString(), &requestJson);

    // Add to history after building the request so the current prompt is not duplicated.
    ChatMessage* histMsg = new ChatMessage;
    histMsg->role.SetAs("user");
    histMsg->content.SetAs(userMsg.GetString());
    m_history.push_back(histMsg);
    UpdateHistoryJson();

    // Launch thread
    StreamThreadParam* param = new StreamThreadParam;
    param->pthis = this;
    param->prompt.SetAs(requestJson.GetString());
    param->systemPrompt.SetAs(sysPrompt);
    param->timeoutMs = 300000;

    if (m_hthread) {
        CloseHandle(m_hthread);
        m_hthread = NULL;
    }

    unsigned threadid;
    m_hthread = (HANDLE)_beginthreadex(NULL, 0, StreamThreadProc, param, 0, &threadid);
}

void TabAI::SendErrorToAI(const wyChar* errorText)
{
    if (!errorText || !errorText[0])
        return;

    if (InterlockedCompareExchange(&m_istreaming, 0, 0) != 0)
        return;

    const char* langcode = GetL10nLangcode();
    bool isChinese = (langcode && (strcmp(langcode, "zh-cn") == 0 || strcmp(langcode, "zh") == 0));

    wyString userMsg;
    if (isChinese) {
        userMsg.Sprintf("\xe8\xaf\xb7\xe5\x88\x86\xe6\x9e\x90\xe4\xbb\xa5\xe4\xb8\x8b SQL \xe9\x94\x99\xe8\xaf\xaf\xe4\xbf\xa1\xe6\x81\xaf\xe5\xb9\xb6\xe6\x8f\x90\xe4\xbe\x9b\xe8\xa7\xa3\xe5\x86\xb3\xe6\x96\xb9\xe6\xa1\x88\xef\xbc\x9a\n\n%s", errorText);
    } else {
        userMsg.Sprintf("Please analyze this SQL error and provide a fix:\n\n%s", errorText);
    }

    AddMessageToDisplay("user", userMsg.GetString());

    InterlockedExchange(&m_istreaming, 1);
    InterlockedExchange(&m_stopstream, 0);
    UpdateSendButton();

    AddMessageToDisplay("assistant", "");
    m_streambuf.Clear();

    SetWindowText(m_hwndstatus, isChinese ? L"\x8BF7\x6C42\x4E2D..." : _(L"Requesting..."));

    AIConfig config;
    AIService::LoadConfig(&config);

    wyString requestJson;
    AIService::BuildErrorAnalysisRequestJson(
        "",
        errorText,
        0,
        config.model.GetString(),
        &requestJson);

    ChatMessage* histMsg = new ChatMessage;
    histMsg->role.SetAs("user");
    histMsg->content.SetAs(userMsg.GetString());
    m_history.push_back(histMsg);
    UpdateHistoryJson();

    StreamThreadParam* param = new StreamThreadParam;
    param->pthis = this;
    param->prompt.SetAs(requestJson.GetString());
    param->systemPrompt.SetAs(AIService::GetPromptErrorAnalysis());
    param->timeoutMs = (config.error_analysis_timeout_ms > 0)
        ? (DWORD)config.error_analysis_timeout_ms
        : 60000;

    if (m_hthread) {
        CloseHandle(m_hthread);
        m_hthread = NULL;
    }

    unsigned threadid;
    m_hthread = (HANDLE)_beginthreadex(NULL, 0, StreamThreadProc, param, 0, &threadid);
    if (!m_hthread) {
        delete param;
        InterlockedExchange(&m_istreaming, 0);
        UpdateSendButton();
        AddMessageToDisplay("error", isChinese ? "\xe5\x90\xaf\xe5\x8a\xa8 AI \xe5\x88\x86\xe6\x9e\x90\xe7\xba\xbf\xe7\xa8\x8b\xe5\xa4\xb1\xe8\xb4\xa5" : _("Failed to start AI analysis thread"));
        SetWindowText(m_hwndstatus, isChinese ? L"\x53D1\x751F\x9519\x8BEF" : _(L"Error occurred"));
    }
}

void TabAI::OnStreamComplete(bool success, const wyChar* error)
{

    InterlockedExchange(&m_istreaming, 0);
    UpdateSendButton();

    const char* langcode = GetL10nLangcode();
    bool isChinese = (langcode && (strcmp(langcode, "zh-cn") == 0 || strcmp(langcode, "zh") == 0));

    if (success) {
        // Check if we actually received any content
        if (m_streambuf.GetLength() > 0) {
            // Convert markdown to display text (remove syntax symbols)
            wyString displayText;
            MarkdownElement* mdElements = NULL;
            int elementCount = 0;
            ConvertMarkdownToDisplay(m_streambuf.GetString(), displayText, &mdElements, elementCount);

            if (m_isscintilla && m_aiMsgStartPos > 0) {
                SendMessage(m_hwnddisplay, SCI_SETREADONLY, FALSE, 0);

                // Replace the streamed text with processed display text
                int endPos = (int)SendMessage(m_hwnddisplay, SCI_GETTEXTLENGTH, 0, 0);
                SendMessage(m_hwnddisplay, SCI_SETSEL, m_aiMsgStartPos, endPos);
                SendMessage(m_hwnddisplay, SCI_REPLACESEL, 0, (LPARAM)displayText.GetString());

                // Apply markdown styling to the replaced text
                int newEndPos = (int)SendMessage(m_hwnddisplay, SCI_GETTEXTLENGTH, 0, 0);
                ApplyMarkdownFormatting(m_aiMsgStartPos, newEndPos, STYLE_AI_MSG, mdElements, elementCount);

                SendMessage(m_hwnddisplay, SCI_SETREADONLY, TRUE, 0);
            }

            if (mdElements) delete[] mdElements;

            // Add completed AI message to history (store original markdown)
            ChatMessage* aiMsg = new ChatMessage;
            aiMsg->role.SetAs("assistant");
            aiMsg->content.SetAs(m_streambuf.GetString());
            m_history.push_back(aiMsg);
            UpdateHistoryJson();
            SetWindowText(m_hwndstatus, isChinese ? L"\x5C31\x7EEA" : _(L"Ready"));  // 就绪
        } else {
            // No content received - show diagnostic message
            wyString errMsg;
            if (isChinese)
                errMsg.SetAs("\xe6\x9c\xaa\xe6\x94\xb6\xe5\x88\xb0\xe5\x93\x8d\xe5\xba\x94\xef\xbc\x8c\xe8\xaf\xb7\xe6\xa3\x80\xe6\x9f\xa5 API \xe8\xae\xbe\xe7\xbd\xae\xef\xbc\x88\xe5\x9c\xb0\xe5\x9d\x80\xe3\x80\x81\xe5\xaf\x86\xe9\x92\xa5\xe3\x80\x81\xe6\xa8\xa1\xe5\x9e\x8b\xef\xbc\x89\xe3\x80\x82");
            else
                errMsg.SetAs(_("No response received. Please check API settings (URL, Key, Model)."));
            AddMessageToDisplay("error", errMsg.GetString());
            SetWindowText(m_hwndstatus, isChinese ? L"\x65E0\x54CD\x5E94" : _(L"No response"));  // 无响应
        }
    } else {
        // Show error
        if (error && strlen(error) > 0) {
            AddMessageToDisplay("error", error);
        } else {
            wyString errMsg;
            if (isChinese)
                errMsg.SetAs("\xe8\xaf\xb7\xe6\xb1\x82\xe5\xa4\xb1\xe8\xb4\xa5\xef\xbc\x8c\xe8\xaf\xb7\xe6\xa3\x80\xe6\x9f\xa5\xe7\xbd\x91\xe7\xbb\x9c\xe5\x92\x8c API \xe8\xae\xbe\xe7\xbd\xae\xe3\x80\x82");
            else
                errMsg.SetAs(_("Request failed. Please check network and API settings."));
            AddMessageToDisplay("error", errMsg.GetString());
        }
        SetWindowText(m_hwndstatus, isChinese ? L"\x53D1\x751F\x9519\x8BEF" : _(L"Error occurred"));  // 发生错误
    }

    m_streambuf.Clear();

    // Close thread handle
    if (m_hthread) {
        CloseHandle(m_hthread);
        m_hthread = NULL;
    }
}

unsigned __stdcall TabAI::StreamThreadProc(void* p)
{
    StreamThreadParam* param = (StreamThreadParam*)p;
    if (!param)
        return 0;
    TabAI* pthis = param->pthis;
    if (!pthis) {
        delete param;
        return 0;
    }

    AIConfig config;
    AIService::LoadConfig(&config);

    wyString errorBuf;
    bool ok = AIService::SendRequestStreaming(
        &config,
        param->prompt.GetString(),
        OnStreamToken,
        pthis,
        &pthis->m_stopstream,
        &errorBuf,
        param->timeoutMs
    );

    // Post completion message
    wyString* perr = NULL;
    if (!ok && errorBuf.GetLength() > 0) {
        perr = new wyString();
        perr->SetAs(errorBuf.GetString());
    }

    if (pthis) {
        PostMessage(pthis->m_hwnddisplay, UM_AI_STREAM_COMPLETE,
                    ok ? 1 : 0, (LPARAM)perr);
    }

    delete param;
    return 0;
}

bool TabAI::OnStreamToken(const wyChar* token, int tokenLen, void* userdata)
{
    TabAI* pthis = (TabAI*)userdata;
    if (!pthis || !token || tokenLen == 0)
        return false;

    // Copy token for PostMessage
    wyString* ptoken = new wyString();
    ptoken->SetAs(token, tokenLen);

    BOOL posted = PostMessage(pthis->m_hwnddisplay, UM_AI_STREAM_TOKEN,
                              0, (LPARAM)ptoken);
    if (!posted)
        delete ptoken;

    return true;
}

void TabAI::OnWMCommand(WPARAM wparam)
{
    switch(LOWORD(wparam))
    {
    case IDC_AI_SENDBTN:
        if (InterlockedCompareExchange(&m_istreaming, 0, 0) != 0)
            OnStop();
        else
            OnSend();
        break;

    case IDC_AI_CLEARBTN:
        ClearConversation();
        break;
    }
}

LRESULT CALLBACK TabAI::DisplayProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TabAI* pthis = (TabAI*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case UM_AI_STREAM_TOKEN:
    {
        wyString* ptoken = (wyString*)lparam;
        if (ptoken && pthis) {
            pthis->AppendToLastMessage(ptoken->GetString());
            pthis->m_streambuf.Add(ptoken->GetString());
            delete ptoken;
        }
        return 0;
    }

    case UM_AI_STREAM_COMPLETE:
    {
        wyString* perr = (wyString*)lparam;
        if (pthis) {
            bool success = (wparam != 0);
            pthis->OnStreamComplete(success, perr ? perr->GetString() : NULL);
        }
        if (perr) delete perr;
        return 0;
    }
    }

    if (pthis && pthis->m_origdisplayproc)
        return CallWindowProc(pthis->m_origdisplayproc, hwnd, msg, wparam, lparam);

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK TabAI::InputProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TabAI* pthis = (TabAI*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_KEYDOWN:
    {
        // Enter sends message, Shift+Enter adds newline
        if (wparam == VK_RETURN && !(GetKeyState(VK_SHIFT) & 0x8000)) {
            if (pthis) pthis->OnSend();
            return 0;
        }
        break;
    }
    case WM_CHAR:
    {
        // Suppress the WM_CHAR for Enter to prevent newline insertion
        if (wparam == VK_RETURN && !(GetKeyState(VK_SHIFT) & 0x8000)) {
            return 0;
        }
        break;
    }
    }

    if (pthis && pthis->m_originputproc)
        return CallWindowProc(pthis->m_originputproc, hwnd, msg, wparam, lparam);

    return DefWindowProc(hwnd, msg, wparam, lparam);
}
