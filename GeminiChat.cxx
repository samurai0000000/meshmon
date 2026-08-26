/*
 * GeminiChat.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include <cctype>
#include <sstream>
#include <iostream>
#include <GeminiChat.hxx>

#ifndef DEBUG_CHATBOT
#define DEBUG_CHATBOT 0
#endif

#define GEMINI_TIMEOUT_S 15
#define GEMINI_CONNECT_TIMEOUT_S 10
#define GEMINI_MAX_OUTPUT_TOKENS 512

GeminiChat::GeminiChat(const string &apiKey, const string &model)
    : ChatBot(),
      _apiKey(apiKey),
      _model(model.empty() ? string("gemini-flash-latest") : model)
{

}

GeminiChat::~GeminiChat()
{

}

bool GeminiChat::enabled(void) const
{
    return !_apiKey.empty() && !_model.empty();
}

string GeminiChat::jsonEscape(const string &s)
{
    string out;
    size_t i;
    char buf[8];

    out.reserve(s.size() + 8);
    for (i = 0; i < s.size(); i++) {
        unsigned char c = static_cast<unsigned char>(s[i]);

        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                snprintf(buf, sizeof(buf), "\\u%04x", (unsigned int) c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }

    return out;
}

bool GeminiChat::parseJsonString(const string &s, size_t &i, string &out)
{
    if ((i >= s.size()) || (s[i] != '"')) {
        return false;
    }

    i++;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];

        if (c == '"') {
            return true;
        }
        if (c != '\\') {
            out += c;
            continue;
        }
        if (i >= s.size()) {
            return false;
        }
        c = s[i++];
        switch (c) {
        case '"':
        case '\\':
        case '/':
            out += c;
            break;
        case 'b':
            out += '\b';
            break;
        case 'f':
            out += '\f';
            break;
        case 'n':
            out += '\n';
            break;
        case 'r':
            out += '\r';
            break;
        case 't':
            out += '\t';
            break;
        case 'u': {
            unsigned int cp = 0;
            size_t n;

            if ((i + 4) > s.size()) {
                return false;
            }
            for (n = 0; n < 4; n++) {
                char h = s[i++];
                unsigned int v;

                if ((h >= '0') && (h <= '9')) {
                    v = (unsigned int) (h - '0');
                } else if ((h >= 'a') && (h <= 'f')) {
                    v = (unsigned int) (h - 'a' + 10);
                } else if ((h >= 'A') && (h <= 'F')) {
                    v = (unsigned int) (h - 'A' + 10);
                } else {
                    return false;
                }
                cp = (cp << 4) | v;
            }
            if (cp < 0x80) {
                out += static_cast<char>(cp);
            } else if (cp < 0x800) {
                out += static_cast<char>(0xc0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3f));
            } else {
                out += static_cast<char>(0xe0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
                out += static_cast<char>(0x80 | (cp & 0x3f));
            }
            break;
        }
        default:
            return false;
        }
    }

    return false;
}

string GeminiChat::getSystemInstruction(uint32_t from, uint32_t dest, uint8_t channel) const
{
    (void)(dest);
    time_t now = time(NULL);
    struct tm tm;
    char timeBuf[64];
    localtime_r(&now, &tm);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S %Z", &tm);

    stringstream ss;
    ss << "You are an AI assistant gateway connected to the Meshtastic mesh network with Internet access. Current local time is " << timeBuf << ".";

    shared_ptr<MeshClient> client = getClient();
    if (client != NULL) {
        uint32_t myId = client->whoami();
        if ((myId != 0) && (myId != 0xffffffffU)) {
            string myShort = client->lookupShortName(myId);
            string myLong = client->lookupLongName(myId);
            char nodeBuf[32];
            snprintf(nodeBuf, sizeof(nodeBuf), " (!%08x)", myId);
            ss << " Your node ID is" << nodeBuf;
            if (!myLong.empty()) {
                ss << ", long name is \"" << myLong << "\"";
            }
            if (!myShort.empty()) {
                ss << ", short name is \"" << myShort << "\"";
            }
            ss << ".";
        }
    }

    if (from != 0) {
        if (client != NULL) {
            string fromShort = client->lookupShortName(from);
            string fromLong = client->lookupLongName(from);
            char nodeBuf[32];
            snprintf(nodeBuf, sizeof(nodeBuf), " (!%08x)", from);
            ss << " You are talking to user" << nodeBuf;
            if (!fromLong.empty()) {
                ss << " with long name \"" << fromLong << "\"";
            }
            if (!fromShort.empty() && (fromShort != fromLong)) {
                ss << " (short name \"" << fromShort << "\")";
            }
            ss << ".";
        } else {
            char nodeBuf[32];
            snprintf(nodeBuf, sizeof(nodeBuf), " (!%08x)", from);
            ss << " You are talking to user" << nodeBuf << ".";
        }
    }

    ss << " Current channel index is " << (unsigned int) channel << ".";

    ss << " You have access to tools to query mesh network nodes, telemetry, stats, locations, "
       << "and schedule future or recurring tasks (schedule_task, list_scheduled_tasks, cancel_scheduled_task). "
       << "For scheduling: all times and cron expressions default to the local time zone. Use delay_seconds (e.g. 600 for 10m), at_time (local datetime format 'YYYY-MM-DD HH:MM:SS' or with timezone offset), or 5-field cron (e.g. '0 6 * * 1' for Monday 6am local time). "
       << "Set action_type to 'message' (static text) or 'prompt' (to re-evaluate prompt and live data at trigger time). "
       << "Optional channel (by name or index) and target_node can be specified to route the message. "
       << "Reply in one short plain-text sentence. Maximum 180 characters. "
       << "No markdown, no lists, no emoji, and no quotes around the whole reply.";

    return ss.str();
}

string GeminiChat::extractCandidateText(const string &body)
{
    size_t cand = body.find("\"candidates\"");
    if (cand == string::npos) {
        return string();
    }

    size_t contentPos = body.find("\"content\"", cand);
    if (contentPos == string::npos) {
        return string();
    }

    size_t partsKey = body.find("\"parts\"", contentPos);
    if (partsKey == string::npos) {
        return string();
    }

    string partsJson;
    if (!extractJsonArray(body, partsKey, partsJson)) {
        return string();
    }

    string found;
    size_t pos = 0;
    while (pos < partsJson.size()) {
        size_t objStart = partsJson.find('{', pos);
        if (objStart == string::npos) {
            break;
        }

        size_t objEnd = string::npos;
        int depth = 0;
        bool inString = false;
        bool escape = false;
        for (size_t i = objStart; i < partsJson.size(); i++) {
            char c = partsJson[i];
            if (inString) {
                if (escape) {
                    escape = false;
                } else if (c == '\\') {
                    escape = true;
                } else if (c == '"') {
                    inString = false;
                }
                continue;
            }
            if (c == '"') {
                inString = true;
                escape = false;
            } else if (c == '{') {
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0) {
                    objEnd = i;
                    break;
                }
            }
        }
        if (objEnd == string::npos) {
            break;
        }

        string partObj = partsJson.substr(objStart, objEnd - objStart + 1);
        pos = objEnd + 1;

        if ((partObj.find("\"thought\":true") != string::npos) ||
            (partObj.find("\"thought\": true") != string::npos) ||
            (partObj.find("\"thought\": 1") != string::npos)) {
            continue;
        }

        if (partObj.find("\"functionCall\"") != string::npos) {
            continue;
        }

        size_t textKey = partObj.find("\"text\"");
        if (textKey == string::npos) {
            continue;
        }
        size_t i = textKey + 6;
        while ((i < partObj.size()) && isspace(static_cast<unsigned char>(partObj[i]))) {
            i++;
        }
        if ((i >= partObj.size()) || (partObj[i] != ':')) {
            continue;
        }
        i++;
        while ((i < partObj.size()) && isspace(static_cast<unsigned char>(partObj[i]))) {
            i++;
        }
        string value;
        if (!parseJsonString(partObj, i, value)) {
            continue;
        }

        while (!value.empty() && isspace(static_cast<unsigned char>(value[value.size() - 1]))) {
            value.erase(value.size() - 1);
        }
        size_t lead = 0;
        while ((lead < value.size()) && isspace(static_cast<unsigned char>(value[lead]))) {
            lead++;
        }
        if (lead != 0) {
            value = value.substr(lead);
        }
        if (value.empty()) {
            continue;
        }

        if (found.empty()) {
            found = value;
        } else {
            found += " " + value;
        }
    }

    return found;
}

bool GeminiChat::extractJsonArray(const string &s, size_t startSearch, string &out)
{
    size_t pos = s.find('[', startSearch);
    if (pos == string::npos) {
        return false;
    }

    size_t start = pos;
    int depth = 0;
    bool inString = false;
    bool escape = false;

    for (size_t i = start; i < s.size(); i++) {
        char c = s[i];

        if (inString) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
            escape = false;
        } else if (c == '[') {
            depth++;
        } else if (c == ']') {
            depth--;
            if (depth == 0) {
                out = s.substr(start, i - start + 1);
                return true;
            }
        }
    }

    return false;
}

bool GeminiChat::extractFunctionCalls(const string &body,
                                     string &modelPartsJson,
                                     vector<GeminiFunctionCall> &fcs)
{
    fcs.clear();
    modelPartsJson.clear();

    size_t cand = body.find("\"candidates\"");
    if (cand == string::npos) {
        return false;
    }

    size_t contentPos = body.find("\"content\"", cand);
    if (contentPos == string::npos) {
        return false;
    }

    size_t partsKey = body.find("\"parts\"", contentPos);
    if (partsKey == string::npos) {
        return false;
    }

    if (!extractJsonArray(body, partsKey, modelPartsJson)) {
        return false;
    }

    size_t pos = 0;
    while (pos < modelPartsJson.size()) {
        size_t fcPos = modelPartsJson.find("\"functionCall\"", pos);
        if (fcPos == string::npos) {
            break;
        }
        pos = fcPos + 14;

        GeminiFunctionCall fc;
        size_t namePos = modelPartsJson.find("\"name\"", fcPos);
        if (namePos != string::npos) {
            size_t i = namePos + 6;
            while ((i < modelPartsJson.size()) && isspace(static_cast<unsigned char>(modelPartsJson[i]))) {
                i++;
            }
            if ((i < modelPartsJson.size()) && (modelPartsJson[i] == ':')) {
                i++;
                while ((i < modelPartsJson.size()) && isspace(static_cast<unsigned char>(modelPartsJson[i]))) {
                    i++;
                }
                parseJsonString(modelPartsJson, i, fc.name);
            }
        }

        size_t argsPos = modelPartsJson.find("\"args\"", fcPos);
        if (argsPos != string::npos) {
            size_t j = argsPos + 6;
            while ((j < modelPartsJson.size()) && isspace(static_cast<unsigned char>(modelPartsJson[j]))) {
                j++;
            }
            if ((j < modelPartsJson.size()) && (modelPartsJson[j] == ':')) {
                j++;
                while ((j < modelPartsJson.size()) && isspace(static_cast<unsigned char>(modelPartsJson[j]))) {
                    j++;
                }
                if ((j < modelPartsJson.size()) && (modelPartsJson[j] == '{')) {
                    size_t startObj = j;
                    int depth = 0;
                    bool inStr = false;
                    bool esc = false;
                    while (j < modelPartsJson.size()) {
                        char c = modelPartsJson[j];
                        if (inStr) {
                            if (esc) {
                                esc = false;
                            } else if (c == '\\') {
                                esc = true;
                            } else if (c == '"') {
                                inStr = false;
                            }
                        } else {
                            if (c == '"') {
                                inStr = true;
                                esc = false;
                            } else if (c == '{') {
                                depth++;
                            } else if (c == '}') {
                                depth--;
                                if (depth == 0) {
                                    j++;
                                    fc.argsJson = modelPartsJson.substr(startObj, j - startObj);
                                    break;
                                }
                            }
                        }
                        j++;
                    }
                }
            }
        }

        if (fc.argsJson.empty()) {
            fc.argsJson = "{}";
        }

        if (!fc.name.empty()) {
            fcs.push_back(fc);
        }
    }

    return !fcs.empty();
}

static const char *kMeshToolDeclarations =
    "{\"google_search\":{}},"
    "{\"functionDeclarations\":["
    "{\"name\":\"get_mesh_nodes\",\"description\":\"Get list of all discovered nodes in the Meshtastic mesh network including node IDs, names, signal quality (SNR), hops away, and last heard time.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{}}},"
    "{\"name\":\"get_node_telemetry\",\"description\":\"Get device metrics (battery percentage, voltage, channel utilization, uptime) and environmental sensor metrics (temperature, humidity, barometric pressure, air quality) for a specific node or all nodes.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"node\":{\"type\":\"STRING\",\"description\":\"Optional node ID (e.g. !12345678 or 0x12345678) or node name. If omitted, returns telemetry for all nodes.\"}}}},"
    "{\"name\":\"get_network_stats\",\"description\":\"Get mesh network traffic statistics (packets and bytes transmitted and received, direct message and channel message counts), LoRa radio settings (preset, region, hop limit, tx power), and active channels.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{}}},"
    "{\"name\":\"get_node_positions\",\"description\":\"Get GPS location coordinates (latitude, longitude, altitude) for nodes on the mesh network.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"node\":{\"type\":\"STRING\",\"description\":\"Optional node ID or name. If omitted, returns positions for all nodes.\"}}}},"
    "{\"name\":\"schedule_task\",\"description\":\"Schedule a future or recurring message or dynamic prompt to be executed and sent over the mesh network.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"content\":{\"type\":\"STRING\",\"description\":\"The message text to send or prompt to evaluate when triggered.\"},\"action_type\":{\"type\":\"STRING\",\"description\":\"'message' (default) to send content directly, or 'prompt' to evaluate content as an AI prompt at trigger time.\"},\"delay_seconds\":{\"type\":\"INTEGER\",\"description\":\"Relative delay in seconds from now (e.g. 600 for 10 minutes).\"},\"at_time\":{\"type\":\"STRING\",\"description\":\"Datetime string in local time (e.g. '2026-08-31 06:00:00' or with timezone offset like '2026-08-31 06:00:00 +08:00').\"},\"cron\":{\"type\":\"STRING\",\"description\":\"5-field cron expression in local time, e.g. '0 6 * * 1' for Monday 6am local time.\"},\"channel\":{\"type\":\"STRING\",\"description\":\"Optional target channel name (e.g. 'CasaMag') or channel index.\"},\"target_node\":{\"type\":\"STRING\",\"description\":\"Optional target node ID (e.g. '!12345678') or 'broadcast'. Defaults to sender context.\"},\"max_repeats\":{\"type\":\"INTEGER\",\"description\":\"Optional maximum repeat count for cron/recurring tasks (default: infinite for cron, 1 for one-shot).\"}},\"required\":[\"content\"]}},"
    "{\"name\":\"list_scheduled_tasks\",\"description\":\"List all currently active scheduled tasks.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{}}},"
    "{\"name\":\"cancel_scheduled_task\",\"description\":\"Cancel a scheduled task by ID or cancel all active scheduled tasks.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"id\":{\"type\":\"INTEGER\",\"description\":\"Task ID to cancel.\"},\"all\":{\"type\":\"BOOLEAN\",\"description\":\"Set to true to cancel all tasks.\"}}}}"
    "]}";

string GeminiChat::buildRequest(uint32_t from,
                                uint32_t dest,
                                uint8_t channel,
                                const vector<ChatTurn> &history,
                                const string &message,
                                const vector<GeminiToolTurn> &toolTurns) const
{
    stringstream ss;
    vector<ChatTurn>::const_iterator it;
    bool first = true;

    ss << "{";
    ss << "\"systemInstruction\":{\"parts\":[{\"text\":\""
       << jsonEscape(getSystemInstruction(from, dest, channel)) << "\"}]},";
    ss << "\"contents\":[";

    for (it = history.begin(); it != history.end(); it++) {
        if (!first) {
            ss << ",";
        }
        first = false;
        ss << "{\"role\":\"" << (it->user ? "user" : "model")
           << "\",\"parts\":[{\"text\":\"" << jsonEscape(it->text)
           << "\"}]}";
    }
    if (!first) {
        ss << ",";
    }
    ss << "{\"role\":\"user\",\"parts\":[{\"text\":\""
       << jsonEscape(message) << "\"}]}";

    for (size_t t = 0; t < toolTurns.size(); t++) {
        ss << ",{\"role\":\"model\",\"parts\":" << toolTurns[t].modelPartsJson << "}";
        ss << ",{\"role\":\"user\",\"parts\":[";
        for (size_t r = 0; r < toolTurns[t].functionResponses.size(); r++) {
            if (r > 0) {
                ss << ",";
            }
            const string &resp = toolTurns[t].functionResponses[r].second;
            string validResp;
            if (resp.empty()) {
                validResp = "{}";
            } else if ((resp[0] == '{') || (resp[0] == '[')) {
                validResp = resp;
            } else {
                validResp = "\"" + jsonEscape(resp) + "\"";
            }
            ss << "{\"functionResponse\":{"
               << "\"name\":\"" << jsonEscape(toolTurns[t].functionResponses[r].first) << "\","
               << "\"response\":{\"result\":" << validResp << "}"
               << "}}";
        }
        ss << "]}";
    }

    ss << "],";
    ss << "\"tools\":[" << kMeshToolDeclarations << "],";
    ss << "\"toolConfig\":{\"includeServerSideToolInvocations\":true},";
    ss << "\"generationConfig\":{"
       << "\"maxOutputTokens\":" << GEMINI_MAX_OUTPUT_TOKENS << ","
       << "\"thinkingConfig\":{"
       << "\"thinkingBudget\":0"
       << "}"
       << "}";
    ss << "}";

    return ss.str();
}

static size_t geminiCurlWrite(char *ptr, size_t size, size_t nmemb,
                              void *userdata)
{
    string *out = (string *) userdata;

    out->append(ptr, size * nmemb);
    return size * nmemb;
}

string GeminiChat::httpPost(const string &url, const string &body) const
{
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    string response;
    string keyHeader;
    CURLcode rc;
    long httpCode = 0;
    string result;

    curl = curl_easy_init();
    if (curl == NULL) {
#if DEBUG_CHATBOT
        cout << "chatbot: curl_easy_init failed" << endl;
#endif
        return string();
    }

    keyHeader = string("x-goog-api-key: ") + _apiKey;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, keyHeader.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, geminiCurlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long) GEMINI_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                     (long) GEMINI_CONNECT_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "meshmon-gemini");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
#if DEBUG_CHATBOT
        cout << "chatbot: HTTP " << httpCode
             << " bytes=" << response.size() << endl;
#endif
        if (httpCode == 200) {
            result = response;
        } else {
            cerr << "gemini: HTTP " << httpCode << ": " << response << endl;
        }
    } else {
#if DEBUG_CHATBOT
        cout << "chatbot: curl " << curl_easy_strerror(rc) << endl;
#endif
        cerr << "gemini: " << curl_easy_strerror(rc) << endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return result;
}

#define GEMINI_MAX_TOOL_ITERATIONS 3

string GeminiChat::generate(uint32_t from,
                            uint32_t dest,
                            uint8_t channel,
                            const vector<ChatTurn> &history,
                            const string &message)
{
    string url;
    string body;
    string response;
    vector<GeminiToolTurn> toolTurns;

    if (!enabled() || message.empty()) {
#if DEBUG_CHATBOT
        cout << "chatbot: generate skip enabled="
             << (enabled() ? 1 : 0)
             << " empty=" << (message.empty() ? 1 : 0) << endl;
#endif
        return string();
    }

    url = string("https://generativelanguage.googleapis.com/v1beta/models/") +
        _model + string(":generateContent");

    for (int iter = 0; iter < GEMINI_MAX_TOOL_ITERATIONS; iter++) {
#if DEBUG_CHATBOT
        cout << "chatbot: POST " << url
             << " history=" << history.size()
             << " toolTurns=" << toolTurns.size()
             << " iter=" << iter << endl;
#endif
        body = buildRequest(from, dest, channel, history, message, toolTurns);
        response = httpPost(url, body);
        if (response.empty()) {
#if DEBUG_CHATBOT
            cout << "chatbot: empty HTTP body" << endl;
#endif
            return string();
        }

        string modelPartsJson;
        vector<GeminiFunctionCall> fcs;
        if (extractFunctionCalls(response, modelPartsJson, fcs)) {
#if DEBUG_CHATBOT
            cout << "chatbot: functionCalls count=" << fcs.size() << endl;
#endif
            GeminiToolTurn turn;
            turn.modelPartsJson = modelPartsJson;

            for (size_t f = 0; f < fcs.size(); f++) {
#if DEBUG_CHATBOT
                cout << "chatbot: functionCall name=" << fcs[f].name
                     << " args=" << fcs[f].argsJson << endl;
#endif
                string toolResult = executeTool(fcs[f].name, fcs[f].argsJson, from, dest, channel);
#if DEBUG_CHATBOT
                cout << "chatbot: toolResult bytes=" << toolResult.size() << endl;
#endif
                turn.functionResponses.push_back(make_pair(fcs[f].name, toolResult));
            }

            toolTurns.push_back(turn);
            continue;
        }

        string text = extractCandidateText(response);
#if DEBUG_CHATBOT
        cout << "chatbot: parsed text bytes=" << text.size() << endl;
        if (text.empty()) {
            string err = response;
            size_t i;
            for (i = 0; i < err.size(); i++) {
                if ((err[i] == '\n') || (err[i] == '\r')) {
                    err[i] = ' ';
                }
            }
            if (err.size() > 400) {
                err = err.substr(0, 400);
            }
            cout << "chatbot: HTTP 200 body " << err << endl;
        }
#endif
        return text;
    }

    return string();
}

/*
 * Local variables:
 * mode: C++
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
