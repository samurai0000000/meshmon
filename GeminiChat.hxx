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

struct GeminiHttpPostResult {
    long httpCode;
    int curlCode;
    string body;
    string errorMessage;
    double elapsedSec;

    GeminiHttpPostResult()
        : httpCode(0), curlCode(0), elapsedSec(0.0) {
    }
};

struct GeminiTokenUsage {
    uint64_t promptTokens;
    uint64_t candidateTokens;
    uint64_t totalTokens;
    uint64_t cachedContentTokens;
    uint64_t lastPromptTokens;
    uint64_t lastCandidateTokens;
    uint64_t lastTotalTokens;
    uint64_t lastCachedContentTokens;
    uint32_t callCount;

    GeminiTokenUsage()
        : promptTokens(0), candidateTokens(0), totalTokens(0),
          cachedContentTokens(0), lastPromptTokens(0),
          lastCandidateTokens(0), lastTotalTokens(0),
          lastCachedContentTokens(0), callCount(0) {
    }
};

class GeminiChat : public ChatBot {

public:

    GeminiChat(const string &apiKey,
               const string &model = "gemini-3.5-flash",
               bool webSearch = true,
               uint32_t timeoutSec = 30,
               uint32_t maxOutputTokens = 512,
               int32_t thinkingBudget = 0);
    virtual ~GeminiChat();

    virtual bool enabled(void) const override;

    void setWebSearch(bool enable);
    bool getWebSearch(void) const;

    void setTimeout(uint32_t seconds);
    uint32_t getTimeout(void) const;

    void setMaxOutputTokens(uint32_t tokens);
    uint32_t getMaxOutputTokens(void) const;

    void setThinkingBudget(int32_t budget);
    int32_t getThinkingBudget(void) const;

    void setTemperature(float temp);
    float getTemperature(void) const;

    void setTopP(float topP);
    float getTopP(void) const;

    void setTopK(int32_t topK);
    int32_t getTopK(void) const;

    void setCustomInstruction(const string &instruction);
    string getCustomInstruction(void) const;

    void setModel(const string &model);
    string getModel(void) const;

    virtual uint64_t getTokensUsed(void) const override;
    GeminiTokenUsage getTokenUsage(void) const;
    void resetTokenUsage(void);

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
    bool extractUsageMetadata(const string &body,
                              uint64_t *outPrompt = NULL,
                              uint64_t *outCand = NULL,
                              uint64_t *outTotal = NULL,
                              uint64_t *outCached = NULL);
    string getSystemInstruction(uint32_t from, uint32_t dest, uint8_t channel) const;
    string buildRequest(uint32_t from,
                        uint32_t dest,
                        uint8_t channel,
                        const vector<ChatTurn> &history,
                        const string &message,
                        const vector<GeminiToolTurn> &toolTurns) const;
    GeminiHttpPostResult httpPost(const string &url, const string &body) const;
    void sendQueryStatus(uint32_t queryCount,
                         double elapsedSec,
                         uint64_t promptTokens, uint64_t candTokens,
                         uint64_t totalTokens, uint64_t cachedTokens,
                         const string &errorMsg);

private:

    string _apiKey;
    string _model;
    bool _webSearchEnabled;
    uint32_t _timeoutSec;
    uint32_t _maxOutputTokens;
    int32_t _thinkingBudget;
    float _temperature;
    float _topP;
    int32_t _topK;
    string _customInstruction;
    GeminiTokenUsage _usage;
    mutable mutex _stateMutex;

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
