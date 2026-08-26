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

string GeminiChat::getSystemInstruction(uint32_t from) const
{
    time_t now = time(NULL);
    struct tm tm;
    char timeBuf[64];
    gmtime_r(&now, &tm);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S UTC", &tm);

    stringstream ss;
    ss << "You are a Meshtastic mesh chatbot. Current time is " << timeBuf << ".";

    if (from != 0) {
        shared_ptr<MeshClient> client = getClient();
        if (client != NULL) {
            string name = client->getDisplayName(from);
            if (!name.empty()) {
                ss << " You are talking to user \"" << name << "\"";
            }
        }
        char nodeBuf[16];
        snprintf(nodeBuf, sizeof(nodeBuf), " (!%08x)", from);
        ss << nodeBuf << ".";
    }

    ss << " Reply in one short plain-text sentence. Maximum 180 characters. "
       << "No markdown, no lists, no emoji, and no quotes around the whole reply.";

    return ss.str();
}

string GeminiChat::extractCandidateText(const string &body)
{
    size_t cand;
    size_t pos;
    string value;
    string found;

    cand = body.find("\"candidates\"");
    if (cand == string::npos) {
        return string();
    }

    pos = cand;
    while (pos < body.size()) {
        size_t textKey;
        size_t i;
        size_t windowBeg;
        size_t windowEnd;
        bool thought = false;

        textKey = body.find("\"text\"", pos);
        if (textKey == string::npos) {
            break;
        }
        pos = textKey + 6;

        i = textKey + 6;
        while ((i < body.size()) &&
               isspace(static_cast<unsigned char>(body[i]))) {
            i++;
        }
        if ((i >= body.size()) || (body[i] != ':')) {
            continue;
        }
        i++;
        while ((i < body.size()) &&
               isspace(static_cast<unsigned char>(body[i]))) {
            i++;
        }
        if (!parseJsonString(body, i, value)) {
            continue;
        }

        while (!value.empty() &&
               isspace(static_cast<unsigned char>(value[value.size() - 1]))) {
            value.erase(value.size() - 1);
        }
        {
            size_t lead = 0;
            while ((lead < value.size()) &&
                   isspace(static_cast<unsigned char>(value[lead]))) {
                lead++;
            }
            if (lead != 0) {
                value = value.substr(lead);
            }
        }
        if (value.empty()) {
            continue;
        }

        windowBeg = (textKey > 96) ? (textKey - 96) : cand;
        windowEnd = (i + 96 < body.size()) ? (i + 96) : body.size();
        if (body.find("\"thought\":true", windowBeg) < windowEnd) {
            thought = true;
        } else if (body.find("\"thought\": true", windowBeg) < windowEnd) {
            thought = true;
        }
        if (thought) {
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

string GeminiChat::buildRequest(uint32_t from,
                                const vector<ChatTurn> &history,
                                const string &message) const
{
    stringstream ss;
    vector<ChatTurn>::const_iterator it;
    bool first = true;

    ss << "{";
    ss << "\"systemInstruction\":{\"parts\":[{\"text\":\""
       << jsonEscape(getSystemInstruction(from)) << "\"}]},";
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

    ss << "],";
    ss << "\"tools\":[{\"google_search\":{}}],";
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
            cerr << "gemini: HTTP " << httpCode << endl;
#if DEBUG_CHATBOT
            {
                string err = response;
                size_t i;
                for (i = 0; i < err.size(); i++) {
                    if ((err[i] == '\n') || (err[i] == '\r')) {
                        err[i] = ' ';
                    }
                }
                if (err.size() > 300) {
                    err = err.substr(0, 300);
                }
                cout << "chatbot: HTTP body " << err << endl;
            }
#endif
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

string GeminiChat::generate(uint32_t from,
                            const vector<ChatTurn> &history,
                            const string &message)
{
    string url;
    string body;
    string response;

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
#if DEBUG_CHATBOT
    cout << "chatbot: POST " << url
         << " history=" << history.size() << endl;
#endif
    body = buildRequest(from, history, message);
    response = httpPost(url, body);
    if (response.empty()) {
#if DEBUG_CHATBOT
        cout << "chatbot: empty HTTP body" << endl;
#endif
        return string();
    }

    {
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
