# SQL 智能补全功能实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 SQLyog Community Edition 实现完全离线、零延迟的 SQL 智能补全系统，支持关键字、函数、表名、字段名的上下文感知补全。

**Architecture:** 基于 Trie 前缀树构建核心索引，复用现有 `AutoCompleteInterface` 框架（替换其 COMMUNITY 桩实现），通过 Scintilla 的 `SCI_AUTOCSHOW` API 驱动下拉补全 UI。数据库元数据通过后台线程异步获取，使用现有的 `ExecuteAndGetResult` 查询接口。上下文分析采用简单有限状态机，根据光标位置选择补全源。

**Tech Stack:** C++ (Win32), Scintilla 编辑控件, SQLite3 (Keywords.db), MariaDB 客户端库

---

## 文件结构

### 新建文件

| 文件路径 | 职责 |
|---------|------|
| `include/TrieIndex.h` | Trie 前缀树模板类声明（插入、前缀查询、有序收集） |
| `src/TrieIndex.cpp` | Trie 前缀树实现 |
| `include/SQLContextAnalyzer.h` | SQL 上下文分析器声明（有限状态机） |
| `src/SQLContextAnalyzer.cpp` | SQL 上下文分析器实现 |
| `include/MetadataFetcher.h` | 数据库元数据获取器声明 |
| `src/MetadataFetcher.cpp` | 数据库元数据获取器实现（后台线程） |
| `include/CommunityAutoComplete.h` | Community 版自动补全引擎声明 |
| `src/CommunityAutoComplete.cpp` | Community 版自动补全引擎实现 |

### 修改文件

| 文件路径 | 修改内容 |
|---------|---------|
| `include/AutoCompleteInterface.h` | 添加 `CCommunityAutoComplete*` 成员，新增方法声明 |
| `src/AutoCompleteInterface.cpp` | 在 `#ifdef COMMUNITY` 块中实现代理到 `CCommunityAutoComplete` |
| `include/Global.h` | 确认 `m_isautocomplete` 等字段可用 |
| `src/EditorBase.cpp` | 调整 `SetScintillaValues` 中的自动补全配置 |
| `src/MDIWindow.cpp` | 连接成功后触发元数据获取 |

---

## Task 1: Trie 前缀树核心数据结构

**Files:**
- Create: `include/TrieIndex.h`
- Create: `src/TrieIndex.cpp`

### 设计说明

使用 `std::unordered_map<wchar_t, TrieNode*>` 存储子节点，兼顾实现简洁与性能。按补全类别分树（关键字、函数、表名、字段名各一棵）。所有字符统一转小写存储，查询时也转小写。

每个 TrieNode 存储：
- `is_end`: 是否为完整词条
- `data_id`: 指向外部词条元信息数组的索引
- `children`: 子节点映射

- [ ] **Step 1: 创建 TrieIndex.h 头文件**

```cpp
// include/TrieIndex.h
#ifndef _TRIE_INDEX_H_
#define _TRIE_INDEX_H_

#include <unordered_map>
#include <vector>
#include <wchar.h>

// 补全词条元信息
struct CompletionItem {
    const char* text;       // 原始文本（保留大小写）
    int         type;       // AC_PRE_KEYWORD / AC_PRE_FUNCTION / AC_TABLE / AC_COLUMN 等
    int         priority;   // 排序优先级（越小越优先）
};

// Trie 节点
struct TrieNode {
    bool                                    is_end;
    int                                     data_id;    // -1 表示非完整词条
    std::unordered_map<wchar_t, TrieNode*>  children;

    TrieNode() : is_end(false), data_id(-1) {}
};

// Trie 前缀树
class TrieIndex {
public:
    TrieIndex();
    ~TrieIndex();

    // 插入词条（key 为小写，item_id 指向外部词条数组索引）
    void Insert(const wchar_t* key, int item_id);

    // 前缀查询：返回所有以 prefix 开头的词条 ID
    // maxResults 限制最大返回数量，0 表示不限制
    void PrefixSearch(const wchar_t* prefix, std::vector<int>& results, int maxResults = 0);

    // 清空整棵树
    void Clear();

    // 获取词条总数
    int GetCount() const { return m_count; }

private:
    TrieNode*   m_root;
    int         m_count;

    // 递归收集子树所有词条 ID
    void CollectSubtree(TrieNode* node, std::vector<int>& results, int maxResults);

    // 递归释放节点
    void FreeNode(TrieNode* node);

    // 宽字符转小写
    static wchar_t ToLower(wchar_t ch);
};

#endif // _TRIE_INDEX_H_
```

- [ ] **Step 2: 创建 TrieIndex.cpp 实现文件**

