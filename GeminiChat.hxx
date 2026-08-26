/*
 * GeminiChat.hxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#ifndef GEMINICHAT_HXX
#define GEMINICHAT_HXX

#include <ChatBot.hxx>

using namespace std;

struct GeminiFunctionCall {
    string name;
    string argsJson;
};

struct GeminiToolTurn {
    string modelPartsJson;
    vector<pair<string, string>> functionResponses;
};

class GeminiChat : public ChatBot {

public:

    GeminiChat(const string &apiKey,
               const string &model = "gemini-flash-latest");
    virtual ~GeminiChat();

    virtual bool enabled(void) const;

protected:

    virtual string generate(uint32_t from,
                            uint32_t dest,
                            uint8_t channel,
                            const vector<ChatTurn> &history,
                            const string &message);

    static string jsonEscape(const string &s);
    static bool parseJsonString(const string &s, size_t &i, string &out);
    static bool extractJsonArray(const string &s, size_t startSearch, string &out);
    static string extractCandidateText(const string &body);
    static bool extractFunctionCalls(const string &body,
                                     string &modelPartsJson,
                                     vector<GeminiFunctionCall> &fcs);
    string getSystemInstruction(uint32_t from, uint32_t dest, uint8_t channel) const;
    string buildRequest(uint32_t from,
                        uint32_t dest,
                        uint8_t channel,
                        const vector<ChatTurn> &history,
                        const string &message,
                        const vector<GeminiToolTurn> &toolTurns) const;
    string httpPost(const string &url, const string &body) const;

private:

    string _apiKey;
    string _model;

};

#endif

/*
 * Local variables:
 * mode: C++
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
