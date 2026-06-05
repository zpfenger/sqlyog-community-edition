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


#include <assert.h>
#include <process.h>
#include "Scintilla.h"
#include "MDIWindow.h"
#include "Global.h"
#include "EditorBase.h"
#include "PreferenceBase.h"
#include "scilexer.h"
#include "EditorFont.h"
#include "GUIHelper.h"
#include "QueryThread.h"
#include "commonhelper.h"
#include "resource.h"
#include "TabEditorSplitter.h"
#include "L10nText.h"
#include "AIService.h"

extern	PGLOBALS		pGlobals;

HWND
StartExecute(MDIWindow * wnd, EXECUTEOPTION opt)
{
	wnd->SetExecuting(wyTrue);
	wnd->m_execoption       =      opt;
	
	///enables disables all the window on start/end of execution
	wnd->EnableWindowOnQueryExecution(wyFalse);

	///disable all the tool buttons 
	pGlobals->m_pcmainwin->EnableToolButtonsAndCombo(
                                pGlobals->m_pcmainwin->m_hwndtool,
                                pGlobals->m_pcmainwin->m_hwndsecondtool,
                                pGlobals->m_pcmainwin->m_hwndtoolcombo, wyFalse, wyTrue);

	///there might be editing happening in the grid. need to finish it */
	//CustomGrid_ApplyChanges(wnd->GetActiveTabEditor()->m_pctabmgmt->m_pcdataviewquery->m_hwndgrid);               

    CustomTab_SetClosable(wnd->m_pctabmodule->m_hwnd, wyTrue, CustomTab_GetItemCount(wnd->m_pctabmodule->m_hwnd));

	OnExecuteOptn(opt, wyTrue);
	
	/* change the cursor to hour glass mode */
	SetCursor(LoadCursor(NULL, IDC_WAIT));

	return NULL;// wnd->GetActiveTabEditor()->m_pctabmgmt->DisableOnExecution(wnd); 
}

/**
  replace the correct icon with stop icon 
  specifically enable the correct icon
*/

VOID
OnExecuteOptn(EXECUTEOPTION& opt, wyBool isstart)
{
    wyInt32 index, indexall;

    index = (isstart)?0:3; // 3 is the execute button index
    indexall = (isstart)?0:4; // 4 is the executeAll button index

	switch(opt)
	{

	case SINGLE:
		{
			::SendMessage(pGlobals->m_pcmainwin->m_hwndtool,
						TB_CHANGEBITMAP,
						(WPARAM)IDM_EXECUTE,
						(LPARAM)index);
			::SendMessage(pGlobals->m_pcmainwin->m_hwndtool,
						TB_SETSTATE,
						(WPARAM)IDM_EXECUTE,
						TBSTATE_ENABLED);
		}
		break;

	case ALL:
		{
			::SendMessage(pGlobals->m_pcmainwin->m_hwndtool,
				        TB_CHANGEBITMAP,
						(WPARAM)ACCEL_EXECUTEALL,
						(LPARAM)indexall);
			::SendMessage(pGlobals->m_pcmainwin->m_hwndtool,
				        TB_SETSTATE,
						(WPARAM)ACCEL_EXECUTEALL,
						TBSTATE_ENABLED);	
		}
		break;

	default:
		assert(0);
		break;
	}
	
}
	
/**

*/
void  
EndExecute(MDIWindow * wnd, HWND hwnd, EXECUTEOPTION opt)
{
    MDIWindow* currentwnd = GetActiveWin();
	wnd->SetExecuting(wyFalse);
	
	//DEBUG_LOG("EndExecute::stop_query");
	
	wnd->m_stopquery     =    0;
	
	wnd->EnableWindowOnQueryExecution(wyTrue);

	/* enable all the tool buttons */
    if(wnd->m_hwnd == currentwnd->m_hwnd)
    {
	    pGlobals->m_pcmainwin->EnableToolButtonsAndCombo( 
                                pGlobals->m_pcmainwin->m_hwndtool,
                                pGlobals->m_pcmainwin->m_hwndsecondtool,
                                pGlobals->m_pcmainwin->m_hwndtoolcombo, wyTrue, wyTrue);
        wnd->EnableToolOnNoQuery();
        /* replace the correct icon with stop icon */
	    OnExecuteOptn(opt, wyFalse);

	    /* change the cursor to arrow mode */
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    }

	CustomTab_SetClosable(wnd->m_pctabmodule->m_hwnd, wyTrue, 1);
	/* no executeoption */
	wnd->m_execoption = INVALID;

	EnableWindow(hwnd, TRUE);
}

EditorBase::EditorBase(HWND hwnd)
{
	m_hwndparent    = hwnd;

	m_edit		    = wyFalse;
	m_isadvedit     = wyFalse;

	m_filename.SetAs("");

	m_save		    = wyFalse;
	m_isfilesave	= wyFalse;

	m_hfont		    = NULL;
	m_nonkey		= wyFalse;
	wpOrigProc		= NULL;
	m_findreplace   = NULL;
    m_isdiscardchange = wyFalse;
    m_stylebeforeac = 0;
    m_styleatac     = 0;
    m_acwithquotedidentifier = 0;

    m_nodeimage     = 0;
    m_dbname.SetAs("");

    // Chapter 9: Async SQL generation
    m_gen_thread = NULL;
    m_gen_trigger_line = 0;
    m_gen_insert_pos = 0;
    m_gen_has_streamed = false;
    InterlockedExchange(&m_gen_thread_running, 0);
}

EditorBase::~EditorBase ()
{
    // Chapter 9: Cleanup async SQL generation thread
    if (m_gen_thread) {
        // Wait for thread to finish
        WaitForSingleObject(m_gen_thread, 5000);
        CloseHandle(m_gen_thread);
        m_gen_thread = NULL;
    }

    DestroyWindows();

}

void 
EditorBase::DestroyWindows()
{
	if(m_hwnd)
		DestroyWindow(m_hwnd);

	m_hwnd = NULL;

	if(m_hwndhelp) 
		DestroyWindow(m_hwndhelp);

	m_hwndhelp = NULL;

	if(m_hfont)
		DeleteFont(m_hfont);

	m_hfont = NULL;

	return;
}

void
EditorBase::Resize()
{
	RECT				rcmain; 
	RECT				rctabsplitter;
	wyInt32				hpos, vpos, width, height;
	
	VERIFY(GetWindowRect(m_hwndparent, &rcmain));
	VERIFY(GetWindowRect(m_pctabeditor->m_pcetsplitter->m_hwnd, &rctabsplitter));

	VERIFY(MapWindowPoints(NULL, m_hwndparent, (LPPOINT)&rcmain, 2));
	VERIFY(MapWindowPoints(NULL, m_hwndparent, (LPPOINT)&rctabsplitter, 2));
	
	rcmain.bottom = rctabsplitter.top;

	hpos	= rcmain.left;// + 5;

	vpos	= CustomTab_GetTabHeight(m_hwndparent);
	
	width	= rcmain.right;// - 10;
	
	height = 14;
	
    pGlobals->m_pcmainwin->m_connection->HandleHelp(m_hwndhelp, 
                                &hpos, &vpos, &width, &height, rcmain.bottom);

	height	=	rcmain.bottom - rcmain.top - vpos;

	VERIFY(MoveWindow(m_hwnd, hpos, vpos, width, height, FALSE));
    InvalidateRect(m_hwnd, NULL, TRUE);
	return;
}

