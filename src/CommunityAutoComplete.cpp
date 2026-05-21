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

#include "CommunityAutoComplete.h"
#include "AutoCompleteInterface.h"
#include "MDIWindow.h"
#include "EditorBase.h"
#include "GUIHelper.h"
#include "CommonHelper.h"
#include "SQLMaker.h"
#include "sqlite3.h"
#include <algorithm>
#include <process.h>
#include <string.h>

namespace {

bool AllowEmptyPrefix(SQLContextType ctx) {
    return ctx == CTX_TABLE_REF ||
           ctx == CTX_COLUMN_REF ||
           ctx == CTX_WHERE_EXPR ||
           ctx == CTX_INSERT_COLS;
}

const char* GetActiveDatabaseName(MDIWindow* wnd) {
    if (!wnd)
        return "";

    if (wnd->m_database.GetLength() > 0)
        return wnd->m_database.GetString();

    if (wnd->m_conninfo.m_db.GetLength() > 0)
        return wnd->m_conninfo.m_db.GetString();

    return "";
}

bool IsTableContextKeyword(const char* word) {
    return strcmp(word, "FROM") == 0 ||
           strcmp(word, "JOIN") == 0 ||
           strcmp(word, "INNER") == 0 ||
           strcmp(word, "LEFT") == 0 ||
           strcmp(word, "RIGHT") == 0 ||
           strcmp(word, "CROSS") == 0 ||
           strcmp(word, "INTO") == 0 ||
           strcmp(word, "UPDATE") == 0 ||
           strcmp(word, "TABLE") == 0;
}

bool IsExprContextKeyword(const char* word) {
    return strcmp(word, "WHERE") == 0 ||
           strcmp(word, "ON") == 0 ||
           strcmp(word, "HAVING") == 0 ||
           strcmp(word, "AND") == 0 ||
           strcmp(word, "OR") == 0 ||
           strcmp(word, "NOT") == 0 ||
           strcmp(word, "SET") == 0 ||
           strcmp(word, "GROUP") == 0 ||
           strcmp(word, "ORDER") == 0 ||
           strcmp(word, "BY") == 0 ||
           strcmp(word, "LIKE") == 0 ||
           strcmp(word, "BETWEEN") == 0 ||
           strcmp(word, "IN") == 0 ||
           strcmp(word, "VALUES") == 0 ||
           strcmp(word, "SELECT") == 0;
}

}

CCommunityAutoComplete::CCommunityAutoComplete() {
    InitializeCriticalSection(&m_cs);
    m_loading = 0;
}

CCommunityAutoComplete::~CCommunityAutoComplete() {
    // Wait for background loading to finish
    while (InterlockedCompareExchange(&m_loading, 0, 0) != 0) {
        Sleep(10);
    }
    DeleteCriticalSection(&m_cs);
}

void
CCommunityAutoComplete::WideToUtf8(const wchar_t* wide, char* utf8, int utf8_size) {
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, utf8_size, NULL, NULL);
}

void
CCommunityAutoComplete::Utf8ToWide(const char* utf8, wchar_t* wide, int wide_size) {
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wide_size);
}

void
CCommunityAutoComplete::InitStaticItems() {
    sqlite3* db = NULL;
    if (OpenKeyWordsDB(&db) != wyTrue || db == NULL)
        return;

    sqlite3_stmt* stmt = NULL;
    int rc;

    // Load keywords (object_type = 1)
    rc = sqlite3_prepare_v2(db,
        "select distinct object_name from objects where object_type = 1 order by lower(object_name)",
        -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = (const char*)sqlite3_column_text(stmt, 0);
            if (!name) continue;

            ACCompletionItem item;
            memset(&item, 0, sizeof(item));
            strncpy(item.text, name, sizeof(item.text) - 1);
            item.type = AC_PRE_KEYWORD;
            item.priority = 1;
            item.table_id = -1;

            int idx = (int)m_static_items.size();
            m_static_items.push_back(item);

            wchar_t wname[128];
            Utf8ToWide(name, wname, 128);
            m_trie_keywords.Insert(wname, idx);
        }
        sqlite3_finalize(stmt);
    }

    // Load functions (object_type = 2)
    stmt = NULL;
    rc = sqlite3_prepare_v2(db,
        "select distinct object_name from objects where object_type = 2 order by lower(object_name)",
        -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = (const char*)sqlite3_column_text(stmt, 0);
            if (!name) continue;

            ACCompletionItem item;
            memset(&item, 0, sizeof(item));
            strncpy(item.text, name, sizeof(item.text) - 1);
            item.type = AC_PRE_FUNCTION;
            item.priority = 2;
            item.table_id = -1;

            int idx = (int)m_static_items.size();
            m_static_items.push_back(item);

            wchar_t wname[128];
            Utf8ToWide(name, wname, 128);
            m_trie_functions.Insert(wname, idx);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}

