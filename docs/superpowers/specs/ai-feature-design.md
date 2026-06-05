# SQLyog AI 功能设计文档

> 版本：v1.0  
> 日期：2026-06-03  
> 状态：设计完成，待实施

---

## 1. 功能概述

为 SQLyog Community 增加 AI 功能，支持配置 OpenAI 兼容 API，提供 SQL 分析、对话、生成、美化四大能力。

### 1.1 功能清单

| 功能 | 触发方式 | 说明 |
|------|----------|------|
| SQL 分析 | 选中 SQL → 右键 "AI 分析" 或 AI Tab 输入 | 性能问题、语法检查、优化建议 |
| SQL 对话 | AI Tab 自由输入 | 多轮对话，带上下文 |
| SQL 生成 | AI Tab 输入自然语言 | 根据描述生成 SQL |
| SQL 美化 | 选中 SQL → 右键 "AI 美化" | AI 格式化/美化 SQL |

### 1.2 支持的 API 服务

支持所有 OpenAI Chat Completions 兼容 API：

| 服务 | URL 示例 |
|------|----------|
| OpenAI | `https://api.openai.com/v1/chat/completions` |
| 本地 Ollama | `http://localhost:11434/v1/chat/completions` |
| 本地 LM Studio | `http://localhost:1234/v1/chat/completions` |
| DeepSeek | `https://api.deepseek.com/v1/chat/completions` |
| 通义千问 | `https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions` |
| 其他 | 任何 OpenAI 兼容端点 |

---

## 2. 架构设计

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│  Tools 菜单                                                  │
│  ├─ AI Settings...  ← 配置对话框                             │
│  └─ Preferences...                                          │
├─────────────────────────────────────────────────────────────┤
│  SQL 编辑器右键菜单                                           │
│  ├─ ─────────────                                            │
│  ├─ AI &Analyze    ← 选中 SQL 分析                           │
│  └─ AI &Beautify   ← 选中 SQL 美化                           │
├─────────────────────────────────────────────────────────────┤
│  底部面板                                                    │
│  [Messages | Table Data | History | Info | AI]  ← 新增 Tab   │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  Scintilla 只读显示区（对话历史）                         │ │
│  │  ┌─────────────────────────────────────────────────┐    │ │
│  │  │ 👤 请分析这条SQL的性能问题                       │    │ │
│  │  │   SELECT * FROM users WHERE name LIKE '%a'      │    │ │
│  │  │ 🤖 这条SQL存在以下性能问题:                      │    │ │
│  │  │   1. LIKE '%a' 无法利用索引                      │    │ │
│  │  │   2. SELECT * 返回不必要的列                     │    │ │
│  │  └─────────────────────────────────────────────────┘    │ │
│  └─────────────────────────────────────────────────────────┘ │
│  ┌──────────────────────────────────────┐ [发送] [清除]       │
│  │ 多行输入框                            │                    │
│  └──────────────────────────────────────┘                    │
│  [状态: Ready / 正在请求...]                                  │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 类图

```
TabQueryTypes (abstract, existing)
    │
    └── TabAI                    ← 新增：AI Tab
            ├── m_hwnddisplay    (Scintilla, 只读, 显示对话)
            ├── m_hwndinput      (Edit, 多行, 用户输入)
            ├── m_hwndsend       (Button, 发送)
            ├── m_hwndclear      (Button, 清除)
            ├── m_hwndstatus     (Static, 状态)
            ├── m_history[]      (对话历史数组)
            └── m_istreaming     (流式请求状态)

AIService (static)               ← 新增：AI 服务层
    ├── LoadConfig()             (从 INI 读取配置)
    ├── SaveConfig()             (写入 INI)
    ├── SendRequest()            (非流式请求)
    └── SendRequestStreaming()   (流式请求, SSE)

AISettingsDlg (static)           ← 新增：配置对话框
    ├── Show()                   (显示对话框)
    └── DlgProc()                (对话框过程)

CHttp (existing, extended)       ← 修改：添加流式读取
    ├── SendData()               (existing)
    ├── GetResponse()            (existing)
    └── ReadResponseStreaming()  ← 新增：SSE 流式读取
```

### 2.3 数据流

```
用户输入 SQL → AI Tab 构造请求
         ↓
    AIService::BuildRequestJson()
    (构造 OpenAI Chat Completions JSON)
         ↓
    后台线程 (_beginthreadex)
         ↓
    CHttp::SendData() + ReadResponseStreaming()
    (POST 到 API, SSE 流式读取)
         ↓
    回调: OnStreamToken() → PostMessage(UM_AI_STREAM_TOKEN)
         ↓
    UI 线程: AppendToLastMessage() → Scintilla 追加文本
         ↓
    完成: PostMessage(UM_AI_STREAM_COMPLETE) → 更新状态
```

---

## 3. 文件清单

### 3.1 新增文件（6 个）

| 文件 | 行数估计 | 用途 |
|------|----------|------|
| `include/TabAI.h` | ~100 | AI Tab 类声明 |
| `src/TabAI.cpp` | ~600 | AI Tab 实现（UI 创建、事件处理、流式显示） |
| `include/AIService.h` | ~60 | AI 服务层声明 |
| `src/AIService.cpp` | ~350 | HTTP 请求构造、SSE 解析、配置管理 |
| `include/AISettingsDlg.h` | ~30 | 配置对话框声明 |
| `src/AISettingsDlg.cpp` | ~200 | 配置对话框实现 |

### 3.2 修改文件（9 个）

| 文件 | 修改内容 | 修改量 |
|------|----------|--------|
| `include/resource.h` | 新增 ~15 个 ID 定义 | +15 行 |
| `include/SQLyog.rc` | 菜单项 + 对话框资源 + 图标 | +50 行 |
| `include/Http.h` | 添加流式回调类型和方法声明 | +15 行 |
| `src/Http.cpp` | 实现 `ReadResponseStreaming()` | +80 行 |
| `include/TabMgmt.h` | 添加 `TabAI*` 成员和 `AddAITab()` | +5 行 |
| `src/TabMgmt.cpp` | 集成 AI Tab（Create/Resize/Delete） | +30 行 |
| `src/FrameWindow.cpp` | 处理 AI Settings 菜单命令 | +10 行 |
| `include/Global.h` | AI 配置全局变量 | +10 行 |
| `src/EditorBase.cpp` | 右键菜单添加 AI 项 | +30 行 |

---

## 4. 详细设计

### 4.1 资源定义

#### resource.h 新增 ID

```cpp
// AI 功能
#define IDD_AISETTINGS          44150   // AI 配置对话框
#define IDI_AIASSISTANT         44151   // AI Tab 图标
#define IDC_AI_URL              44152   // API URL 编辑框
#define IDC_AI_KEY              44153   // API Key 编辑框
#define IDC_AI_MODEL            44154   // 模型名编辑框
#define IDC_AI_TESTBTN          44155   // 测试连接按钮
#define IDC_AI_INPUT            44156   // AI Tab 输入框
#define IDC_AI_SENDBTN          44157   // 发送按钮
#define IDC_AI_CLEARBTN         44158   // 清除按钮
#define IDC_AI_STATUS           44159   // 状态标签
#define ID_TOOLS_AISETTINGS     41829   // Tools 菜单项
#define ID_AI_ANALYZE           41830   // 右键 "AI 分析"
#define ID_AI_BEAUTIFY          41831   // 右键 "AI 美化"
```

#### SQLyog.rc 新增资源

**Tools 菜单项**（在 `"&Preferences..."` 之前插入）：

```rc
MENUITEM "AI &Settings...",         ID_TOOLS_AISETTINGS
```

**AI 配置对话框**：

```rc
IDD_AISETTINGS DIALOGEX 0, 0, 320, 160
STYLE DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION
CAPTION "AI Settings"
FONT 8, "MS Shell Dlg", 400, 0, 0x1
BEGIN
    LTEXT       "API URL:",  IDC_STATIC,    10, 12,  50, 10
    EDITTEXT    IDC_AI_URL,                  65, 10,  240, 14, ES_AUTOHSCROLL
    LTEXT       "API Key:",  IDC_STATIC,    10, 32,  50, 10
    EDITTEXT    IDC_AI_KEY,                  65, 30,  240, 14, ES_AUTOHSCROLL | ES_PASSWORD
    LTEXT       "Model:",    IDC_STATIC,    10, 52,  50, 10
    EDITTEXT    IDC_AI_MODEL,                65, 50,  240, 14, ES_AUTOHSCROLL
    PUSHBUTTON  "Test",      IDC_AI_TESTBTN, 10, 72,  40, 14
    LTEXT       "",          IDC_AI_STATUS,  60, 75,  240, 10
    DEFPUSHBUTTON "OK",      IDOK,          200, 100, 50, 14
    PUSHBUTTON    "Cancel",  IDCANCEL,      255, 100, 50, 14
END
```

### 4.2 INI 配置存储

Section: `[AI]`

| Key | 说明 | 存储方式 |
|-----|------|----------|
| `AIUrl` | API 地址 | 明文 |
| `AIKey` | API Key | AES 加密 + Base64 |
| `AIModel` | 模型名 | 明文 |

加密复用现有 `EncodePassword()` / `DecodePassword()`（Crypto++ AES-128 CTR），与 MySQL 密码存储方式一致。

### 4.3 HTTP 流式读取

#### 新增回调类型和方法

```cpp
// Http.h 新增

// 回调类型：每次读取一个 chunk 调用一次
// 返回 true 继续，返回 false 停止
typedef bool (*StreamCallback)(const char* chunk, int chunkLen, void* userdata);

class CHttp {
public:
    // ... 现有方法 ...

    // 新增：流式读取响应（SSE 支持）
    bool ReadResponseStreaming(StreamCallback callback, void* userdata, bool* stop = NULL);
};
```

#### 实现要点