wyBool
EditorBase::ShowResultWindow()
{
	wyInt32	      lstyle;
	wyUInt32	  menustate;
	wyUInt32	  ret;
	HMENU	      hmenu, hsubmenu;

	TabMgmt				*ptabmgmt;	
	TabEditorSplitter	*ptesplitter;
	MDIWindow	*	pCQueryWnd =(MDIWindow*)GetActiveWin();

	if (pCQueryWnd->GetActiveTabEditor()->m_isresultwnd)
		return wyTrue;

	VERIFY(hmenu = GetMenu(pGlobals->m_pcmainwin->m_hwndmain));

	lstyle = GetWindowLongPtr(pCQueryWnd->m_hwnd, GWL_STYLE);

	if ((lstyle & WS_MAXIMIZE) && wyTheme::IsSysmenuEnabled(pCQueryWnd->m_hwnd))
		VERIFY(hsubmenu = GetSubMenu(hmenu, 2));
	else
		VERIFY(hsubmenu = GetSubMenu(hmenu, 1));

	menustate = GetMenuState(hsubmenu, IDC_EDIT_SHOWRESULT, MF_BYCOMMAND);

	ret = CheckMenuItem(hsubmenu, IDC_EDIT_SHOWRESULT, MF_BYCOMMAND | MF_UNCHECKED);
	pCQueryWnd->GetActiveTabEditor()->m_isresultwnd = wyTrue;

	ptesplitter = pCQueryWnd->GetActiveTabEditor()->m_pcetsplitter;
	
	if(ptesplitter->m_leftortoppercent == ptesplitter->m_lasttoppercent && 
           ptesplitter->m_leftortoppercent == 100 && 
           ptesplitter->m_lasttoppercent == 100)
        {
            ptesplitter->m_lasttoppercent = 50;
            ptesplitter->m_leftortoppercent = ptesplitter->m_lasttoppercent;
        }
		else
            ptesplitter->m_leftortoppercent = ptesplitter->m_lasttoppercent;
		
	pCQueryWnd->m_pctabmodule->Resize();

	ptabmgmt = pCQueryWnd->GetActiveTabEditor()->m_pctabmgmt;

	VERIFY(UpdateWindow(ptabmgmt->m_hwnd));
	//VERIFY(UpdateWindow(ptabmgmt->m_pcdataviewquery->m_hwndgrid));

    return wyTrue;
}



void
EditorBase::AddErrorMsg(Tunnel * tunnel, PMYSQL mysql, wyString& errorormsg, wyUInt32 timetaken)
{
	wyUInt32     errnum;
	wyString	 newerror;

	errnum = tunnel->mysql_errno(*mysql);

	/* it may happen that due to http error the errornumber is 0 so we return */
	if(0 == errnum)
		return;

	VERIFY(newerror.Sprintf(_("Error Code: %d\n%s\n(%lu ms taken)\n\n"), errnum, tunnel->mysql_error(*mysql), timetaken) < SIZE_1024);

	errorormsg.Add(newerror.GetString());
	
	return;
}

void
EditorBase::AddNonResultMsg (Tunnel * tunnel, PMYSQL mysql, wyString& errorormsg, wyUInt32 timetaken )
{
	wyUInt32		rowsret;
	wyString		newmsg;

	rowsret		=	(wyUInt32)tunnel->mysql_affected_rows(*mysql);

	VERIFY(newmsg.Sprintf(_("(%lu row(s) affected)\n(%lu ms taken)\n\n"), rowsret, timetaken ) < SIZE_1024);

	errorormsg.Add(newmsg.GetString());
	
	return;
}

void
EditorBase::AddResultMsg(Tunnel * tunnel, MYSQL_RES * myres, wyString& errorormsg, wyUInt32 timetaken)
{
	wyUInt32		rowsret;
	wyString		newmsg;

	rowsret		=	(wyUInt32)tunnel->mysql_num_rows(myres);

	VERIFY(newmsg.Sprintf(_("(%lu row(s) returned)\n(%d ms taken)\n\n"), rowsret, timetaken  ) < SIZE_128);

	errorormsg.SetAs(newmsg);
	
	return;
}

/*This function makes the selected text in the edit box into uppercase.*/
void
EditorBase::MakeSelUppercase ()
{
	SendMessage(m_hwnd, SCI_UPPERCASE, 0, 0);
}

/*This function makes the selected text in the edit box into lowercase.*/
void
EditorBase::MakeSelLowercase ()
{
	SendMessage(m_hwnd, SCI_LOWERCASE, 0, 0);	
}

void
EditorBase::CommentSel(wyBool isuncomment)
{
	wyInt32 i, p1, p2, line1, line2, j;
    wyBool  flag = wyFalse;

	p1 = SendMessage(m_hwnd, SCI_GETSELECTIONSTART, 0, 0);
    p2 = SendMessage(m_hwnd, SCI_GETSELECTIONEND, 0, 0);

    if(p1 - p2 == 0)
    {
        p1 = SendMessage(m_hwnd, SCI_GETCURRENTPOS, 0, 0);
        p1 = SendMessage(m_hwnd, SCI_LINEFROMPOSITION, (WPARAM)p1, 0);
        p2 = SendMessage(m_hwnd, SCI_GETLINEENDPOSITION, (WPARAM)p1, 0);
        p1 = SendMessage(m_hwnd, SCI_POSITIONFROMLINE, (WPARAM)p1, 0);
        flag = wyTrue;
    }

    line1 = SendMessage(m_hwnd, SCI_LINEFROMPOSITION, (WPARAM)p1, 0);
    line2 = SendMessage(m_hwnd, SCI_LINEFROMPOSITION, (WPARAM)p2, 0);

    if(SendMessage(m_hwnd, SCI_POSITIONFROMLINE, (WPARAM)line2, 0) == p2 && line2 - line1 != 0 )
    {
        line2--;
    }

    SendMessage(m_hwnd, SCI_BEGINUNDOACTION, 0, 0);

    for(i = line1; i <= line2; ++i)
    {
        if(flag == wyTrue)
        {
            p1 = SendMessage(m_hwnd, SCI_POSITIONFROMLINE, (WPARAM)i, 0);
        }

        if(isuncomment == wyFalse)
        {
            SendMessage(m_hwnd, SCI_INSERTTEXT, (WPARAM)p1, (LPARAM)"-- ");
        }
        else
        {
            p2 = SendMessage(m_hwnd, SCI_GETLINEENDPOSITION, (WPARAM)i, 0);

            for(j = p1; j <= p2 && SendMessage(m_hwnd, SCI_GETSTYLEAT, (WPARAM)j, 0) != SCE_MYSQL_COMMENTLINE; ++j);

            if(SendMessage(m_hwnd, SCI_GETCHARAT, (WPARAM)j, 0) == '-')
            {
                SendMessage(m_hwnd, SCI_SETSEL, (WPARAM)j, (LPARAM)j + 3);
                SendMessage(m_hwnd, SCI_REPLACESEL, 0, (LPARAM)"");
            }
        }

        flag = wyTrue;
    }

    SendMessage(m_hwnd, SCI_ENDUNDOACTION, 0, 0);
}