void
CCommunityAutoComplete::LoadMetadata(MDIWindow* wnd) {
    if (!wnd || !wnd->m_tunnel || !wnd->m_mysql)
        return;

    ClearDynamicMetadata();

    wyString query;
    MYSQL_RES* myres = NULL;
    MYSQL_ROW row;

    const char* dbname = GetActiveDatabaseName(wnd);

    // 0. Load database list
    query.Sprintf("SHOW DATABASES");
    myres = ExecuteAndGetResult(wnd, wnd->m_tunnel, &wnd->m_mysql, query,
                                wyFalse, wyFalse, wyTrue, true);
    if (myres) {
        while ((row = wnd->m_tunnel->mysql_fetch_row(myres))) {
            if (!row[0]) continue;

            ACCompletionItem item;
            memset(&item, 0, sizeof(item));
            strncpy(item.text, row[0], sizeof(item.text) - 1);
            item.type = AC_DATABASE;
            item.priority = 0;
            item.table_id = -1;

            int idx = (int)m_static_items.size() + (int)m_dynamic_items.size();
            m_dynamic_items.push_back(item);

            wchar_t wname[128];
            Utf8ToWide(row[0], wname, 128);
            m_trie_databases.Insert(wname, idx);
        }
        wnd->m_tunnel->mysql_free_result(myres);
    }

    // 1. Load table list (仅当 dbname 有效时执行)
    if (dbname && dbname[0]) {
    query.Sprintf("SHOW TABLES FROM `%s`", dbname);
    myres = ExecuteAndGetResult(wnd, wnd->m_tunnel, &wnd->m_mysql, query,
                                wyFalse, wyFalse, wyTrue, true);
    if (myres) {
        while ((row = wnd->m_tunnel->mysql_fetch_row(myres))) {
            if (!row[0]) continue;

            ACCompletionItem item;
            memset(&item, 0, sizeof(item));
            strncpy(item.text, row[0], sizeof(item.text) - 1);
            item.type = AC_TABLE;
            item.priority = 3;
            item.table_id = -1;

            int idx = (int)m_static_items.size() + (int)m_dynamic_items.size();
            m_dynamic_items.push_back(item);

            ACTableInfo ti;
            memset(&ti, 0, sizeof(ti));
            strncpy(ti.name, row[0], sizeof(ti.name) - 1);
            ti.alias[0] = '\0';
            ti.field_start = -1;
            ti.field_count = 0;
            m_tables.push_back(ti);

            wchar_t wname[128];
            Utf8ToWide(row[0], wname, 128);
            m_trie_tables.Insert(wname, idx);

            if (m_tables.size() > 500)
                break;
        }
        wnd->m_tunnel->mysql_free_result(myres);
    }

    // 2. Load columns via single INFORMATION_SCHEMA query (avoids N+1)
    query.Sprintf(
        "SELECT TABLE_NAME, COLUMN_NAME FROM INFORMATION_SCHEMA.COLUMNS "
        "WHERE TABLE_SCHEMA = '%s' ORDER BY TABLE_NAME, ORDINAL_POSITION",
        dbname);
    myres = ExecuteAndGetResult(wnd, wnd->m_tunnel, &wnd->m_mysql, query,
                                wyFalse, wyFalse, wyTrue, true);
    if (myres) {
        int last_table_id = -1;
        while ((row = wnd->m_tunnel->mysql_fetch_row(myres))) {
            if (!row[0] || !row[1]) continue;

            // Find table index by name
            int t = -1;
            for (size_t i = 0; i < m_tables.size(); i++) {
                if (strcmp(m_tables[i].name, row[0]) == 0) {
                    t = (int)i;
                    break;
                }
            }
            if (t < 0) continue;

            // Track field_start for each table
            if (t != last_table_id) {
                m_tables[t].field_start = (int)m_dynamic_items.size();
                last_table_id = t;
            }

            ACCompletionItem item;
            memset(&item, 0, sizeof(item));
            strncpy(item.text, row[1], sizeof(item.text) - 1);
            item.type = AC_COLUMN;
            item.priority = 4;
            item.table_id = t;

            int idx = (int)m_static_items.size() + (int)m_dynamic_items.size();
            m_dynamic_items.push_back(item);
            m_tables[t].field_count++;

            wchar_t wname[128];
            Utf8ToWide(row[1], wname, 128);
            m_trie_columns.Insert(wname, idx);

            if (m_dynamic_items.size() > 10000)
                break;
        }
        wnd->m_tunnel->mysql_free_result(myres);
    }
    } // end if (dbname && dbname[0])
}