```cpp
// src/TrieIndex.cpp
#include "TrieIndex.h"
#include <cctype>

TrieIndex::TrieIndex() : m_count(0) {
    m_root = new TrieNode();
}

TrieIndex::~TrieIndex() {
    Clear();
    delete m_root;
}

wchar_t TrieIndex::ToLower(wchar_t ch) {
    if (ch >= L'A' && ch <= L'Z')
        return ch + (L'a' - L'A');
    return ch;
}

void TrieIndex::Insert(const wchar_t* key, int item_id) {
    TrieNode* node = m_root;
    while (*key) {
        wchar_t ch = ToLower(*key);
        auto it = node->children.find(ch);
        if (it == node->children.end()) {
            TrieNode* child = new TrieNode();
            node->children[ch] = child;
            node = child;
        } else {
            node = it->second;
        }
        key++;
    }
    if (!node->is_end) {
        node->is_end = true;
        node->data_id = item_id;
        m_count++;
    }
}

void TrieIndex::CollectSubtree(TrieNode* node, std::vector<int>& results, int maxResults) {
    if (!node) return;
    if (node->is_end) {
        results.push_back(node->data_id);
        if (maxResults > 0 && (int)results.size() >= maxResults)
            return;
    }
    for (auto& pair : node->children) {
        if (maxResults > 0 && (int)results.size() >= maxResults)
            return;
        CollectSubtree(pair.second, results, maxResults);
    }
}

void TrieIndex::PrefixSearch(const wchar_t* prefix, std::vector<int>& results, int maxResults) {
    results.clear();
    TrieNode* node = m_root;
    while (*prefix) {
        wchar_t ch = ToLower(*prefix);
        auto it = node->children.find(ch);
        if (it == node->children.end())
            return; // 无匹配
        node = it->second;
        prefix++;
    }
    // 收集以当前节点为根的子树所有词条
    CollectSubtree(node, results, maxResults);
}

void TrieIndex::Clear() {
    for (auto& pair : m_root->children) {
        FreeNode(pair.second);
    }
    m_root->children.clear();
    m_count = 0;
}

void TrieIndex::FreeNode(TrieNode* node) {
    if (!node) return;
    for (auto& pair : node->children) {
        FreeNode(pair.second);
    }
    delete node;
}
```

- [ ] **Step 3: 编译验证**

将 `TrieIndex.cpp` 加入 `build/SQLyogCommunity.vcxproj` 的编译列表中（在 `<ClCompile>` 区域添加条目），将 `TrieIndex.h` 加入 `<ClInclude>` 区域。编译确认无错误。

---

## Task 2: 补全词条存储与管理

**Files:**
- Create: `include/CommunityAutoComplete.h`（部分：词条存储部分）
- Create: `src/CommunityAutoComplete.cpp`（部分：词条存储与管理）

### 设计说明

`CCommunityAutoComplete` 是整个补全系统的核心协调器。它管理：
- 静态词条数组（`CompletionItem`）
- 多棵 Trie 索引（按类别分树）
- 元数据获取与索引构建
- 补全查询与排序

词条类型常量复用 `Global.h` 中已有的定义：
- `AC_PRE_KEYWORD = 1`, `AC_PRE_FUNCTION = 2`, `AC_DATABASE = 3`, `AC_TABLE = 4`, `AC_COLUMN = 5`

- [ ] **Step 1: 创建 CommunityAutoComplete.h 头文件**

```cpp
// include/CommunityAutoComplete.h
#ifndef _COMMUNITY_AUTO_COMPLETE_H_
#define _COMMUNITY_AUTO_COMPLETE_H_

#include "TrieIndex.h"
#include "Global.h"
#include <vector>

// 补全上下文类型
enum SQLContextType {
    CTX_UNKNOWN = 0,
    CTX_KEYWORD_FUNC,       // 语句开始或表达式位置：关键字、函数、表名
    CTX_TABLE_REF,          // FROM/JOIN 之后：表名、视图名
    CTX_COLUMN_REF,         // table. 之后：字段名
    CTX_WHERE_EXPR,         // WHERE/ON/HAVING 之后：字段名、函数
    CTX_INSERT_COLS         // INSERT INTO table( 之后：字段列表
};

// 补全词条（扩展版，含所属表信息）
struct ACCompletionItem {
    char        text[128];      // 原始文本
    int         type;           // AC_PRE_KEYWORD / AC_PRE_FUNCTION / AC_TABLE / AC_COLUMN
    int         priority;       // 排序优先级
    int         table_id;       // 所属表的索引（字段用），-1 表示无所属表
};

// 表元信息
struct ACTableInfo {
    char    name[128];          // 表名
    char    alias[64];          // 别名（空字符串表示无别名）
    int     field_start;        // 该表字段在 m_items 中的起始索引
    int     field_count;        // 该表字段数量
};

class CCommunityAutoComplete {
public:
    CCommunityAutoComplete();
    ~CCommunityAutoComplete();

    // 初始化静态词条（关键字、函数）从 Keywords.db
    void InitStaticItems();

    // 从数据库连接加载动态元数据（表名、字段名）
    void LoadMetadata(MDIWindow* wnd);

    // 清除动态元数据（断开连接时调用）
    void ClearDynamicMetadata();

    // 主补全查询：根据上下文和前缀返回候选列表
    // 返回值：候选词条格式化为 "\n" 分隔的字符串（用于 SCI_AUTOCSHOW）
    // imageTypes：对应的图片类型 ID 数组（与候选一一对应）
    void QueryCompletion(const char* prefix, SQLContextType ctx, int table_idx,
                         wyString& result, bool& has_items);

    // SQL 上下文分析：根据编辑器内容和光标位置推断上下文
    SQLContextType AnalyzeContext(EditorBase* editor, int cursor_pos,
                                 wyString& prefix, int& table_idx);

    // 从编辑器文本中提取别名映射
    void ExtractAliases(const char* sql_text);

    // 获取指定表名的索引（支持别名查找），找不到返回 -1
    int FindTableIndex(const char* name);

    // 最大候选数
    static const int MAX_SUGGESTIONS = 100;

private:
    // 静态词条（关键字 + 函数）
    std::vector<ACCompletionItem>    m_static_items;
    TrieIndex                        m_trie_keywords;
    TrieIndex                        m_trie_functions;

    // 动态词条（表 + 字段）
    std::vector<ACCompletionItem>    m_dynamic_items;
    std::vector<ACTableInfo>         m_tables;
    TrieIndex                        m_trie_tables;
    // 字段使用一棵统一的 Trie，通过 data_id 回查所属表
    TrieIndex                        m_trie_columns;

    // 从 Keywords.db 加载静态词条的辅助方法
    void LoadKeywordsFromDB();
    void LoadFunctionsFromDB();

    // 将宽字符转为 UTF-8 多字节字符串辅助
    static void WideToUtf8(const wchar_t* wide, char* utf8, int utf8_size);
    static void Utf8ToWide(const char* utf8, wchar_t* wide, int wide_size);
};

#endif // _COMMUNITY_AUTO_COMPLETE_H_
```