```cpp
// Http.cpp
bool CHttp::ReadResponseStreaming(StreamCallback callback, void* userdata, bool* stop)
{
    // 1. 复用 AllocHandles() + SendData() 发送请求
    // 2. 在 ReadResponse() 循环基础上，每读一个 chunk 调用回调
    // 3. 使用 INTERNET_OPTION_RECEIVE_TIMEOUT（8小时）保护长连接
    // 4. stop 参数支持中途取消

    char buffer[4096];
    DWORD buffersize, downloaded;

    do {
        InternetQueryDataAvailable(m_HttpOpenRequest, &buffersize, 0, 0);
        if (buffersize == 0) break;
        if (stop && *stop) break;

        // 限制单次读取大小
        if (buffersize > sizeof(buffer))
            buffersize = sizeof(buffer);

        yog_InternetReadFile(buffer, buffersize, &downloaded);
        if (downloaded == 0) break;

        // 调用回调交付 chunk
        if (!callback(buffer, downloaded, userdata))
            break;
    } while (true);

    return true;
}
```

### 4.4 SSE 响应解析

#### OpenAI 流式响应格式

```
data: {"id":"chatcmpl-xxx","choices":[{"delta":{"content":"Hello"},"index":0,"finish_reason":null}]}

data: {"id":"chatcmpl-xxx","choices":[{"delta":{"content":" world"},"index":0,"finish_reason":null}]}

data: {"id":"chatcmpl-xxx","choices":[{"delta":{},"index":0,"finish_reason":"stop"}]}

data: [DONE]
```

#### 解析逻辑

```cpp
bool AIService::ParseSSELine(const char* line, wyString& content)
{
    // 1. 检查 "data: " 前缀
    if (strncmp(line, "data: ", 6) != 0)
        return false;

    const char* data = line + 6;

    // 2. 跳过 [DONE]
    if (strcmp(data, "[DONE]") == 0)
        return false;

    // 3. 解析 JSON
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(data, data + strlen(data), root))
        return false;

    // 4. 提取 choices[0].delta.content
    if (root.isMember("choices") && root["choices"].isArray() &&
        root["choices"].size() > 0)
    {
        const Json::Value& delta = root["choices"][0]["delta"];
        if (delta.isMember("content"))
        {
            content.SetAs(delta["content"].asCString());
            return true;
        }
    }

    return false;
}
```

### 4.5 多轮对话上下文

#### 消息历史结构

```cpp
struct ChatMessage {
    wyString role;     // "user" / "assistant"
    wyString content;
};
wyArray m_history;  // ChatMessage* 指针数组
```

#### 请求构造

```cpp
wyString AIService::BuildRequestJson(const char* prompt, const char* systemPrompt,
                                      const Json::Value* history)
{
    Json::Value root;
    root["model"] = GetModelName().GetString();
    root["stream"] = true;

    Json::Value messages;

    // System prompt
    Json::Value sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;
    messages.append(sysMsg);

    // 历史消息（最近 5 轮 = 10 条）
    if (history && history->isArray())
    {
        int start = max(0, (int)history->size() - 10);
        for (int i = start; i < (int)history->size(); i++)
        {
            messages.append((*history)[i]);
        }
    }

    // 当前用户消息
    Json::Value userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = prompt;
    messages.append(userMsg);

    root["messages"] = messages;

    Json::FastWriter writer;
    wyString result;
    result.SetAs(writer.write(root).c_str());
    return result;
}
```

### 4.6 预置 System Prompts

```cpp
static const char* PROMPT_ANALYZE =
    "You are a MySQL expert. Analyze the given SQL for performance issues, "
    "potential bugs, and optimization suggestions. Reply in the same language "
    "as the user's question. Be concise and specific.";

static const char* PROMPT_BEAUTIFY =
    "You are a SQL formatter. Beautify and format the given SQL statement "
    "with proper indentation and line breaks. Only return the formatted SQL, "
    "no explanations.";

static const char* PROMPT_GENERATE =
    "You are a MySQL expert. Generate SQL statements based on the user's "
    "natural language description. Return only the SQL, with brief comments "
    "if needed.";

static const char* PROMPT_CHAT =
    "You are a MySQL database expert assistant. Help users with SQL questions, "
    "database design, performance tuning, and troubleshooting. "
    "Reply concisely and professionally.";
```

### 4.7 TabAI 类设计

#### 类声明

```cpp
// TabAI.h
#ifndef _TABAI_H_
#define _TABAI_H_

#include "TabQueryTypes.h"
#include "wyString.h"
#include "wyArray.h"

// 自定义消息
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

    // 外部调用：发送 SQL 到 AI
    void    SendSQLToAI(const wyChar* sql, const wyChar* action);

    // 清除对话历史
    void    ClearConversation();

private:
    // UI 控件
    HWND    m_hwnddisplay;   // 对话显示区（Scintilla，只读）
    HWND    m_hwndinput;     // 输入框（Edit，多行）
    HWND    m_hwndsend;      // 发送按钮
    HWND    m_hwndclear;     // 清除按钮
    HWND    m_hwndstatus;    // 状态标签

    // 窗口过程
    static LRESULT CALLBACK DisplayProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK InputProc(HWND, UINT, WPARAM, LPARAM);
    WNDPROC m_origdisplayproc;
    WNDPROC m_originputproc;

    // 对话历史
    struct ChatMessage {
        wyString role;
        wyString content;
    };
    wyArray m_history;

    // 流式请求状态
    bool     m_istreaming;
    wyString m_streambuf;
    HANDLE   m_hthread;
    bool     m_stopstream;

    // 内部方法
    void    AddMessageToDisplay(const wyChar* role, const wyChar* content);
    void    AppendToLastMessage(const wyChar* token);
    void    OnSend();
    void    OnStreamComplete(bool success, const wyChar* error = NULL);
    void    OnWMCommand(WPARAM wparam);

    // 线程
    struct StreamThreadParam {
        TabAI*   pthis;
        wyString prompt;
        wyString systemPrompt;
    };
    static unsigned __stdcall StreamThreadProc(void* param);
    static void OnStreamToken(const wyChar* token, void* userdata);
};

#endif
```

#### UI 布局

```
┌─────────────────────────────────────────────────────────┐
│ [Messages | Table Data | History | Info | AI]           │
│                                                         │
│ ┌─────────────────────────────────────────────────────┐ │
│ │                                                     │ │
│ │  Scintilla 只读显示区 (m_hwnddisplay)                │ │
│ │  - 用户消息：蓝色粗体                                │ │
│ │  - AI 回复：绿色正常                                │ │
│ │  - 错误信息：红色                                   │ │
│ │                                                     │ │
│ └─────────────────────────────────────────────────────┘ │
│                                                         │
│ ┌────────────────────────────────────┐ [发送] [清除]     │
│ │ 多行输入框 (m_hwndinput)           │                  │
│ └────────────────────────────────────┘                  │
│ [状态: Ready / 正在请求...]                              │
└─────────────────────────────────────────────────────────┘
```

#### Resize 逻辑

```cpp
void TabAI::Resize()
{
    RECT rc;
    GetClientRect(m_hwndparent, &rc);
    int tabH = CustomTab_GetTabHeight(m_hwndparent);
    int btnH = 20;
    int inputH = 60;
    int statusH = 16;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int displayH = h - tabH - inputH - btnH - statusH;

    MoveWindow(m_hwnddisplay, 0, tabH, w, displayH, TRUE);
    MoveWindow(m_hwndinput, 0, tabH + displayH, w - 120, inputH, TRUE);
    MoveWindow(m_hwndsend, w - 115, tabH + displayH, 55, btnH, TRUE);
    MoveWindow(m_hwndclear, w - 55, tabH + displayH, 55, btnH, TRUE);
    MoveWindow(m_hwndstatus, 0, tabH + displayH + inputH, w, statusH, TRUE);
}
```

### 4.8 线程安全设计

```
UI 线程                          后台线程
    │                                │
    ├── 用户点击发送 ──────────────→ │
    │                                ├── AIService::SendRequestStreaming()
    │   PostMessage(TOKEN) ←─────────┤   回调: 每收到一个 token
    │   AppendToLastMessage()        │
    │   PostMessage(TOKEN) ←─────────┤
    │   AppendToLastMessage()        │
    │   PostMessage(COMPLETE) ←──────┤   请求完成
    │   OnStreamComplete()           │
    │                                │
```

#### 线程函数

```cpp
unsigned __stdcall TabAI::StreamThreadProc(void* param)
{
    StreamThreadParam* p = (StreamThreadParam*)param;
    TabAI* pthis = p->pthis;

    bool ok = AIService::SendRequestStreaming(
        p->prompt.GetString(),
        p->systemPrompt.GetString(),
        TabAI::OnStreamToken,
        pthis,
        &pthis->m_stopstream
    );

    PostMessage(pthis->m_hwnddisplay, UM_AI_STREAM_COMPLETE,
                ok ? 1 : 0, (LPARAM)pthis);

    delete p;
    return 0;
}
```

#### 流式回调

```cpp
void TabAI::OnStreamToken(const wyChar* token, void* userdata)
{
    TabAI* pthis = (TabAI*)userdata;
    wyString* ptoken = new wyString(token);
    PostMessage(pthis->m_hwnddisplay, UM_AI_STREAM_TOKEN,
                0, (LPARAM)ptoken);
}
```

### 4.9 右键菜单集成

在 `EditorBase::OnContextMenuHelper()` 中追加：

```cpp
// 在 TrackPopupMenu 之前
AppendMenu(htrackmenu, MF_SEPARATOR, 0, NULL);
AppendMenu(htrackmenu, MF_STRING, ID_AI_ANALYZE,  _("AI &Analyze"));
AppendMenu(htrackmenu, MF_STRING, ID_AI_BEAUTIFY, _("AI &Beautify"));
```

处理逻辑（在 `FrameWindow::OnWmCommand()` 中）：

```cpp
case ID_AI_ANALYZE:
case ID_AI_BEAUTIFY:
{
    // 1. 获取当前 MDI 窗口的编辑器
    MDIWindow* wnd = GetActiveMDIWindow();
    if (!wnd) break;
    TabEditor* tab = wnd->m_pctabmodule->GetActiveTabEditor();
    if (!tab) break;
    EditorBase* editor = tab->m_pcqueryedit;
    if (!editor) break;

    // 2. 获取选中文本
    wyString seltext;
    SendMessage(editor->GetHWND(), SCI_GETSELTEXT, 0, (LPARAM)tmp);

    // 3. 切换到 AI Tab 并发送
    TabAI* aitab = tab->m_pctabmgmt->m_paitab;
    if (aitab)
    {
        const char* action = (LOWORD(wparam) == ID_AI_ANALYZE) ? "analyze" : "beautify";
        aitab->SendSQLToAI(seltext.GetString(), action);
    }
    break;
}
```

