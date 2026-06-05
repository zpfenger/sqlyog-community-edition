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

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "AIService.h"
#include "Global.h"
#include "CommonHelper.h"
#include "wyIni.h"
#include "jsoncpp.h"
#include "FrameWindow.h"
#include "L10nText.h"

extern PGLOBALS		pGlobals;

#define AI_SECTION  "AI"
#define AI_KEY_URL  "AIUrl"
#define AI_KEY_KEY  "AIKey"
#define AI_KEY_MODEL "AIModel"

// Comment-to-SQL keys (Chapter 9)
#define AI_KEY_COMMENT_TRIGGER_ENABLED "AICommentTriggerEnabled"
#define AI_KEY_COMMENT_TRIGGER_WORD    "AICommentTriggerWord"

// Error Analysis keys (Chapter 10)
#define AI_KEY_AUTO_ANALYZE_ERROR      "AIAutoAnalyzeError"
#define AI_KEY_ERROR_ANALYSIS_TIMEOUT  "AIErrorAnalysisTimeout"

// Predefined system prompts (English)
static const char* PROMPT_ANALYZE_EN =
    "You are a MySQL expert. Analyze the given SQL for performance issues, "
    "potential bugs, and optimization suggestions. Reply in the same language "
    "as the user's question. Be concise and specific.";

static const char* PROMPT_BEAUTIFY_EN =
    "You are a SQL formatter. Beautify and format the given SQL statement "
    "with proper indentation and line breaks. Only return the formatted SQL, "
    "no explanations.";

static const char* PROMPT_GENERATE_EN =
    "You are a MySQL expert. Generate SQL statements based on the user's "
    "natural language description. Return only the SQL, with brief comments "
    "if needed.";

static const char* PROMPT_CHAT_EN =
    "You are a MySQL database expert assistant. Help users with SQL questions, "
    "database design, performance tuning, and troubleshooting. "
    "Reply concisely and professionally.";

// Chapter 10: Error Analysis prompt
static const char* PROMPT_ERROR_ANALYSIS_EN =
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

// Predefined system prompts (Chinese)
static const char* PROMPT_ANALYZE_CN =
    "你是 MySQL 专家。分析给定的 SQL 语句，指出性能问题、潜在 bug 和优化建议。"
    "用中文回复，简洁具体。";

static const char* PROMPT_BEAUTIFY_CN =
    "你是 SQL 格式化工具。美化并格式化给定的 SQL 语句，"
    "添加适当的缩进和换行。只返回格式化后的 SQL，不要解释。";

static const char* PROMPT_GENERATE_CN =
    "你是 MySQL 专家。根据用户的自然语言描述生成 SQL 语句。"
    "只返回 SQL，必要时添加简短注释。";

static const char* PROMPT_CHAT_CN =
    "你是 MySQL 数据库专家助手。帮助用户解决 SQL 问题、"
    "数据库设计、性能调优和故障排除。用中文简洁专业地回复。";

// Chapter 10: Error Analysis prompt (Chinese)
static const char* PROMPT_ERROR_ANALYSIS_CN =
    "你是 MySQL 专家。分析 SQL 错误并提供解决方案。\n\n"
    "规则：\n"
    "1. 清楚解释错误原因\n"
    "2. 提供具体的修复建议\n"
    "3. 如果可能，给出正确的 SQL\n"
    "4. 如果是常见错误，提及预防措施\n"
    "5. 用中文回复\n\n"
    "格式：\n"
    "**错误原因：** [解释]\n\n"
    "**修复建议：**\n"
    "1. [建议 1]\n"
    "2. [建议 2]\n\n"
    "**正确的 SQL：**\n"
    "```sql\n"
    "[修正后的 SQL]\n"
    "```";

// Check if current language is Chinese
static bool IsChineseLang()
{
    const char* langcode = GetL10nLangcode();
    return (langcode && (strcmp(langcode, "zh-cn") == 0 || strcmp(langcode, "zh") == 0));
}