void 
EditorBase::GetCompleteText(wyString &query)
{
	wyUInt32		nstrlen;
	wyChar			*data;

	nstrlen = SendMessage(m_hwnd, SCI_GETTEXTLENGTH, 0, 0);

	data = AllocateBuff(nstrlen + 1);

	SendMessage(m_hwnd, SCI_GETTEXT, (WPARAM)nstrlen+1, (LPARAM)data);

	query.SetAs(data);

	free(data);

	return;
}

void 
EditorBase::GetCompleteTextByPost(wyString &query, MDIWindow *wnd)
{
	wyUInt32		nstrlen;
	wyChar			*data;

	THREAD_MSG_PARAM tmp = {0};

    //set the lparam sent
    
	if(GetWindowThreadProcessId(m_hwnd , NULL) == GetCurrentThreadId())
    {
        nstrlen = SendMessage(m_hwnd, SCI_GETTEXTLENGTH, 0, 0);
		data = AllocateBuff(nstrlen + 1);
		SendMessage(m_hwnd, SCI_GETTEXT, (WPARAM)nstrlen+1, (LPARAM)data);
		query.SetAs(data);

		free(data);
    }
    else
    {
		if(WaitForSingleObject(pGlobals->m_pcmainwin->m_sqlyogcloseevent, 0) != WAIT_OBJECT_0 )
		{
			query.SetAs("");
			return;
		}
		tmp.m_lparam = (LPARAM)&query;
		tmp.m_hevent = CreateEvent(NULL, TRUE, FALSE, NULL);

		//now post the message to ui thread and wait for the event to be set
		PostMessage(wnd->GetHwnd(), UM_GETEDITORTEXT, (WPARAM)this->GetHWND(), (LPARAM)&tmp);
		if(WaitForSingleObject(tmp.m_hevent, 10000) == WAIT_TIMEOUT)
		{
			//CloseHandle(tmp.m_hevent);
			query.SetAs("");
			//return;
		}
		//WaitForSingleObject(tmp.m_hevent, INFINITE);


		//close the event handle
		CloseHandle(tmp.m_hevent);
		tmp.m_hevent = NULL;
		//data = (wyChar*)tmp.m_lparam;
		
	}
	

	return;
}



void
EditorBase::PasteData()
{
	wyUInt32  start, end;
	HGLOBAL   hglb; 
    LPWSTR    lptstr;
	wyString  copystr;

	// now if the focus is not on the edit control then we dont do anything.
	if(GetFocus () != m_hwnd)
		SetFocus(m_hwnd);

    if(!OpenClipboard(m_hwnd)) 
    {
        return; 
    }

    hglb = GetClipboardData(CF_UNICODETEXT);  

	if(hglb != NULL) 
    { 
        end = ::SendMessage(m_hwnd,SCI_GETSELECTIONEND,0,0);
	    start = ::SendMessage(m_hwnd,SCI_GETSELECTIONSTART,0,0);

		lptstr = (wyWChar*)GlobalLock(hglb);

		if(lptstr != NULL) 
		{	
			wyUInt32	len = (wcslen(lptstr) + start) ;
			copystr.SetAs(lptstr);
			::SendMessage(m_hwnd, SCI_REPLACESEL, (WPARAM)TRUE, (LPARAM)copystr.GetString());
			GlobalUnlock(hglb); 

			::SendMessage(m_hwnd, SCI_SETSEL, (WPARAM)len, (LPARAM)len);
			EditorFont::SetLineNumberWidth(m_hwnd);

			if(copystr.GetLength())
				m_edit = wyTrue;          // ontabclosing display messagebox based on it
		} 
	} 
	
	CloseClipboard();
	return;
	
}

wyBool
EditorBase::SelectFirstTableToEdit(wyBool isedit)
{	
	MDIWindow		*wnd = GetActiveWin();
    wyString        currenttable;
    ResultView*     presultview;

    if(!wnd || !wnd->GetActiveTabEditor() || 
		!wnd->GetActiveTabEditor()->m_pctabmgmt || 
        !(presultview = wnd->GetActiveTabEditor()->m_pctabmgmt->m_presultview))
    {
        return wyFalse;
    }
    
    presultview->SelectTableComboItem(isedit == wyTrue ? 1 : 0);
	return wyTrue;
}



void
EditorBase::SetTabEditorptr(TabEditor *te)
{
	m_pctabeditor = te;

	return;
}


TabEditor *
EditorBase::GetParentPtr()
{
	return (m_pctabeditor);
}

wyInt32
EditorBase::OnContextMenuHelper(LPARAM lParam)
{
	wyBool	    nmenuselect;
	LONG	    lstyle=0;
	HMENU	    hmenu, htrackmenu;
	POINT	    pnt;
	wyInt32		pos;
    RECT        rect;
    MDIWindow*  wnd = GetActiveWin();

	VERIFY(hmenu = GetMenu(pGlobals->m_pcmainwin->m_hwndmain));

	//lStyle = GetWindowLongPtr ( m_hwndparent, GWL_STYLE );
	lstyle = GetWindowLongPtr(GetParent(m_hwndparent), GWL_STYLE);

    if ((lstyle & WS_MAXIMIZE) && wyTheme::IsSysmenuEnabled(GetParent(m_hwndparent)))
		VERIFY(htrackmenu =	GetSubMenu(hmenu, 2));
	else
		VERIFY(htrackmenu =	GetSubMenu(hmenu, 1));

	//If we are pressing the context button,then lParam is -1.
	if(lParam == -1)
	{		
		//for getting the current cursor pos.
		pos = SendMessage(m_hwnd, SCI_GETCURRENTPOS, 0, 0);
		pnt.x = SendMessage(m_hwnd, SCI_POINTXFROMPOSITION, 0, pos) ; 
		pnt.y = SendMessage(m_hwnd, SCI_POINTYFROMPOSITION, 0, pos); 
		VERIFY(ClientToScreen(m_hwnd, &pnt));
	}
	else
	{
		pnt.x = GET_X_LPARAM(lParam); 
		pnt.y = GET_Y_LPARAM(lParam); 
	}

    GetClientRect(m_hwnd, &rect);
    MapWindowPoints(m_hwnd, NULL, (LPPOINT)&rect, 2);

    if(!PtInRect(&rect, pnt))
    {
        return -1;
    }

	//VERIFY(ClientToScreen(m_hwnd, &pnt));
    SetFocus(m_hwnd);

	// Now change the menu item.
	ChangeEditMenuItem(htrackmenu);

    pGlobals->m_pcmainwin->m_connection->HandleTagsMenu((HMENU)hmenu);

    if(wnd)
    {
        //FrameWindow::RecursiveMenuEnable(htrackmenu, wyFalse, MF_ENABLED);

        if(wnd->m_executing == wyTrue || wnd->m_pingexecuting == wyTrue)
        {
            FrameWindow::RecursiveMenuEnable(htrackmenu, wyFalse, MF_DISABLED);
        }

        // Append AI menu items (only if not already added)
        if (GetMenuState(htrackmenu, ID_AI_ANALYZE, MF_BYCOMMAND) == (UINT)-1) {
            AppendMenu(htrackmenu, MF_SEPARATOR, 0, NULL);
            // Use Chinese menu text if language is Chinese
            const char* langcode = GetL10nLangcode();
            bool isChinese = (langcode && (strcmp(langcode, "zh-cn") == 0 || strcmp(langcode, "zh") == 0));
            if (isChinese) {
                AppendMenu(htrackmenu, MF_STRING, ID_AI_ANALYZE,  L"AI 分析(&A)");
                AppendMenu(htrackmenu, MF_STRING, ID_AI_BEAUTIFY, L"AI 美化(&B)");
            } else {
                AppendMenu(htrackmenu, MF_STRING, ID_AI_ANALYZE,  _(L"AI &Analyze"));
                AppendMenu(htrackmenu, MF_STRING, ID_AI_BEAUTIFY, _(L"AI &Beautify"));
            }
        }

        wyTheme::SetMenuItemOwnerDraw(htrackmenu);
	    nmenuselect = (wyBool)TrackPopupMenu(htrackmenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pnt.x, pnt.y, 0, pGlobals->m_pcmainwin->m_hwndmain, NULL);
    }
    return 1;
}