---

## 5. 技术依赖

### 5.1 已有依赖（复用）

| 库 | 用途 | 文件位置 |
|----|------|----------|
| WinInet | HTTP 请求 | `Http.h` / `Http.cpp` |
| jsoncpp 1.8.3 | JSON 构造/解析 | `jsoncpp.h` / `jsoncpp.cpp` |
| Crypto++ (AES) | API Key 加密 | `CommonHelper.h` / `CommonHelper.cpp` |
| Scintilla | 对话显示区 | 已有控件 |
| wyString | 字符串处理 | `wyString.h` |
| wyIni | INI 文件读写 | `wyIni.h` |

### 5.2 新增依赖

无。所有功能基于现有库实现。

---

## 6. 实施步骤

| 步骤 | 内容 | 依赖 | 预估工时 |
|------|------|------|----------|
| 1 | 资源定义（resource.h + SQLyog.rc） | 无 | 0.5h |
| 2 | Global.h 添加 AI 配置变量 | 无 | 0.5h |
| 3 | Http.h/cpp 添加流式读取 | 无 | 2h |
| 4 | AISettingsDlg 配置对话框 | 步骤 1, 2 | 2h |
| 5 | AIService 服务层 | 步骤 2, 3 | 3h |
| 6 | TabAI UI 实现 | 步骤 1, 5 | 4h |
| 7 | TabMgmt 集成 | 步骤 6 | 1h |
| 8 | FrameWindow 菜单处理 | 步骤 4 | 1h |
| 9 | EditorBase 右键菜单 | 步骤 6 | 1h |
| 10 | FrameWindowHelper 配置读写 | 步骤 2 | 1h |

**总计预估**：约 16 小时

---

## 7. 验证方案

### 7.1 编译验证

- Debug x64 编译通过，无错误无警告
- Release x64 编译通过

### 7.2 配置验证

- [ ] Tools → AI Settings 打开对话框
- [ ] 输入 URL/Key/Model，点 OK 保存
- [ ] 重新打开对话框，值正确回显
- [ ] Test Connection 功能正常
- [ ] INI 文件中 Key 已加密存储

### 7.3 Tab 验证

- [ ] AI Tab 正确显示在底部面板
- [ ] 点击切换到 AI Tab
- [ ] 拖拽调整底部面板大小，AI Tab 内容自适应
- [ ] 切换到其他 Tab 后再切回来，内容保持

### 7.4 对话验证

- [ ] 输入框输入问题，点击发送
- [ ] 流式输出逐字显示
- [ ] 状态栏显示 "正在请求..."
- [ ] 请求完成后状态变为 "Ready"
- [ ] 点击清除按钮，对话历史清空

### 7.5 SQL 分析验证

- [ ] 在编辑器输入 SQL，选中，右键 → AI 分析
- [ ] 自动切换到 AI Tab，发送请求
- [ ] 分析结果正确显示

### 7.6 SQL 美化验证

- [ ] 选中未格式化的 SQL，右键 → AI 美化
- [ ] 美化后的 SQL 正确显示

### 7.7 多轮对话验证

- [ ] 连续发送 3 个问题
- [ ] AI 能引用之前的上下文
- [ ] 超过 5 轮后，旧消息被裁剪

### 7.8 错误处理验证

- [ ] 错误的 URL → 显示连接错误
- [ ] 错误的 Key → 显示认证错误
- [ ] 空输入 → 不发送请求
- [ ] 请求超时 → 显示超时提示

---

## 8. AI SQL 智能预测（补全增强）

> 版本：v1.0
> 日期：2026-06-04
> 状态：设计完成，待实施

### 8.1 功能概述

在现有 SQL 补全列表中混入 AI 预测结果，用特殊标识区分。AI 预测基于当前 SQL 上下文和数据库 schema，提供更智能的补全建议。

### 8.2 交互设计

```
┌─────────────────────────────────────────────────────────────┐
│ SELECT * FROM employees WHERE                               │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ 🔵 SELECT                    (关键字)                    │ │
│ │ 🔵 SET                       (关键字)                    │ │
│ │ 🤖 salary > 50000            [AI] ⚡                     │ │  ← AI 预测
│ │ 🤖 status = 'active'         [AI] ⚡                     │ │  ← AI 预测
│ │ 🤖 hire_date > '2024-01-01'  [AI] ⚡                     │ │  ← AI 预测
│ │ 🟢 SUM()                     (函数)                      │ │
│ │ 🟢 AVG()                     (函数)                      │ │
│ └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### 8.3 架构设计

#### 8.3.1 整体流程

```
用户输入字符
      │
      ▼
┌─────────────────┐
│ HandlerOnWMChar  │
│ (现有补全入口)   │
└────────┬────────┘
         │
         ├──────────────────────────────────┐
         │                                  │
         ▼                                  ▼
┌─────────────────┐                 ┌─────────────────┐
│ Trie 本地补全   │                 │ AI 预测请求     │
│ (立即返回)      │                 │ (后台线程)      │
└────────┬────────┘                 └────────┬────────┘
         │                                  │
         ▼                                  ▼
┌─────────────────┐                 ┌─────────────────┐
│ SCI_AUTOCSHOW   │                 │ 合并结果        │
│ (显示本地结果)  │ ←───────────────│ 重新显示        │
└─────────────────┘                 └─────────────────┘
```

#### 8.3.2 类图

```
AIPredictionService (新增)
    ├── RequestPrediction(prefix, context, callback)
    ├── CancelPendingRequests()
    ├── DebounceTimer (500ms)
    └── m_lastRequestId (防重复)

CommunityAutoComplete (修改)
    ├── QueryCompletion() ← 增加 AI 结果合并
    ├── MergeAIResults(local_results, ai_results)
    └── FormatWithAI标记(items)
```

### 8.4 详细设计

#### 8.4.1 AI 预测请求构造

```cpp
// System Prompt
static const char* PROMPT_PREDICT =
    "You are a MySQL expert assistant. Given the current SQL context and database schema, "
    "predict the most likely next SQL clause or expression the user wants to write. "
    "Return up to 5 suggestions, one per line. Only return the SQL fragment, no explanations. "
    "Consider:\n"
    "1. Common SQL patterns\n"
    "2. The table/column names in the schema\n"
    "3. The current cursor position in the SQL statement\n"
    "4. Syntactic correctness";

// 请求 JSON 构造
wyString AIService::BuildPredictionRequestJson(const char* prefix,
                                                const char* sqlContext,
                                                const char* schemaInfo)
{
    Json::Value root;
    root["model"] = GetModelName().GetString();
    root["stream"] = false;  // 预测用非流式，减少延迟
    root["max_tokens"] = 200;
    root["temperature"] = 0.3;  // 低温度，更确定性的结果

    Json::Value messages;

    // System prompt
    Json::Value sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = PROMPT_PREDICT;
    messages.append(sysMsg);

    // 用户消息：包含上下文
    Json::Value userMsg;
    userMsg["role"] = "user";

    wyString prompt;
    prompt.Sprintf("Database Schema:\n%s\n\nCurrent SQL:\n%s\n\nCursor prefix: %s\n\n"
                   "Predict the next SQL fragment:",
                   schemaInfo, sqlContext, prefix);
    userMsg["content"] = prompt.GetString();
    messages.append(userMsg);

    root["messages"] = messages;

    Json::FastWriter writer;
    wyString result;
    result.SetAs(writer.write(root).c_str());
    return result;
}
```

#### 8.4.2 防抖与请求管理

```cpp
class AIPredictionService {
public:
    // 请求预测（带防抖）
    void RequestPrediction(const char* prefix, const char* sqlContext,
                           const char* schemaInfo, PredictionCallback callback);

    // 取消所有待处理请求
    void CancelPending();

    // 防抖延迟（毫秒）
    static const int DEBOUNCE_MS = 500;

private:
    // 防抖定时器
    HANDLE m_debounceTimer;
    bool m_timerActive;

    // 当前请求 ID（用于忽略过期响应）
    volatile LONG m_currentRequestId;

    // 待处理的请求参数
    wyString m_pendingPrefix;
    wyString m_pendingContext;
    wyString m_pendingSchema;
    PredictionCallback m_pendingCallback;

    // 定时器回调
    static VOID CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

    // 实际发送请求
    void DoRequest();
};
```

#### 8.4.3 结果合并策略

```cpp
void CCommunityAutoComplete::MergeAIResults(const wyString& localResults,
                                              const wyString& aiResults,
                                              wyString& mergedResult)
{
    mergedResult.Clear();

    // 1. 先添加本地结果
    if (localResults.GetLength() > 0) {
        mergedResult.Add(localResults);
    }

    // 2. 添加分隔符（如果两边都有结果）
    if (localResults.GetLength() > 0 && aiResults.GetLength() > 0) {
        mergedResult.Add("\n---\n");  // Scintilla 分隔线
    }

    // 3. 添加 AI 结果（带特殊标记）
    if (aiResults.GetLength() > 0) {
        // 解析 AI 结果，为每项添加 [AI] 前缀
        wyArray aiItems;
        SplitByNewline(aiResults, aiItems);

        for (int i = 0; i < aiItems.GetCount(); i++) {
            wyString* item = (wyString*)aiItems.GetAt(i);
            if (item && item->GetLength() > 0) {
                if (mergedResult.GetLength() > 0)
                    mergedResult.Add("\n");

                // 添加 AI 标记前缀和特殊图片 ID
                mergedResult.Add("[AI] ");
                mergedResult.Add(*item);
                mergedResult.Add("?8");  // 图片 ID 8 = AI 标记图标
            }
        }
    }
}
```

#### 8.4.4 Scintilla 图标注册

```cpp
// 在 EditorBase::SetScintillaValues 中注册 AI 图标
// 图标 ID 8 用于 AI 预测结果
SendMessage(hwndedit, SCI_REGISTERIMAGE, 8, (LPARAM)aiIconBitmap);
```

### 8.5 性能优化

| 优化点 | 策略 | 效果 |
|--------|------|------|
| 防抖 | 500ms 延迟，用户停止输入后才请求 | 减少 90% 无用请求 |
| 缓存 | 相同前缀+上下文的请求直接返回缓存 | 响应 < 10ms |
| 超时 | AI 请求 3 秒超时，超时只显示本地结果 | 保证流畅体验 |
| 并发 | 新请求自动取消旧请求 | 避免响应乱序 |
| 降级 | AI 不可用时自动禁用，不报错 | 不影响核心功能 |

### 8.6 配置选项

在 INI 文件 `[AI]` 节新增：

| Key | 说明 | 默认值 |
|-----|------|--------|
| `AIPredictionEnabled` | 是否启用 AI 预测 | `0`（关闭） |
| `AIPredictionDebounce` | 防抖延迟（毫秒） | `500` |
| `AIPredictionTimeout` | 请求超时（毫秒） | `3000` |
| `AIPredictionMaxItems` | AI 最大返回条数 | `5` |

---

## 9. 注释生成 SQL

> 版本：v1.0
> 日期：2026-06-04
> 状态：设计完成，待实施

### 9.1 功能概述

用户在编辑器中写自然语言注释，通过触发词调用 AI 自动生成对应的 SQL 语句。

### 9.2 交互设计

#### 9.2.1 基本用法

```sql
-- 查询所有员工的平均工资
-- @ai generate
```

按回车后，AI 自动生成：

```sql
-- 查询所有员工的平均工资
SELECT AVG(salary) AS avg_salary FROM employees;
```

#### 9.2.2 带参数的用法

```sql
-- 查询工资高于指定金额的员工
-- @ai generate
```

生成：

```sql
-- 查询工资高于指定金额的员工
SELECT * FROM employees WHERE salary > :amount;
```

#### 9.2.3 多行注释块

```sql
/*
 * 统计每个部门的员工数量和平均工资
 * 按平均工资降序排列
 * 只显示平均工资超过50000的部门
-- @ai generate
*/
```

生成：

```sql
/*
 * 统计每个部门的员工数量和平均工资
 * 按平均工资降序排列
 * 只显示平均工资超过50000的部门
 */