const char* AIService::GetPromptAnalyze()   { return IsChineseLang() ? PROMPT_ANALYZE_CN : PROMPT_ANALYZE_EN; }
const char* AIService::GetPromptBeautify()  { return IsChineseLang() ? PROMPT_BEAUTIFY_CN : PROMPT_BEAUTIFY_EN; }
const char* AIService::GetPromptGenerate()  { return IsChineseLang() ? PROMPT_GENERATE_CN : PROMPT_GENERATE_EN; }
const char* AIService::GetPromptChat()      { return IsChineseLang() ? PROMPT_CHAT_CN : PROMPT_CHAT_EN; }
const char* AIService::GetPromptErrorAnalysis() { return IsChineseLang() ? PROMPT_ERROR_ANALYSIS_CN : PROMPT_ERROR_ANALYSIS_EN; }

void AIService::LoadConfig(AIConfig* config)
{
    if (!config)
        return;

    wyString path;
    wyString val;

    // Default AI configuration
    static const char* DEFAULT_URL   = "https://token-plan-cn.xiaomimimo.com/v1/chat/completions";
    static const char* DEFAULT_KEY   = "tp-ciqe4igxj2etx0rodo0eim7d125vkp3lv7rjlruxqacqc9dh";
    static const char* DEFAULT_MODEL = "mimo-v2.5-pro";

    // Get INI path from global
    pGlobals->m_pcmainwin->GetSQLyogIniPath(&path);
    if (!path.GetLength())
    {
        // No INI file - use defaults
        config->url.SetAs(DEFAULT_URL);
        config->key.SetAs(DEFAULT_KEY);
        config->model.SetAs(DEFAULT_MODEL);
        return;
    }

    // Read URL (use default if empty)
    wyIni::IniGetString(AI_SECTION, AI_KEY_URL, "", &val, path.GetString());
    if (val.GetLength() > 0)
        config->url.SetAs(val.GetString());
    else
        config->url.SetAs(DEFAULT_URL);

    // Read encrypted Key, then decrypt (use default if empty)
    wyIni::IniGetString(AI_SECTION, AI_KEY_KEY, "", &val, path.GetString());
    if (val.GetLength() > 0) {
        config->key.SetAs(val.GetString());
        DecodePassword(config->key);
    } else {
        config->key.SetAs(DEFAULT_KEY);
    }

    // Read Model (use default if empty)
    wyIni::IniGetString(AI_SECTION, AI_KEY_MODEL, "", &val, path.GetString());
    if (val.GetLength() > 0)
        config->model.SetAs(val.GetString());
    else
        config->model.SetAs(DEFAULT_MODEL);

    // Read Comment-to-SQL settings (Chapter 9)
    config->comment_trigger_enabled = wyIni::IniGetInt(AI_SECTION, AI_KEY_COMMENT_TRIGGER_ENABLED, 1, path.GetString()) != 0;
    wyIni::IniGetString(AI_SECTION, AI_KEY_COMMENT_TRIGGER_WORD, "@ai", &val, path.GetString());
    config->comment_trigger_word.SetAs(val.GetString());

    // Read Error Analysis settings (Chapter 10)
    config->auto_analyze_error = wyIni::IniGetInt(AI_SECTION, AI_KEY_AUTO_ANALYZE_ERROR, 0, path.GetString()) != 0;
    config->error_analysis_timeout_ms = wyIni::IniGetInt(AI_SECTION, AI_KEY_ERROR_ANALYSIS_TIMEOUT, 10000, path.GetString());
}

