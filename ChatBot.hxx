/*
 * ChatBot.hxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#ifndef CHATBOT_HXX
#define CHATBOT_HXX

#include <stdint.h>
#include <ctime>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <MeshClient.hxx>

using namespace std;

struct ChatTurn {
    bool user;
    string text;
};

struct ChatReply {
    uint32_t from;
    uint32_t dest;
    uint8_t channel;
    string text;
};

class ChatBot {

public:

    ChatBot(shared_ptr<MeshClient> client = NULL);
    virtual ~ChatBot();

    virtual void setClient(shared_ptr<MeshClient> client);
    virtual bool enabled(void) const;

    void ask(uint32_t from, uint32_t dest, uint8_t channel,
             const string &message);
    bool pollReply(ChatReply &reply);

    void start(void);
    void stop(void);
    void join(void);

protected:

    virtual string generate(uint32_t from,
                            const vector<ChatTurn> &history,
                            const string &message) = 0;

    string truncateToMesh(const string &text) const;

    inline shared_ptr<MeshClient> getClient(void) const {
        return _client;
    }

    virtual string toolGetMeshNodes(void) const;
    virtual string toolGetNodeTelemetry(const string &nodeQuery = "") const;
    virtual string toolGetNetworkStats(void) const;
    virtual string toolGetNodePositions(const string &nodeQuery = "") const;
    virtual string executeTool(const string &name, const string &argsJson) const;
    uint32_t resolveNodeId(const string &nodeQuery) const;

private:

    struct ChatJob {
        uint32_t from;
        uint32_t dest;
        uint8_t channel;
        string message;
    };

    struct Conversation {
        vector<ChatTurn> turns;
        time_t lastUsed;

        Conversation() : lastUsed(0) {
        }
    };

    static void thread_function(ChatBot *bot);
    void run(void);
    void processJob(const ChatJob &job);
    void expireIdle(time_t now);

private:

    shared_ptr<MeshClient> _client;

    mutex _mutex;
    condition_variable _cv;
    shared_ptr<thread> _thread;
    atomic<bool> _isRunning;
    deque<ChatJob> _queue;
    deque<ChatReply> _replies;
    map<uint32_t, Conversation> _conversations;

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