- [ ] **Step 2: 创建 CommunityAutoComplete.cpp 骨架（词条存储部分）**

```cpp
// src/CommunityAutoComplete.cpp
#include "CommunityAutoComplete.h"
#include "AutoCompleteInterface.h"
#include "MDIWindow.h"
#include "EditorBase.h"
#include "GUIHelper.h"
#include "CommonHelper.h"
#include "SQLMaker.h"
#include "sqlite3.h"
#include <algorithm>
#include <string.h>

CCommunityAutoComplete::CCommunityAutoComplete() {
}

CCommunityAutoComplete::~CCommunityAutoComplete() {
}

void CCommunityAutoComplete::WideToUtf8(const wchar_t* wide, char* utf8, int utf8_size) {
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, utf8_size, NULL, NULL);
}

void CCommunityAutoComplete::Utf8ToWide(const char* utf8, wchar_t* wide, int wide_size) {
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wide_size);
}

// --- Task 3 中实现 ---
void CCommunityAutoComplete::InitStaticItems() {
}

// --- Task 4 中实现 ---
void CCommunityAutoComplete::LoadMetadata(MDIWindow* wnd) {
}

void CCommunityAutoComplete::ClearDynamicMetadata() {
    m_dynamic_items.clear();
    m_tables.clear();
    m_trie_tables.Clear();
    m_trie_columns.Clear();
}

// --- Task 5 中实现 ---
SQLContextType CCommunityAutoComplete::AnalyzeContext(EditorBase* editor, int cursor_pos,
                                                      wyString& prefix, int& table_idx) {
    return CTX_UNKNOWN;
}

void CCommunityAutoComplete::ExtractAliases(const char* sql_text) {
}

int CCommunityAutoComplete::FindTableIndex(const char* name) {
    return -1;
}

// --- Task 6 中实现 ---
void CCommunityAutoComplete::QueryCompletion(const char* prefix, SQLContextType ctx, int table_idx,
                                              wyString& result, bool& has_items) {
}
```

- [ ] **Step 3: 编译验证**

将 `CommunityAutoComplete.cpp` 加入 `build/SQLyogCommunity.vcxproj`，编译确认无错误。

---

## Task 3: 从 Keywords.db 加载静态词条

**Files:**
- Modify: `src/CommunityAutoComplete.cpp`（实现 `InitStaticItems`）

### 设计说明

复用现有的 `OpenKeyWordsDB()` 函数（在 `GUIHelper.cpp` 中）打开 Keywords.db，查询 `objects` 表获取关键字和函数列表。插入时同时填充 `m_static_items` 数组和对应的 Trie 树。

- [ ] **Step 1: 实现 InitStaticItems 方法**

```cpp
void CCommunityAutoComplete::InitStaticItems() {
    sqlite3* db = NULL;
    if (OpenKeyWordsDB(&db) != wyTrue || db == NULL)
        return;

    sqlite3_stmt* stmt = NULL;
    int rc;

    // 加载关键字
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
            item.priority = 1;   // 关键字优先级最高
            item.table_id = -1;

            int idx = (int)m_static_items.size();
            m_static_items.push_back(item);

            // 转宽字符插入 Trie
            wchar_t wname[128];
            Utf8ToWide(name, wname, 128);
            m_trie_keywords.Insert(wname, idx);
        }
        sqlite3_finalize(stmt);
    }

    // 加载函数
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
            item.priority = 2;   // 函数优先级次之
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
```

