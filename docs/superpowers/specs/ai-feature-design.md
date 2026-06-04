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

## 8. 后续扩展（可选）

- [ ] 对话导出为文件
- [ ] 自定义 System Prompt
- [ ] 温度参数调节
- [ ] 代码高亮渲染（AI 回复中的 SQL 代码块）
- [ ] 快捷键支持（Ctrl+Shift+A 打开 AI Tab）
- [ ] 对话历史持久化存储

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