void
CCommunityAutoComplete::ClearDynamicMetadata() {
    EnterCriticalSection(&m_cs);
    m_dynamic_items.clear();
    m_tables.clear();
    m_trie_databases.Clear();
    m_trie_tables.Clear();
    m_trie_columns.Clear();
    LeaveCriticalSection(&m_cs);
}

// Background thread procedure for async metadata loading
struct AsyncLoadParam {
    CCommunityAutoComplete* ac;
    MDIWindow* wnd;
};

unsigned __stdcall
AsyncLoadThread(void* arg) {
    AsyncLoadParam* param = (AsyncLoadParam*)arg;
    CCommunityAutoComplete* ac = param->ac;
    MDIWindow* wnd = param->wnd;

    // Build new data into local containers
    std::vector<ACCompletionItem> new_items;
    std::vector<ACTableInfo> new_tables;
    TrieIndex new_trie_databases;
    TrieIndex new_trie_tables;
    TrieIndex new_trie_columns;

    int static_count = (int)ac->m_static_items.size();

    if (wnd && wnd->m_tunnel && wnd->m_mysql) {
        wyString query;
        MYSQL_RES* myres = NULL;
        MYSQL_ROW row;
        const char* dbname = GetActiveDatabaseName(wnd);

        // 0. Load database list
        query.Sprintf("SHOW DATABASES");
        myres = ExecuteAndGetResult(wnd, wnd->m_tunnel, &wnd->m_mysql, query,
                                    wyFalse, wyFalse, wyTrue, true);
        if (myres) {
            while ((row = wnd->m_tunnel->mysql_fetch_row(myres))) {
                if (!row[0]) continue;

                ACCompletionItem item;
                memset(&item, 0, sizeof(item));
                strncpy(item.text, row[0], sizeof(item.text) - 1);
                item.type = AC_DATABASE;
                item.priority = 0;
                item.table_id = -1;

                int idx = static_count + (int)new_items.size();
                new_items.push_back(item);

                wchar_t wname[128];
                CCommunityAutoComplete::Utf8ToWide(row[0], wname, 128);
                new_trie_databases.Insert(wname, idx);
            }
            wnd->m_tunnel->mysql_free_result(myres);
        }

        if (dbname && dbname[0]) {
            // 1. Load table list
            query.Sprintf("SHOW TABLES FROM `%s`", dbname);
            myres = ExecuteAndGetResult(wnd, wnd->m_tunnel, &wnd->m_mysql, query,
                                        wyFalse, wyFalse, wyTrue, true);
            if (myres) {
                while ((row = wnd->m_tunnel->mysql_fetch_row(myres))) {
                    if (!row[0]) continue;

                    ACCompletionItem item;
                    memset(&item, 0, sizeof(item));
                    strncpy(item.text, row[0], sizeof(item.text) - 1);
                    item.type = AC_TABLE;
                    item.priority = 3;
                    item.table_id = -1;

                    int idx = static_count + (int)new_items.size();
                    new_items.push_back(item);

                    ACTableInfo ti;
                    memset(&ti, 0, sizeof(ti));
                    strncpy(ti.name, row[0], sizeof(ti.name) - 1);
                    ti.alias[0] = '\0';
                    ti.field_start = -1;
                    ti.field_count = 0;
                    new_tables.push_back(ti);

                    wchar_t wname[128];
                    CCommunityAutoComplete::Utf8ToWide(row[0], wname, 128);
                    new_trie_tables.Insert(wname, idx);

                    if (new_tables.size() > 500)
                        break;
                }
                wnd->m_tunnel->mysql_free_result(myres);
            }

            // 2. Load columns via INFORMATION_SCHEMA
            query.Sprintf(
                "SELECT TABLE_NAME, COLUMN_NAME FROM INFORMATION_SCHEMA.COLUMNS "
                "WHERE TABLE_SCHEMA = '%s' ORDER BY TABLE_NAME, ORDINAL_POSITION",
                dbname);
            myres = ExecuteAndGetResult(wnd, wnd->m_tunnel, &wnd->m_mysql, query,
                                        wyFalse, wyFalse, wyTrue, true);
            if (myres) {
                int last_table_id = -1;
                while ((row = wnd->m_tunnel->mysql_fetch_row(myres))) {
                    if (!row[0] || !row[1]) continue;

                    int t = -1;
                    for (size_t i = 0; i < new_tables.size(); i++) {
                        if (strcmp(new_tables[i].name, row[0]) == 0) {
                            t = (int)i;
                            break;
                        }
                    }
                    if (t < 0) continue;

                    if (t != last_table_id) {
                        new_tables[t].field_start = (int)new_items.size();
                        last_table_id = t;
                    }

                    ACCompletionItem item;
                    memset(&item, 0, sizeof(item));
                    strncpy(item.text, row[1], sizeof(item.text) - 1);
                    item.type = AC_COLUMN;
                    item.priority = 4;
                    item.table_id = t;

                    int idx = static_count + (int)new_items.size();
                    new_items.push_back(item);
                    new_tables[t].field_count++;

                    wchar_t wname[128];
                    CCommunityAutoComplete::Utf8ToWide(row[1], wname, 128);
                    new_trie_columns.Insert(wname, idx);

                    if (new_items.size() > 10000)
                        break;
                }
                wnd->m_tunnel->mysql_free_result(myres);
            }
        }
    }

    // Swap under lock
    EnterCriticalSection(&ac->m_cs);
    ac->m_dynamic_items.swap(new_items);
    ac->m_tables.swap(new_tables);
    ac->m_trie_databases.Swap(new_trie_databases);
    ac->m_trie_tables.Swap(new_trie_tables);
    ac->m_trie_columns.Swap(new_trie_columns);
    LeaveCriticalSection(&ac->m_cs);

    InterlockedExchange(&ac->m_loading, 0);

    delete param;
    return 0;
}