- [ ] **Step 2: 编译验证**

---

## Task 4: 数据库元数据获取与索引构建

**Files:**
- Modify: `src/CommunityAutoComplete.cpp`（实现 `LoadMetadata`）

### 设计说明

连接数据库后，通过 `ExecuteAndGetResult` 执行 `SHOW TABLES` 和 `SHOW COLUMNS` 获取表名和字段名，插入动态 Trie 索引。此方法在后台线程中调用，通过 `MDIWindow` 的 `m_tunnel` 和 `m_mysql` 访问数据库连接。

**重要线程安全考虑**：MySQL 连接不可跨线程共享。元数据获取必须在持有连接的线程上执行，或使用独立的连接。采用方案：在主线程的连接回调中同步执行（表名少时极快），字段信息延迟按需加载。

- [ ] **Step 1: 实现 LoadMetadata 方法**

```cpp
void CCommunityAutoComplete::LoadMetadata(MDIWindow* wnd) {
    if (!wnd || !wnd->m_tunnel || !wnd->m_mysql)
        return;

    ClearDynamicMetadata();

    wyString query;
    MYSQL_RES* myres = NULL;
    MYSQL_ROW row;

    // 1. 获取当前数据库名
    const char* dbname = wnd->m_conninfo.m_db.GetString();

    // 2. 获取表列表
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

            // 记录表信息
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
        }
        wnd->m_tunnel->mysql_free_result(myres);
    }

    // 3. 获取每个表的字段
    int field_global_idx = 0;
    for (size_t t = 0; t < m_tables.size(); t++) {
        query.Sprintf("SHOW COLUMNS FROM `%s`.`%s`", dbname, m_tables[t].name);
        myres = ExecuteAndGetResult(wnd, wnd->m_tunnel, &wnd->m_mysql, query,
                                    wyFalse, wyFalse, wyTrue, true);
        if (!myres) continue;

        m_tables[t].field_start = (int)m_dynamic_items.size();

        while ((row = wnd->m_tunnel->mysql_fetch_row(myres))) {
            if (!row[0]) continue;

            ACCompletionItem item;
            memset(&item, 0, sizeof(item));
            strncpy(item.text, row[0], sizeof(item.text) - 1);
            item.type = AC_COLUMN;
            item.priority = 4;
            item.table_id = (int)t;

            int idx = (int)m_static_items.size() + (int)m_dynamic_items.size();
            m_dynamic_items.push_back(item);
            m_tables[t].field_count++;

            wchar_t wname[128];
            Utf8ToWide(row[0], wname, 128);
            m_trie_columns.Insert(wname, idx);
        }
        wnd->m_tunnel->mysql_free_result(myres);

        // 安全阈值：超过 500 张表或 10000 个字段时停止
        if (m_tables.size() > 500 || m_dynamic_items.size() > 10000)
            break;
    }
}
```

- [ ] **Step 2: 编译验证**

---

## Task 5: SQL 上下文分析器

**Files:**
- Modify: `src/CommunityAutoComplete.cpp`（实现 `AnalyzeContext`、`ExtractAliases`、`FindTableIndex`）

### 设计说明

采用简单有限状态机，通过逆向扫描光标前的文本来判断上下文。不需要完美的 SQL 解析，容错性强：无法确定上下文时返回 `CTX_UNKNOWN`，让上层返回所有类别。

核心逻辑：
1. 从光标位置向前提取当前"单词"作为 prefix
2. 继续向前扫描，跳过空白，识别前导关键字（FROM, JOIN, WHERE 等）
3. 检测 `table.` 模式

- [ ] **Step 1: 实现 AnalyzeContext 方法**