VOID		
EditorBase::SetAdvancedEditor(wyBool val)
{
	m_isadvedit = val;
}

wyBool		
EditorBase::GetAdvancedEditor()
{
	return(m_isadvedit);
}

wyBool		
EditorBase::ShowToolTip(MDIWindow	*wnd)
{
	return wyTrue;
}

wyBool		
EditorBase::IsComment(HWND hwndeditor, wyBool isfunc, wyInt32 position)
{
	wyInt32 pos, style;

    if(position == -1)
    {
        pos = SendMessage(hwndeditor, SCI_GETCURRENTPOS, 0, 0);
	pos = (isfunc) ? (pos - 2) : (pos - 1);
    }
    else
    {
        pos = position;
    }

	style = SendMessage(hwndeditor, SCI_GETSTYLEAT, pos, 0);

	if(style == SCE_MYSQL_COMMENT || 
	   style == SCE_MYSQL_COMMENTLINE || 
       style == SCE_MYSQL_HIDDENCOMMAND ||
	   style == SCE_MYSQL_SQSTRING ||
       style == SCE_MYSQL_DQSTRING)
    {	   
	     return wyTrue;
    }
	
	return wyFalse;
}

wyInt32
EditorBase::OnWMKeyUp(HWND hwnd, WPARAM wparam)
{
    MDIWindow *wnd;

    wnd = GetParentPtr()->GetParentPtr()->GetParentPtr();
	EditorFont::SetLineNumberWidth(m_hwnd);
    PostMessage(wnd->m_pctabmodule->m_hwnd, UM_SETSTATUSLINECOL, (WPARAM)hwnd, 1);
 
    if(wnd->m_acinterface->HandlerOnWMKeyUp(hwnd, this, wparam) == 1)
    {
        m_nonkey= wyTrue;
    }

    return 1;
}

wyInt32
EditorBase::OnWMChar(HWND hwnd, WPARAM wparam, MDIWindow *wnd, EditorBase *ebase)
{
	wyInt32 state = 0;

	state = GetKeyState(VK_CONTROL);
	if(state & 0x8000)
	{
		state = GetKeyState('C');

		if(state & 0x8000) //COPY
		{
			CopyStyledTextToClipBoard(hwnd);
			return 0;
		}			

		state = GetKeyState('X'); //CUT
		if(state & 0x8000)
		{
			CopyStyledTextToClipBoard(hwnd);
			//cut is doing by copying text same as copy to clipboard process (copy process)
			//and finally replacing the selected text with empty string.
			SendMessage(hwnd, SCI_REPLACESEL, 0, (LPARAM)""); 
			return 0;			
		}	
	}		

    state = GetKeyState(VK_MENU);
    {
        if(state & 0x8000)
	    {
		    state = GetKeyState('C');

		    if(state & 0x8000) //COPY WITHOUT FORMATTING
		    {
                CopyWithoutWhiteSpaces(hwnd);
                return 0;	
            }
        }
    }

	if(wnd->m_acinterface->HandlerOnWMChar(hwnd, ebase, wparam) == 1)
    {
        ebase->m_nonkey = wyTrue;
        return 1;
    }
    ebase->m_nonkey = wyFalse;

	return -1;
}