SELECT
    d.department_name,
    COUNT(e.employee_id) AS employee_count,
    AVG(e.salary) AS avg_salary
FROM departments d
JOIN employees e ON d.department_id = e.department_id
GROUP BY d.department_id, d.department_name
HAVING AVG(e.salary) > 50000
ORDER BY avg_salary DESC;
```

### 9.3 触发词设计

| 触发词 | 功能 | 示例 |
|--------|------|------|
| `-- @ai` | 生成 SQL | `-- @ai 查询所有员工` |
| `-- @ai generate` | 显式生成 | `-- @ai generate` |
| `-- @ai explain` | 解释 SQL | `-- @ai explain` (下方有 SQL 时) |
| `-- @ai optimize` | 优化 SQL | `-- @ai optimize` (下方有 SQL 时) |
| `-- @ai test` | 生成测试数据 | `-- @ai test` |

### 9.4 架构设计

#### 9.4.1 处理流程

```
用户在编辑器中输入
         │
         ▼
┌─────────────────────────┐
│ EditorBase::OnWMChar    │
│ 检测回车键              │
└────────┬────────────────┘
         │
         ▼
┌─────────────────────────┐
│ 检查光标前文本          │
│ 是否包含触发词          │
└────────┬────────────────┘
         │ 是
         ▼
┌─────────────────────────┐
│ 提取注释内容            │
│ (触发词上方的所有注释)  │
└────────┬────────────────┘
         │
         ▼
┌─────────────────────────┐
│ 构造 AI 请求            │
│ 发送到后台线程          │
└────────┬────────────────┘
         │
         ▼
┌─────────────────────────┐
│ AI 返回结果             │
│ 插入到注释下方          │
└─────────────────────────┘
```

#### 9.4.2 触发词检测

```cpp
// EditorBase.cpp
bool EditorBase::CheckAITrigger(int cursor_pos)
{
    HWND hwnd = m_hwnd;

    // 获取当前行
    int line = (int)SendMessage(hwnd, SCI_LINEFROMPOSITION, cursor_pos, 0);
    int line_start = (int)SendMessage(hwnd, SCI_POSITIONFROMLINE, line, 0);
    int line_end = (int)SendMessage(hwnd, SCI_GETLINEENDPOSITION, line, 0);

    // 获取当前行文本
    int line_len = line_end - line_start;
    if (line_len <= 0 || line_len > 1000) return false;

    char* line_text = new char[line_len + 1];
    struct TextRange tr;
    tr.chrg.cpMin = line_start;
    tr.chrg.cpMax = line_end;
    tr.lpstrText = line_text;
    SendMessage(hwnd, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
    line_text[line_len] = '\0';

    // 检查是否包含触发词
    bool found = false;
    const char* trigger = strstr(line_text, "@ai");
    if (trigger) {
        // 确保 @ai 在注释中（前面有 -- 或 // 或 /*）
        const char* comment_start = line_text;
        while (*comment_start == ' ' || *comment_start == '\t') comment_start++;

        if (strncmp(comment_start, "--", 2) == 0 ||
            strncmp(comment_start, "//", 2) == 0 ||
            strncmp(comment_start, "/*", 2) == 0) {
            found = true;
        }
    }

    delete[] line_text;
    return found;
}
```

#### 9.4.3 注释提取

```cpp
wyString EditorBase::ExtractCommentBlock(int trigger_line)
{
    HWND hwnd = m_hwnd;
    wyString result;

    // 向上扫描，收集连续的注释行
    int line = trigger_line - 1;  // 从触发行的上一行开始
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

        // 去除空白
        char* trimmed = line_text;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        // 检查是否是注释行
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

        // 提取注释内容（去掉注释符号）
        const char* content = trimmed;
        if (strncmp(content, "--", 2) == 0 ||
            strncmp(content, "//", 2) == 0) {
            content += 2;
        } else if (strncmp(content, "/*", 2) == 0) {
            content += 2;
        } else if (*content == '*') {
            content += 1;
        }

        // 去除前导空白
        while (*content == ' ' || *content == '\t') content++;

        // 去除尾部的 */ （如果有）
        char* end_marker = strstr(content, "*/");
        if (end_marker) *end_marker = '\0';

        // 添加到结果（倒序拼接）
        wyString line_str;
        line_str.SetAs(content);
        if (result.GetLength() > 0) {
            line_str.Add("\n");
            line_str.Add(result);
        }
        result = line_str;

        delete[] line_text;
        line--;
    }

    return result;
}
```

#### 9.4.4 AI Prompt 构造

```cpp
wyString AIService::BuildGenerateRequestJson(const char* comment, const char* schemaInfo)
{
    Json::Value root;
    root["model"] = GetModelName().GetString();
    root["stream"] = true;

    Json::Value messages;

    // System prompt
    Json::Value sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] =
        "You are a MySQL expert. Convert the user's natural language description into "
        "a valid MySQL SQL statement. Rules:\n"
        "1. Only return the SQL statement, no explanations\n"
        "2. Use standard MySQL syntax\n"
        "3. If the description is ambiguous, make reasonable assumptions\n"
        "4. Add brief comments if helpful\n"
        "5. Consider the provided database schema for table/column names";
    messages.append(sysMsg);

    // 用户消息
    Json::Value userMsg;
    userMsg["role"] = "user";

    wyString prompt;
    prompt.Sprintf("Database Schema:\n%s\n\nDescription:\n%s\n\nGenerate SQL:",
                   schemaInfo, comment);
    userMsg["content"] = prompt.GetString();
    messages.append(userMsg);

    root["messages"] = messages;

    Json::FastWriter writer;
    wyString result;
    result.SetAs(writer.write(root).c_str());
    return result;
}
```

### 9.5 结果插入

```cpp
void EditorBase::InsertGeneratedSQL(const char* sql, int trigger_line)
{
    HWND hwnd = m_hwnd;

    // 找到触发行的下一行位置
    int next_line = trigger_line + 1;
    int insert_pos;

    // 检查下一行是否为空行或不存在
    int max_line = (int)SendMessage(hwnd, SCI_GETLINECOUNT, 0, 0);
    if (next_line >= max_line) {
        // 在文件末尾插入
        insert_pos = (int)SendMessage(hwnd, SCI_GETTEXTLENGTH, 0, 0);
        // 先添加换行
        SendMessage(hwnd, SCI_INSERTTEXT, insert_pos, (LPARAM)"\n");
        insert_pos++;
    } else {
        insert_pos = (int)SendMessage(hwnd, SCI_POSITIONFROMLINE, next_line, 0);
    }

    // 开始撤销操作组
    SendMessage(hwnd, SCI_BEGINUNDOACTION, 0, 0);

    // 插入 SQL
    SendMessage(hwnd, SCI_INSERTTEXT, insert_pos, (LPARAM)sql);

    // 结束撤销操作组
    SendMessage(hwnd, SCI_ENDUNDOACTION, 0, 0);

    // 移动光标到插入内容的末尾
    int new_pos = insert_pos + (int)strlen(sql);
    SendMessage(hwnd, SCI_GOTOPOS, new_pos, 0);
}
```

### 9.6 配置选项

在 INI 文件 `[AI]` 节新增：

| Key | 说明 | 默认值 |
|-----|------|--------|
| `AICommentTriggerEnabled` | 是否启用注释触发 | `1`（开启） |
| `AICommentTriggerWord` | 自定义触发词 | `@ai` |

---

## 10. SQL 错误 AI 分析

> 版本：v1.0
> 日期：2026-06-04
> 状态：设计完成，待实施

### 10.1 功能概述

SQL 执行出错时，自动或手动调用 AI 分析错误原因，提供修复建议和正确的 SQL 示例。

### 10.2 交互设计

#### 10.2.1 自动触发

```
┌─────────────────────────────────────────────────────────────┐
│ SQL 编辑器                                                  │
│ SELECT * FROM employes WHERE id = 1;  ← 拼写错误            │
│ [执行]                                                      │
├─────────────────────────────────────────────────────────────┤
│ [Messages | Table Data | History | Info | AI]               │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ ❌ Error: Table 'db.employes' doesn't exist             │ │
│ │                                                         │ │
│ │ 🤖 [AI 分析]                                            │ │
│ │                                                         │ │
│ │ 错误原因：表名拼写错误，"employes" 应该是 "employees"   │ │
│ │                                                         │ │
│ │ 修复建议：                                              │ │
│ │ 1. 检查表名拼写                                        │ │
│ │ 2. 使用 SHOW TABLES 确认表名                           │ │
│ │                                                         │ │
│ │ 正确的 SQL：                                            │ │
│ │ SELECT * FROM employees WHERE id = 1;                   │ │
│ └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

