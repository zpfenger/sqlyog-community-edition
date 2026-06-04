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

#ifndef _AISettingsDlg_H_
#define _AISettingsDlg_H_

#include <windows.h>
#include "wyString.h"

class AISettingsDlg
{
public:
    // Show the AI Settings dialog (modal)
    static void Show(HWND hparent);

private:
    // Dialog procedure
    static INT_PTR CALLBACK DlgProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    // Handlers
    static void OnInitDialog(HWND hwnd);
    static void OnOK(HWND hwnd);
    static void OnTest(HWND hwnd);

    // Test connection in background thread
    struct TestParam {
        HWND    hwnd;
        wyString url;
        wyString key;
        wyString model;
    };
    static unsigned __stdcall TestThreadProc(void* param);
};

#endif