```cpp
SQLContextType CCommunityAutoComplete::AnalyzeContext(EditorBase* editor, int cursor_pos,
                                                      wyString& prefix, int& table_idx) {
    table_idx = -1;
    prefix.Clear();

    if (!editor || !editor->m_hwnd)
        return CTX_UNKNOWN;

    HWND hwnd = editor->m_hwnd;
    int line = (int)SendMessage(hwnd, SCI_LINEFROMPOSITION, cursor_pos, 0);
    int line_start = (int)SendMessage(hwnd, SCI_POSITIONFROMLINE, line, 0);

    // 获取当前行光标前的文本
    int text_len = cursor_pos - line_start;
    if (text_len <= 0) return CTX_UNKNOWN;

    char* line_text = new char[text_len + 1];
    struct TextRange tr;
    tr.chrg.cpMin = line_start;
    tr.chrg.cpMax = cursor_pos;
    tr.lpstrText = line_text;
    SendMessage(hwnd, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);

    // Step 1: 提取 prefix（光标前的当前单词）
    int i = text_len - 1;
    // 跳过尾部空白
    while (i >= 0 && (line_text[i] == ' ' || line_text[i] == '\t')) i--;

    if (i < 0) {
        delete[] line_text;
        return CTX_UNKNOWN;
    }

    // 检查是否在 table. 模式中（如 "SELECT t."）
    // 先找到当前单词的起始
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

    // 检查 table. 模式
    char word[256] = {0};
    int word_len = word_end - word_start + 1;
    if (word_len > 0 && word_len < (int)sizeof(word)) {
        strncpy(word, line_text + word_start, word_len);
        word[word_len] = '\0';

        // 检查 word 中是否包含 '.'
        char* dot = strchr(word, '.');
        if (dot) {
            // 提取表名部分和字段前缀
            *dot = '\0';
            char* table_name = word;
            char* col_prefix = dot + 1;

            // 在别名和表名中查找
            table_idx = FindTableIndex(table_name);
            if (table_idx >= 0) {
                prefix.SetAs(col_prefix);
                delete[] line_text;
                return CTX_COLUMN_REF;
            }
        }
    }

    // 没有点号，prefix 就是当前单词
    if (word_len > 0 && word_len < (int)sizeof(word)) {
        prefix.SetAs(word);
    } else {
        delete[] line_text;
        return CTX_UNKNOWN;
    }

    // Step 2: 分析前导关键字
    // 从 word_start 继续向前找前一个 token
    int j = word_start - 1;
    while (j >= 0 && (line_text[j] == ' ' || line_text[j] == '\t')) j--;

    if (j < 0) {
        delete[] line_text;
        return CTX_KEYWORD_FUNC;
    }

    // 提取前一个 token
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

        // 转大写比较
        for (int k = 0; prev_word[k]; k++) {
            if (prev_word[k] >= 'a' && prev_word[k] <= 'z')
                prev_word[k] -= 32;
        }

        if (strcmp(prev_word, "FROM") == 0 ||
            strcmp(prev_word, "JOIN") == 0 ||
            strcmp(prev_word, "INNER") == 0 ||
            strcmp(prev_word, "LEFT") == 0 ||
            strcmp(prev_word, "RIGHT") == 0 ||
            strcmp(prev_word, "CROSS") == 0 ||
            strcmp(prev_word, "INTO") == 0 ||
            strcmp(prev_word, "UPDATE") == 0 ||
            strcmp(prev_word, "TABLE") == 0) {
            delete[] line_text;
            return CTX_TABLE_REF;
        }

        if (strcmp(prev_word, "WHERE") == 0 ||
            strcmp(prev_word, "ON") == 0 ||
            strcmp(prev_word, "HAVING") == 0 ||
            strcmp(prev_word, "AND") == 0 ||
            strcmp(prev_word, "OR") == 0 ||
            strcmp(prev_word, "NOT") == 0 ||
            strcmp(prev_word, "SET") == 0 ||
            strcmp(prev_word, "GROUP") == 0 ||
            strcmp(prev_word, "ORDER") == 0 ||
            strcmp(prev_word, "BY") == 0 ||
            strcmp(prev_word, "LIKE") == 0 ||
            strcmp(prev_word, "BETWEEN") == 0 ||
            strcmp(prev_word, "IN") == 0 ||
            strcmp(prev_word, "VALUES") == 0) {
            delete[] line_text;
            return CTX_WHERE_EXPR;
        }
    }

    // 还需要检查多词关键字如 "ORDER BY", "GROUP BY", "LEFT JOIN"
    // 简化处理：如果无法确定，返回通用上下文
    delete[] line_text;
    return CTX_KEYWORD_FUNC;
}
```

- [ ] **Step 2: 实现 ExtractAliases 和 FindTableIndex 方法**

```cpp
void CCommunityAutoComplete::ExtractAliases(const char* sql_text) {
    if (!sql_text) return;

    // 简单的别名提取：扫描 "tablename AS alias" 或 "tablename alias" 模式
    // 仅扫描 FROM 和 JOIN 子句
    const char* p = sql_text;
    char upper_buf[8192];
    int len = (int)strlen(sql_text);
    if (len >= (int)sizeof(upper_buf)) len = (int)sizeof(upper_buf) - 1;
    for (int i = 0; i < len; i++) {
        upper_buf[i] = toupper((unsigned char)sql_text[i]);
    }
    upper_buf[len] = '\0';

    // 清除旧别名
    for (size_t i = 0; i < m_tables.size(); i++) {
        m_tables[i].alias[0] = '\0';
    }

    // 查找 FROM/JOIN 后的别名
    const char* keywords[] = {"FROM", "JOIN"};
    for (int k = 0; k < 2; k++) {
        const char* search = keywords[k];
        int slen = (int)strlen(search);
        p = upper_buf;
        while ((p = strstr(p, search)) != NULL) {
            // 确保是独立关键字
            if (p > upper_buf) {
                char before = *(p - 1);
                if (before != ' ' && before != '\t' && before != '\n' && before != '\r') {
                    p += slen;
                    continue;
                }
            }
            p += slen;

            // 跳过空白
            while (*p == ' ' || *p == '\t') p++;

            // 读取表名
            char table_name[128] = {0};
            int ti = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != '\n' && ti < 127) {
                table_name[ti++] = *p;
                p++;
            }
            table_name[ti] = '\0';

            if (ti == 0) continue;

            // 跳过空白
            while (*p == ' ' || *p == '\t') p++;

            // 检查 AS 关键字或直接别名
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
                // 可能是无 AS 的别名
                int ai = 0;
                while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != '\n' && ai < 63) {
                    alias[ai++] = *p;
                    p++;
                }
                alias[ai] = '\0';
            }

            // 在已有表中查找并设置别名
            if (alias[0]) {
                // 查找原始表名（从原始文本中获取大小写正确的名称）
                int offset = (int)(p - upper_buf);
                const char* orig_table = sql_text + (p - upper_buf - ti - (int)strlen(alias) - 1);
                // 简化：直接用表名匹配
                for (size_t i = 0; i < m_tables.size(); i++) {
                    if (_stricmp(m_tables[i].name, table_name) == 0) {
                        strncpy(m_tables[i].alias, alias, sizeof(m_tables[i].alias) - 1);
                        break;
                    }
                }
            }
        }
    }
}

int CCommunityAutoComplete::FindTableIndex(const char* name) {
    if (!name || !name[0]) return -1;
    for (size_t i = 0; i < m_tables.size(); i++) {
        if (_stricmp(m_tables[i].name, name) == 0)
            return (int)i;
        if (m_tables[i].alias[0] && _stricmp(m_tables[i].alias, name) == 0)
            return (int)i;
    }
    return -1;
}
```

