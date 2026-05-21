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


#include "AutoCompleteInterface.h"

#ifndef COMMUNITY
#include "AutoCompleteEnt.h"
#else
#include "CommunityAutoComplete.h"
#include "scintilla/Scintilla.h"

namespace {

bool CommunityAllowsEmptyPrefix(SQLContextType ctx) {
    return ctx == CTX_TABLE_REF ||
           ctx == CTX_COLUMN_REF ||
           ctx == CTX_WHERE_EXPR ||
           ctx == CTX_INSERT_COLS;
}

}
#endif

AutoCompleteInterface::AutoCompleteInterface()
{
    m_autocomplete = NULL;
#ifdef COMMUNITY
    m_community_ac = new CCommunityAutoComplete();
    m_community_ac->InitStaticItems();
#endif
}

AutoCompleteInterface::~AutoCompleteInterface()
{
#ifndef COMMUNITY
    if(m_autocomplete)
        delete m_autocomplete;

    m_autocomplete = NULL;
#else
    delete m_community_ac;
    m_community_ac = NULL;
#endif
}

wyBool
AutoCompleteInterface::HandlerInitAutoComplete(MDIWindow * wnd)
{
#ifndef COMMUNITY
    if(IsAutoComplete() == wyTrue)
    {
        m_autocomplete = InitAutoComplete(wnd);

        if(m_autocomplete)
            return wyTrue;
    }
#else
    if(pGlobals)
        pGlobals->m_isautocomplete = wyTrue;
    return wyTrue;
#endif
    return wyFalse;
}

void	            
AutoCompleteInterface::HandlerBuildTags(MDIWindow *wnd)
{
#ifndef COMMUNITY
    if(pGlobals->m_isautocomplete == wyTrue)
		if(wnd->m_psqlite->m_refcount == 1)
            HandlerStoreObjects(wnd, wyFalse);
#endif
}

void
AutoCompleteInterface::HandlerStoreObjects(MDIWindow *wnd, wyBool rebuild)
{
#ifndef COMMUNITY
    if(m_autocomplete)
    {
        StoreObjects(wnd, m_autocomplete, rebuild);
    }
#else
    // P0-FIX: 改为同步加载。原异步实现违反两条 mysql 客户端规则：
    //   1) 后台线程未调用 mysql_thread_init()/mysql_thread_end()
    //   2) 与主线程共享同一个 mysql 连接（libmysql 单连接非线程安全）
    // 同步在主线程执行 LoadMetadata 在连接刚建立时是安全的（此时无其他 mysql 操作）。
    if(m_community_ac && wnd)
        m_community_ac->LoadMetadata(wnd);
#endif
}

wyInt32
AutoCompleteInterface::HandlerOnWMChar(HWND hwnd, EditorBase *eb, WPARAM wparam)
{
#ifndef COMMUNITY
    if(m_autocomplete)
    {
        return m_autocomplete->OnWMChar(hwnd, wparam, NULL);
    }
#else
    // Community edition auto-complete
    if(!m_community_ac || !pGlobals || !pGlobals->m_isautocomplete)
    {
        eb->SetAutoIndentation(hwnd, wparam);
        return 0;
    }

    // Check stop characters
    {
        // Keep whitespace flowing into context analysis so `FROM ` / `SELECT ` can
        // trigger empty-prefix suggestions instead of being cancelled early.
        char stop_chars[] = "~`!@#$%^&*+|\\=-?><,/\":;'{}[]";
        char ch = (char)wparam;
        for(int i = 0; stop_chars[i]; i++)
        {
            if(ch == stop_chars[i])
            {
                SendMessage(hwnd, SCI_AUTOCCANCEL, 0, 0);
                eb->SetAutoIndentation(hwnd, wparam);
                return 0;
            }
        }
    }

    // Analyze context and query completion
    {
        int cursor_pos = (int)SendMessage(hwnd, SCI_GETCURRENTPOS, 0, 0);
        wyString prefix;
        int table_idx = -1;
        SQLContextType ctx = m_community_ac->AnalyzeContext(eb, cursor_pos, prefix, table_idx);

        // For CTX_INSERT_COLS, allow empty prefix (e.g. right after "(")
        if(prefix.GetLength() > 0 || CommunityAllowsEmptyPrefix(ctx))
        {
            const char* prefix_str = prefix.GetLength() > 0 ? prefix.GetString() : "";
            int prefix_len = prefix.GetLength();

            wyString candidates;
            bool has_items = false;
            m_community_ac->QueryCompletion(prefix_str, ctx, table_idx, candidates, has_items);

            if(has_items)
            {
                SendMessage(hwnd, SCI_AUTOCSETTYPESEPARATOR, (WPARAM)'?', 0);
                SendMessage(hwnd, SCI_AUTOCSHOW, prefix_len, (LPARAM)candidates.GetString());
            }
            else
            {
                SendMessage(hwnd, SCI_AUTOCCANCEL, 0, 0);
            }
        }
    }

    eb->SetAutoIndentation(hwnd, wparam);
    return 0;
#endif
}

