// CommunityAutoComplete.h - Community edition auto-completion engine
#ifndef _COMMUNITY_AUTO_COMPLETE_H_
#define _COMMUNITY_AUTO_COMPLETE_H_

#include "TrieIndex.h"
#include "Global.h"
#include <vector>
#include <string>
#include <utility>

class EditorBase;

enum SQLContextType {
    CTX_UNKNOWN = 0,
    CTX_KEYWORD_FUNC,
    CTX_TABLE_REF,
    CTX_COLUMN_REF,
    CTX_WHERE_EXPR,
    CTX_INSERT_COLS
};

struct ACCompletionItem {
    char        text[128];
    int         type;
    int         priority;
    int         table_id;
};

struct ACTableInfo {
    char    name[128];
    char    alias[64];
    int     field_start;
    int     field_count;
};

class CCommunityAutoComplete {
public:
    CCommunityAutoComplete();
    ~CCommunityAutoComplete();

    void InitStaticItems();

    void LoadMetadata(MDIWindow* wnd);

    void LoadMetadataAsync(MDIWindow* wnd);

    void ClearDynamicMetadata();

    void QueryCompletion(const char* prefix, SQLContextType ctx, int table_idx,
                         wyString& result, bool& has_items);

    SQLContextType AnalyzeContext(EditorBase* editor, int cursor_pos,
                                 wyString& prefix, int& table_idx);

    void ExtractAliases(const char* sql_text);

    int FindTableIndex(const char* name);

    int GetCompletionType(const char* text);

    static const int MAX_SUGGESTIONS = 100;

    volatile long m_loading;

private:
    std::vector<ACCompletionItem>    m_static_items;
    TrieIndex                        m_trie_keywords;
    TrieIndex                        m_trie_functions;

    std::vector<ACCompletionItem>    m_dynamic_items;
    std::vector<ACTableInfo>         m_tables;
    std::vector<std::pair<std::string, int>> m_alias_map;  // alias -> table index (supports multiple aliases per table)
    TrieIndex                        m_trie_databases;
    TrieIndex                        m_trie_tables;
    TrieIndex                        m_trie_columns;

    CRITICAL_SECTION                 m_cs;

    static void WideToUtf8(const wchar_t* wide, char* utf8, int utf8_size);
    static void Utf8ToWide(const char* utf8, wchar_t* wide, int wide_size);

    friend unsigned __stdcall AsyncLoadThread(void* arg);
};

#endif // _COMMUNITY_AUTO_COMPLETE_H_