void
CCommunityAutoComplete::LoadMetadataAsync(MDIWindow* wnd) {
    if (!wnd) return;
    if (InterlockedCompareExchange(&m_loading, 1, 0) != 0)
        return; // Already loading

    AsyncLoadParam* param = new AsyncLoadParam;
    param->ac = this;
    param->wnd = wnd;

    HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, AsyncLoadThread, param, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        InterlockedExchange(&m_loading, 0);
        delete param;
    }
}

SQLContextType
CCommunityAutoComplete::AnalyzeContext(EditorBase* editor, int cursor_pos,
                                       wyString& prefix, int& table_idx) {
    table_idx = -1;
    prefix.Clear();

    if (!editor || !editor->m_hwnd)
        return CTX_UNKNOWN;

    HWND hwnd = editor->m_hwnd;
    int line = (int)SendMessage(hwnd, SCI_LINEFROMPOSITION, cursor_pos, 0);
    int line_start = (int)SendMessage(hwnd, SCI_POSITIONFROMLINE, line, 0);

    int text_len = cursor_pos - line_start;
    if (text_len <= 0) return CTX_UNKNOWN;

    char* line_text = new char[text_len + 1];
    struct TextRange tr;
    tr.chrg.cpMin = line_start;
    tr.chrg.cpMax = cursor_pos;
    tr.lpstrText = line_text;
    SendMessage(hwnd, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);

    // Extract prefix (current word before cursor)
    bool cursor_after_space = (line_text[text_len - 1] == ' ' || line_text[text_len - 1] == '\t');

    int i = text_len - 1;
    while (i >= 0 && (line_text[i] == ' ' || line_text[i] == '\t')) i--;

    if (i < 0) {
        delete[] line_text;
        return CTX_UNKNOWN;
    }

    int word_end = i;
    int word_start = i;
    while (word_start > 0) {
        char ch = line_text[word_start - 1];
        if (ch == ' ' || ch == '\t' || ch == ',' || ch == '(' ||
            ch == ')' || ch == '=' || ch == '<' || ch == '>' ||
            ch == '!' || ch == '&' || ch == '|' || ch == '+' ||
            ch == '-' || ch == '*' || ch == '/' || ch == '\n' ||
            ch == '\r' || ch == '`') {
            break;
        }
        word_start--;
    }

    // Check for table.column pattern
    char word[256] = {0};
    int word_len = word_end - word_start + 1;
    if (word_len > 0 && word_len < (int)sizeof(word)) {
        strncpy(word, line_text + word_start, word_len);
        word[word_len] = '\0';

        ExtractAliases(line_text);

        char* dot = strchr(word, '.');
        if (dot) {
            *dot = '\0';
            char* table_name = word;
            char* col_prefix = dot + 1;

            table_idx = FindTableIndex(table_name);
            if (table_idx >= 0) {
                prefix.SetAs(col_prefix);
                delete[] line_text;
                return CTX_COLUMN_REF;
            }
        }
    }

    // No dot: prefix is the current word
    if (word_len > 0 && word_len < (int)sizeof(word)) {
        prefix.SetAs(word);
    } else {
        delete[] line_text;
        return CTX_UNKNOWN;
    }

    // Analyze preceding keyword
    int j = word_start - 1;
    while (j >= 0 && (line_text[j] == ' ' || line_text[j] == '\t')) j--;

    if (j < 0) {
        delete[] line_text;
        return CTX_KEYWORD_FUNC;
    }

    int prev_end = j;
    int prev_start = j;
    while (prev_start > 0) {
        char ch = line_text[prev_start - 1];
        if (ch == ' ' || ch == '\t' || ch == ',' || ch == '(' ||
            ch == ')' || ch == '\n' || ch == '\r') {
            break;
        }
        prev_start--;
    }

    char prev_word[128] = {0};
    int prev_len = prev_end - prev_start + 1;
    if (prev_len > 0 && prev_len < (int)sizeof(prev_word)) {
        strncpy(prev_word, line_text + prev_start, prev_len);
        prev_word[prev_len] = '\0';

        for (int k = 0; prev_word[k]; k++) {
            if (prev_word[k] >= 'a' && prev_word[k] <= 'z')
                prev_word[k] -= 32;
        }

        if (cursor_after_space) {
            char current_word[128] = {0};
            int copy_len = min(word_len, (int)sizeof(current_word) - 1);
            strncpy(current_word, word, copy_len);
            current_word[copy_len] = '\0';
            for (int k = 0; current_word[k]; k++) {
                if (current_word[k] >= 'a' && current_word[k] <= 'z')
                    current_word[k] -= 32;
            }

            if (IsTableContextKeyword(current_word)) {
                prefix.Clear();
                delete[] line_text;
                return CTX_TABLE_REF;
            }

            if (IsExprContextKeyword(current_word)) {
                prefix.Clear();
                delete[] line_text;
                return CTX_WHERE_EXPR;
            }
        }

        if (IsTableContextKeyword(prev_word)) {
            delete[] line_text;
            return CTX_TABLE_REF;
        }

        if (IsExprContextKeyword(prev_word)) {
            delete[] line_text;
            return CTX_WHERE_EXPR;
        }
    }

    // Check for INSERT INTO tablename ( pattern
    {
        char* p = line_text;
        char* insert_pos = NULL;
        while ((p = strstr(p, "INSERT")) != NULL) {
            // Check word boundary
            if (p > line_text) {
                char before = *(p - 1);
                if (before != ' ' && before != '\t' && before != '\n' && before != '\r') {
                    p += 6;
                    continue;
                }
            }
            // Check case-insensitive: also match lowercase "insert"
            char check[7];
            strncpy(check, p, 6);
            check[6] = '\0';
            for (int ci = 0; check[ci]; ci++)
                check[ci] = toupper((unsigned char)check[ci]);
            if (strcmp(check, "INSERT") != 0) {
                p += 6;
                continue;
            }
            insert_pos = p;
            p += 6;
        }

        if (insert_pos) {
            char* into_pos = strstr(insert_pos, "INTO");
            if (!into_pos) {
                // Try lowercase
                char* lower_into = strstr(insert_pos, "into");
                if (lower_into) into_pos = lower_into;
            }
            // Try mixed case by scanning manually
            if (!into_pos) {
                char* scan = insert_pos + 6;
                while (*scan) {
                    if ((*scan == 'I' || *scan == 'i') &&
                        (scan[1] == 'N' || scan[1] == 'n') &&
                        (scan[2] == 'T' || scan[2] == 't') &&
                        (scan[3] == 'O' || scan[3] == 'o')) {
                        // Check word boundary before
                        if (scan == insert_pos + 6 || scan[-1] == ' ' || scan[-1] == '\t') {
                            into_pos = scan;
                            break;
                        }
                    }
                    scan++;
                }
            }

            if (into_pos && into_pos > insert_pos) {
                into_pos += 4;
                while (*into_pos == ' ' || *into_pos == '\t') into_pos++;

                // Extract table name
                char ins_table[128] = {0};
                int iti = 0;
                while (*into_pos && *into_pos != ' ' && *into_pos != '\t' &&
                       *into_pos != '(' && *into_pos != '\n' && iti < 127) {
                    ins_table[iti++] = *into_pos++;
                }
                ins_table[iti] = '\0';

                // Check if there's an opening parenthesis after table name
                while (*into_pos == ' ' || *into_pos == '\t') into_pos++;
                if (*into_pos == '(') {
                    // We're inside INSERT INTO tablename ( - return CTX_INSERT_COLS
                    table_idx = FindTableIndex(ins_table);
                    delete[] line_text;
                    return CTX_INSERT_COLS;
                }
            }
        }
    }

    delete[] line_text;
    return CTX_KEYWORD_FUNC;
}