wyInt32
AutoCompleteInterface::HandlerOnWMKeyDown(HWND hwnd, EditorBase *eb, WPARAM wparam)
{
#ifndef COMMUNITY
    if(m_autocomplete)
    {
        return m_autocomplete->OnWMKeyDown(hwnd, eb, wparam);
    }
#else
    // Community edition: intercept Ctrl+Space to trigger auto-completion
    if(wparam == VK_SPACE && (GetKeyState(VK_CONTROL) & 0x8000))
    {
        TriggerCompletion(hwnd, eb);
        return 1;
    }
#endif
    return 0;
}

wyInt32 
AutoCompleteInterface::HandlerOnWMKeyUp(HWND hwnd, EditorBase *eb, WPARAM wparam)
{
#ifndef COMMUNITY
    if(m_autocomplete)
    {
        return m_autocomplete->OnWMKeyUp(hwnd, eb, wparam);
    }
#endif
    return 0;
}

wyBool
AutoCompleteInterface::TriggerCompletion(HWND hwnd, EditorBase *eb)
{
#ifndef COMMUNITY
    return wyFalse;
#else
    if(!m_community_ac || !pGlobals || !pGlobals->m_isautocomplete)
        return wyFalse;

    int cursor_pos = (int)SendMessage(hwnd, SCI_GETCURRENTPOS, 0, 0);
    wyString prefix;
    int table_idx = -1;
    SQLContextType ctx = m_community_ac->AnalyzeContext(eb, cursor_pos, prefix, table_idx);

    if(prefix.GetLength() > 0 || CommunityAllowsEmptyPrefix(ctx))
    {
        wyString candidates;
        bool has_items = false;
        const char* prefix_str = prefix.GetLength() > 0 ? prefix.GetString() : "";
        m_community_ac->QueryCompletion(prefix_str, ctx, table_idx, candidates, has_items);

        if(has_items)
        {
            int prefix_len = prefix.GetLength();
            SendMessage(hwnd, SCI_AUTOCSETTYPESEPARATOR, (WPARAM)'?', 0);
            SendMessage(hwnd, SCI_AUTOCSHOW, prefix_len, (LPARAM)candidates.GetString());
            return wyTrue;
        }
    }
    return wyFalse;
#endif
}

wyBool
AutoCompleteInterface::HandlerAddToMessageQueue(MDIWindow*	wnd, wyChar *query)
{
#ifndef COMMUNITY
    if(m_autocomplete)
    {
        return m_autocomplete->HandlerAddToMessageQueue(wnd, query);
    }
#endif
    return wyFalse;
}

void 
AutoCompleteInterface::HandlerProcessMessageQueue(MDIWindow *wnd)
{
#ifndef COMMUNITY
    if(m_autocomplete)
    {
        m_autocomplete->ProcessMessageQueue(wnd);
    }
#endif
}