void
EditorBase::SetScintillaValues(HWND hwndedit)
{   
	/* XPM */
	static  wyChar *keyword_xpm[] = KEYWORD_XPM;
    static  wyChar *function_xpm[] = FUNCTION_XPM;
    static  wyChar *database_xpm[] = DATABASE_XPM;
    static  wyChar *table_xpm[] = TABLE_XPM;
    static  wyChar *field_xpm[] = FIELD_XPM;
    static  wyChar *sp_xpm[] = SP_XPM;
    static  wyChar *func_xpm[] = FUNC_XPM;
	static  wyChar *alias_xpm[] = ALIAS_XPM;

    SendMessage(m_hwnd, SCI_SETLEXERLANGUAGE, 0, (LPARAM)"MySQL");
	SendMessage(m_hwnd, SCI_USEPOPUP, 0, 0);
    SendMessage(hwndedit, SCI_CLEARREGISTEREDIMAGES, 0, 0);
	SendMessage(hwndedit, SCI_REGISTERIMAGE, 8, (LPARAM)alias_xpm);
	SendMessage(hwndedit, SCI_REGISTERIMAGE, 7, (LPARAM)func_xpm);
	SendMessage(hwndedit, SCI_REGISTERIMAGE, 6, (LPARAM)sp_xpm);
	SendMessage(hwndedit, SCI_REGISTERIMAGE, 5, (LPARAM)field_xpm);
	SendMessage(hwndedit, SCI_REGISTERIMAGE, 4, (LPARAM)table_xpm);
	SendMessage(hwndedit, SCI_REGISTERIMAGE, 3, (LPARAM)database_xpm);
	SendMessage(hwndedit, SCI_REGISTERIMAGE, 2, (LPARAM)function_xpm);
	SendMessage(hwndedit, SCI_REGISTERIMAGE, 1, (LPARAM)keyword_xpm);

    if(IsInsertSpacesForTab() == wyTrue)
        SendMessage(hwndedit,SCI_SETUSETABS, FALSE, 0);
    else
        SendMessage(hwndedit,SCI_SETUSETABS, TRUE, 0);

    SendMessage(hwndedit, SCI_SETTABWIDTH, GetTabSize(), 0 );
    SendMessage(hwndedit, SCI_AUTOCSETIGNORECASE, TRUE, 0);
	SendMessage(hwndedit, SCI_AUTOCSETSEPARATOR, (LPARAM)'\n', 0);
	SendMessage(hwndedit, SCI_AUTOCSETAUTOHIDE, 0, 0);
	SendMessage(hwndedit, SCI_AUTOCSTOPS, 0, (LPARAM)" ~`!@#$%^&*()+|\\=-?><,/\":;'{}[]");
	
	SendMessage(hwndedit, SCI_AUTOCSETTYPESEPARATOR, (WPARAM)'?', 0);
	SendMessage(hwndedit, SCI_CALLTIPSETBACK, RGB(255, 255, 225), 0);
	SendMessage(hwndedit, SCI_CALLTIPSETFORE, RGB(0, 0, 0), 0);
    SendMessage(hwndedit, SCI_CALLTIPSETFOREHLT, RGB(255, 0, 0), 0);
    SendMessage(hwndedit, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
    
    //Increase the Default Autocomplete Window Height
	SendMessage(hwndedit, SCI_AUTOCSETMAXHEIGHT, 15, 0);
    SetParanthesisHighlighting(hwndedit);

    if(pGlobals->m_pcmainwin->m_editorcolumnline > 0)
    {
        SendMessage(hwndedit, SCI_SETEDGEMODE, EDGE_LINE, 0);
        SendMessage(hwndedit, SCI_SETEDGECOLUMN, pGlobals->m_pcmainwin->m_editorcolumnline, 0);
    }
}

//function to create the richedit window.
HWND
EditorBase::CreateEditor(MDIWindow * wnd, HWND hwnd)
{
	wyUInt32	exstyles =  0;
	wyUInt32	styles   =  WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL;
	HWND	    hwndedit;
	
	VERIFY(hwndedit	= ::CreateWindowEx(exstyles, L"Scintilla", L"Source", styles, 0, 0, 0, 0,
                        hwnd, (HMENU)IDC_QUERYEDIT, pGlobals->m_pcmainwin->GetHinstance(), this));

	m_hwnd = hwndedit;

    /*set the lexer language*/
	SetScintillaValues(hwndedit);
    
    EnableFolding(hwndedit);

    wpOrigProc = (WNDPROC)SetWindowLongPtr(m_hwnd, GWLP_WNDPROC, (LONG_PTR)EditorBase::WndProc);	
	SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

	// Change the font.
	EditorFont::FormatEditor(hwndedit, wyTrue, wnd->m_keywordstring, 
                             wnd->m_functionstring);

	//Improves the slowness problem while typing editor(if it already load big file like 1 MB)
	SendMessage(hwndedit, SCI_SETLAYOUTCACHE, SC_CACHE_PAGE, 0);
    SendMessage(hwndedit, SCI_SETSCROLLWIDTHTRACKING, (WPARAM)1, 0);
    SendMessage(hwndedit, SCI_SETSCROLLWIDTH, 10, 0);
    //SendMessage(hwndedit, SCI_SETFONTQUALITY, (WPARAM)SC_EFF_QUALITY_LCD_OPTIMIZED, 0);

	EditorFont::SetLineNumberWidth(hwndedit);

    VERIFY(m_hwndhelp = CreateWindow(L"STATIC", L"", WS_CHILD | SS_WORDELLIPSIS, 0, 0, 0, 0,
							        hwnd, (HMENU)IDC_CAPTION,
									pGlobals->m_pcmainwin->GetHinstance(),
									NULL));

	SetFont();
	ShowWindow(m_hwndhelp, wyFalse);

	return hwndedit;
}

void
EditorBase::SetFont()
{
	/// For the file name and path printed on the top of the tab
    HDC	        hdc = GetDC(m_hwndhelp);
	wyInt32     fontheight = -MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72);

	m_hfont = CreateFont(fontheight, 0, 0, 0,
		        FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, L"Verdana");
	
	ReleaseDC(m_hwndhelp, hdc);
	::SendMessage(m_hwndhelp, WM_SETFONT, (WPARAM)m_hfont, TRUE);

	/// For the contents of the tab
	EditorFont::SetFont(m_hwnd, "EditFont", wyTrue);

    return;	
}

wyInt32
EditorBase::ExecuteQueryThread(wyString query, wyInt32 *stop, MDIWindow *wnd, wyInt32& curline, wyBool isanalyze)
{
	wyInt32			 start=0, end=0, *err = 0;
	QueryThread		 thd;
	HANDLE			 evt;
	QueryResultList	 *list = NULL;
	wyString         *str = new wyString;
    wyString        *querystr;
	PMYSQL			tmpmysql;

    err = new wyInt32;
	*err = 0;
    querystr   = new wyString();
    querystr->SetAs(query.GetString());

	wnd->m_stopmysql=  wnd->m_mysql;

    tmpmysql = &wnd->m_stopmysql;
   		
    QUERYTHREADPARAMS *param = new QUERYTHREADPARAMS;
    list = new QueryResultList;

    param->startpos = start; 
    param->endpos = end; 
    param->linenum = curline;
    param->executestatus = EXECUTE_ALL;
	param->query = querystr;
    param->stop = stop; 
    param->list = list; 
    param->str = str;
	param->tab = wnd->GetActiveTabEditor()->m_pctabmgmt; 
    param->tunnel = wnd->m_tunnel; 
    param->mysql = &wnd->m_mysql; 
	param->error = err;	
    param->isadvedit = m_isadvedit; 
    param->lpcs = &wnd->m_cs;
    param->wnd  = wnd;
	param->isprofile = wyTrue;
	param->m_highlimitvalue = -1;
	param->m_lowlimitvalue = -1;
	param->m_iseditor = isanalyze;
	param->executeoption = ALL;
	param->isedit = wyFalse;
	param->tmpmysql = tmpmysql; 
	param->isexplain = wyFalse;

    InitializeExecution(param);

   	evt = thd.Execute(param);
	return 1;
}

// Function to implement context menu of the edit box.
wyInt32
EditorBase::OnContextMenu(LPARAM lParam)
{
	return OnContextMenuHelper(lParam);
}
	