void AIService::SaveConfig(AIConfig* config)
{
    if (!config)
        return;

    wyString path;
    pGlobals->m_pcmainwin->GetSQLyogIniPath(&path);
    if (!path.GetLength())
        return;

    // Save URL (plain text)
    wyIni::IniWriteString(AI_SECTION, AI_KEY_URL, config->url.GetString(), path.GetString());

    // Encrypt Key then save
    wyString encKey;
    encKey.SetAs(config->key.GetString());
    if (encKey.GetLength() > 0) {
        EncodePassword(encKey);
    }
    wyIni::IniWriteString(AI_SECTION, AI_KEY_KEY, encKey.GetString(), path.GetString());

    // Save Model (plain text)
    wyIni::IniWriteString(AI_SECTION, AI_KEY_MODEL, config->model.GetString(), path.GetString());

    // Save Comment-to-SQL settings (Chapter 9)
    wyIni::IniWriteInt(AI_SECTION, AI_KEY_COMMENT_TRIGGER_ENABLED, config->comment_trigger_enabled ? 1 : 0, path.GetString());
    wyIni::IniWriteString(AI_SECTION, AI_KEY_COMMENT_TRIGGER_WORD, config->comment_trigger_word.GetString(), path.GetString());

    // Save Error Analysis settings (Chapter 10)
    wyIni::IniWriteInt(AI_SECTION, AI_KEY_AUTO_ANALYZE_ERROR, config->auto_analyze_error ? 1 : 0, path.GetString());
    wyIni::IniWriteInt(AI_SECTION, AI_KEY_ERROR_ANALYSIS_TIMEOUT, config->error_analysis_timeout_ms, path.GetString());

    // Update globals
    pGlobals->m_aiurl.SetAs(config->url.GetString());
    pGlobals->m_aikey.SetAs(encKey.GetString());
    pGlobals->m_aimodel.SetAs(config->model.GetString());

    // Update m_aiconfig so code generation and other features use the saved values
    pGlobals->m_aiconfig.url.SetAs(config->url.GetString());
    pGlobals->m_aiconfig.key.SetAs(config->key.GetString());
    pGlobals->m_aiconfig.model.SetAs(config->model.GetString());
    pGlobals->m_aiconfig.comment_trigger_enabled = config->comment_trigger_enabled;
    pGlobals->m_aiconfig.comment_trigger_word.SetAs(config->comment_trigger_word.GetString());
    pGlobals->m_aiconfig.auto_analyze_error = config->auto_analyze_error;
    pGlobals->m_aiconfig.error_analysis_timeout_ms = config->error_analysis_timeout_ms;
}

void AIService::BuildRequestJson(const char* prompt, const char* systemPrompt,
                                 const char* historyJson, const char* configModel,
                                 wyString* result)
{
    if (!result)
        return;

    Json::Value root;
    Json::Value messages(Json::arrayValue);

    // System prompt
    Json::Value sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt ? systemPrompt : GetPromptChat();
    messages.append(sysMsg);

    // History messages (parse JSON array if provided)
    if (historyJson && historyJson[0]) {
        Json::Reader reader;
        Json::Value history;
        if (reader.parse(historyJson, history) && history.isArray()) {
            // Keep last 10 messages (5 rounds)
            int start = max(0, (int)history.size() - 10);
            for (int i = start; i < (int)history.size(); i++) {
                messages.append(history[i]);
            }
        }
    }

    // Current user message
    Json::Value userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = prompt ? prompt : "";
    messages.append(userMsg);

    root["model"] = configModel ? configModel : "";
    root["stream"] = true;
    root["messages"] = messages;

    Json::FastWriter writer;
    result->SetAs(writer.write(root).c_str());
}

void AIService::BuildGenerateRequestJson(const char* comment,
                                          const char* schemaInfo,
                                          const char* configModel,
                                          wyString* result)
{
    if (!result)
        return;

    Json::Value root;
    Json::Value messages(Json::arrayValue);

    // System prompt
    Json::Value sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = GetPromptGenerate();
    messages.append(sysMsg);

    // User message
    Json::Value userMsg;
    userMsg["role"] = "user";

    wyString prompt;
    prompt.Sprintf("Database Schema:\n%s\n\nDescription:\n%s\n\nGenerate SQL:",
                   schemaInfo ? schemaInfo : "",
                   comment ? comment : "");
    userMsg["content"] = prompt.GetString();
    messages.append(userMsg);

    root["model"] = configModel ? configModel : "";
    root["stream"] = true;  // Use streaming for all requests
    root["messages"] = messages;

    Json::FastWriter writer;
    result->SetAs(writer.write(root).c_str());
}