#### 10.2.2 手动触发

用户在错误信息上右键，选择 "AI 分析此错误"。

### 10.3 常见错误类型与 Prompt

```cpp
// 错误分析 Prompt 模板
static const char* PROMPT_ERROR_ANALYSIS =
    "You are a MySQL expert. Analyze the SQL error and provide a solution.\n\n"
    "Rules:\n"
    "1. Explain the error cause clearly\n"
    "2. Provide specific fix suggestions\n"
    "3. Give the corrected SQL if possible\n"
    "4. If it's a common mistake, mention prevention tips\n"
    "5. Reply in the same language as the error message\n\n"
    "Format your response as:\n"
    "**错误原因：** [explanation]\n\n"
    "**修复建议：**\n"
    "1. [suggestion 1]\n"
    "2. [suggestion 2]\n\n"
    "**正确的 SQL：**\n"
    "```sql\n"
    "[corrected SQL]\n"
    "```";

// 常见错误类型识别
enum SQLErrorType {
    ERR_SYNTAX,           // 语法错误
    ERR_TABLE_NOT_FOUND,  // 表不存在
    ERR_COLUMN_NOT_FOUND, // 列不存在
    ERR_DUPLICATE_KEY,    // 主键/唯一键重复
    ERR_FOREIGN_KEY,      // 外键约束
    ERR_PERMISSION,       // 权限不足
    ERR_CONNECTION,       // 连接问题
    ERR_UNKNOWN           // 未知错误
};

// 根据错误类型构造更有针对性的 Prompt
wyString BuildErrorPrompt(const char* sql, const char* error, int error_code)
{
    wyString prompt;

    SQLErrorType type = ClassifyError(error, error_code);

    switch (type) {
    case ERR_TABLE_NOT_FOUND:
        prompt.Sprintf(
            "SQL执行出错，错误代码 %d\n\n"
            "SQL: %s\n\n"
            "错误信息: %s\n\n"
            "这是一个表不存在的错误。请：\n"
            "1. 检查表名是否拼写正确\n"
            "2. 列出可能的正确表名\n"
            "3. 提供修正后的 SQL",
            error_code, sql, error);
        break;

    case ERR_COLUMN_NOT_FOUND:
        prompt.Sprintf(
            "SQL执行出错，错误代码 %d\n\n"
            "SQL: %s\n\n"
            "错误信息: %s\n\n"
            "这是一个列名不存在的错误。请：\n"
            "1. 检查列名是否拼写正确\n"
            "2. 检查是否需要表名前缀\n"
            "3. 提供修正后的 SQL",
            error_code, sql, error);
        break;

    case ERR_SYNTAX:
        prompt.Sprintf(
            "SQL执行出错，错误代码 %d\n\n"
            "SQL: %s\n\n"
            "错误信息: %s\n\n"
            "这是一个语法错误。请：\n"
            "1. 指出语法错误的位置\n"
            "2. 解释正确的语法\n"
            "3. 提供修正后的 SQL",
            error_code, sql, error);
        break;

    default:
        prompt.Sprintf(
            "SQL执行出错，错误代码 %d\n\n"
            "SQL: %s\n\n"
            "错误信息: %s\n\n"
            "请分析错误原因并提供修复建议。",
            error_code, sql, error);
        break;
    }

    return prompt;
}
```

### 10.4 架构设计

#### 10.4.1 处理流程

```
SQL 执行
    │
    ▼
┌─────────────────┐
│ 执行失败        │
│ 获取错误信息    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ 显示错误信息    │
│ (现有行为)      │
└────────┬────────┘
         │
         ├──────────────────────────────────┐
         │ 自动模式                         │ 手动模式
         ▼                                  ▼
┌─────────────────┐                 ┌─────────────────┐
│ 检查配置        │                 │ 用户右键菜单    │
│ AutoAnalyze?    │                 │ "AI 分析错误"   │
└────────┬────────┘                 └────────┬────────┘
         │ 是                                │
         ▼                                  │