void
AutoCompleteInterface::HandlerOnAutoThreadExit(MDIWindow *wnd, LPARAM lparam)
{
#ifndef COMMUNITY
    if(m_autocomplete)
    {
        return m_autocomplete->OnAutoThreadExit(wnd, lparam);
    }
#endif
}

//Sets the "AutocompleteTagbuilded" to '1' once the Build tags process is completed
VOID
AutoCompleteInterface::UpdateTagBuildFlag()
{
	wyString	dirstr;
	wyInt32		ret = 0, value = 0;;
	wyWChar		directory[MAX_PATH] = {0}, *lpfileport = 0;
	ConnectionBase *conbase = pGlobals->m_pcmainwin->m_connection;

	ret = SearchFilePath(L"sqlyog", L".ini", MAX_PATH - 1, directory, &lpfileport);
	if(ret == 0)
		return;

	dirstr.SetAs(directory);

	//write to .ini the 'Building' tag file is completed successful(AutocompleteTagbuilded = 1)
	if(conbase)
	{
		value = conbase->m_isbuiltactagfile == wyTrue ? 1 : 0;
        
		wyIni::IniWriteInt(conbase->m_consectionname.GetString(), 
			"AutocompleteTagbuilded", value, dirstr.GetString());		
	}
}

wyBool
AutoCompleteInterface::OnACNotification(WPARAM wparam, LPARAM lparam)
{
#ifndef COMMUNITY
    if(m_autocomplete)
    {
        if(((LPNMHDR)lparam)->code == SCN_AUTOCSELECTION)
        {
            CAutoComplete::OnACSelection(wparam, lparam);
            return wyTrue;
        }
        else if(((LPNMHDR)lparam)->code == SCN_CALLTIPCLICK)
        {
            m_autocomplete->OnCallTipClick(((LPNMHDR)lparam)->hwndFrom, wparam, lparam);
            return wyTrue;
        }
        else if(((LPNMHDR)lparam)->code == SCN_CHARADDED)
        {
            m_autocomplete->OnWMChar(((LPNMHDR)lparam)->hwndFrom, wparam, lparam);
            return wyFalse;
        }
        else if(((LPNMHDR)lparam)->code == SCN_UPDATEUI)
        {
            m_autocomplete->OnUpdateUI(wparam, lparam);
            return wyFalse;
        }
    }
#else
    // Community edition: handle auto-completion selection
    if(lparam && ((LPNMHDR)lparam)->code == SCN_AUTOCSELECTION)
    {
        SCNotification* scn = (SCNotification*)lparam;

        if(m_community_ac)
        {
            const char* selected = scn->text;
            if(selected && selected[0])
            {
                int type_val = m_community_ac->GetCompletionType(selected);
                HWND hwnd = ((LPNMHDR)lparam)->hwndFrom;

                if(type_val == AC_PRE_FUNCTION)
                {
                    // Append () and place cursor inside
                    int cur_pos = (int)SendMessage(hwnd, SCI_GETCURRENTPOS, 0, 0);
                    SendMessage(hwnd, SCI_INSERTTEXT, cur_pos, (LPARAM)"()");
                    SendMessage(hwnd, SCI_SETCURRENTPOS, cur_pos + 1, 0);
                    SendMessage(hwnd, SCI_SETSELECTIONSTART, cur_pos + 1, 0);
                    SendMessage(hwnd, SCI_SETSELECTIONEND, cur_pos + 1, 0);
                }
                else if(type_val == AC_PRE_KEYWORD)
                {
                    // Append a space after keyword
                    int cur_pos = (int)SendMessage(hwnd, SCI_GETCURRENTPOS, 0, 0);
                    SendMessage(hwnd, SCI_INSERTTEXT, cur_pos, (LPARAM)" ");
                    SendMessage(hwnd, SCI_SETCURRENTPOS, cur_pos + 1, 0);
                    SendMessage(hwnd, SCI_SETSELECTIONSTART, cur_pos + 1, 0);
                    SendMessage(hwnd, SCI_SETSELECTIONEND, cur_pos + 1, 0);
                }
            }
        }
        return wyTrue;
    }
#endif
    return wyFalse;
}