void AIService::BuildErrorAnalysisRequestJson(const char* sql,
                                               const char* errorMsg,
                                               int errorCode,
                                               const char* configModel,
                                               wyString* result)
{
    if (!result)
        return;

    Json::Value root;
    Json::Value messages(Json::arrayValue);

    // System prompt
    Json::Value sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = GetPromptErrorAnalysis();
    messages.append(sysMsg);

    // Classify error for targeted prompt
    SQLErrorType errType = ClassifyError(errorMsg, errorCode);
    wyString prompt;

    switch (errType) {
    case ERR_TABLE_NOT_FOUND:
        prompt.Sprintf(
            "SQL执行出错，错误代码 %d\n\n"
            "SQL: %s\n\n"
            "错误信息: %s\n\n"
            "这是一个表不存在的错误。请：\n"
            "1. 检查表名是否拼写正确\n"
            "2. 列出可能的正确表名\n"
            "3. 提供修正后的 SQL",
            errorCode,
            sql ? sql : "",
            errorMsg ? errorMsg : "");
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
            errorCode,
            sql ? sql : "",
            errorMsg ? errorMsg : "");
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
            errorCode,
            sql ? sql : "",
            errorMsg ? errorMsg : "");
        break;

    case ERR_DUPLICATE_KEY:
        prompt.Sprintf(
            "SQL执行出错，错误代码 %d\n\n"
            "SQL: %s\n\n"
            "错误信息: %s\n\n"
            "这是一个主键/唯一键重复错误。请：\n"
            "1. 解释重复的原因\n"
            "2. 提供使用 INSERT IGNORE 或 ON DUPLICATE KEY UPDATE 的解决方案",
            errorCode,
            sql ? sql : "",
            errorMsg ? errorMsg : "");
        break;

    case ERR_FOREIGN_KEY:
        prompt.Sprintf(
            "SQL执行出错，错误代码 %d\n\n"
            "SQL: %s\n\n"
            "错误信息: %s\n\n"
            "这是一个外键约束错误。请：\n"
            "1. 解释外键约束关系\n"
            "2. 检查引用的记录是否存在\n"
            "3. 提供正确的操作顺序",
            errorCode,
            sql ? sql : "",
            errorMsg ? errorMsg : "");
        break;

    case ERR_PERMISSION:
        prompt.Sprintf(
            "SQL执行出错，错误代码 %d\n\n"
            "SQL: %s\n\n"
            "错误信息: %s\n\n"
            "这是一个权限不足错误。请：\n"
            "1. 说明需要哪些权限\n"
            "2. 提供 GRANT 语句示例",
            errorCode,
            sql ? sql : "",
            errorMsg ? errorMsg : "");
        break;

    case ERR_CONNECTION:
        prompt.Sprintf(
            "SQL执行出错，错误代码 %d\n\n"
            "SQL: %s\n\n"
            "错误信息: %s\n\n"
            "这是一个连接问题。请：\n"
            "1. 分析可能的连接问题原因\n"
            "2. 提供排查步骤",
            errorCode,
            sql ? sql : "",
            errorMsg ? errorMsg : "");
        break;

    default:
        prompt.Sprintf(
            "SQL执行出错，错误代码 %d\n\n"
            "SQL: %s\n\n"
            "错误信息: %s\n\n"
            "请分析错误原因并提供修复建议。",
            errorCode,
            sql ? sql : "",
            errorMsg ? errorMsg : "");
        break;
    }

    // User message with error details
    Json::Value userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = prompt.GetString();
    messages.append(userMsg);

    root["model"] = configModel ? configModel : "";
    root["stream"] = true;
    root["messages"] = messages;

    Json::FastWriter writer;
    result->SetAs(writer.write(root).c_str());
}