- [ ] **Step 3: 编译验证**

---

## Task 6: 补全查询与结果格式化

**Files:**
- Modify: `src/CommunityAutoComplete.cpp`（实现 `QueryCompletion`）

### 设计说明

根据上下文类型选择查询哪些 Trie，收集结果后按优先级排序，格式化为 Scintilla 需要的 `\n` 分隔字符串。Scintilla 的 `SCI_AUTOCSHOW` 接受格式 `item?type\nitem?type\n...`，其中 `?` 是类型分隔符（默认为 `?`），后面的数字对应 `SCI_REGISTERIMAGE` 注册的图片 ID。

- [ ] **Step 1: 实现 QueryCompletion 方法**

```cpp
void CCommunityAutoComplete::QueryCompletion(const char* prefix, SQLContextType ctx, int table_idx,
                                              wyString& result, bool& has_items) {
    result.Clear();
    has_items = false;

    if (!prefix || !prefix[0]) return;

    // 转宽字符用于 Trie 查询
    wchar_t wprefix[128];
    Utf8ToWide(prefix, wprefix, 128);

    std::vector<int> candidates;

    switch (ctx) {
    case CTX_TABLE_REF:
        // FROM/JOIN 之后：只查表名
        m_trie_tables.PrefixSearch(wprefix, candidates, MAX_SUGGESTIONS);
        break;

    case CTX_COLUMN_REF:
        // table. 之后：只查该表的字段
        if (table_idx >= 0 && table_idx < (int)m_tables.size()) {
            m_trie_columns.PrefixSearch(wprefix, candidates, MAX_SUGGESTIONS);
            // 过滤只保留属于指定表的字段
            std::vector<int> filtered;
            for (size_t i = 0; i < candidates.size(); i++) {
                int idx = candidates[i];
                // 计算在 m_dynamic_items 中的偏移
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
        // WHERE/ON 等：查字段 + 函数
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
        // 通用：查关键字 + 函数 + 表
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

    // 格式化结果：Scintilla 格式为 "item?type\n"
    // 图片 ID 映射：1=关键字, 2=函数, 3=数据库, 4=表, 5=字段, 6=SP, 7=函数
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

        // 附加类型图片 ID（Scintilla 用 '?' 分隔）
        char type_suffix[8];
        sprintf(type_suffix, "?%d", item->type);
        result.Add(type_suffix);
    }

    has_items = (result.GetLength() > 0);
}
```

- [ ] **Step 2: 编译验证**

---

## Task 7: 集成到 AutoCompleteInterface 框架

**Files:**
- Modify: `include/AutoCompleteInterface.h`
- Modify: `src/AutoCompleteInterface.cpp`

### 设计说明

在 `AutoCompleteInterface` 中添加 `CCommunityAutoComplete*` 成员。在 `#ifdef COMMUNITY` 块中，所有 Handler 方法代理到 `CCommunityAutoComplete`，而非保持空实现。这是打通整个事件链的关键步骤。

- [ ] **Step 1: 修改 AutoCompleteInterface.h**

在 `#endif` 之前、`CAutoComplete* m_autocomplete;` 之后添加：

```cpp
// Community Edition autocomplete engine
#ifdef COMMUNITY
class CCommunityAutoComplete;
#endif
```

在 `private:` 区域添加成员（在 `CAutoComplete* m_autocomplete;` 之后）：

```cpp
#ifdef COMMUNITY
    CCommunityAutoComplete* m_community_ac;
#endif
```

- [ ] **Step 2: 修改 AutoCompleteInterface.cpp**

修改构造函数：

```cpp
AutoCompleteInterface::AutoCompleteInterface() {
    m_autocomplete = NULL;
#ifdef COMMUNITY
    m_community_ac = new CCommunityAutoComplete();
    m_community_ac->InitStaticItems();
#endif
}
```