┌─────────────────┐                         │
│ 构造 AI 请求    │ ←───────────────────────┘
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ 切换到 AI Tab   │
│ 显示分析结果    │
└─────────────────┘
```

#### 10.4.2 集成点

在 SQL 执行错误处理处添加调用：

```cpp
// EditorQuery.cpp 或执行 SQL 的相关文件
void OnSQLExecutionComplete(MDIWindow* wnd, const char* sql,
                            bool success, const char* error_msg, int error_code)
{
    // 现有的错误处理逻辑...

    if (!success && error_msg) {
        // 新增：AI 错误分析
        if (pGlobals && pGlobals->m_aiconfig.m_auto_analyze_error) {
            // 构造 prompt
            wyString prompt = BuildErrorPrompt(sql, error_msg, error_code);

            // 获取 AI Tab 并发送
            TabMgmt* tabmgmt = wnd->m_pctabmodule->GetActiveTabModule();
            if (tabmgmt && tabmgmt->m_paitab) {
                // 切换到 AI Tab
                tabmgmt->SetActiveTab(TAB_AI);

                // 发送分析请求
                tabmgmt->m_paitab->SendSQLToAI(prompt.GetString(), "error_analysis");
            }
        }
    }
}
```

#### 10.4.3 右键菜单集成

```cpp
// 在错误信息显示区域（Scintilla）的右键菜单中添加
void OnContextMenu(HWND hwnd, LPARAM lparam)
{
    HMENU hmenu = CreatePopupMenu();

    // 现有菜单项...
    AppendMenu(hmenu, MF_STRING, ID_COPY, _("&Copy"));

    // 新增 AI 分析选项
    AppendMenu(hmenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hmenu, MF_STRING, ID_AI_ANALYZE_ERROR, _("&AI Analyze This Error"));

    // 显示菜单
    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hmenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hmenu);
}
```

### 10.5 错误分类器

```cpp
SQLErrorType ClassifyError(const char* error_msg, int error_code)
{
    if (!error_msg) return ERR_UNKNOWN;

    // 转换为小写以便匹配
    char lower_msg[4096];
    strncpy(lower_msg, error_msg, sizeof(lower_msg) - 1);
    lower_msg[sizeof(lower_msg) - 1] = '\0';
    for (int i = 0; lower_msg[i]; i++) {
        lower_msg[i] = tolower(lower_msg[i]);
    }

    // 根据错误代码和消息内容分类
    switch (error_code) {
    case 1064:  // SQL 语法错误
        return ERR_SYNTAX;

    case 1146:  // 表不存在
    case 1051:  // 未知表
        return ERR_TABLE_NOT_FOUND;

    case 1054:  // 列不存在
    case 1060:  // 列重复
        return ERR_COLUMN_NOT_FOUND;

    case 1062:  // 唯一键重复
    case 1586:  // 唯一键重复（新版本）
        return ERR_DUPLICATE_KEY;

    case 1451:  // 外键约束
    case 1452:  // 外键约束
        return ERR_FOREIGN_KEY;

    case 1045:  // 访问拒绝
    case 1142:  // 权限不足
        return ERR_PERMISSION;

    case 2003:  // 连接失败
    case 2006:  // 服务器消失
    case 2013:  // 连接丢失
        return ERR_CONNECTION;
    }

    // 根据消息内容进一步判断
    if (strstr(lower_msg, "doesn't exist") || strstr(lower_msg, "not found"))
        return ERR_TABLE_NOT_FOUND;

    if (strstr(lower_msg, "unknown column"))
        return ERR_COLUMN_NOT_FOUND;

    if (strstr(lower_msg, "syntax error") || strstr(lower_msg, "near"))
        return ERR_SYNTAX;

    return ERR_UNKNOWN;
}
```

### 10.6 配置选项

在 INI 文件 `[AI]` 节新增：

| Key | 说明 | 默认值 |
|-----|------|--------|
| `AIAutoAnalyzeError` | 出错时自动调用 AI 分析 | `0`（关闭） |
| `AIErrorAnalysisTimeout` | 分析请求超时（毫秒） | `10000` |

---

## 11. 后续扩展（可选）

- [ ] 对话导出为文件
- [ ] 自定义 System Prompt
- [ ] 温度参数调节
- [ ] 代码高亮渲染（AI 回复中的 SQL 代码块）
- [ ] 快捷键支持（Ctrl+Shift+A 打开 AI Tab）
- [ ] 对话历史持久化存储
- [ ] AI 预测结果本地缓存（LRU 缓存策略）
- [ ] 支持自定义注释触发词列表
- [ ] 错误分析历史记录与统计
- [ ] 批量 SQL 错误分析（选中多条 SQL 逐条分析）

---

## 9. 工程审计报告

> 审计日期：2026-06-03  
> 审计方法：/plan-eng-review  
> 审计范围：架构、代码质量、测试、性能

### 9.1 审计结果总览

| 维度 | 发现数 | P1 | P2 | P3 |
|------|--------|----|----|-----|
| 架构 | 4 | 2 | 2 | 0 |
| 代码质量 | 4 | 0 | 3 | 1 |
| 测试 | 1 | 0 | 1 | 0 |
| 性能 | 0 | 0 | 0 | 0 |
| **合计** | **9** | **2** | **6** | **1** |

所有问题已确认解决方案，详见下方。

### 9.2 架构问题及解决方案

#### [A1] SSE 行缓冲缺失（P1，confidence: 9/10）

**问题**：`ReadResponseStreaming()` 的回调交付原始 TCP chunk，但 `ParseSSELine()` 期望完整 SSE 行。TCP chunk 不保证对齐行边界，会导致解析失败或数据丢失。

**解决**：在 `CHttp::ReadResponseStreaming()` 内部实现行缓冲，回调只交付完整的 SSE 行（以 `\n` 结尾）。跨 chunk 的行在 CHttp 内部拼接，AIService 无需关心 chunk 边界。

**影响**：`include/Http.h`、`src/Http.cpp`

#### [A2] 缺少 Authorization Header（P1，confidence: 9/10）

**问题**：方案构造了 JSON body，但没有展示如何添加 `Authorization: Bearer <key>` header。`CHttp` 只有 `SetContentType()`，没有通用 header 设置方法。

**解决**：给 `CHttp` 新增 `SetHeader(const wyWChar* header)` 方法，支持添加任意 HTTP header。AIService 在发送前调用 `http.SetHeader(L"Authorization: Bearer <key>")`。

**影响**：`include/Http.h`、`src/Http.cpp`

#### [A3] 线程安全 — `m_stopstream` 无同步（P2，confidence: 8/10）

**问题**：`m_stopstream` 是普通 `bool`，UI 线程写、后台线程读，无同步保护。编译器可能优化掉后台线程的读取。

**解决**：将 `m_stopstream` 改为 `volatile LONG`，使用 `InterlockedExchange` 写入、`InterlockedCompareAccess` 读取。

**影响**：`include/TabAI.h`、`src/TabAI.cpp`

#### [A4] PostMessage 内存管理（P2，confidence: 8/10）

**问题**：`OnStreamToken` 中 `new wyString(token)` 通过 PostMessage 传递，如果窗口销毁前消息未处理，会导致内存泄漏。

**解决**：TabAI 析构函数中用 `PeekMessage` 循环清理未处理的 `UM_AI_STREAM_TOKEN` 消息，释放对应的 `wyString*`。

**影响**：`src/TabAI.cpp`

### 9.3 代码质量问题及解决方案

#### [C1] SSE 解析未处理 HTTP 错误状态码（P2，confidence: 9/10）

**问题**：API 返回 401/429/500 时，当前设计会尝试解析错误响应体为 SSE 流。

**解决**：在 `SendRequestStreaming()` 中，`SendData()` 之后、`ReadResponseStreaming()` 之前，调用 `CheckError()` 检查 HTTP 状态码。非 200 时用 `GetResponse()` 读取完整错误体，返回错误信息给 UI。

**影响**：`src/AIService.cpp`

#### [C2] 请求生命周期文档有误导（P2，confidence: 8/10）

**问题**：设计文档注释说 `ReadResponseStreaming` "复用 AllocHandles() + SendData()"，但这些是私有方法。实际上 `SendData()` 已完成请求发送，`ReadResponseStreaming` 只需替代 `ReadResponse()`。

**解决**：修正文档注释。调用顺序为：`SetUrl()` → `SetContentType()` → `SetHeader()` → `SendData()` → `ReadResponseStreaming()`。

**影响**：设计文档

#### [C3] 右键菜单代码 bug（P2，confidence: 9/10）

**问题**：`SCI_GETSELTEXT` 的 `tmp` 变量未定义且未分配缓冲区，会导致崩溃。

**解决**：使用与 `EditorQuery.cpp:204` 一致的模式：`AllocateBuff(end - start + 1)` → `SCI_GETSELTEXT` → `SetAs()` → `free()`。

**影响**：`src/FrameWindow.cpp`

#### [C4] 缺少取消请求机制（P2，confidence: 7/10）

**问题**：用户发送请求后无法取消，只能等待超时。

**解决**：请求进行中时，发送按钮切换为停止按钮（文本和功能都变）。点击停止设置 `m_stopstream = true`。切换 Tab 或关闭窗口时也自动取消。

**影响**：`src/TabAI.cpp`

### 9.4 测试问题及解决方案

#### [T1] 验证清单缺少边界情况（P2）

**问题**：当前验证清单（第 7 节）覆盖了主要流程，但缺少边界情况。

**解决**：补充以下验证项：

- [ ] 取消请求：发送后点停止，请求中断，状态恢复
- [ ] 防重入：流式进行中时点发送，不重复发起请求
- [ ] 空输入：输入框为空时点发送，无反应
- [ ] 未选中 SQL：编辑器无选中文本时右键 AI 分析，显示提示
- [ ] 跨 chunk 行缓冲：长回复的 SSE 数据跨 TCP chunk，解析正确
- [ ] Tab 关闭清理：请求进行中切换/关闭 Tab，线程正确停止，内存释放
- [ ] 窗口关闭清理：关闭 MDI 窗口时，后台线程正确终止
- [ ] 并发请求：快速连续发送两次，第一次自动取消

### 9.5 性能

无阻塞性问题。Scintilla 高频追加 token 的性能影响较小，因为 Scintilla 内部有批量更新优化。

### 9.6 审计决策记录

| # | 问题 | 决策 | 原因 |
|---|------|------|------|
| A1 | SSE 行缓冲 | CHttp 内部实现 | 职责内聚，调用者无需关心 |
| A2 | Auth Header | CHttp 加 SetHeader() | 通用化，未来可复用 |
| A3 | 线程安全 | volatile + Interlocked | 轻量级，符合项目现有模式 |
| A4 | 内存管理 | 析构时清理队列 | 简单有效，无需改架构 |
| C1 | HTTP 状态码 | 检查 + 读错误体 | 用户需要看到具体错误信息 |
| C2 | 文档误导 | 修正注释 | 文档准确性 |
| C3 | 右键菜单 bug | 修正为正确模式 | 与现有代码一致 |
| C4 | 取消机制 | 发送/停止按钮切换 | 用户体验必需 |
| T1 | 验证清单 | 补充边界情况 | 提高覆盖率 |

---

## 12. 工程审计报告（第 8/9/10 章）

> 审计日期：2026-06-04
> 审计方法：/plan-eng-review
> 审计范围：第 8 章（AI SQL 预测）、第 9 章（注释生成 SQL）、第 10 章（错误 AI 分析）

### 12.1 审计结果总览

| 维度 | 发现数 | P1 | P2 | P3 |
|------|--------|----|----|-----|
| 架构 | 6 | 3 | 3 | 0 |
| 代码质量 | 4 | 0 | 4 | 0 |
| 测试 | 1 | 1 | 0 | 0 |
| 性能 | 3 | 0 | 3 | 0 |
| **合计** | **14** | **4** | **10** | **0** |

所有问题已确认解决方案，详见下方。

### 12.2 架构问题及解决方案

#### [A5] AIPredictionService 与 AIService 职责重叠（P1，confidence: 9/10）

**问题**：Chapter 8 引入了新的 `AIPredictionService` 类来处理预测请求，但 `AIService` 已经封装了所有 HTTP 请求、SSE 解析、配置管理。新增一个平行的 AI 服务类违反 DRY，且两个类都需要访问 AIConfig、构造 HTTP 请求。

**解决**：将预测功能作为 `AIService` 的新方法（如 `SendPredictionRequest`），不创建独立类。

**影响**：`include/AIService.h`、`src/AIService.cpp`

#### [A6] Schema 信息获取未定义（P1，confidence: 9/10）

**问题**：三个功能都需要 `schemaInfo`（表名、列名）传给 AI，但设计没有说明如何获取。`CCommunityAutoComplete::LoadMetadata()` 已经获取了表/列信息，但没有暴露为可复用的 schema 字符串。

**解决**：在 `CCommunityAutoComplete` 中添加 `GetSchemaSummary()` 方法，返回当前连接的表/列摘要字符串。三个功能共用。

**影响**：`include/CommunityAutoComplete.h`、`src/CommunityAutoComplete.cpp`

#### [A7] AI 预测防抖与现有补全触发冲突（P1，confidence: 8/10）

**问题**：Chapter 8 设计了 `AIPredictionService` 内部的 500ms 防抖，但 `AutoCompleteInterface::HandlerOnWMChar` 每次按键都会触发补全。如果 AI 请求在后台进行，用户继续输入会触发新的 Trie 补全 + 新的 AI 请求，导致多个 AI 请求并发。

**解决**：将防抖逻辑放在 `AutoCompleteInterface` 层，统一管理本地补全和 AI 预测的触发时机。

**影响**：`include/AutoCompleteInterface.h`、`src/AutoCompleteInterface.cpp`

#### [A8] 错误分析集成点不明确（P2，confidence: 7/10）

**问题**：Chapter 10 引用了 `OnSQLExecutionComplete` 函数，但实际代码中 SQL 执行的错误处理分散在多个位置。

**解决**：在 `ExecuteQuery` 函数的错误返回路径中添加 AI 分析调用，这是所有 SQL 执行的统一出口。

**影响**：需要确认具体的执行文件

#### [A9] SCI_AUTOCSHOW 不支持动态更新（P2，confidence: 9/10）

**问题**：Chapter 8 设计了"先显示本地结果，AI 返回后更新"的流程，但 Scintilla 的 SCI_AUTOCSHOW 不支持向已显示的列表追加项目。调用 SCI_AUTOCSHOW 会替换整个列表，导致闪烁。

**解决**：先显示本地结果 → AI 返回后关闭旧列表 → 重新调用 SCI_AUTOCSHOW 显示合并结果。闪烁可接受。

**影响**：`src/AutoCompleteInterface.cpp`

#### [A10] 触发词检测与自动补全冲突（P2，confidence: 8/10）

**问题**：用户输入 `-- @ai` 时，`@` 和 `a`、`i` 都会触发自动补全，同时 Chapter 9 需要在 Enter 时检测触发词。如果补全列表弹出，用户按 Enter 会选择补全项而不是触发 AI 生成。

**解决**：在检测到触发词时禁用自动补全，AI 触发优先。

**影响**：`src/EditorBase.cpp`、`src/AutoCompleteInterface.cpp`

### 12.3 代码质量问题及解决方案

#### [C5] 未定义的 SplitByNewline 函数（P2，confidence: 9/10）

**问题**：Chapter 8 的 `MergeAIResults` 方法调用了 `SplitByNewline(aiResults, aiItems)` 但这个函数在代码库中不存在。

**解决**：使用现有的 wyString 或 std::string 方法手动解析，避免引入新的工具函数。

**影响**：`src/CommunityAutoComplete.cpp`

#### [C6] m_auto_analyze_error 字段不存在（P2，confidence: 9/10）

**问题**：Chapter 10 引用了 `pGlobals->m_aiconfig.m_auto_analyze_error` 但这个字段不存在。现有的 `AIConfig` 结构只有 `url`、`key`、`model` 三个字段。

**解决**：扩展 `AIConfig` 结构添加新的配置字段（如 `auto_analyze_error`、`prediction_enabled` 等）。

**影响**：`include/AIService.h`、`src/AIService.cpp`、INI 读写逻辑

#### [C7] SQL 获取逻辑未定义（P2，confidence: 8/10）

**问题**：Chapter 10 的 `BuildErrorPrompt` 接收 `sql` 参数，但没有说明这个 SQL 从哪里获取。

**解决**：使用 Scintilla 的 SCI_GETTEXT 或 SCI_GETSELTEXT 获取选中文本，传给 AI 进行分析。与现有右键菜单的 SQL 获取逻辑一致。

**影响**：错误处理代码

#### [C8] 错误分析 Prompt 模板重复（P2，confidence: 9/10）

**问题**：Chapter 10 的 `BuildErrorPrompt` 函数有大量重复的 Prompt 模板。每个错误类型的 Prompt 结构相同，只是中间的指导语不同。违反 DRY 原则。

**解决**：使用一个通用的 BuildErrorPrompt 函数，根据错误类型动态调整提示内容。减少代码重复。

**影响**：`src/AIService.cpp`

### 12.4 测试问题及解决方案

#### [T2] 测试覆盖率为 0%（P1）

**问题**：三个章节的所有代码路径和用户流程都没有测试用例。共有 35 个路径需要覆盖（20 个代码路径 + 15 个用户流程）。

**解决**：在设计文档中添加完整的测试清单，实施时逐个验证。

**测试覆盖图**：

```
CODE PATHS                                            USER FLOWS
[+] Chapter 8: AI SQL Prediction                      [+] Prediction trigger
  ├── AIService.SendPredictionRequest                   ├── [GAP] Type prefix → AI predicts → select item
  │   ├── [GAP] happy path                              ├── [GAP] Type fast → debounce fires once
  │   ├── [GAP] Timeout → show local only               ├── [GAP] AI timeout → local results remain
  │   └── [GAP] Cancel pending on new request           └── [GAP] Close autocomplete → cancel AI
  ├── CommunityAutoComplete.MergeAIResults
  │   ├── [GAP] Both local + AI results                 [+] Error states
  │   ├── [GAP] Only AI results (no local)              ├── [GAP] AI unavailable → silent fallback
  │   └── [GAP] Only local results (AI empty)           └── [GAP] Network error → local results shown
  └── AutoCompleteInterface (debounce)
      ├── [GAP] Debounce 500ms fires correctly
      └── [GAP] New keystroke cancels old timer

