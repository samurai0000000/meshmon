/*
 * ChatBot.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <time.h>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <ChatBot.hxx>

#ifndef DEBUG_CHATBOT
#define DEBUG_CHATBOT 0
#endif

#define CHAT_HISTORY_MAX    10
#define CHAT_IDLE_SECONDS   3600
#define CHAT_MAX_BYTES      200

using namespace libconfig;

static string jsonEscape(const string &s)
{
    string out;
    char buf[8];

    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); i++) {
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

static bool parseJsonStringRaw(const string &s, size_t &i, string &out)
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

bool ChatBot::parseJsonStringField(const string &json, const string &field, string &val)
{
    string target = "\"" + field + "\"";
    size_t pos = json.find(target);
    if (pos == string::npos) {
        return false;
    }

    size_t colon = json.find(':', pos + target.size());
    if (colon == string::npos) {
        return false;
    }

    size_t i = colon + 1;
    while ((i < json.size()) && isspace(static_cast<unsigned char>(json[i]))) {
        i++;
    }

    if ((i >= json.size()) || (json[i] != '"')) {
        return false;
    }

    return parseJsonStringRaw(json, i, val);
}

bool ChatBot::parseJsonIntField(const string &json, const string &field, int64_t &val)
{
    string target = "\"" + field + "\"";
    size_t pos = json.find(target);
    if (pos == string::npos) {
        return false;
    }

    size_t colon = json.find(':', pos + target.size());
    if (colon == string::npos) {
        return false;
    }

    size_t i = colon + 1;
    while ((i < json.size()) && isspace(static_cast<unsigned char>(json[i]))) {
        i++;
    }

    if (i >= json.size()) {
        return false;
    }

    if (json[i] == '"') {
        string s;
        if (!parseJsonStringRaw(json, i, s)) {
            return false;
        }
        char *endptr = NULL;
        val = strtoll(s.c_str(), &endptr, 10);
        return (endptr != NULL) && (endptr != s.c_str());
    }

    char *endptr = NULL;
    val = strtoll(json.c_str() + i, &endptr, 10);
    return (endptr != NULL) && (endptr != json.c_str() + i);
}

bool ChatBot::parseJsonBoolField(const string &json, const string &field, bool &val)
{
    string target = "\"" + field + "\"";
    size_t pos = json.find(target);
    if (pos == string::npos) {
        return false;
    }

    size_t colon = json.find(':', pos + target.size());
    if (colon == string::npos) {
        return false;
    }

    size_t i = colon + 1;
    while ((i < json.size()) && isspace(static_cast<unsigned char>(json[i]))) {
        i++;
    }

    if (i >= json.size()) {
        return false;
    }

    if (json[i] == '"') {
        string s;
        if (!parseJsonStringRaw(json, i, s)) {
            return false;
        }
        if ((s == "true") || (s == "1") || (s == "yes")) {
            val = true;
            return true;
        } else if ((s == "false") || (s == "0") || (s == "no")) {
            val = false;
            return true;
        }
        return false;
    }

    if (json.compare(i, 4, "true") == 0) {
        val = true;
        return true;
    } else if (json.compare(i, 5, "false") == 0) {
        val = false;
        return true;
    }

    return false;
}

bool ChatBot::parseTimeString(const string &str, time_t &outTime)
{
    if (str.empty()) {
        return false;
    }

    // Check if numeric timestamp
    bool allDigits = true;
    for (size_t k = 0; k < str.size(); k++) {
        if (!isdigit(static_cast<unsigned char>(str[k])) && (str[k] != '-')) {
            allDigits = false;
            break;
        }
    }
    if (allDigits && (str.size() >= 8)) {
        char *endptr = NULL;
        long long v = strtoll(str.c_str(), &endptr, 10);
        if (endptr && (*endptr == '\0') && (v > 1000000000LL)) {
            outTime = static_cast<time_t>(v);
            return true;
        }
    }

    struct tm tm;
    memset(&tm, 0, sizeof(tm));

    int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;
    int tzSign = 0, tzHour = 0, tzMin = 0;

    int matched = sscanf(str.c_str(), "%d-%d-%d%*1[T ]%d:%d:%d",
                         &year, &mon, &day, &hour, &min, &sec);
    if (matched < 3) {
        return false;
    }

    if (matched == 3) {
        hour = 0;
        min = 0;
        sec = 0;
    }

    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;

    // Check for explicit timezone suffix
    bool hasTz = false;
    if ((str.find("UTC") != string::npos) || (str.find("GMT") != string::npos) || (str.find("Z") != string::npos)) {
        hasTz = true;
        tzSign = 0;
    } else {
        size_t tzPos = str.find_last_of("+-");
        if ((tzPos != string::npos) && (tzPos > 8)) {
            char sign = str[tzPos];
            if (sscanf(str.c_str() + tzPos + 1, "%d:%d", &tzHour, &tzMin) >= 1) {
                hasTz = true;
                tzSign = (sign == '-') ? -1 : 1;
            } else if (sscanf(str.c_str() + tzPos + 1, "%2d%2d", &tzHour, &tzMin) >= 1) {
                hasTz = true;
                tzSign = (sign == '-') ? -1 : 1;
            }
        }
    }

    if (hasTz) {
        tm.tm_isdst = 0;
        time_t epoch = timegm(&tm);
        if (epoch == -1) {
            return false;
        }
        if (tzSign != 0) {
            epoch -= tzSign * (tzHour * 3600 + tzMin * 60);
        }
        outTime = epoch;
        return true;
    }

    // Default to local timezone
    tm.tm_isdst = -1;
    time_t localEpoch = mktime(&tm);
    if (localEpoch == -1) {
        return false;
    }

    outTime = localEpoch;
    return true;
}

static bool parseCronField(const string &field, int minVal, int maxVal, vector<bool> &allowed)
{
    allowed.assign(maxVal + 1, false);

    stringstream ss(field);
    string token;

    while (getline(ss, token, ',')) {
        if (token.empty()) {
            continue;
        }

        int step = 1;
        size_t slash = token.find('/');
        if (slash != string::npos) {
            string stepStr = token.substr(slash + 1);
            token = token.substr(0, slash);
            step = atoi(stepStr.c_str());
            if (step <= 0) {
                return false;
            }
        }

        int start = minVal;
        int end = maxVal;

        if (token == "*") {
            start = minVal;
            end = maxVal;
        } else {
            size_t dash = token.find('-');
            if (dash != string::npos) {
                start = atoi(token.substr(0, dash).c_str());
                end = atoi(token.substr(dash + 1).c_str());
            } else {
                start = atoi(token.c_str());
                end = (slash != string::npos) ? maxVal : start;
            }
        }

        if ((start < minVal) || (end > maxVal) || (start > end)) {
            return false;
        }

        for (int v = start; v <= end; v += step) {
            allowed[v] = true;
        }
    }

    return true;
}

bool ChatBot::computeNextCronOccurrence(const string &cronExpr, time_t fromTime, time_t &nextTime)
{
    stringstream ss(cronExpr);
    vector<string> fields;
    string f;

    while (ss >> f) {
        fields.push_back(f);
    }

    if (fields.size() != 5) {
        return false;
    }

    vector<bool> allowMin, allowHour, allowDom, allowMon, allowDow;
    if (!parseCronField(fields[0], 0, 59, allowMin) ||
        !parseCronField(fields[1], 0, 23, allowHour) ||
        !parseCronField(fields[2], 1, 31, allowDom) ||
        !parseCronField(fields[3], 1, 12, allowMon) ||
        !parseCronField(fields[4], 0, 7, allowDow)) {
        return false;
    }

    // Map 7 (Sunday) to 0
    if (allowDow[7]) {
        allowDow[0] = true;
    }

    bool domRestricted = (fields[2] != "*");
    bool dowRestricted = (fields[4] != "*");

    // Start looking from the next minute in local time
    time_t curr = (fromTime / 60) * 60 + 60;
    time_t maxSearch = fromTime + (5 * 365 * 86400); // Search up to 5 years

    while (curr < maxSearch) {
        struct tm tm;
        localtime_r(&curr, &tm);

        int mon = tm.tm_mon + 1;
        if (!allowMon[mon]) {
            // Advance to next month in local time
            tm.tm_mday = 1;
            tm.tm_hour = 0;
            tm.tm_min = 0;
            tm.tm_sec = 0;
            tm.tm_mon++;
            tm.tm_isdst = -1;
            curr = mktime(&tm);
            continue;
        }

        int dom = tm.tm_mday;
        int dow = tm.tm_wday;
        bool dayMatch = false;

        if (domRestricted && dowRestricted) {
            dayMatch = allowDom[dom] || allowDow[dow];
        } else if (domRestricted) {
            dayMatch = allowDom[dom];
        } else if (dowRestricted) {
            dayMatch = allowDow[dow];
        } else {
            dayMatch = true;
        }

        if (!dayMatch) {
            // Advance to next day in local time
            tm.tm_hour = 0;
            tm.tm_min = 0;
            tm.tm_sec = 0;
            tm.tm_mday++;
            tm.tm_isdst = -1;
            curr = mktime(&tm);
            continue;
        }

        if (!allowHour[tm.tm_hour]) {
            // Advance to next hour in local time
            tm.tm_min = 0;
            tm.tm_sec = 0;
            tm.tm_hour++;
            tm.tm_isdst = -1;
            curr = mktime(&tm);
            continue;
        }

        if (!allowMin[tm.tm_min]) {
            curr += 60;
            continue;
        }

        nextTime = curr;
        return true;
    }

    return false;
}

ChatBot::ChatBot(shared_ptr<MeshClient> client)
    : _client(client),
      _thread(NULL),
      _isRunning(false),
      _nextTaskId(1),
      _enabled(true),
      _maxHistoryTurns(CHAT_HISTORY_MAX),
      _idleTimeoutSec(CHAT_IDLE_SECONDS)
{

}

ChatBot::~ChatBot()
{
    stop();
    join();
}

void ChatBot::setClient(shared_ptr<MeshClient> client)
{
    _client = client;
}

void ChatBot::setEnabled(bool enable)
{
    _enabled = enable;
}

bool ChatBot::enabled(void) const
{
    return _enabled;
}

void ChatBot::setMaxHistoryTurns(size_t turns)
{
    lock_guard<mutex> lock(_mutex);
    _maxHistoryTurns = (turns == 0) ? 1 : turns;
}

size_t ChatBot::getMaxHistoryTurns(void) const
{
    lock_guard<mutex> lock(_mutex);
    return _maxHistoryTurns;
}

void ChatBot::setIdleTimeout(uint32_t seconds)
{
    lock_guard<mutex> lock(_mutex);
    _idleTimeoutSec = seconds;
}

uint32_t ChatBot::getIdleTimeout(void) const
{
    lock_guard<mutex> lock(_mutex);
    return _idleTimeoutSec;
}

void ChatBot::clearConversations(uint32_t nodeId)
{
    lock_guard<mutex> lock(_mutex);
    if (nodeId == 0) {
        _conversations.clear();
    } else {
        _conversations.erase(nodeId);
    }
}

ChatBotStats ChatBot::getStats(void) const
{
    lock_guard<mutex> lock(_mutex);
    return _stats;
}

void ChatBot::resetStats(void)
{
    lock_guard<mutex> lock(_mutex);
    _stats = ChatBotStats();
}

uint64_t ChatBot::getTokensUsed(void) const
{
    return 0;
}

void ChatBot::setConfigPath(const string &path)
{
    lock_guard<mutex> lock(_mutex);
    _configPath = path;
}

const string &ChatBot::getConfigPath(void) const
{
    lock_guard<mutex> lock(_mutex);
    return _configPath;
}

bool ChatBot::loadTasksFromConfig(Config &cfg)
{
    lock_guard<mutex> lock(_mutex);
    Setting &root = cfg.getRoot();

    if (!root.exists("scheduled_tasks")) {
        return false;
    }

    _tasks.clear();
    uint32_t maxId = 0;

    try {
        Setting &taskList = root["scheduled_tasks"];
        for (int i = 0; i < taskList.getLength(); i++) {
            Setting &elem = taskList[i];
            if (elem.getType() != Setting::TypeGroup) {
                continue;
            }

            ScheduledTask task;
            int cfgId = 0;
            int cfgChannel = 0;
            string cfgType;
            string cfgFrom;
            string cfgDest;
            long long cfgDue = 0;
            long long cfgInterval = 0;
            int cfgMaxRepeats = 1;
            int cfgExecCount = 0;
            long long cfgCreatedAt = 0;

            if (elem.lookupValue("id", cfgId)) {
                task.id = static_cast<uint32_t>(cfgId);
            }
            if (elem.lookupValue("from", cfgFrom)) {
                task.from = resolveNodeId(cfgFrom);
            }
            if (elem.lookupValue("dest", cfgDest)) {
                task.dest = resolveNodeId(cfgDest);
            }
            if (elem.lookupValue("channel", cfgChannel)) {
                task.channel = static_cast<uint8_t>(cfgChannel);
            }
            elem.lookupValue("channel_name", task.channelName);
            if (elem.lookupValue("type", cfgType)) {
                if (cfgType == "prompt") {
                    task.type = TASK_PROMPT;
                } else {
                    task.type = TASK_MESSAGE;
                }
            }
            elem.lookupValue("content", task.content);
            if (elem.lookupValue("due_time", cfgDue)) {
                task.dueTime = static_cast<time_t>(cfgDue);
            }
            elem.lookupValue("cron", task.cronExpr);
            if (elem.lookupValue("interval", cfgInterval)) {
                task.repeatIntervalSec = static_cast<int64_t>(cfgInterval);
            }
            if (elem.lookupValue("max_repeats", cfgMaxRepeats)) {
                task.maxRepeats = cfgMaxRepeats;
            }
            if (elem.lookupValue("execution_count", cfgExecCount)) {
                task.executionCount = cfgExecCount;
            }
            if (elem.lookupValue("created_at", cfgCreatedAt)) {
                task.createdAt = static_cast<time_t>(cfgCreatedAt);
            }

            if ((task.id != 0) && !task.content.empty() && (task.dueTime != 0)) {
                _tasks.push_back(task);
                if (task.id > maxId) {
                    maxId = task.id;
                }
            }
        }
    } catch (...) {
        return false;
    }

    _nextTaskId = maxId + 1;
    return true;
}

bool ChatBot::writeTasksToConfig(Config &cfg) const
{
    Setting &root = cfg.getRoot();

    if (root.exists("scheduled_tasks")) {
        root.remove("scheduled_tasks");
    }

    if (_tasks.empty()) {
        return true;
    }

    Setting &taskList = root.add("scheduled_tasks", Setting::TypeList);
    for (size_t i = 0; i < _tasks.size(); i++) {
        const ScheduledTask &task = _tasks[i];
        Setting &group = taskList.add(Setting::TypeGroup);

        group.add("id", Setting::TypeInt) = static_cast<int>(task.id);

        char fromBuf[16], destBuf[16];
        snprintf(fromBuf, sizeof(fromBuf), "!%08x", task.from);
        snprintf(destBuf, sizeof(destBuf), "!%08x", task.dest);
        group.add("from", Setting::TypeString) = string(fromBuf);
        group.add("dest", Setting::TypeString) = string(destBuf);
        group.add("channel", Setting::TypeInt) = static_cast<int>(task.channel);
        if (!task.channelName.empty()) {
            group.add("channel_name", Setting::TypeString) = task.channelName;
        }

        group.add("type", Setting::TypeString) = (task.type == TASK_PROMPT) ? string("prompt") : string("message");
        group.add("content", Setting::TypeString) = task.content;
        group.add("due_time", Setting::TypeInt64) = static_cast<long long>(task.dueTime);

        if (!task.cronExpr.empty()) {
            group.add("cron", Setting::TypeString) = task.cronExpr;
        }
        if (task.repeatIntervalSec > 0) {
            group.add("interval", Setting::TypeInt64) = static_cast<long long>(task.repeatIntervalSec);
        }
        group.add("max_repeats", Setting::TypeInt) = task.maxRepeats;
        group.add("execution_count", Setting::TypeInt) = task.executionCount;
        group.add("created_at", Setting::TypeInt64) = static_cast<long long>(task.createdAt);
    }

    return true;
}

static string resolveConfigPath(const string &path, const string &storedPath)
{
    if (!path.empty()) {
        return path;
    }
    if (!storedPath.empty()) {
        return storedPath;
    }
    const char *homedir = getenv("HOME");
    if ((homedir != NULL) && (homedir[0] != '\0')) {
        return string(homedir) + "/.meshmon.sched";
    }
    return ".meshmon.sched";
}

bool ChatBot::loadTasksFromFile(const string &path)
{
    string targetPath;
    {
        lock_guard<mutex> lock(_mutex);
        targetPath = resolveConfigPath(path, _configPath);
        _configPath = targetPath;
    }
    Config cfg;

    try {
        cfg.readFile(targetPath.c_str());
    } catch (const FileIOException &) {
        return false;
    } catch (const ParseException &e) {
        cerr << "Parse error in " << e.getFile() << " line "
             << e.getLine() << ": " << e.getError() << endl;
        return false;
    }

    return loadTasksFromConfig(cfg);
}

bool ChatBot::saveTasksToConfig(const string &path) const
{
    string targetPath;
    {
        lock_guard<mutex> lock(_mutex);
        targetPath = resolveConfigPath(path, _configPath);
    }

    Config cfg;
    try {
        cfg.readFile(targetPath.c_str());
    } catch (...) {
    }

    {
        lock_guard<mutex> lock(_mutex);
        if (!writeTasksToConfig(cfg)) {
            return false;
        }
    }

    try {
        cfg.writeFile(targetPath.c_str());
    } catch (const FileIOException &e) {
        cerr << targetPath << ": " << strerror(errno) << endl;
        return false;
    }

    return true;
}

uint32_t ChatBot::scheduleTask(const ScheduledTask &task)
{
    ScheduledTask t = task;
    uint32_t assignedId = 0;

    {
        unique_lock<mutex> lock(_mutex);
        if (t.id == 0) {
            t.id = _nextTaskId++;
        } else if (t.id >= _nextTaskId) {
            _nextTaskId = t.id + 1;
        }
        if (t.createdAt == 0) {
            t.createdAt = time(NULL);
        }
        _tasks.push_back(t);
        assignedId = t.id;
    }

    saveTasksToConfig();
    _cv.notify_one();
    return assignedId;
}

bool ChatBot::cancelTask(uint32_t id, uint32_t from)
{
    bool found = false;
    {
        unique_lock<mutex> lock(_mutex);
        vector<ScheduledTask>::iterator it = _tasks.begin();
        while (it != _tasks.end()) {
            if ((it->id == id) && ((from == 0) || (it->from == from))) {
                it = _tasks.erase(it);
                found = true;
                break;
            } else {
                it++;
            }
        }
    }

    if (found) {
        saveTasksToConfig();
        _cv.notify_one();
    }
    return found;
}

vector<ScheduledTask> ChatBot::getTasks(uint32_t from) const
{
    lock_guard<mutex> lock(_mutex);
    if (from == 0) {
        return _tasks;
    }

    vector<ScheduledTask> res;
    for (size_t i = 0; i < _tasks.size(); i++) {
        if (_tasks[i].from == from) {
            res.push_back(_tasks[i]);
        }
    }
    return res;
}

void ChatBot::ask(uint32_t from, uint32_t dest, uint8_t channel,
                  const string &message)
{
    deque<ChatJob>::iterator it;

    if (!enabled() || !_isRunning.load() || message.empty()) {
#if DEBUG_CHATBOT
        cout << "chatbot: ask dropped enabled=" << (enabled() ? 1 : 0)
             << " running=" << (_isRunning.load() ? 1 : 0)
             << " empty=" << (message.empty() ? 1 : 0) << endl;
#endif
        return;
    }

    {
        unique_lock<mutex> lock(_mutex);

        for (it = _queue.begin(); it != _queue.end(); it++) {
            if (it->from == from) {
                it->dest = dest;
                it->channel = channel;
                it->message = message;
#if DEBUG_CHATBOT
                cout << "chatbot: queue replace from=" << from
                     << " dest=" << dest
                     << " channel=" << (unsigned int) channel
                     << " msg='" << message << "'" << endl;
#endif
                return;
            }
        }

        ChatJob job;

        job.from = from;
        job.dest = dest;
        job.channel = channel;
        job.message = message;
        _queue.push_back(job);
#if DEBUG_CHATBOT
        cout << "chatbot: queued from=" << from
             << " dest=" << dest
             << " channel=" << (unsigned int) channel
             << " msg='" << message << "'"
             << " qlen=" << _queue.size() << endl;
#endif
    }

    _cv.notify_one();
}

bool ChatBot::pollReply(ChatReply &reply)
{
    unique_lock<mutex> lock(_mutex);

    if (_replies.empty()) {
        return false;
    }

    reply = _replies.front();
    _replies.pop_front();
    return true;
}

void ChatBot::queueReply(uint32_t from, uint32_t dest, uint8_t channel,
                         const string &text)
{
    string replyText = truncateToMesh(text);
    if (replyText.empty()) {
        return;
    }

    ChatReply reply;
    reply.from = from;
    reply.dest = dest;
    reply.channel = channel;
    reply.text = replyText;

    unique_lock<mutex> lock(_mutex);
    _replies.push_back(reply);
}

void ChatBot::start(void)
{
    if (!_isRunning.load()) {
        if (_thread == NULL) {
            _isRunning.store(true);
            _thread = make_shared<thread>(thread_function, this);
        }
    }
}

void ChatBot::stop(void)
{
    if (_isRunning.load()) {
        _isRunning.store(false);
        _cv.notify_one();
    }
}

void ChatBot::join(void)
{
    if (_thread != NULL) {
        if (_thread->joinable()) {
            _thread->join();
        }
        _thread.reset();
    }
}

void ChatBot::thread_function(ChatBot *bot)
{
    bot->run();
}

void ChatBot::processDueTasks(time_t now)
{
    vector<ScheduledTask> dueTasks;
    bool modified = false;

    {
        unique_lock<mutex> lock(_mutex);
        vector<ScheduledTask>::iterator it = _tasks.begin();
        while (it != _tasks.end()) {
            if (now >= it->dueTime) {
                dueTasks.push_back(*it);
                it->executionCount++;

                bool keep = false;
                time_t nextDue = 0;

                if ((it->maxRepeats <= 0) || (it->executionCount < it->maxRepeats)) {
                    if (!it->cronExpr.empty()) {
                        if (computeNextCronOccurrence(it->cronExpr, now, nextDue)) {
                            it->dueTime = nextDue;
                            keep = true;
                        }
                    } else if (it->repeatIntervalSec > 0) {
                        it->dueTime = now + it->repeatIntervalSec;
                        keep = true;
                    }
                }

                if (keep) {
                    it++;
                } else {
                    it = _tasks.erase(it);
                }
                modified = true;
            } else {
                it++;
            }
        }
    }

    if (modified) {
        saveTasksToConfig();
    }

    for (size_t i = 0; i < dueTasks.size(); i++) {
        const ScheduledTask &task = dueTasks[i];
        string outputText;

        if (task.type == TASK_PROMPT) {
            {
                unique_lock<mutex> lock(_mutex);
                _stats.invocations++;
            }
            vector<ChatTurn> history;
            {
                unique_lock<mutex> lock(_mutex);
                map<uint32_t, Conversation>::iterator cIt = _conversations.find(task.from);
                if (cIt != _conversations.end()) {
                    history = cIt->second.turns;
                }
            }
            outputText = generate(task.from, task.dest, task.channel, history, task.content);
            {
                unique_lock<mutex> lock(_mutex);
                if (!outputText.empty()) {
                    _stats.successes++;
                } else {
                    _stats.failures++;
                }
            }
        } else {
            outputText = task.content;
        }

        if (outputText.empty()) {
            continue;
        }

        string replyText = truncateToMesh(outputText);
        if (replyText.empty()) {
            continue;
        }

        {
            unique_lock<mutex> lock(_mutex);
            ChatReply reply;
            reply.from = task.from;
            reply.dest = task.dest;
            reply.channel = task.channel;
            reply.text = replyText;
            _replies.push_back(reply);

            // Record into conversation history if active
            map<uint32_t, Conversation>::iterator cIt = _conversations.find(task.from);
            if (cIt != _conversations.end()) {
                ChatTurn modelTurn;
                modelTurn.user = false;
                modelTurn.text = replyText;
                cIt->second.turns.push_back(modelTurn);
                while (cIt->second.turns.size() > CHAT_HISTORY_MAX) {
                    cIt->second.turns.erase(cIt->second.turns.begin());
                }
                cIt->second.lastUsed = now;
            }
        }
#if DEBUG_CHATBOT
        cout << "chatbot: scheduled task " << task.id << " executed, reply queued to dest="
             << task.dest << " ch=" << (unsigned int) task.channel << endl;
#endif
    }
}

void ChatBot::run(void)
{
    while (_isRunning.load()) {
        ChatJob job;
        bool haveJob = false;

        {
            unique_lock<mutex> lock(_mutex);

            while (_isRunning.load() && _queue.empty()) {
                time_t now = time(NULL);
                time_t earliestDue = 0;

                for (size_t i = 0; i < _tasks.size(); i++) {
                    if (_tasks[i].dueTime <= now) {
                        earliestDue = now;
                        break;
                    }
                    if ((earliestDue == 0) || (_tasks[i].dueTime < earliestDue)) {
                        earliestDue = _tasks[i].dueTime;
                    }
                }

                if (earliestDue != 0) {
                    if (earliestDue <= now) {
                        break;
                    }
                    _cv.wait_until(lock, chrono::system_clock::from_time_t(earliestDue));
                } else {
                    _cv.wait(lock);
                }

                if (!_tasks.empty()) {
                    time_t checkNow = time(NULL);
                    for (size_t i = 0; i < _tasks.size(); i++) {
                        if (_tasks[i].dueTime <= checkNow) {
                            break;
                        }
                    }
                }
            }

            if (!_queue.empty()) {
                job = _queue.front();
                _queue.pop_front();
                haveJob = true;
            }
        }

        // Process any due tasks
        processDueTasks(time(NULL));

        if (haveJob) {
            processJob(job);
        }
    }
}

void ChatBot::expireIdle(time_t now)
{
    map<uint32_t, Conversation>::iterator it;
    time_t timeoutSec;

    {
        lock_guard<mutex> lock(_mutex);
        timeoutSec = (time_t) _idleTimeoutSec;
    }

    it = _conversations.begin();
    while (it != _conversations.end()) {
        if ((now - it->second.lastUsed) >= timeoutSec) {
            map<uint32_t, Conversation>::iterator eraseIt = it;
            it++;
            _conversations.erase(eraseIt);
        } else {
            it++;
        }
    }
}

string ChatBot::truncateToMesh(const string &text) const
{
    string out = text;
    size_t i;
    size_t cut;

    for (i = 0; i < out.size(); i++) {
        if ((out[i] == '\n') || (out[i] == '\r')) {
            out[i] = ' ';
        }
    }

    if (out.size() <= CHAT_MAX_BYTES) {
        return out;
    }

    cut = CHAT_MAX_BYTES;
    while ((cut > 0) &&
           ((static_cast<unsigned char>(out[cut]) & 0xc0) == 0x80)) {
        cut--;
    }

    return out.substr(0, cut);
}

void ChatBot::processJob(const ChatJob &job)
{
    time_t now = time(NULL);
    string generated;
    string reply;
    ChatTurn userTurn;
    ChatTurn modelTurn;
    bool success = false;

    expireIdle(now);

    Conversation &conv = _conversations[job.from];

    {
        unique_lock<mutex> lock(_mutex);
        _stats.invocations++;
    }

    generated = generate(job.from, job.dest, job.channel, conv.turns, job.message);
    success = !generated.empty();
    {
        unique_lock<mutex> lock(_mutex);
        if (success) {
            _stats.successes++;
        } else {
            _stats.failures++;
        }
    }
#if DEBUG_CHATBOT
    cout << "chatbot: generate from=" << job.from
         << " history=" << conv.turns.size()
         << " empty=" << (success ? 0 : 1)
         << " bytes=" << generated.size() << endl;
#endif
    if (!success) {
        return;
    }

    reply = truncateToMesh(generated);
    if (reply.empty()) {
        return;
    }

    userTurn.user = true;
    userTurn.text = job.message;
    modelTurn.user = false;
    modelTurn.text = reply;
    conv.turns.push_back(userTurn);
    conv.turns.push_back(modelTurn);
    {
        size_t maxTurns;
        {
            lock_guard<mutex> lock(_mutex);
            maxTurns = _maxHistoryTurns;
        }
        while (conv.turns.size() > maxTurns) {
            conv.turns.erase(conv.turns.begin());
        }
    }
    while (!conv.turns.empty() && !conv.turns.front().user) {
        conv.turns.erase(conv.turns.begin());
    }
    conv.lastUsed = now;

    // Address the sender when replying to a user message, if within 160 character limit
    string outText = reply;
    if ((job.from != 0) && (job.from != 0xffffffffU)) {
        string shortName;
        string longName;
        char hexBuf[16];
        snprintf(hexBuf, sizeof(hexBuf), "!%08x", job.from);
        string idStr = hexBuf;

        shared_ptr<MeshClient> client = getClient();
        if (client != NULL) {
            shortName = client->lookupShortName(job.from);
            longName = client->lookupLongName(job.from);
        }

        // Check if reply already begins with addressing the sender
        bool alreadyAddressed = false;
        vector<string> nameChecks;
        if (!shortName.empty()) nameChecks.push_back(shortName);
        if (!longName.empty()) nameChecks.push_back(longName);
        nameChecks.push_back(idStr);

        for (size_t i = 0; i < nameChecks.size(); i++) {
            const string &name = nameChecks[i];
            if (reply.size() >= name.size()) {
                bool startsWithName = true;
                for (size_t c = 0; c < name.size(); c++) {
                    if (tolower(static_cast<unsigned char>(reply[c])) !=
                        tolower(static_cast<unsigned char>(name[c]))) {
                        startsWithName = false;
                        break;
                    }
                }
                if (startsWithName) {
                    if (reply.size() == name.size()) {
                        alreadyAddressed = true;
                        break;
                    }
                    char delim = reply[name.size()];
                    if (delim == ':' || delim == ',' || delim == '!' || isspace(static_cast<unsigned char>(delim))) {
                        alreadyAddressed = true;
                        break;
                    }
                }
            }
        }

        if (!alreadyAddressed) {
            string prefix;
            if (!shortName.empty() && (shortName != idStr)) {
                prefix = shortName + ": ";
            } else if (!longName.empty() && (longName != idStr)) {
                prefix = longName + ": ";
            } else {
                prefix = idStr + ": ";
            }

            if ((prefix.size() + reply.size()) <= 160) {
                outText = prefix + reply;
            }
        }
    }

    {
        ChatReply out;

        out.from = job.from;
        out.dest = job.dest;
        out.channel = job.channel;
        out.text = outText;

        unique_lock<mutex> lock(_mutex);
        _replies.push_back(out);
    }
#if DEBUG_CHATBOT
    cout << "chatbot: reply ready from=" << job.from
         << " dest=" << job.dest
         << " bytes=" << outText.size() << endl;
#endif
}

namespace {

struct ClientScopeLock {
    const SimpleClient *_c;
    ClientScopeLock(const shared_ptr<SimpleClient> &c)
        : _c(c ? c.get() : NULL)
    {
        if (_c) {
            _c->lock();
        }
    }
    ~ClientScopeLock()
    {
        if (_c) {
            _c->unlock();
        }
    }
};

} // namespace

uint8_t ChatBot::resolveChannel(const string &channelQuery, uint8_t defaultChannel) const
{
    if (channelQuery.empty()) {
        return defaultChannel;
    }

    bool allDigits = true;
    for (size_t i = 0; i < channelQuery.size(); i++) {
        if (!isdigit(static_cast<unsigned char>(channelQuery[i]))) {
            allDigits = false;
            break;
        }
    }
    if (allDigits) {
        int v = atoi(channelQuery.c_str());
        if ((v >= 0) && (v <= 7)) {
            return static_cast<uint8_t>(v);
        }
    }

    if (_client != NULL) {
        ClientScopeLock clientLock(_client);
        uint8_t ch = _client->getChannel(channelQuery);
        if (ch != 0xffU) {
            return ch;
        }
        const map<uint8_t, meshtastic_Channel> &chans = _client->channels();
        for (map<uint8_t, meshtastic_Channel>::const_iterator it = chans.begin();
             it != chans.end(); it++) {
            if (it->second.settings.name == channelQuery) {
                return it->first;
            }
        }
    }

    return defaultChannel;
}

uint32_t ChatBot::resolveNodeId(const string &nodeQuery) const
{
    if (nodeQuery.empty()) {
        return 0xffffffffU;
    }

    if ((nodeQuery == "broadcast") || (nodeQuery == "all") || (nodeQuery == "everyone")) {
        return 0xffffffffU;
    }

    if (_client != NULL) {
        if ((nodeQuery == "self") || (nodeQuery == "me") || (nodeQuery == "local")) {
            return _client->whoami();
        }
    }

    if (nodeQuery[0] == '!') {
        char *endptr = NULL;
        unsigned long v = strtoul(nodeQuery.c_str() + 1, &endptr, 16);
        if (endptr && (*endptr == '\0')) {
            return (uint32_t) v;
        }
    }

    if ((nodeQuery.size() > 2) && (nodeQuery[0] == '0') &&
        ((nodeQuery[1] == 'x') || (nodeQuery[1] == 'X'))) {
        char *endptr = NULL;
        unsigned long v = strtoul(nodeQuery.c_str() + 2, &endptr, 16);
        if (endptr && (*endptr == '\0')) {
            return (uint32_t) v;
        }
    }

    if (_client != NULL) {
        ClientScopeLock clientLock(_client);
        const map<uint32_t, meshtastic_NodeInfo> &nodes = _client->nodeInfos();
        for (map<uint32_t, meshtastic_NodeInfo>::const_iterator it = nodes.begin();
             it != nodes.end(); it++) {
            if (it->second.has_user) {
                const char *sn = it->second.user.short_name;
                if ((sn != NULL) && (strcasecmp(sn, nodeQuery.c_str()) == 0)) {
                    return it->first;
                }
            }
        }
        for (map<uint32_t, meshtastic_NodeInfo>::const_iterator it = nodes.begin();
             it != nodes.end(); it++) {
            if (it->second.has_user) {
                const char *ln = it->second.user.long_name;
                if ((ln != NULL) && (strcasecmp(ln, nodeQuery.c_str()) == 0)) {
                    return it->first;
                }
            }
        }
        uint32_t idByName = _client->getId(nodeQuery);
        if (idByName != 0xffffffffU) {
            return idByName;
        }
    }

    if (nodeQuery.size() == 8) {
        char *endptr = NULL;
        unsigned long v = strtoul(nodeQuery.c_str(), &endptr, 16);
        if (endptr && (*endptr == '\0')) {
            return (uint32_t) v;
        }
    }

    return 0xffffffffU;
}

string ChatBot::toolGetMeshNodes(void) const
{
    if (_client == NULL) {
        return "{\"error\": \"client not connected\"}";
    }

    ClientScopeLock clientLock(_client);
    stringstream ss;
    const map<uint32_t, meshtastic_NodeInfo> &nodes = _client->nodeInfos();
    uint32_t myId = _client->whoami();

    ss << "{\"total_nodes\":" << nodes.size() << ",\"nodes\":[";
    bool first = true;
    for (map<uint32_t, meshtastic_NodeInfo>::const_iterator it = nodes.begin();
         it != nodes.end(); it++) {
        if (!first) {
            ss << ",";
        }
        first = false;

        uint32_t id = it->first;
        char idBuf[16];
        snprintf(idBuf, sizeof(idBuf), "!%08x", id);

        string sName = _client->lookupShortName(id);
        string lName = _client->lookupLongName(id);

        ss << "{\"id\":\"" << idBuf << "\"";
        if (id == myId) {
            ss << ",\"is_self\":true";
        }
        if (!sName.empty()) {
            ss << ",\"short_name\":\"" << jsonEscape(sName) << "\"";
        }
        if (!lName.empty()) {
            ss << ",\"long_name\":\"" << jsonEscape(lName) << "\"";
        }
        if (it->second.has_hops_away) {
            ss << ",\"hops\":" << (unsigned int) it->second.hops_away;
        }
        if (it->second.snr != 0.0f) {
            ss << ",\"snr\":" << it->second.snr;
        }
        if (it->second.last_heard != 0) {
            time_t now = time(NULL);
            int ago = (int)(now - it->second.last_heard);
            if (ago >= 0) {
                ss << ",\"last_heard_seconds_ago\":" << ago;
            }
        }
        ss << "}";
    }
    ss << "]}";
    return ss.str();
}

string ChatBot::toolGetNodeTelemetry(const string &nodeQuery) const
{
    if (_client == NULL) {
        return "{\"error\": \"client not connected\"}";
    }

    ClientScopeLock clientLock(_client);
    uint32_t targetId = 0xffffffffU;
    if (!nodeQuery.empty()) {
        targetId = resolveNodeId(nodeQuery);
        if (targetId == 0xffffffffU) {
            return "{\"error\": \"node not found for query: " + jsonEscape(nodeQuery) + "\"}";
        }
    }

    stringstream ss;
    ss << "{\"telemetry\":[";
    bool first = true;

    const map<uint32_t, meshtastic_NodeInfo> &nodes = _client->nodeInfos();
    for (map<uint32_t, meshtastic_NodeInfo>::const_iterator it = nodes.begin();
         it != nodes.end(); it++) {
        uint32_t id = it->first;
        if ((targetId != 0xffffffffU) && (id != targetId)) {
            continue;
        }

        map<uint32_t, meshtastic_DeviceMetrics>::const_iterator dIt =
            _client->deviceMetrics().find(id);
        map<uint32_t, meshtastic_EnvironmentMetrics>::const_iterator eIt =
            _client->environmentMetrics().find(id);

        const meshtastic_DeviceMetrics *dm = NULL;
        if (dIt != _client->deviceMetrics().end()) {
            dm = &dIt->second;
        } else if (it->second.has_device_metrics) {
            dm = &it->second.device_metrics;
        }

        if ((dm == NULL) &&
            (eIt == _client->environmentMetrics().end()) &&
            (targetId == 0xffffffffU)) {
            continue;
        }

        if (!first) {
            ss << ",";
        }
        first = false;

        char idBuf[16];
        snprintf(idBuf, sizeof(idBuf), "!%08x", id);
        ss << "{\"id\":\"" << idBuf << "\"";
        string sName = _client->lookupShortName(id);
        if (!sName.empty()) {
            ss << ",\"name\":\"" << jsonEscape(sName) << "\"";
        }

        if (dm != NULL) {
            if (dm->has_battery_level) {
                ss << ",\"battery_level\":" << dm->battery_level;
            }
            if (dm->has_voltage) {
                ss << ",\"voltage\":" << dm->voltage;
            }
            if (dm->has_channel_utilization) {
                ss << ",\"channel_utilization\":" << dm->channel_utilization;
            }
            if (dm->has_air_util_tx) {
                ss << ",\"air_util_tx\":" << dm->air_util_tx;
            }
            if (dm->has_uptime_seconds) {
                ss << ",\"uptime_seconds\":" << dm->uptime_seconds;
            }
        }

        if (eIt != _client->environmentMetrics().end()) {
            if (eIt->second.has_temperature) {
                ss << ",\"temperature_c\":" << eIt->second.temperature;
            }
            if (eIt->second.has_relative_humidity) {
                ss << ",\"humidity\":" << eIt->second.relative_humidity;
            }
            if (eIt->second.has_barometric_pressure) {
                ss << ",\"pressure_hpa\":" << eIt->second.barometric_pressure;
            }
            if (eIt->second.has_iaq) {
                ss << ",\"iaq\":" << eIt->second.iaq;
            }
        }
        ss << "}";
    }
    ss << "]}";
    return ss.str();
}

string ChatBot::toolGetNetworkStats(void) const
{
    if (_client == NULL) {
        return "{\"error\": \"client not connected\"}";
    }

    ClientScopeLock clientLock(_client);
    stringstream ss;
    ss << "{\"mesh_stats\":{";
    ss << "\"packets_rx\":" << _client->meshDevicePacketsReceived() << ",";
    ss << "\"packets_tx\":" << _client->meshDevicePacketsSent() << ",";
    ss << "\"bytes_rx\":" << _client->meshDeviceBytesReceived() << ",";
    ss << "\"bytes_tx\":" << _client->meshDeviceBytesSent() << ",";
    ss << "\"direct_messages_rx\":" << _client->dmRx() << ",";
    ss << "\"direct_messages_tx\":" << _client->dmTx() << ",";
    ss << "\"channel_messages_rx\":" << _client->cmRx() << ",";
    ss << "\"channel_messages_tx\":" << _client->cmTx() << ",";
    ss << "\"total_nodes_known\":" << _client->nodeInfos().size();
    ss << "},";

    ss << "\"lora_config\":{";
    const meshtastic_Config_LoRaConfig &lora = _client->loraConfig();
    ss << "\"hop_limit\":" << (unsigned int) lora.hop_limit << ",";
    ss << "\"tx_power\":" << (int) lora.tx_power << ",";
    ss << "\"region\":" << (int) lora.region << ",";
    ss << "\"modem_preset\":" << (int) lora.modem_preset;
    ss << "},";

    ss << "\"channels\":[";
    bool first = true;
    for (map<uint8_t, meshtastic_Channel>::const_iterator it = _client->channels().begin();
         it != _client->channels().end(); it++) {
        if (it->second.role == meshtastic_Channel_Role_DISABLED) {
            continue;
        }
        if (!first) {
            ss << ",";
        }
        first = false;
        ss << "{\"index\":" << (unsigned int) it->first;
        if (it->second.settings.name[0] != '\0') {
            ss << ",\"name\":\"" << jsonEscape(it->second.settings.name) << "\"";
        }
        ss << ",\"role\":" << (int) it->second.role << "}";
    }
    ss << "]}";

    return ss.str();
}

string ChatBot::toolGetNodePositions(const string &nodeQuery) const
{
    if (_client == NULL) {
        return "{\"error\": \"client not connected\"}";
    }

    ClientScopeLock clientLock(_client);
    uint32_t targetId = 0xffffffffU;
    if (!nodeQuery.empty()) {
        targetId = resolveNodeId(nodeQuery);
        if (targetId == 0xffffffffU) {
            return "{\"error\": \"node not found for query: " + jsonEscape(nodeQuery) + "\"}";
        }
    }

    set<uint32_t> allIds;
    if (targetId != 0xffffffffU) {
        allIds.insert(targetId);
    } else {
        const map<uint32_t, meshtastic_NodeInfo> &nodes = _client->nodeInfos();
        for (map<uint32_t, meshtastic_NodeInfo>::const_iterator it = nodes.begin();
             it != nodes.end(); it++) {
            allIds.insert(it->first);
        }
        const map<uint32_t, meshtastic_Position> &posMap = _client->positions();
        for (map<uint32_t, meshtastic_Position>::const_iterator it = posMap.begin();
             it != posMap.end(); it++) {
            allIds.insert(it->first);
        }
    }

    stringstream ss;
    ss << "{\"positions\":[";
    bool first = true;

    const map<uint32_t, meshtastic_Position> &pMap = _client->positions();
    const map<uint32_t, meshtastic_NodeInfo> &nMap = _client->nodeInfos();

    for (set<uint32_t>::const_iterator it = allIds.begin(); it != allIds.end(); it++) {
        uint32_t id = *it;
        const meshtastic_Position *pos = NULL;

        map<uint32_t, meshtastic_Position>::const_iterator pIt = pMap.find(id);
        if (pIt != pMap.end() && (pIt->second.latitude_i != 0 || pIt->second.longitude_i != 0)) {
            pos = &pIt->second;
        } else {
            map<uint32_t, meshtastic_NodeInfo>::const_iterator nIt = nMap.find(id);
            if (nIt != nMap.end() && nIt->second.has_position &&
                (nIt->second.position.latitude_i != 0 || nIt->second.position.longitude_i != 0)) {
                pos = &nIt->second.position;
            }
        }

        if (pos == NULL) {
            continue;
        }

        if (!first) {
            ss << ",";
        }
        first = false;

        char idBuf[16];
        snprintf(idBuf, sizeof(idBuf), "!%08x", id);
        double lat = pos->latitude_i * 1e-7;
        double lon = pos->longitude_i * 1e-7;

        ss << "{\"id\":\"" << idBuf << "\"";
        string sName = _client->lookupShortName(id);
        if (!sName.empty()) {
            ss << ",\"name\":\"" << jsonEscape(sName) << "\"";
        }
        ss << ",\"latitude\":" << lat;
        ss << ",\"longitude\":" << lon;
        if (pos->altitude != 0) {
            ss << ",\"altitude_m\":" << pos->altitude;
        }
        ss << "}";
    }
    ss << "]}";

    return ss.str();
}

string ChatBot::toolScheduleTask(uint32_t from, uint32_t dest, uint8_t channel, const string &argsJson)
{
    ScheduledTask task;
    task.from = from;
    task.dest = dest;
    task.channel = channel;
    task.type = TASK_MESSAGE;
    task.maxRepeats = 1;
    task.executionCount = 0;
    task.createdAt = time(NULL);

    string actionType;
    if (parseJsonStringField(argsJson, "action_type", actionType) ||
        parseJsonStringField(argsJson, "type", actionType)) {
        if (actionType == "prompt") {
            task.type = TASK_PROMPT;
        } else {
            task.type = TASK_MESSAGE;
        }
    }

    string content;
    if (!parseJsonStringField(argsJson, "content", content)) {
        if (!parseJsonStringField(argsJson, "message", content)) {
            if (!parseJsonStringField(argsJson, "prompt", content)) {
                parseJsonStringField(argsJson, "text", content);
            }
        }
    }
    if (content.empty()) {
        return "{\"error\": \"Missing 'content' or 'message' parameter for scheduled task.\"}";
    }
    task.content = content;

    // Channel override
    string chStr;
    if (parseJsonStringField(argsJson, "channel", chStr) ||
        parseJsonStringField(argsJson, "target_channel", chStr)) {
        task.channel = resolveChannel(chStr, channel);
        task.channelName = chStr;
        task.dest = 0xffffffffU; // Broadcast when explicit channel is specified
    } else {
        int64_t chInt = 0;
        if (parseJsonIntField(argsJson, "channel", chInt) ||
            parseJsonIntField(argsJson, "target_channel", chInt)) {
            if ((chInt >= 0) && (chInt <= 7)) {
                task.channel = static_cast<uint8_t>(chInt);
                task.dest = 0xffffffffU;
            }
        }
    }

    // Target node override
    string nodeStr;
    if (parseJsonStringField(argsJson, "target_node", nodeStr) ||
        parseJsonStringField(argsJson, "target", nodeStr) ||
        parseJsonStringField(argsJson, "node", nodeStr)) {
        task.dest = resolveNodeId(nodeStr);
    }

    time_t now = time(NULL);
    time_t targetDue = 0;

    string cronStr;
    if (parseJsonStringField(argsJson, "cron", cronStr) ||
        parseJsonStringField(argsJson, "cron_expr", cronStr) ||
        parseJsonStringField(argsJson, "schedule", cronStr)) {
        time_t nextCron = 0;
        if (!computeNextCronOccurrence(cronStr, now, nextCron)) {
            return "{\"error\": \"Invalid cron expression: " + jsonEscape(cronStr) + "\"}";
        }
        task.cronExpr = cronStr;
        targetDue = nextCron;
        task.maxRepeats = -1; // Default recurring for cron
    } else {
        int64_t delaySec = 0;
        bool haveDelay = false;

        if (parseJsonIntField(argsJson, "delay_seconds", delaySec) ||
            parseJsonIntField(argsJson, "seconds", delaySec) ||
            parseJsonIntField(argsJson, "delay", delaySec)) {
            haveDelay = true;
        } else {
            int64_t delayMin = 0;
            if (parseJsonIntField(argsJson, "delay_minutes", delayMin) ||
                parseJsonIntField(argsJson, "minutes", delayMin)) {
                delaySec = delayMin * 60;
                haveDelay = true;
            }
        }

        if (haveDelay) {
            if (delaySec <= 0) {
                return "{\"error\": \"delay_seconds must be positive.\"}";
            }
            targetDue = now + delaySec;
        } else {
            string atTimeStr;
            if (parseJsonStringField(argsJson, "at_time", atTimeStr) ||
                parseJsonStringField(argsJson, "time", atTimeStr) ||
                parseJsonStringField(argsJson, "timestamp", atTimeStr)) {
                if (!parseTimeString(atTimeStr, targetDue)) {
                    return "{\"error\": \"Invalid at_time format: " + jsonEscape(atTimeStr) + "\"}";
                }
                if (targetDue <= now) {
                    return "{\"error\": \"at_time is in the past.\"}";
                }
            }
        }
    }

    if (targetDue == 0) {
        return "{\"error\": \"Must specify one of delay_seconds, at_time, or cron.\"}";
    }

    int64_t maxRep = 1;
    if (parseJsonIntField(argsJson, "max_repeats", maxRep) ||
        parseJsonIntField(argsJson, "repeats", maxRep)) {
        task.maxRepeats = static_cast<int>(maxRep);
    }

    int64_t intervalSec = 0;
    if (parseJsonIntField(argsJson, "repeat_interval_seconds", intervalSec) ||
        parseJsonIntField(argsJson, "interval", intervalSec)) {
        task.repeatIntervalSec = intervalSec;
        if (task.maxRepeats == 1) {
            task.maxRepeats = -1;
        }
    }

    task.dueTime = targetDue;

    uint32_t taskId = scheduleTask(task);

    struct tm tm;
    char timeBuf[64];
    localtime_r(&targetDue, &tm);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S %Z", &tm);

    stringstream ss;
    ss << "{\"status\":\"scheduled\","
       << "\"task_id\":" << taskId << ","
       << "\"type\":\"" << ((task.type == TASK_PROMPT) ? "prompt" : "message") << "\","
       << "\"channel\":" << (unsigned int) task.channel << ",";
    if (!task.channelName.empty()) {
        ss << "\"channel_name\":\"" << jsonEscape(task.channelName) << "\",";
    }
    char destBuf[16];
    snprintf(destBuf, sizeof(destBuf), "!%08x", task.dest);
    ss << "\"dest\":\"" << destBuf << "\","
       << "\"due_time\":\"" << timeBuf << "\","
       << "\"remaining_seconds\":" << (targetDue - now) << ",";
    if (!task.cronExpr.empty()) {
        ss << "\"cron\":\"" << jsonEscape(task.cronExpr) << "\",";
    }
    ss << "\"content\":\"" << jsonEscape(task.content) << "\"}";

    return ss.str();
}

string ChatBot::toolListTasks(uint32_t from) const
{
    vector<ScheduledTask> tasks = getTasks(from);
    time_t now = time(NULL);

    stringstream ss;
    ss << "{\"total_tasks\":" << tasks.size() << ",\"tasks\":[";
    for (size_t i = 0; i < tasks.size(); i++) {
        if (i > 0) {
            ss << ",";
        }
        const ScheduledTask &t = tasks[i];
        struct tm tm;
        char timeBuf[64];
        localtime_r(&t.dueTime, &tm);
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S %Z", &tm);

        char fromBuf[16], destBuf[16];
        snprintf(fromBuf, sizeof(fromBuf), "!%08x", t.from);
        snprintf(destBuf, sizeof(destBuf), "!%08x", t.dest);

        ss << "{\"id\":" << t.id
           << ",\"type\":\"" << ((t.type == TASK_PROMPT) ? "prompt" : "message") << "\""
           << ",\"from\":\"" << fromBuf << "\""
           << ",\"dest\":\"" << destBuf << "\""
           << ",\"channel\":" << (unsigned int) t.channel;
        if (!t.channelName.empty()) {
            ss << ",\"channel_name\":\"" << jsonEscape(t.channelName) << "\"";
        }
        ss << ",\"due_time\":\"" << timeBuf << "\""
           << ",\"remaining_seconds\":" << ((t.dueTime > now) ? (t.dueTime - now) : 0);
        if (!t.cronExpr.empty()) {
            ss << ",\"cron\":\"" << jsonEscape(t.cronExpr) << "\"";
        }
        if (t.repeatIntervalSec > 0) {
            ss << ",\"interval_seconds\":" << t.repeatIntervalSec;
        }
        ss << ",\"execution_count\":" << t.executionCount
           << ",\"max_repeats\":" << t.maxRepeats
           << ",\"content\":\"" << jsonEscape(t.content) << "\"}";
    }
    ss << "]}";

    return ss.str();
}

string ChatBot::toolCancelTask(uint32_t from, const string &argsJson)
{
    (void) from;
    bool cancelAll = false;
    parseJsonBoolField(argsJson, "all", cancelAll);

    if (cancelAll) {
        int count = 0;
        {
            unique_lock<mutex> lock(_mutex);
            count = static_cast<int>(_tasks.size());
            _tasks.clear();
        }
        if (count > 0) {
            saveTasksToConfig();
            _cv.notify_one();
        }
        stringstream ss;
        ss << "{\"status\":\"ok\",\"cancelled_count\":" << count << "}";
        return ss.str();
    }

    int64_t id = 0;
    if (parseJsonIntField(argsJson, "id", id) ||
        parseJsonIntField(argsJson, "task_id", id)) {
        bool ok = cancelTask(static_cast<uint32_t>(id), 0);
        stringstream ss;
        ss << "{\"status\":\"" << (ok ? "ok" : "not_found") << "\",\"cancelled_count\":" << (ok ? 1 : 0) << "}";
        return ss.str();
    }

    return "{\"error\": \"Must specify 'id' or 'all': true to cancel.\"}";
}

string ChatBot::executeTool(const string &name, const string &argsJson,
                            uint32_t from, uint32_t dest, uint8_t channel)
{
    if (name == "get_mesh_nodes") {
        return toolGetMeshNodes();
    } else if (name == "get_node_telemetry") {
        string node;
        parseJsonStringField(argsJson, "node", node);
        return toolGetNodeTelemetry(node);
    } else if (name == "get_network_stats") {
        return toolGetNetworkStats();
    } else if (name == "get_node_positions") {
        string node;
        parseJsonStringField(argsJson, "node", node);
        return toolGetNodePositions(node);
    } else if (name == "schedule_task") {
        return toolScheduleTask(from, dest, channel, argsJson);
    } else if (name == "list_scheduled_tasks") {
        return toolListTasks(0);
    } else if (name == "cancel_scheduled_task") {
        return toolCancelTask(from, argsJson);
    }
    return "{\"error\": \"unknown tool: " + name + "\"}";
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