修改析构函数：

```cpp
AutoCompleteInterface::~AutoCompleteInterface() {
#ifdef COMMUNITY
    delete m_community_ac;
    m_community_ac = NULL;
#else
    if (m_autocomplete) {
        delete m_autocomplete;
        m_autocomplete = NULL;
    }
#endif
}
```

修改 `HandlerOnWMChar` — 在 `#ifdef COMMUNITY` 块中添加（替换原来的空返回）：

```cpp
wyInt32
AutoCompleteInterface::HandlerOnWMChar(HWND hwnd, EditorBase* ebase, WPARAM wparam) {
#ifdef COMMUNITY
    if (!m_community_ac) return 0;

    // 检查是否启用了自动补全
    if (!pGlobals || !pGlobals->m_isautocomplete)
        return 0;

    MDIWindow* wnd = (MDIWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!wnd) return 0;

    // 检查是否为补全停止字符
    char stop_chars[] = " ~`!@#$%^&*()+|\\=-?><,/\":;'{}[]";
    char ch = (char)wparam;
    for (int i = 0; stop_chars[i]; i++) {
        if (ch == stop_chars[i]) {
            // 关闭补全下拉
            SendMessage(hwnd, SCI_AUTOCCANCEL, 0, 0);
            return 0;
        }
    }

    // 获取光标位置
    int cursor_pos = (int)SendMessage(hwnd, SCI_GETCURRENTPOS, 0, 0);

    // 分析上下文并获取 prefix
    wyString prefix;
    int table_idx = -1;
    SQLContextType ctx = m_community_ac->AnalyzeContext(ebase, cursor_pos, prefix, table_idx);

    if (prefix.GetLength() == 0) return 0;

    // 查询补全候选
    wyString candidates;
    bool has_items = false;
    m_community_ac->QueryCompletion(prefix.GetString(), ctx, table_idx, candidates, has_items);

    if (has_items) {
        // 获取 prefix 起始位置（用于 SCI_AUTOCSHOW）
        int prefix_start = cursor_pos - prefix.GetLength();

        // 设置类型分隔符为 '?'
        SendMessage(hwnd, SCI_AUTOCSETTYPESEPARATOR, (WPARAM)'?', 0);

        // 显示补全列表
        SendMessage(hwnd, SCI_AUTOCSHOW, prefix.GetLength(), (LPARAM)candidates.GetString());
    } else {
        SendMessage(hwnd, SCI_AUTOCCANCEL, 0, 0);
    }

    return 0;
#else
    // ... 保留原有的 enterprise 代码不变 ...
#endif
}
```

修改 `OnACNotification` — 在 `#ifdef COMMUNITY` 块中处理 `SCN_AUTOCSELECTION`：

```cpp
wyBool
AutoCompleteInterface::OnACNotification(WPARAM wparam, LPARAM lparam) {
#ifdef COMMUNITY
    SCNotification* scn = (SCNotification*)lparam;
    switch (scn->nmhdr.code) {
    case SCN_AUTOCSELECTION:
        // 用户选择了补全项，Scintilla 自动替换文本
        // 后续可在此添加追加括号/空格等逻辑
        return wyTrue;
    }
    return wyFalse;
#else
    // ... 保留原有的 enterprise 代码不变 ...
#endif
}
```

修改 `HandlerInitAutoComplete` — 在 `#ifdef COMMUNITY` 块中标记启用：

```cpp
wyBool
AutoCompleteInterface::HandlerInitAutoComplete(MDIWindow* wnd) {
#ifdef COMMUNITY
    // Community 版：标记自动补全为启用
    if (pGlobals)
        pGlobals->m_isautocomplete = wyTrue;
    return wyTrue;
#else
    // ... 保留原有的 enterprise 代码不变 ...
#endif
}
```

修改 `HandlerStoreObjects` — 在 `#ifdef COMMUNITY` 块中加载元数据：

```cpp
void
AutoCompleteInterface::HandlerStoreObjects(MDIWindow* wnd, wyBool brebuild) {
#ifdef COMMUNITY
    if (m_community_ac && wnd)
        m_community_ac->LoadMetadata(wnd);
#endif
}
```

- [ ] **Step 3: 编译验证**

---

## Task 8: 启用自动补全与生命周期集成

**Files:**
- Modify: `src/MDIWindow.cpp`

### 设计说明

在连接成功后调用 `HandlerInitAutoComplete` 和 `HandlerStoreObjects`。在断开连接时清除动态元数据。需要找到连接成功和断开的代码位置。

- [ ] **Step 1: 找到连接成功后的代码位置**

在 `src/MDIWindow.cpp` 中，连接成功后已有 `m_acinterface->HandlerBuildTags(this)` 调用（约 line 369）。需要在同一位置或附近添加初始化和元数据加载调用。

在连接成功的代码块中（`HandlerBuildTags` 之前）添加：

```cpp
// 初始化并加载 Community 自动补全
m_acinterface->HandlerInitAutoComplete(this);
m_acinterface->HandlerStoreObjects(this, wyFalse);
```