[+] Chapter 9: Comment-to-SQL                         [+] Comment trigger
  ├── EditorBase::CheckAITrigger                        ├── [GAP] "-- @ai generate" → Enter → SQL appears
  │   ├── [GAP] "-- @ai" detected                       ├── [GAP] "-- @ai explain" with SQL below
  │   ├── [GAP] "// @ai" detected                       ├── [GAP] Multi-line comment block
  │   └── [GAP] "/* @ai" detected                       └── [GAP] Trigger in non-comment → ignored
  ├── EditorBase::ExtractCommentBlock
  │   ├── [GAP] Single-line comment                     [+] Error states
  │   ├── [GAP] Multi-line "--" comments                ├── [GAP] AI timeout → no SQL inserted
  │   ├── [GAP] "/* */" block comments                  ├── [GAP] Empty comment → no request
  │   └── [GAP] Mixed comment styles                    └── [GAP] AI returns invalid SQL → insert as-is
  └── EditorBase::InsertGeneratedSQL
      ├── [GAP] Insert after trigger line
      ├── [GAP] Insert at end of file
      └── [GAP] Undo works correctly

[+] Chapter 10: Error Analysis                        [+] Error trigger
  ├── ClassifyError                                     ├── [GAP] Auto-trigger on error
  │   ├── [GAP] Error code 1146 → TABLE_NOT_FOUND      ├── [GAP] Right-click → AI Analyze
  │   ├── [GAP] Error code 1054 → COLUMN_NOT_FOUND     └── [GAP] Error in Messages tab → AI analyzes
  │   ├── [GAP] Error code 1064 → SYNTAX
  │   └── [GAP] Unknown code + message matching        [+] Error states
  ├── BuildErrorPrompt                                  ├── [GAP] AI timeout → error shown without analysis
  │   ├── [GAP] TABLE_NOT_FOUND prompt                  ├── [GAP] AI unavailable → silent
  │   └── [GAP] Default prompt                          └── [GAP] Very long error message → truncated?
  └── OnSQLExecutionComplete (hook)
      └── [GAP] Hook fires on execution failure

