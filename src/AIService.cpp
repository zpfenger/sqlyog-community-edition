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

    // Update globals
    pGlobals->m_aiurl.SetAs(config->url.GetString());
    pGlobals->m_aikey.SetAs(encKey.GetString());
    pGlobals->m_aimodel.SetAs(config->model.GetString());
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
                if (choice.isMember("message") && choice["message"].isMember("content")) {
                    content.SetAs(choice["message"]["content"].asCString());
                    InterlockedIncrement(&ctx->tokenCount);
                    return ctx->originalCallback(content.GetString(), content.GetLength(), ctx->originalUserdata);
                }
                // Also try choices[0].text (older format)
                if (choice.isMember("text") && choice["text"].isString()) {
                    content.SetAs(choice["text"].asCString());
                    InterlockedIncrement(&ctx->tokenCount);
                    return ctx->originalCallback(content.GetString(), content.GetLength(), ctx->originalUserdata);
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
                                      wyString* errorBuf)
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

    // Set longer timeout for AI requests (5 minutes)
    http.SetTimeOut(300000);

    // Send request (checkauth=false for AI API, no challenge/response needed)
    int status = 0;
    wyString body;
    body.SetAs(requestJson);

    if (!http.SendData((char*)body.GetString(), body.GetLength(), false, &status, false)) {
        if (errorBuf) errorBuf->Sprintf(_("HTTP request failed (status: %d)"), status);
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

    // Extract choices[0].delta.content or choices[0].delta.reasoning_content
    if (root.isMember("choices") && root["choices"].isArray() &&
        root["choices"].size() > 0)
    {
        const Json::Value& choices = root["choices"];
        if (choices[0].isMember("delta")) {
            const Json::Value& delta = choices[0]["delta"];

            // Try "content" first (standard OpenAI format)
            if (delta.isMember("content") && delta["content"].isString()) {
                const char* text = delta["content"].asCString();
                if (text && text[0] != '\0') {
                    content.SetAs(text);
                    return true;
                }
            }

            // Try "reasoning_content" (used by some models like mimo)
            if (delta.isMember("reasoning_content") && delta["reasoning_content"].isString()) {
                const char* text = delta["reasoning_content"].asCString();
                if (text && text[0] != '\0') {
                    content.SetAs(text);
                    return true;
                }
            }
        }
    }

    return false;
}
