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

#ifndef _AIService_H_
#define _AIService_H_

#include "wyString.h"
#include "Http.h"

// AI service configuration
struct AIConfig {
    wyString url;
    wyString key;       // decrypted key
    wyString model;
};

// Stream callback context
struct AIStreamContext {
    StreamCallback  callback;
    void*           userdata;
    wyString        linebuf;    // SSE line buffer
};

class AIService
{
public:
    // Load/save AI configuration from/to INI
    static void LoadConfig(AIConfig* config);
    static void SaveConfig(AIConfig* config);

    // Build OpenAI Chat Completions request JSON
    // history: JSON array of {role, content} messages (can be NULL)
    // result: output parameter to receive the JSON string
    static void BuildRequestJson(const char* prompt, const char* systemPrompt,
                                 const char* historyJson, const char* configModel,
                                 wyString* result);

    // Send streaming request (blocking, call from worker thread)
    // Returns true on success, false on error
    // errorBuf receives error message on failure
    static bool SendRequestStreaming(AIConfig* config,
                                      const char* requestJson,
                                      StreamCallback callback,
                                      void* userdata,
                                      volatile LONG* stop,
                                      wyString* errorBuf);

    // Parse one SSE line, extract delta content
    // Returns true if content was extracted, false otherwise
    static bool ParseSSELine(const char* line, int lineLen, wyString& content);

    // Predefined system prompts
    static const char* GetPromptAnalyze();
    static const char* GetPromptBeautify();
    static const char* GetPromptGenerate();
    static const char* GetPromptChat();

private:
    // SSE stream callback wrapper (delivers complete lines)
    static bool SSELineCallback(const char* line, int lineLen, void* userdata);
};

#endif
