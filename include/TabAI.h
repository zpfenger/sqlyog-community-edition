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

#ifndef _TABAI_H_
#define _TABAI_H_

#include <windows.h>
#include <vector>
#include "TabQueryTypes.h"
#include "wyString.h"

// Custom messages for thread-to-UI communication
#define UM_AI_STREAM_TOKEN    (WM_USER + 350)
#define UM_AI_STREAM_COMPLETE (WM_USER + 351)

class TabAI : public TabQueryTypes
{
public:
    TabAI(MDIWindow* wnd, HWND hwndparent);
    ~TabAI();

    wyBool  Create();
    void    Resize();
    void    OnTabSelChange(wyBool isselected);
    void    UpdateStatusBar(StatusBarMgmt* pmgmt);

    // External call: send SQL to AI from right-click menu
    void    SendSQLToAI(const wyChar* sql, const wyChar* action);

    // Clear conversation history
    void    ClearConversation();

    // Cancel current streaming request
    void    CancelStream();

    // Handle commands from child controls routed by TabMgmt
    void    OnWMCommand(WPARAM wparam);

private:
    // UI controls
    HWND    m_hwnddisplay;   // Conversation display (Scintilla, read-only)
    HWND    m_hwndinput;     // Input box (Edit, multiline)
    HWND    m_hwndsend;      // Send/Stop button
    HWND    m_hwndclear;     // Clear button
    HWND    m_hwndstatus;    // Status label

    // Window procedures
    static LRESULT CALLBACK DisplayProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK InputProc(HWND, UINT, WPARAM, LPARAM);
    WNDPROC m_origdisplayproc;
    WNDPROC m_originputproc;

    // Conversation history
    struct ChatMessage {
        wyString role;
        wyString content;
    };
    std::vector<ChatMessage*> m_history;
    wyString m_historyJson;  // Cached JSON representation

    // Streaming state (audit fix A3: volatile LONG for thread safety)
    volatile LONG m_istreaming;
    wyString m_streambuf;
    HANDLE   m_hthread;
    volatile LONG m_stopstream;
    BOOL     m_isscintilla;  // true if Scintilla control, false if EDIT fallback
    int      m_aiMsgStartPos; // Start position of current AI message for markdown formatting

    // Internal methods
    void    AddMessageToDisplay(const wyChar* role, const wyChar* content);
    void    AppendToLastMessage(const wyChar* token);
    void    OnSend();
    void    OnStop();
    void    OnStreamComplete(bool success, const wyChar* error = NULL);
    void    UpdateSendButton();
    void    UpdateHistoryJson();

    // Markdown formatting
    struct MarkdownElement {
        int start;   // Start position in display text
        int len;     // Length in display text
        int style;   // Style to apply
    };
    void    ConvertMarkdownToDisplay(const wyChar* markdown, wyString& displayText, MarkdownElement** elements, int& elementCount);
    void    ApplyMarkdownFormatting(int startPos, int endPos, int baseStyle, MarkdownElement* elements, int elementCount);

    // Thread
    struct StreamThreadParam {
        TabAI*   pthis;
        wyString prompt;
        wyString systemPrompt;
    };
    static unsigned __stdcall StreamThreadProc(void* param);
    static bool OnStreamToken(const wyChar* token, int tokenLen, void* userdata);
};

#endif