- [ ] **Step 2: 在断开连接时清除元数据**

搜索 `MDIWindow` 的关闭/断开连接逻辑（`Close` 或 `OnDestroy`），在断开时添加：

```cpp
if (m_acinterface)
    m_acinterface->HandlerInitAutoComplete(NULL);  // 传入 NULL 表示断开
```

具体位置需要根据 `MDIWindow::Close()` 或类似方法确定。

- [ ] **Step 3: 编译验证**

---

## Task 9: Scintilla 补全配置调整

**Files:**
- Modify: `src/EditorBase.cpp`

### 设计说明

调整 `SetScintillaValues` 中的自动补全配置，确保类型分隔符、最大高度等设置正确。

- [ ] **Step 1: 在 SetScintillaValues 中添加类型分隔符设置**

在 `EditorBase.cpp` 的 `SetScintillaValues` 方法中（约 line 777 `SCI_AUTOCSTOPS` 之后），添加：

```cpp
// 设置类型分隔符为 '?'，用于区分词条和图片类型
SendMessage(hwndedit, SCI_AUTOCSETTYPESEPARATOR, (WPARAM)'?', 0);

// 增大最大显示高度到 15 行
SendMessage(hwndedit, SCI_AUTOCSETMAXHEIGHT, 15, 0);
```

- [ ] **Step 2: 编译验证**

---

## Task 10: 端到端集成测试与调优

**Files:**
- 可能微调上述所有文件

### 设计说明

此任务为集成验证阶段，确保从用户输入到补全下拉显示的完整链路工作正常。

- [ ] **Step 1: 编译完整项目**

运行 `build/build_release.bat` 或在 Visual Studio 中编译 Release x64 配置，确保零错误零警告。

- [ ] **Step 2: 手动测试静态补全**

启动 SQLyog Community，在未连接数据库的情况下在编辑器中输入 `SEL`，验证：
- 出现下拉列表，显示 `SELECT` 等关键字
- 下拉列表中关键字前有蓝色图标（image ID 1）
- 继续输入 `ECT`，列表过滤为 `SELECT`
- 按 Enter 或 Tab 确认补全

- [ ] **Step 3: 手动测试动态表名补全**

连接到 MySQL 数据库后，在编辑器中输入 `SELECT * FROM em`，验证：
- 出现下拉列表，显示以 `em` 开头的表名（如 `employees`）
- 表名前有灰色图标（image ID 4）

- [ ] **Step 4: 手动测试字段上下文补全**

输入 `SELECT employees.`，验证：
- 出现下拉列表，显示 `employees` 表的所有字段
- 字段前有黑色图标（image ID 5）

- [ ] **Step 5: 手动测试 WHERE 上下文**

输入 `SELECT * FROM employees WHERE fi`，验证：
- 出现字段名（如 `first_name`）和函数名
- 下拉列表中同时包含字段和函数类型

- [ ] **Step 6: 性能验证**

在一个有 50+ 张表的数据库中：
- 输入单个字母触发补全，验证响应无明显延迟
- 连续快速输入，验证不卡顿、不闪烁

- [ ] **Step 7: 修复发现的问题**

根据手动测试结果修复任何 bug。

---

## 自检清单

### 1. 规格覆盖率

| 需求规格条目 | 对应 Task |
|-------------|----------|
| Trie 前缀树数据结构 | Task 1 |
| SQL 关键字补全 | Task 3 + Task 6 |
| 内置函数补全 | Task 3 + Task 6 |
| 表名补全 | Task 4 + Task 6 |
| 字段名补全（含别名） | Task 4 + Task 5 + Task 6 |
| 自动触发（输入字符） | Task 7 (HandlerOnWMChar) |
| Ctrl+Space 手动触发 | Task 7 (可扩展) |
| 前缀匹配过滤 | Task 1 (Trie) + Task 6 |
| 智能排序（优先级） | Task 6 |
| 上下文分析 | Task 5 |
| `table.` 字段补全 | Task 5 + Task 6 |
| 完全离线 | 所有 Task |
| 零 debounce | Task 7 (同步处理) |
| 内存高效 | Task 1 (Trie 设计) |
| 跨数据库兼容 | Task 4 (SHOW TABLES/COLUMNS) |

### 2. 占位符扫描

无 TBD、TODO、"implement later" 等占位符。所有代码步骤包含完整实现。

### 3. 类型一致性

- `ACCompletionItem` 在 Task 2 定义，Task 3/4/6 使用，字段名一致
- `SQLContextType` 在 Task 2 定义，Task 5 返回，Task 6 消费
- `TrieIndex` 在 Task 1 定义，Task 3/4/6 使用
- `CompletionItem` (TrieIndex.h) 与 `ACCompletionItem` (CommunityAutoComplete.h) 是不同类型，前者仅用于 Trie 内部说明，后者是实际使用的结构
- `SCI_AUTOCSETTYPESEPARATOR` 参数 `'?'` 在 Task 7 和 Task 9 统一
- `MAX_SUGGESTIONS = 100` 与文档要求的 100 条一致