// Subclass procedure for the edit box.
LRESULT	CALLBACK 
EditorBase::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) 
{
	EditorBase *ebase = (EditorBase*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    MDIWindow   *wnd = NULL;
	wyInt32		ret = 0;
		
	static wyChar	keyword[128];
	static wyBool	keyflag;
	static wyInt32  wrdlen = 0;

    VERIFY(wnd = GetActiveWin());
				
    //if message is from find replace dialog - pass it to the right handler
	//forced type cast
	if(message == (wyUInt32)pGlobals->m_pcmainwin->m_findmsg) 
	{		
		if( ebase->m_findreplace->FindReplace(hwnd, lparam) == wyFalse)
		{
            //when we are closing the find dialog, deleting the memory allocated for Find
			delete(ebase->m_findreplace);
			ebase->m_findreplace = NULL;
		}
		return 0;		
	}    
	
	switch(message)	
	{
	    case WM_HELP:
            ShowHelp("http://sqlyogkb.webyog.com/article/45-sql-window");
		    return wyTrue;

	    case WM_CONTEXTMENU:
            if(wnd->GetActiveTabEditor())
		    {
			    //CustomGrid_ApplyChanges(wnd->GetActiveTabEditor()->m_pctabmgmt->m_insert->m_hwndgrid, wyTrue);
		    }

            if(ebase->OnContextMenu(lparam) == 1)
            {
                return 1;
            }
		    break;

	    case WM_KEYUP:
	        ebase->OnWMKeyUp(hwnd, wparam);
            break;
		
	    case WM_KEYDOWN:
		    {
			    //This needs to work Ctrl+X to be handled in WM_CHAR, if it wont return '0', that wont process as expected

			    if(GetKeyState(VK_CONTROL) & 0x8000)
			    {
				    if(wparam == 'X')
					    return 0;

				    if(wparam == VK_INSERT)
				    {
					    CopyStyledTextToClipBoard(hwnd);
					    return 0;
				    }

				    // Chapter 9: Ctrl+Enter to trigger comment-to-SQL generation
				    if(wparam == VK_RETURN && pGlobals && pGlobals->m_aiconfig.comment_trigger_enabled)
				    {
					    int cursor_pos = (int)SendMessage(hwnd, SCI_GETCURRENTPOS, 0, 0);
					    if(ebase->CheckAITrigger(cursor_pos))
					    {
						    int line = (int)SendMessage(hwnd, SCI_LINEFROMPOSITION, cursor_pos, 0);
						    wyString comment;
						    ebase->ExtractCommentBlock(line, comment);
						    if(comment.GetLength() > 0)
						    {
							    // Get schema from AutoCompleteInterface if available
							    wyString schemaSummary;
							    if(wnd && wnd->m_acinterface && wnd->m_acinterface->m_community_ac)
							    {
								    wnd->m_acinterface->m_community_ac->GetSchemaSummary(schemaSummary);
							    }

							    // Start async SQL generation
							    ebase->StartSQLGeneration(
								    comment.GetString(),
								    schemaSummary.GetString(),
								    line
							    );
						    }
						    return 1;
					    }
				    }
			    }

			    if(wnd->m_acinterface->HandlerOnWMKeyDown(hwnd, ebase, wparam))
				    return 1;
		    }
		    break;

        case WM_SYSKEYDOWN:
             if(lparam >> 29)
            {
                //check whether it is insert/delete
                if (wparam == 'C' || wparam == 'c')//switch(wparam)
                {
                    ret = ebase->OnWMChar(hwnd, wparam, wnd, ebase);
                }
            }
             break;

        case WM_SETFOCUS:
            if(wnd)
            {
                PostMessage(wnd->m_pctabmodule->m_hwnd, UM_SETSTATUSLINECOL, (WPARAM)hwnd, 1);
            }

            break;

        case WM_KILLFOCUS:
            if(wnd)
            {
                PostMessage(wnd->m_pctabmodule->m_hwnd, UM_SETSTATUSLINECOL, (WPARAM)NULL, 0);
            }

            break;		
		
	    case WM_LBUTTONUP:
		    ebase->OnLButtonUp(wnd,hwnd);
		    break;

	    case UM_FOCUS:
		    ::SendMessage(hwnd, SCI_GRABFOCUS, 0, 0);
		    break;

	    case WM_CHAR:

		    ret = ebase->OnWMChar(hwnd, wparam, wnd, ebase);

		    if(ret == -1)
			    break;

		    return ret;		

        case UM_ADDBACKQUOTEONAC:
            if(lparam & 1)
            {
                SendMessage(hwnd, SCI_INSERTTEXT, wparam, (LPARAM)"`");
            }

            if(lparam & 2)
            {
               SendMessage(hwnd, SCI_REPLACESEL, (WPARAM)0, (LPARAM)"`");
            }

            SendMessage(hwnd, SCI_ENDUNDOACTION, 0, 0);
            break;

        // Chapter 9: Handle streamed SQL generation token
        case UM_SQL_GEN_TOKEN:
        {
            wyString* pToken = (wyString*)lparam;
            if (pToken) {
                ebase->OnSQLGenerationToken(pToken->GetString());
            }
            delete pToken;
            return 0;
        }

        // Chapter 9: Handle SQL generation completion
        case UM_SQL_GEN_COMPLETE:
        {
            wyString* pResult = (wyString*)lparam;
            bool success = (wparam == 1);
            if (pResult) {
                ebase->OnSQLGenerationComplete(pResult->GetString(), success);
            }
            delete pResult;
            return 0;
        }
	}

	return CallWindowProc(ebase->wpOrigProc, hwnd, message, wparam, lparam);
}

void
EditorBase::OnLButtonUp(MDIWindow *wnd, HWND hwnd)
{
    EditorFont::SetLineNumberWidth(this->m_hwnd );
    PostMessage(wnd->m_pctabmodule->m_hwnd, UM_SETSTATUSLINECOL, (WPARAM)hwnd, 1);
	
    if(wnd->GetActiveTabEditor())
    {	
		SetFocus(hwnd);
	}
}

//Setting indentation in editor
wyBool
EditorBase::SetAutoIndentation(HWND hwnd, WPARAM wparam)
{
	wyInt32		curline, prevlinelength, curpos, pos;
	wyChar		*prevlinedata = NULL;
	wyString	strdata;

	//if it is a enter key
	if(LOWORD(wparam) == VK_RETURN)
	{
		// for getting the current line in the editor
		curpos		= SendMessage(hwnd, SCI_GETCURRENTPOS, 0, 0);
		curline		= SendMessage(hwnd, SCI_LINEFROMPOSITION, curpos, 0);

		if(curline  >  0 )
		{
			prevlinelength  = SendMessage(hwnd, SCI_LINELENGTH,  curline - 1 , 0);
			prevlinedata = AllocateBuff(prevlinelength + 1);
			SendMessage(hwnd, SCI_GETLINE,(WPARAM)curline - 1,(LPARAM)prevlinedata);
			prevlinedata[prevlinelength ]  =  '\0';				

			for(pos = 0; prevlinedata[pos]; pos++)  
			{
				//if it is not a space or tab charater then we will replace that charater with '\0' 
				if(prevlinedata[pos] != ' ' && prevlinedata[pos] != '\t')
					prevlinedata[pos] = '\0';
			}

			strdata.SetAs(prevlinedata);
            SendMessage(hwnd, SCI_REPLACESEL, 0, (LPARAM)strdata.GetString());    
			
			if(prevlinedata)
				 free(prevlinedata);

			return wyTrue;
		}
	}

	return wyFalse;
}

// Chapter 9: Check if current line contains AI trigger word
bool EditorBase::CheckAITrigger(int cursor_pos)
{
    HWND hwnd = m_hwnd;

    // Get current line
    int line = (int)SendMessage(hwnd, SCI_LINEFROMPOSITION, cursor_pos, 0);
    int line_start = (int)SendMessage(hwnd, SCI_POSITIONFROMLINE, line, 0);
    int line_end = (int)SendMessage(hwnd, SCI_GETLINEENDPOSITION, line, 0);

    // Get current line text
    int line_len = line_end - line_start;
    if (line_len <= 0 || line_len > 1000) return false;

    char* line_text = new char[line_len + 1];
    struct TextRange tr;
    tr.chrg.cpMin = line_start;
    tr.chrg.cpMax = line_end;
    tr.lpstrText = line_text;
    SendMessage(hwnd, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
    line_text[line_len] = '\0';

    // Check if line contains trigger word
    bool found = false;
    if (pGlobals && pGlobals->m_aiconfig.comment_trigger_enabled) {
        const char* trigger = pGlobals->m_aiconfig.comment_trigger_word.GetString();
        if (trigger && trigger[0]) {
            const char* pos = strstr(line_text, trigger);
            if (pos) {
                // Ensure trigger is in a comment (preceded by -- or // or /*)
                const char* comment_start = line_text;
                while (*comment_start == ' ' || *comment_start == '\t') comment_start++;

                if (strncmp(comment_start, "--", 2) == 0 ||
                    strncmp(comment_start, "//", 2) == 0 ||
                    strncmp(comment_start, "/*", 2) == 0) {
                    found = true;
                }
            }
        }
    }

    delete[] line_text;
    return found;
}

// Chapter 9: Extract comment block above trigger line
void EditorBase::ExtractCommentBlock(int trigger_line, wyString& result)
{
    HWND hwnd = m_hwnd;
    result.Clear();

    // Scan upward, collecting consecutive comment lines
    int line = trigger_line - 1;
    while (line >= 0) {
        int line_start = (int)SendMessage(hwnd, SCI_POSITIONFROMLINE, line, 0);
        int line_end = (int)SendMessage(hwnd, SCI_GETLINEENDPOSITION, line, 0);
        int line_len = line_end - line_start;

        if (line_len <= 0 || line_len > 1000) break;

        char* line_text = new char[line_len + 1];
        struct TextRange tr;
        tr.chrg.cpMin = line_start;
        tr.chrg.cpMax = line_end;
        tr.lpstrText = line_text;
        SendMessage(hwnd, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
        line_text[line_len] = '\0';

        // Trim leading whitespace
        char* trimmed = line_text;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        // Check if it's a comment line
        bool is_comment = false;
        if (strncmp(trimmed, "--", 2) == 0 ||
            strncmp(trimmed, "//", 2) == 0 ||
            strncmp(trimmed, "/*", 2) == 0 ||
            strncmp(trimmed, "*", 1) == 0) {
            is_comment = true;
        }

        if (!is_comment) {
            delete[] line_text;
            break;
        }

        // Extract comment content (remove comment markers)
        const char* content = trimmed;
        if (strncmp(content, "--", 2) == 0 || strncmp(content, "//", 2) == 0) {
            content += 2;
        } else if (strncmp(content, "/*", 2) == 0) {
            content += 2;
        } else if (*content == '*') {
            content += 1;
        }

        // Trim leading whitespace
        while (*content == ' ' || *content == '\t') content++;

        // Remove trailing */ if present
        char* end_marker = const_cast<char*>(strstr(content, "*/"));
        if (end_marker) *end_marker = '\0';

        // Add to result (prepend to maintain order)
        wyString line_str;
        line_str.SetAs(content);
        if (result.GetLength() > 0) {
            line_str.Add("\n");
            line_str.Add(result.GetString());
        }
        result.SetAs(line_str.GetString());

        delete[] line_text;
        line--;
    }
}

// Chapter 9: Insert generated SQL below trigger line
void EditorBase::InsertGeneratedSQL(const char* sql, int trigger_line)
{
    HWND hwnd = m_hwnd;

    int next_line = trigger_line + 1;
    int insert_pos;

    int max_line = (int)SendMessage(hwnd, SCI_GETLINECOUNT, 0, 0);
    if (next_line >= max_line) {
        insert_pos = (int)SendMessage(hwnd, SCI_GETTEXTLENGTH, 0, 0);
        SendMessage(hwnd, SCI_INSERTTEXT, insert_pos, (LPARAM)"\n");
        insert_pos++;
    } else {
        insert_pos = (int)SendMessage(hwnd, SCI_POSITIONFROMLINE, next_line, 0);
    }

    SendMessage(hwnd, SCI_BEGINUNDOACTION, 0, 0);
    SendMessage(hwnd, SCI_INSERTTEXT, insert_pos, (LPARAM)sql);
    SendMessage(hwnd, SCI_ENDUNDOACTION, 0, 0);

    int new_pos = insert_pos + (int)strlen(sql);
    SendMessage(hwnd, SCI_GOTOPOS, new_pos, 0);
}

// Chapter 9: Show loading placeholder below trigger line
void EditorBase::ShowCommentLoadingPlaceholder(int trigger_line)
{
    HWND hwnd = m_hwnd;

    int next_line = trigger_line + 1;
    int insert_pos;

    int max_line = (int)SendMessage(hwnd, SCI_GETLINECOUNT, 0, 0);
    if (next_line >= max_line) {
        insert_pos = (int)SendMessage(hwnd, SCI_GETTEXTLENGTH, 0, 0);
        SendMessage(hwnd, SCI_INSERTTEXT, insert_pos, (LPARAM)"\n");
        insert_pos++;
    } else {
        insert_pos = (int)SendMessage(hwnd, SCI_POSITIONFROMLINE, next_line, 0);
    }

    const char* placeholder = "-- \xe6\xad\xa3\xe5\x9c\xa8\xe7\x94\x9f\xe6\x88\x90 SQL...";  // "-- 正在生成 SQL..."
    SendMessage(hwnd, SCI_INSERTTEXT, insert_pos, (LPARAM)placeholder);
}

// Chapter 9: Remove loading placeholder below trigger line
void EditorBase::RemoveCommentLoadingPlaceholder(int trigger_line)
{
    HWND hwnd = m_hwnd;

    int next_line = trigger_line + 1;
    int max_line = (int)SendMessage(hwnd, SCI_GETLINECOUNT, 0, 0);
    if (next_line >= max_line) return;

    int line_start = (int)SendMessage(hwnd, SCI_POSITIONFROMLINE, next_line, 0);
    int line_end = (int)SendMessage(hwnd, SCI_GETLINEENDPOSITION, next_line, 0);
    int line_len = line_end - line_start;

    if (line_len <= 0 || line_len > 100) return;

    char* line_text = new char[line_len + 1];
    struct TextRange tr;
    tr.chrg.cpMin = line_start;
    tr.chrg.cpMax = line_end;
    tr.lpstrText = line_text;
    SendMessage(hwnd, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
    line_text[line_len] = '\0';

    // Check if it's the placeholder
    if (strstr(line_text, "\xe6\xad\xa3\xe5\x9c\xa8\xe7\x94\x9f\xe6\x88\x90 SQL...") != NULL) {
        SendMessage(hwnd, SCI_SETTARGETSTART, line_start, 0);
        SendMessage(hwnd, SCI_SETTARGETEND, line_end + 1, 0);  // +1 to include newline
        SendMessage(hwnd, SCI_REPLACETARGET, 0, (LPARAM)"");
    }

    delete[] line_text;
}

// Chapter 9: Show failure recovery message
void EditorBase::ShowGenerationFailureMessage(int trigger_line, const char* errorMsg)
{
    HWND hwnd = m_hwnd;

    // Remove loading placeholder first
    RemoveCommentLoadingPlaceholder(trigger_line);

    // Find position after trigger line
    int next_line = trigger_line + 1;
    int insert_pos;

    int max_line = (int)SendMessage(hwnd, SCI_GETLINECOUNT, 0, 0);
    if (next_line >= max_line) {
        insert_pos = (int)SendMessage(hwnd, SCI_GETTEXTLENGTH, 0, 0);
        SendMessage(hwnd, SCI_INSERTTEXT, insert_pos, (LPARAM)"\n");
        insert_pos++;
    } else {
        insert_pos = (int)SendMessage(hwnd, SCI_POSITIONFROMLINE, next_line, 0);
    }

    // Build failure message
    wyString failMsg;
    if (errorMsg && errorMsg[0]) {
        failMsg.Sprintf("-- \xe7\x94\x9f\xe6\x88\x90\xe5\xa4\xb1\xe8\xb4\xa5: %s", errorMsg);  // "-- 生成失败: ..."
    } else {
        failMsg.SetAs("-- \xe7\x94\x9f\xe6\x88\x90\xe5\xa4\xb1\xe8\xb4\xa5\xef\xbc\x8c\xe8\xaf\xb7\xe6\x8c\x89 Ctrl+Z \xe6\x92\xa4\xe9\x94\x80\xef\xbc\x8c\xe6\x88\x96\xe9\x87\x8d\xe6\x96\xb0\xe8\xbe\x93\xe5\x85\xa5\xe8\xa7\xa6\xe5\x8f\x91\xe8\xaf\x8d\xe9\x87\x8d\xe8\xaf\x95");  // "-- 生成失败，请按 Ctrl+Z 撤销，或重新输入触发词重试"
    }

    // Insert failure message
    SendMessage(hwnd, SCI_INSERTTEXT, insert_pos, (LPARAM)failMsg.GetString());
}

// Chapter 9: Start async SQL generation
void EditorBase::StartSQLGeneration(const char* comment, const char* schemaInfo, int trigger_line)
{
    // Check if thread is already running
    if (InterlockedCompareExchange(&m_gen_thread_running, 1, 0) == 1)
        return;

    // Store trigger line for completion handler
    m_gen_trigger_line = trigger_line;
    m_gen_insert_pos = 0;
    m_gen_has_streamed = false;

    // Show loading placeholder
    ShowCommentLoadingPlaceholder(trigger_line);

    // Build request
    wyString requestJson;
    AIService::BuildGenerateRequestJson(
        comment,
        schemaInfo,
        pGlobals->m_aimodel.GetString(),
        &requestJson
    );

    // Create thread parameter
    GenThreadParam* param = new GenThreadParam;
    param->pThis = this;
    param->requestJson.SetAs(requestJson.GetString());
    param->triggerLine = trigger_line;
    param->hwndEditor = m_hwnd;

    // Start background thread
    if (m_gen_thread) {
        CloseHandle(m_gen_thread);
        m_gen_thread = NULL;
    }
    m_gen_thread = (HANDLE)_beginthreadex(NULL, 0, SQLGenerationThreadProc, param, 0, NULL);
    if (!m_gen_thread) {
        InterlockedExchange(&m_gen_thread_running, 0);
        delete param;
        ShowGenerationFailureMessage(trigger_line, _("Failed to start AI generation thread"));
    }
}

// Stream callback for SQL generation - collect all tokens
struct GenStreamCtx {
    wyString result;
    HWND     hwndEditor;
};

static bool GenStreamCallback(const char* token, int len, void* ud)
{
    GenStreamCtx* ctx = (GenStreamCtx*)ud;
    if (!ctx)
        return false;

    if (token && len > 0) {
        if (ctx && ctx->hwndEditor) {
            wyString* pToken = new wyString();
            pToken->SetAs(token, len);
            BOOL posted = PostMessage(ctx->hwndEditor, UM_SQL_GEN_TOKEN, 0, (LPARAM)pToken);
            if (!posted)
                delete pToken;
        }

        // Create null-terminated copy since wyString::Add(ptr, len) is private
        char* buf = new char[len + 1];
        memcpy(buf, token, len);
        buf[len] = '\0';
        ctx->result.Add(buf);
        delete[] buf;
    }
    return true;  // continue reading
}

// Background thread for SQL generation
unsigned __stdcall EditorBase::SQLGenerationThreadProc(void* param)
{
    GenThreadParam* genParam = (GenThreadParam*)param;
    if (!genParam || !genParam->pThis) {
        delete genParam;
        return 0;
    }

    // Send AI request with streaming callback
    GenStreamCtx streamCtx;
    streamCtx.hwndEditor = genParam->hwndEditor;
    wyString errorBuf;
    DWORD timeoutMs = (pGlobals && pGlobals->m_aiconfig.error_analysis_timeout_ms > 0)
        ? (DWORD)pGlobals->m_aiconfig.error_analysis_timeout_ms
        : 60000;

    // Use the request JSON directly (stream: true)
    bool success = AIService::SendRequestStreaming(
        &pGlobals->m_aiconfig,
        genParam->requestJson.GetString(),
        GenStreamCallback,
        &streamCtx,
        NULL,  // No stop flag
        &errorBuf,
        timeoutMs
    );

    // Prepare result message to post to UI thread
    wyString* pResult = new wyString();
    if (success && streamCtx.result.GetLength() > 0) {
        pResult->SetAs(streamCtx.result.GetString());
    } else {
        pResult->SetAs(errorBuf.GetString());
    }

    // Post message to UI thread to update the editor
    PostMessage(genParam->hwndEditor, UM_SQL_GEN_COMPLETE,
                success ? 1 : 0, (LPARAM)pResult);

    // Cleanup
    InterlockedExchange(&genParam->pThis->m_gen_thread_running, 0);
    delete genParam;
    return 0;
}

// Chapter 9: Insert streamed SQL generation token on UI thread
void EditorBase::OnSQLGenerationToken(const char* token)
{
    if (!token || !token[0])
        return;

    HWND hwnd = m_hwnd;

    if (!m_gen_has_streamed) {
        RemoveCommentLoadingPlaceholder(m_gen_trigger_line);

        int next_line = m_gen_trigger_line + 1;
        int max_line = (int)SendMessage(hwnd, SCI_GETLINECOUNT, 0, 0);
        if (next_line >= max_line) {
            m_gen_insert_pos = (int)SendMessage(hwnd, SCI_GETTEXTLENGTH, 0, 0);
            SendMessage(hwnd, SCI_INSERTTEXT, m_gen_insert_pos, (LPARAM)"\n");
            m_gen_insert_pos++;
        } else {
            m_gen_insert_pos = (int)SendMessage(hwnd, SCI_POSITIONFROMLINE, next_line, 0);
        }

        SendMessage(hwnd, SCI_BEGINUNDOACTION, 0, 0);
        m_gen_has_streamed = true;
    }

    SendMessage(hwnd, SCI_INSERTTEXT, m_gen_insert_pos, (LPARAM)token);
    m_gen_insert_pos += (int)strlen(token);
    SendMessage(hwnd, SCI_GOTOPOS, m_gen_insert_pos, 0);
}

// Chapter 9: Handle SQL generation completion on UI thread
void EditorBase::OnSQLGenerationComplete(const char* result, bool success)
{
    if (m_gen_has_streamed) {
        if (!success && result && result[0]) {
            wyString failMsg;
            failMsg.Sprintf("\n-- \xe7\x94\x9f\xe6\x88\x90\xe4\xb8\xad\xe6\x96\xad: %s", result);  // "-- 生成中断: ..."
            SendMessage(m_hwnd, SCI_INSERTTEXT, m_gen_insert_pos, (LPARAM)failMsg.GetString());
            m_gen_insert_pos += failMsg.GetLength();
        }
        SendMessage(m_hwnd, SCI_ENDUNDOACTION, 0, 0);
        m_gen_has_streamed = false;
        m_gen_insert_pos = 0;
        return;
    }

    if (success && result && result[0]) {
        // Remove loading placeholder and insert generated SQL
        RemoveCommentLoadingPlaceholder(m_gen_trigger_line);
        InsertGeneratedSQL(result, m_gen_trigger_line);
    } else {
        // Show failure message
        ShowGenerationFailureMessage(m_gen_trigger_line, result);
    }
}