void
CCommunityAutoComplete::ExtractAliases(const char* sql_text) {
    if (!sql_text) return;

    int len = (int)strlen(sql_text);
    if (len <= 0) return;

    char* upper_buf = new char[len + 1];
    for (int i = 0; i < len; i++)
        upper_buf[i] = toupper((unsigned char)sql_text[i]);
    upper_buf[len] = '\0';

    for (size_t i = 0; i < m_tables.size(); i++)
        m_tables[i].alias[0] = '\0';

    const char* keywords[] = {"FROM", "JOIN"};
    for (int k = 0; k < 2; k++) {
        const char* search = keywords[k];
        int slen = (int)strlen(search);
        const char* p = upper_buf;
        while ((p = strstr(p, search)) != NULL) {
            if (p > upper_buf) {
                char before = *(p - 1);
                if (before != ' ' && before != '\t' && before != '\n' && before != '\r') {
                    p += slen;
                    continue;
                }
            }
            p += slen;

            while (*p == ' ' || *p == '\t') p++;

            char table_name[128] = {0};
            int ti = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != '\n' && ti < 127) {
                table_name[ti++] = *p;
                p++;
            }
            table_name[ti] = '\0';

            if (ti == 0) continue;

            while (*p == ' ' || *p == '\t') p++;

            char alias[64] = {0};
            if (strncmp(p, "AS", 2) == 0 && (p[2] == ' ' || p[2] == '\t')) {
                p += 2;
                while (*p == ' ' || *p == '\t') p++;
                int ai = 0;
                while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != '\n' && ai < 63) {
                    alias[ai++] = *p;
                    p++;
                }
                alias[ai] = '\0';
            } else if (*p && *p != ',' && *p != '\n' && *p != '\r' &&
                       strncmp(p, "WHERE", 5) != 0 && strncmp(p, "ON", 2) != 0 &&
                       strncmp(p, "INNER", 5) != 0 && strncmp(p, "LEFT", 4) != 0 &&
                       strncmp(p, "RIGHT", 5) != 0 && strncmp(p, "JOIN", 4) != 0 &&
                       strncmp(p, "SET", 3) != 0 && strncmp(p, "GROUP", 5) != 0 &&
                       strncmp(p, "ORDER", 5) != 0 && strncmp(p, "HAVING", 6) != 0 &&
                       strncmp(p, "LIMIT", 5) != 0) {
                int ai = 0;
                while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != '\n' && ai < 63) {
                    alias[ai++] = *p;
                    p++;
                }
                alias[ai] = '\0';
            }

            if (alias[0]) {
                for (size_t i = 0; i < m_tables.size(); i++) {
                    if (_stricmp(m_tables[i].name, table_name) == 0) {
                        strncpy(m_tables[i].alias, alias, sizeof(m_tables[i].alias) - 1);
                        break;
                    }
                }
            }
        }
    }

    delete[] upper_buf;
}

