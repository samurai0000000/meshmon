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
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <libconfig.h++>
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

enum ScheduledTaskType {
    TASK_MESSAGE = 0,
    TASK_PROMPT  = 1,
};

struct ScheduledTask {
    uint32_t id;
    uint32_t from;
    uint32_t dest;
    uint8_t channel;
    string channelName;
    ScheduledTaskType type;
    string content;
    time_t dueTime;
    string cronExpr;
    int64_t repeatIntervalSec;
    int maxRepeats;      // -1 for infinite, 1 for one-shot, N for finite count
    int executionCount;
    time_t createdAt;

    ScheduledTask()
        : id(0), from(0), dest(0xffffffffU), channel(0),
          type(TASK_MESSAGE), dueTime(0), repeatIntervalSec(0),
          maxRepeats(1), executionCount(0), createdAt(0) {
    }
};

struct ChatBotStats {
    uint64_t invocations;
    uint64_t successes;
    uint64_t failures;

    ChatBotStats() : invocations(0), successes(0), failures(0) {}
};

class ChatBot {

public:

    ChatBot(shared_ptr<MeshClient> client = NULL);
    virtual ~ChatBot();

    virtual void setClient(shared_ptr<MeshClient> client);
    virtual bool enabled(void) const;

    ChatBotStats getStats(void) const;
    void resetStats(void);
    virtual uint64_t getTokensUsed(void) const;

    void setConfigPath(const string &path);
    const string &getConfigPath(void) const;
    bool loadTasksFromFile(const string &path = "");
    bool loadTasksFromConfig(libconfig::Config &cfg);
    bool writeTasksToConfig(libconfig::Config &cfg) const;
    bool saveTasksToConfig(const string &path = "") const;

    void ask(uint32_t from, uint32_t dest, uint8_t channel,
             const string &message);
    bool pollReply(ChatReply &reply);

    void start(void);
    void stop(void);
    void join(void);

    uint32_t scheduleTask(const ScheduledTask &task);
    bool cancelTask(uint32_t id, uint32_t from = 0);
    vector<ScheduledTask> getTasks(uint32_t from = 0) const;

    static bool parseTimeString(const string &str, time_t &outTime);
    static inline bool parseUtcTimeString(const string &str, time_t &outTime) {
        return parseTimeString(str, outTime);
    }
    static bool computeNextCronOccurrence(const string &cronExpr, time_t fromTime, time_t &nextTime);
    static bool parseJsonStringField(const string &json, const string &field, string &val);
    static bool parseJsonIntField(const string &json, const string &field, int64_t &val);
    static bool parseJsonBoolField(const string &json, const string &field, bool &val);

protected:

    virtual string generate(uint32_t from,
                            uint32_t dest,
                            uint8_t channel,
                            const vector<ChatTurn> &history,
                            const string &message) = 0;

    string truncateToMesh(const string &text) const;

    inline shared_ptr<MeshClient> getClient(void) const {
        return _client;
    }

    uint8_t resolveChannel(const string &channelQuery, uint8_t defaultChannel = 0) const;
    uint32_t resolveNodeId(const string &nodeQuery) const;

    virtual string toolGetMeshNodes(void) const;
    virtual string toolGetNodeTelemetry(const string &nodeQuery = "") const;
    virtual string toolGetNetworkStats(void) const;
    virtual string toolGetNodePositions(const string &nodeQuery = "") const;
    virtual string toolScheduleTask(uint32_t from, uint32_t dest, uint8_t channel, const string &argsJson);
    virtual string toolListTasks(uint32_t from) const;
    virtual string toolCancelTask(uint32_t from, const string &argsJson);
    virtual string executeTool(const string &name, const string &argsJson,
                               uint32_t from = 0, uint32_t dest = 0, uint8_t channel = 0);

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
    void processDueTasks(time_t now);
    void expireIdle(time_t now);

private:

    shared_ptr<MeshClient> _client;

    string _configPath;
    mutable mutex _mutex;
    condition_variable _cv;
    shared_ptr<thread> _thread;
    atomic<bool> _isRunning;
    deque<ChatJob> _queue;
    deque<ChatReply> _replies;
    map<uint32_t, Conversation> _conversations;
    vector<ScheduledTask> _tasks;
    uint32_t _nextTaskId;
    ChatBotStats _stats;

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
