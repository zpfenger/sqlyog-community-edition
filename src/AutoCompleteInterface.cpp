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
    if(m_community_ac && wnd)
        m_community_ac->LoadMetadataAsync(wnd);
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
        char stop_chars[] = " ~`!@#$%^&*()+|\\=-?><,/\":;'{}[]";
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

        if(prefix.GetLength() > 0)
        {
            wyString candidates;
            bool has_items = false;
            m_community_ac->QueryCompletion(prefix.GetString(), ctx, table_idx, candidates, has_items);

            if(has_items)
            {
                SendMessage(hwnd, SCI_AUTOCSETTYPESEPARATOR, (WPARAM)'?', 0);
                SendMessage(hwnd, SCI_AUTOCSHOW, prefix.GetLength(), (LPARAM)candidates.GetString());
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

    if(prefix.GetLength() > 0)
    {
        wyString candidates;
        bool has_items = false;
        m_community_ac->QueryCompletion(prefix.GetString(), ctx, table_idx, candidates, has_items);

        if(has_items)
        {
            SendMessage(hwnd, SCI_AUTOCSETTYPESEPARATOR, (WPARAM)'?', 0);
            SendMessage(hwnd, SCI_AUTOCSHOW, prefix.GetLength(), (LPARAM)candidates.GetString());
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
        return wyTrue;
    }
#endif
    return wyFalse;
}