int
CCommunityAutoComplete::FindTableIndex(const char* name) {
    if (!name || !name[0]) return -1;
    for (size_t i = 0; i < m_tables.size(); i++) {
        if (_stricmp(m_tables[i].name, name) == 0)
            return (int)i;
        if (m_tables[i].alias[0] && _stricmp(m_tables[i].alias, name) == 0)
            return (int)i;
    }
    return -1;
}

int
CCommunityAutoComplete::GetCompletionType(const char* text) {
    if (!text || !text[0]) return 0;

    // Search in static items
    for (size_t i = 0; i < m_static_items.size(); i++) {
        if (_stricmp(m_static_items[i].text, text) == 0)
            return m_static_items[i].type;
    }

    // Search in dynamic items
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_dynamic_items.size(); i++) {
        if (_stricmp(m_dynamic_items[i].text, text) == 0) {
            int type = m_dynamic_items[i].type;
            LeaveCriticalSection(&m_cs);
            return type;
        }
    }
    LeaveCriticalSection(&m_cs);

    return 0;
}

void
CCommunityAutoComplete::QueryCompletion(const char* prefix, SQLContextType ctx, int table_idx,
                                         wyString& result, bool& has_items) {
    result.Clear();
    has_items = false;

    if (!prefix || ((!prefix[0]) && AllowEmptyPrefix(ctx) == false)) return;

    wchar_t wprefix[128];
    Utf8ToWide(prefix, wprefix, 128);

    std::vector<int> candidates;

    switch (ctx) {
    case CTX_TABLE_REF:
        {
            std::vector<int> tmp;
            m_trie_databases.PrefixSearch(wprefix, tmp, MAX_SUGGESTIONS / 2);
            candidates.insert(candidates.end(), tmp.begin(), tmp.end());
            // P0-FIX: 不能直接把 candidates 传入 PrefixSearch，因为它内部会 results.clear()
            m_trie_tables.PrefixSearch(wprefix, tmp, MAX_SUGGESTIONS / 2);
            candidates.insert(candidates.end(), tmp.begin(), tmp.end());
        }
        break;

    case CTX_INSERT_COLS:
        if (table_idx >= 0 && table_idx < (int)m_tables.size()) {
            m_trie_columns.PrefixSearch(wprefix, candidates, MAX_SUGGESTIONS);
            std::vector<int> filtered;
            for (size_t i = 0; i < candidates.size(); i++) {
                int idx = candidates[i];
                int dyn_idx = idx - (int)m_static_items.size();
                if (dyn_idx >= 0 && dyn_idx < (int)m_dynamic_items.size()) {
                    if (m_dynamic_items[dyn_idx].table_id == table_idx) {
                        filtered.push_back(idx);
                    }
                }
            }
            candidates = filtered;
        }
        break;

    case CTX_COLUMN_REF:
        if (table_idx >= 0 && table_idx < (int)m_tables.size()) {
            m_trie_columns.PrefixSearch(wprefix, candidates, MAX_SUGGESTIONS);
            std::vector<int> filtered;
            for (size_t i = 0; i < candidates.size(); i++) {
                int idx = candidates[i];
                int dyn_idx = idx - (int)m_static_items.size();
                if (dyn_idx >= 0 && dyn_idx < (int)m_dynamic_items.size()) {
                    if (m_dynamic_items[dyn_idx].table_id == table_idx) {
                        filtered.push_back(idx);
                    }
                }
            }
            candidates = filtered;
        }
        break;

    case CTX_WHERE_EXPR:
        {
            std::vector<int> tmp;
            m_trie_functions.PrefixSearch(wprefix, tmp, MAX_SUGGESTIONS / 2);
            candidates.insert(candidates.end(), tmp.begin(), tmp.end());
            m_trie_columns.PrefixSearch(wprefix, tmp, MAX_SUGGESTIONS / 2);
            candidates.insert(candidates.end(), tmp.begin(), tmp.end());
        }
        break;

    case CTX_KEYWORD_FUNC:
    case CTX_UNKNOWN:
    default:
        {
            std::vector<int> tmp;
            m_trie_keywords.PrefixSearch(wprefix, tmp, MAX_SUGGESTIONS / 3);
            candidates.insert(candidates.end(), tmp.begin(), tmp.end());
            m_trie_functions.PrefixSearch(wprefix, tmp, MAX_SUGGESTIONS / 3);
            candidates.insert(candidates.end(), tmp.begin(), tmp.end());
            m_trie_tables.PrefixSearch(wprefix, tmp, MAX_SUGGESTIONS / 3);
            candidates.insert(candidates.end(), tmp.begin(), tmp.end());
        }
        break;
    }

    if (candidates.empty()) return;

    // Sort: exact match first, then by priority, then alphabetically
    std::sort(candidates.begin(), candidates.end(),
        [&](int a, int b) {
            ACCompletionItem* ia = NULL;
            ACCompletionItem* ib = NULL;

            if (a < (int)m_static_items.size())
                ia = &m_static_items[a];
            else {
                int d = a - (int)m_static_items.size();
                if (d < (int)m_dynamic_items.size())
                    ia = &m_dynamic_items[d];
            }

            if (b < (int)m_static_items.size())
                ib = &m_static_items[b];
            else {
                int d = b - (int)m_static_items.size();
                if (d < (int)m_dynamic_items.size())
                    ib = &m_dynamic_items[d];
            }

            if (!ia && !ib) return false;
            if (!ia) return false;
            if (!ib) return true;

            // Exact match first
            int exact_a = (_stricmp(ia->text, prefix) == 0) ? 0 : 1;
            int exact_b = (_stricmp(ib->text, prefix) == 0) ? 0 : 1;
            if (exact_a != exact_b) return exact_a < exact_b;

            // Then by priority
            if (ia->priority != ib->priority)
                return ia->priority < ib->priority;

            // Then alphabetically
            return _stricmp(ia->text, ib->text) < 0;
        });

    // Format results for Scintilla SCI_AUTOCSHOW: "item?type\n"
    for (size_t i = 0; i < candidates.size() && i < (size_t)MAX_SUGGESTIONS; i++) {
        int idx = candidates[i];
        ACCompletionItem* item = NULL;

        if (idx < (int)m_static_items.size()) {
            item = &m_static_items[idx];
        } else {
            int dyn_idx = idx - (int)m_static_items.size();
            if (dyn_idx < (int)m_dynamic_items.size()) {
                item = &m_dynamic_items[dyn_idx];
            }
        }

        if (!item) continue;

        if (result.GetLength() > 0)
            result.Add("\n");

        result.Add(item->text);

        char type_suffix[8];
        sprintf(type_suffix, "?%d", item->type);
        result.Add(type_suffix);
    }

    has_items = (result.GetLength() > 0);
}