COVERAGE: 0/35 paths tested (0%)  |  Code paths: 0/20 (0%)  |  User flows: 0/15 (0%)
QUALITY: ★★★:0 ★★:0 ★:0  |  GAPS: 35 (8 E2E, 2 eval)
```

### 12.5 性能问题及解决方案

#### [P1] AI 预测结果缓存策略未实现（P2，confidence: 8/10）

**问题**：Chapter 8 性能优化表中提到了"缓存"但没有具体实现方案。每次用户输入都会触发 AI 请求（即使前缀相同），浪费 API 调用和增加延迟。

**解决**：在 CommunityAutoComplete 中添加 LRU 缓存（如最近 50 个结果），缓存最近的 AI 预测结果。相同前缀+上下文直接返回缓存。

**影响**：`include/CommunityAutoComplete.h`、`src/CommunityAutoComplete.cpp`

#### [P2] AI 请求模式不一致（P2，confidence: 9/10）

**问题**：Chapter 8 使用非流式（stream: false）因为需要快速返回；Chapter 9 使用流式因为需要逐字显示；Chapter 10 未明确指定。

**解决**：Chapter 10 也使用非流式，因为错误分析结果需要完整显示，逐字显示没有价值。

**影响**：`src/AIService.cpp`

#### [P3] Schema 信息重复获取（P2，confidence: 8/10）

**问题**：三个功能都需要 schemaInfo，如果每次都从数据库重新获取（SHOW TABLES/COLUMNS），会增加延迟。

**解决**：Schema 信息在连接时获取并缓存为字符串，后续请求直接使用缓存的字符串。避免重复查询数据库。

**影响**：`include/CommunityAutoComplete.h`、`src/CommunityAutoComplete.cpp`

### 12.6 审计决策记录

| # | 问题 | 决策 | 原因 |
|---|------|------|------|
| A5 | AIPredictionService 重叠 | 合并到 AIService | DRY，职责单一 |
| A6 | Schema 获取未定义 | 添加 GetSchemaSummary() | 三个功能共用 |
| A7 | 防抖冲突 | 集成到 AutoCompleteInterface | 统一管理触发时机 |
| A8 | 错误分析集成点 | Hook 到 ExecuteQuery | 统一出口 |
| A9 | SCI_AUTOCSHOW 更新 | 关闭后重新显示 | 闪烁可接受 |
| A10 | 触发词冲突 | AI 触发优先 | 用户体验优先 |
| C5 | SplitByNewline 未定义 | 使用现有字符串方法 | 避免新工具函数 |
| C6 | 字段不存在 | 扩展 AIConfig | 复用现有结构 |
| C7 | SQL 获取未定义 | 使用 SCI_GETTEXT | 与现有逻辑一致 |
| C8 | Prompt 重复 | 统一构造函数 | DRY |
| T2 | 测试覆盖 0% | 添加完整测试清单 | 35 个路径全覆盖 |
| P1 | 缓存未实现 | 添加 LRU 缓存 | 减少 API 调用 |
| P2 | 请求模式不一致 | Chapter 10 非流式 | 完整显示更有价值 |
| P3 | Schema 重复获取 | 缓存 Schema 字符串 | 避免重复查询 |

### 12.7 NOT in scope

- AI 预测的模型微调或本地模型支持
- 多数据库方言支持（仅 MySQL）
- AI 结果的语法高亮渲染
- 对话历史持久化存储
- 自定义 System Prompt 配置 UI

### 12.8 What already exists

| 组件 | 状态 | 复用情况 |
|------|------|----------|
| AIService (HTTP + SSE) | ✅ 已实现 | 完全复用，扩展新方法 |
| TabAI (对话 UI) | ✅ 已实现 | 完全复用 |
| CommunityAutoComplete | ✅ 已实现 | 扩展 MergeAIResults + GetSchemaSummary |
| AutoCompleteInterface | ✅ 已实现 | 扩展防抖逻辑 |
| EditorBase | ✅ 已实现 | 扩展触发词检测 |
| AIConfig | ✅ 已实现 | 扩展新字段 |

### 12.9 Worktree 并行化策略

| 步骤 | 模块 | 依赖 |
|------|------|------|
| AIService 扩展 | AIService | — |
| CommunityAutoComplete 扩展 | CommunityAutoComplete | AIService (GetSchemaSummary) |
| AutoCompleteInterface 防抖 | AutoCompleteInterface | AIService |
| EditorBase 触发词 | EditorBase | — |
| 错误分析 Hook | ExecuteQuery | AIService |
| 测试清单 | 设计文档 | — |

**并行执行**：
- Lane A: AIService 扩展 → CommunityAutoComplete 扩展 → AutoCompleteInterface 防抖
- Lane B: EditorBase 触发词（独立）
- Lane C: 错误分析 Hook（独立）

**执行顺序**：Lane A + B + C 可并行启动。Lane A 内部顺序执行。

---

## GSTACK REVIEW REPORT

| Review | Trigger | Why | Runs | Status | Findings |
|--------|---------|-----|------|--------|----------|
| Eng Review | `/plan-eng-review` | Architecture & tests (required) | 1 | issues_open | 14 issues, 0 critical gaps |

- **UNRESOLVED:** 0 unresolved decisions
- **VERDICT:** ENG REVIEW NOT CLEARED — 14 issues found, all with confirmed solutions. Ready to implement after applying fixes.

---

## 13. 设计审计报告（第 8/9/10 章）

> 审计日期：2026-06-04
> 审计方法：/plan-design-review
> 审计范围：第 8 章（AI SQL 预测）、第 9 章（注释生成 SQL）、第 10 章（错误 AI 分析）

### 13.1 审计结果总览

| 维度 | 初始评分 | 最终评分 | 发现数 |
|------|----------|----------|--------|
| 信息架构 | 6/10 | 8/10 | 3 |
| 交互状态 | 3/10 | 8/10 | 2 |
| 用户旅程 | 4/10 | 7/10 | 2 |
| AI Slop 风险 | 7/10 | 7/10 | 1 |
| 设计系统 | 5/10 | 7/10 | 1 |
| 响应式/无障碍 | 4/10 | 7/10 | 1 |
| 未决决策 | 4/10 | 7/10 | 3 |
| **合计** | **5/10** | **7/10** | **13** |

### 13.2 信息架构问题及解决方案

#### [D1] AI 预测结果到达时的视觉过渡未定义（P2）

**问题**：当用户已经看到本地补全结果，AI 结果到达时应该怎么表现？

**解决**：追加模式 — 本地结果立即显示，AI 结果返回后在列表底部追加，用分隔线隔开。用户看到列表变长。

**影响**：`src/AutoCompleteInterface.cpp`

#### [D2] 注释生成 SQL 的加载状态未定义（P2）

**问题**：用户按下 Enter 后，AI 可能需要 2-5 秒才能返回结果。这段时间用户看到什么？

**解决**：占位符模式 — 在触发词下方显示 `-- 正在生成...` 灰色注释，AI 返回后替换为实际 SQL。

**影响**：`src/EditorBase.cpp`

#### [D3] 错误分析结果的显示位置未明确（P2）

**问题**：当 AI 分析完成时，用户应该在哪里看到结果？

**解决**：双 Tab 模式 — 错误信息保留在 Messages Tab，AI 分析在 AI Tab 显示。用户可以来回切换查看。

**影响**：`src/TabAI.cpp`

### 13.3 交互状态问题及解决方案

#### [D4] 交互状态完全未定义（P1）

**问题**：三个功能都没有指定加载、空、错误、成功、部分状态的视觉表现。

**解决**：为每个功能定义所有 5 种状态：

**Chapter 8: AI Prediction States**

| 状态 | 用户看到什么 |
|------|-------------|
| LOADING | 本地结果已显示，列表底部显示 "🤖 正在预测..." 灰色项 |
| EMPTY | 只显示本地结果，无 AI 项（正常情况） |
| ERROR | AI 不可用时，不显示任何 AI 项（静默降级） |
| SUCCESS | 本地结果 + AI 结果，用分隔线隔开 |
| PARTIAL | AI 返回部分结果（如只返回 2 个），正常显示 |

**Chapter 9: Comment-to-SQL States**

| 状态 | 用户看到什么 |
|------|-------------|
| LOADING | 触发词下方显示 "-- 正在生成 SQL..." 灰色注释 |
| EMPTY | 注释内容为空时，不触发 AI 请求 |
| ERROR | AI 超时或失败时，显示 "-- 生成失败: [错误信息]" 红色注释 |
| SUCCESS | 生成的 SQL 插入到注释下方，语法高亮 |
| PARTIAL | 流式返回时，逐字显示生成的 SQL |

**Chapter 10: Error Analysis States**

| 状态 | 用户看到什么 |
|------|-------------|
| LOADING | AI Tab 显示 "正在分析错误..." 状态 |
| EMPTY | 无错误时不触发分析（正常情况） |
| ERROR | AI 分析失败时，显示 "分析失败: [错误信息]" |
| SUCCESS | AI Tab 显示完整的错误分析结果 |
| PARTIAL | 流式返回时，逐字显示分析内容 |

**影响**：所有三个功能的实现

#### [D5] AI 预测的加载状态视觉设计（P2）

**问题**：加载状态应该显示什么？

**解决**：在补全列表底部显示 "🤖 正在预测..." 灰色项，让用户知道 AI 正在工作。

**影响**：`src/AutoCompleteInterface.cpp`

### 13.4 用户旅程问题及解决方案

#### [D6] AI 预测的等待体验未设计（P2）

**问题**：用户看到本地结果后，不知道 AI 正在工作。

**解决**：在补全列表底部添加加载指示器（如 "🤖 正在预测..."）。

**影响**：`src/AutoCompleteInterface.cpp`

#### [D7] 注释生成 SQL 的失败恢复路径未设计（P2）

**问题**：当 AI 生成的 SQL 不正确时，用户不知道如何重试或撤销。

**解决**：生成失败时，在注释下方显示提示信息：`-- 生成失败，按 Ctrl+Z 撤销，或重新输入 @ai generate 重试`

**影响**：`src/EditorBase.cpp`

### 13.5 AI Slop 风险

#### [D8] 补全列表的视觉指示器风格未定义（P3）

**问题**：计划使用 emoji（🔵🟢🤖）作为类型指示器，但 emoji 在不同系统上可能显示不一致。

**解决**：用户选择继续使用 emoji。不更改。

**影响**：无

### 13.6 设计系统问题及解决方案

#### [D9] AI 内容的颜色方案未定义（P2）

**问题**：AI Tab 中的对话内容、错误分析结果、预测结果都需要视觉区分。

**解决**：定义统一的颜色方案：
- 用户消息：蓝色（#0066CC）
- AI 消息：绿色（#008800）
- 错误信息：红色（#CC0000）
- 加载状态：灰色（#888888）

**影响**：`src/TabAI.cpp`

### 13.7 响应式/无障碍问题及解决方案

#### [D10] 键盘导航未定义（P2）

**问题**：用户如何通过键盘选择 AI 预测结果？如何触发注释生成？如何触发错误分析？

**解决**：定义统一的键盘快捷键：
- `Ctrl+Space`：手动触发 AI 预测
- `Enter`：确认选择补全项
- `Escape`：取消补全列表
- `Ctrl+Enter`：触发注释生成（可选）

**影响**：`src/EditorBase.cpp`、`src/AutoCompleteInterface.cpp`

### 13.8 未决决策

#### [D11] AI 预测结果的排序规则未定义（P2）

**问题**：当本地结果和 AI 结果混合显示时，应该按什么顺序排序？

**解决**：按相关性排序 — 本地结果优先，AI 结果按置信度排序。

**影响**：`src/CommunityAutoComplete.cpp`

#### [D12] 注释生成的 SQL 格式化策略未定义（P2）

**问题**：AI 返回的 SQL 可能没有缩进或换行，直接插入会影响可读性。

**解决**：自动格式化 — AI 返回后自动格式化 SQL，使用现有的美化逻辑。

**影响**：`src/EditorBase.cpp`

#### [D13] 错误分析结果的长度限制未定义（P3）

**问题**：AI 可能返回很长的分析结果，撑爆 AI Tab 或影响阅读体验。

**解决**：用户选择不限制长度。允许滚动查看完整结果。

**影响**：无

### 13.9 审计决策记录

| # | 问题 | 决策 | 原因 |
|---|------|------|------|
| D1 | AI 预测视觉过渡 | 追加模式 | 用户看到列表变长，自然 |
| D2 | 注释生成加载状态 | 占位符模式 | 用户知道系统在工作 |
| D3 | 错误分析显示位置 | 双 Tab 模式 | 保留原始错误，可切换 |
| D4 | 交互状态未定义 | 定义所有 5 种状态 | 完整的用户体验 |
| D5 | 加载状态视觉设计 | "🤖 正在预测..." | 与现有图标风格一致 |
| D6 | AI 预测等待体验 | 添加加载指示器 | 避免用户困惑 |
| D7 | 失败恢复路径 | 显示提示信息 | 指导用户操作 |
| D8 | 视觉指示器风格 | 继续使用 emoji | 用户选择 |
| D9 | 颜色方案 | 定义统一方案 | 视觉区分 |
| D10 | 键盘导航 | 定义快捷键 | 无障碍访问 |
| D11 | 排序规则 | 按相关性排序 | 最智能 |
| D12 | SQL 格式化 | 自动格式化 | 可读性 |
| D13 | 长度限制 | 不限制 | 用户选择 |

### 13.10 NOT in scope

- 视觉 mockup 生成（designer 不可用）
- 完整的无障碍支持（屏幕阅读器等）
- 响应式布局（桌面应用，窗口大小固定）
- 动画效果（Win32 原生控件不支持）

### 13.11 What already exists

| 组件 | 状态 | 复用情况 |
|------|------|----------|
| Scintilla 样式系统 | ✅ 已实现 | 复用于 AI 内容着色 |
| SCI_REGISTERIMAGE | ✅ 已实现 | 复用于 AI 图标 |
| TabMgmt | ✅ 已实现 | 复用于 AI Tab |
| Win32 对话框 | ✅ 已实现 | 复用于设置对话框 |

### 13.12 Implementation Tasks

- [ ] **T9 (P2, human: ~1h / CC: ~10min)** — AutoCompleteInterface — 实现 AI 预测加载指示器
  - Surfaced by: Design Review — D5, D6
  - Files: `src/AutoCompleteInterface.cpp`
  - Verify: 输入时显示 "🤖 正在预测..."

- [ ] **T10 (P2, human: ~30min / CC: ~5min)** — EditorBase — 实现注释生成加载占位符
  - Surfaced by: Design Review — D2
  - Files: `src/EditorBase.cpp`
  - Verify: 触发后显示 "-- 正在生成..."

- [ ] **T11 (P2, human: ~30min / CC: ~5min)** — EditorBase — 实现失败恢复提示
  - Surfaced by: Design Review — D7
  - Files: `src/EditorBase.cpp`
  - Verify: 失败时显示重试提示

- [ ] **T12 (P2, human: ~1h / CC: ~10min)** — TabAI — 定义 AI 内容颜色方案
  - Surfaced by: Design Review — D9
  - Files: `src/TabAI.cpp`
  - Verify: 用户消息蓝色，AI 消息绿色

- [ ] **T13 (P2, human: ~30min / CC: ~5min)** — EditorBase — 定义键盘快捷键
  - Surfaced by: Design Review — D10
  - Files: `src/EditorBase.cpp`
  - Verify: Ctrl+Space 触发预测

---

## GSTACK REVIEW REPORT

| Review | Trigger | Why | Runs | Status | Findings |
|--------|---------|-----|------|--------|----------|
| Eng Review | `/plan-eng-review` | Architecture & tests (required) | 1 | issues_open | 14 issues, 0 critical gaps |
| Design Review | `/plan-design-review` | UI/UX gaps | 1 | issues_open | 13 issues, 5/10 → 7/10 |

- **UNRESOLVED:** 0 unresolved decisions
- **VERDICT:** ENG + DESIGN REVIEW NOT CLEARED — 27 issues total, all with confirmed solutions. Ready to implement after applying fixes.