// Classify SQL error by error code and message content (Chapter 10)
SQLErrorType AIService::ClassifyError(const char* errorMsg, int errorCode)
{
    // Classify by MySQL error code first
    switch (errorCode) {
    case 1064:  // SQL syntax error
        return ERR_SYNTAX;

    case 1146:  // Table doesn't exist
    case 1051:  // Unknown table
        return ERR_TABLE_NOT_FOUND;

    case 1054:  // Unknown column
    case 1060:  // Duplicate column name
        return ERR_COLUMN_NOT_FOUND;

    case 1062:  // Duplicate entry for key
    case 1586:  // Duplicate entry for key (newer versions)
        return ERR_DUPLICATE_KEY;

    case 1451:  // Cannot delete a parent row (foreign key constraint)
    case 1452:  // Cannot add a child row (foreign key constraint)
        return ERR_FOREIGN_KEY;

    case 1045:  // Access denied
    case 1142:  // Command denied
        return ERR_PERMISSION;

    case 2003:  // Can't connect to server
    case 2006:  // Server has gone away
    case 2013:  // Lost connection
        return ERR_CONNECTION;
    }

    // Fall back to message content analysis
    if (!errorMsg || !errorMsg[0])
        return ERR_UNKNOWN;

    // Convert to lowercase for matching
    char lowerMsg[4096];
    strncpy(lowerMsg, errorMsg, sizeof(lowerMsg) - 1);
    lowerMsg[sizeof(lowerMsg) - 1] = '\0';
    for (int i = 0; lowerMsg[i]; i++) {
        lowerMsg[i] = (char)tolower((unsigned char)lowerMsg[i]);
    }

    if (strstr(lowerMsg, "doesn't exist") || strstr(lowerMsg, "not found"))
        return ERR_TABLE_NOT_FOUND;

    if (strstr(lowerMsg, "unknown column"))
        return ERR_COLUMN_NOT_FOUND;

    if (strstr(lowerMsg, "syntax error") || strstr(lowerMsg, "near"))
        return ERR_SYNTAX;

    return ERR_UNKNOWN;
}

// Wrapper for stream callback that tracks token count
struct TokenCounterContext {
    StreamCallback originalCallback;
    void*          originalUserdata;
    volatile LONG  tokenCount;
};

bool AIService::SSELineCallback(const char* line, int lineLen, void* userdata)
{
    TokenCounterContext* ctx = (TokenCounterContext*)userdata;
    if (!ctx || !ctx->originalCallback)
        return false;

    bool hasDataPrefix = (line && lineLen >= 6 && strncmp(line, "data: ", 6) == 0);
    bool isDone = (hasDataPrefix && lineLen == 12 && strncmp(line + 6, "[DONE]", 6) == 0);

    if (isDone)
        return false;

    wyString content;
    if (ParseSSELine(line, lineLen, content)) {
        InterlockedIncrement(&ctx->tokenCount);
        return ctx->originalCallback(content.GetString(), content.GetLength(), ctx->originalUserdata);
    }

    // Try parsing as plain JSON response (non-streaming API)
    if (lineLen > 0 && line[0] == '{') {
        Json::Reader reader;
        Json::Value root;
        if (reader.parse(line, line + lineLen, root)) {
            // Try OpenAI format: choices[0].message.content
            if (root.isMember("choices") && root["choices"].isArray() && root["choices"].size() > 0) {
                const Json::Value& choice = root["choices"][0];
                if (choice.isMember("message") &&
                    choice["message"].isMember("content") &&
                    choice["message"]["content"].isString()) {
                    const char* text = choice["message"]["content"].asCString();
                    if (text && text[0] != '\0') {
                        content.SetAs(text);
                        InterlockedIncrement(&ctx->tokenCount);
                        return ctx->originalCallback(content.GetString(), content.GetLength(), ctx->originalUserdata);
                    }
                }
                // Also try choices[0].text (older format)
                if (choice.isMember("text") && choice["text"].isString()) {
                    const char* text = choice["text"].asCString();
                    if (text && text[0] != '\0') {
                        content.SetAs(text);
                        InterlockedIncrement(&ctx->tokenCount);
                        return ctx->originalCallback(content.GetString(), content.GetLength(), ctx->originalUserdata);
                    }
                }
            }
        }
    }

    return true;  // continue reading
}

bool AIService::SendRequestStreaming(AIConfig* config,
                                      const char* requestJson,
                                      StreamCallback callback,
                                      void* userdata,
                                      volatile LONG* stop,
                                      wyString* errorBuf,
                                      DWORD timeoutMs)
{
    if (!config || !requestJson || !callback)
        return false;

    CHttp http;
    wyWChar wurl[2048] = {0};

    // Convert URL to wide string
    wyString urlStr;
    urlStr.SetAs(config->url.GetString());
    wcsncpy(wurl, urlStr.GetAsWideChar(), 2047);

    if (!http.SetUrl(wurl)) {
        if (errorBuf) errorBuf->SetAs(_("Invalid API URL"));
        return false;
    }

    http.SetContentType(L"application/json; charset=utf-8");

    // Set Authorization header
    wyString authHeader;
    authHeader.Sprintf("Authorization: Bearer %s", config->key.GetString());
    wyWChar wauth[4096] = {0};
    wcsncpy(wauth, authHeader.GetAsWideChar(), 4095);
    http.SetHeader(wauth);

    // Let callers choose short timeouts for latency-sensitive requests.
    http.SetTimeOut(timeoutMs > 0 ? timeoutMs : 300000);

    // Send request (checkauth=false for AI API, no challenge/response needed)
    int status = 0;
    wyString body;
    body.SetAs(requestJson);

    if (!http.SendData((char*)body.GetString(), body.GetLength(), false, &status, false)) {
        DWORD err = GetLastError();
        if (errorBuf) errorBuf->Sprintf(_("HTTP request failed (status: %d, error: %lu)"), status, err);
        return false;
    }

    // Check HTTP status code (audit fix: C1)
    if (status != 200) {
        // Read error body
        char* errResp = http.GetResponse(NULL);
        if (errResp) {
            if (errorBuf) {
                // Try to parse JSON error message
                Json::Reader reader;
                Json::Value errJson;
                if (reader.parse(errResp, errJson) && errJson.isMember("error")) {
                    const Json::Value& err = errJson["error"];
                    if (err.isMember("message")) {
                        errorBuf->Sprintf(_("API Error (%d): %s"), status, err["message"].asCString());
                    } else {
                        errorBuf->Sprintf(_("API Error (%d): %s"), status, errResp);
                    }
                } else {
                    errorBuf->Sprintf(_("API Error (%d): %s"), status, errResp);
                }
            }
            free(errResp);
        } else {
            if (errorBuf) errorBuf->Sprintf(_("HTTP Error: %d"), status);
        }
        return false;
    }

    // Stream response with token counting
    AIStreamContext ctx;
    ctx.callback = callback;
    ctx.userdata = userdata;

    TokenCounterContext counterCtx;
    counterCtx.originalCallback = callback;
    counterCtx.originalUserdata = userdata;
    InterlockedExchange(&counterCtx.tokenCount, 0);

    bool ok = http.ReadResponseStreaming(SSELineCallback, &counterCtx, stop);
    if (!ok) {
        if (errorBuf) errorBuf->SetAs(_("Failed to read streaming response"));
        return false;
    }

    // Check if any tokens were received
    if (InterlockedCompareExchange(&counterCtx.tokenCount, 0, 0) == 0) {
        if (errorBuf) errorBuf->Sprintf(_("No response received from API (status: %d). Check API key and model settings."), status);
        return false;
    }

    return true;
}

bool AIService::ParseSSELine(const char* line, int lineLen, wyString& content)
{
    if (!line || lineLen < 6)
        return false;

    // Check "data: " prefix
    if (strncmp(line, "data: ", 6) != 0)
        return false;

    const char* data = line + 6;
    int dataLen = lineLen - 6;

    // Skip [DONE]
    if (dataLen == 6 && strncmp(data, "[DONE]", 6) == 0)
        return false;

    // Parse JSON
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(data, data + dataLen, root))
        return false;

    // Extract choices[0].delta.content (ignore reasoning_content - it's the thinking process)
    if (root.isMember("choices") && root["choices"].isArray() &&
        root["choices"].size() > 0)
    {
        const Json::Value& choices = root["choices"];
        if (choices[0].isMember("delta")) {
            const Json::Value& delta = choices[0]["delta"];

            // Only extract "content" (standard OpenAI format)
            // Ignore "reasoning_content" - it's the model's thinking process
            if (delta.isMember("content") && delta["content"].isString()) {
                const char* text = delta["content"].asCString();
                if (text && text[0] != '\0') {
                    content.SetAs(text);
                    return true;
                }
            }
        }
    }

    return false;
}
