/*
 * ChatBot.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <time.h>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <iomanip>
#include <ChatBot.hxx>

#ifndef DEBUG_CHATBOT
#define DEBUG_CHATBOT 0
#endif

#define CHAT_HISTORY_MAX    20
#define CHAT_IDLE_SECONDS   3600
#define CHAT_MAX_BYTES      200

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

ChatBot::ChatBot(shared_ptr<MeshClient> client)
    : _client(client),
      _thread(NULL),
      _isRunning(false)
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

bool ChatBot::enabled(void) const
{
    return true;
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

void ChatBot::run(void)
{
    while (_isRunning.load()) {
        ChatJob job;
        bool haveJob = false;

        {
            unique_lock<mutex> lock(_mutex);
            while (_isRunning.load() && _queue.empty()) {
                _cv.wait(lock);
            }
            if (!_queue.empty()) {
                job = _queue.front();
                _queue.pop_front();
                haveJob = true;
            }
        }

        if (haveJob) {
            processJob(job);
        }
    }
}

void ChatBot::expireIdle(time_t now)
{
    map<uint32_t, Conversation>::iterator it;

    it = _conversations.begin();
    while (it != _conversations.end()) {
        if ((now - it->second.lastUsed) >= (time_t) CHAT_IDLE_SECONDS) {
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

    generated = generate(job.from, conv.turns, job.message);
    success = !generated.empty();
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
    while (conv.turns.size() > CHAT_HISTORY_MAX) {
        conv.turns.erase(conv.turns.begin());
    }
    while (!conv.turns.empty() && !conv.turns.front().user) {
        conv.turns.erase(conv.turns.begin());
    }
    conv.lastUsed = now;

    {
        ChatReply out;

        out.from = job.from;
        out.dest = job.dest;
        out.channel = job.channel;
        out.text = reply;

        unique_lock<mutex> lock(_mutex);
        _replies.push_back(out);
    }
#if DEBUG_CHATBOT
    cout << "chatbot: reply ready from=" << job.from
         << " dest=" << job.dest
         << " bytes=" << reply.size() << endl;
#endif
}

uint32_t ChatBot::resolveNodeId(const string &nodeQuery) const
{
    if (nodeQuery.empty() || (_client == NULL)) {
        return 0xffffffffU;
    }

    if ((nodeQuery == "self") || (nodeQuery == "me") || (nodeQuery == "local")) {
        return _client->whoami();
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

    uint32_t idByName = _client->getId(nodeQuery);
    if (idByName != 0xffffffffU) {
        return idByName;
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

        if ((dIt == _client->deviceMetrics().end()) &&
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

        if (dIt != _client->deviceMetrics().end()) {
            if (dIt->second.has_battery_level) {
                ss << ",\"battery_level\":" << dIt->second.battery_level;
            }
            if (dIt->second.has_voltage) {
                ss << ",\"voltage\":" << dIt->second.voltage;
            }
            if (dIt->second.has_channel_utilization) {
                ss << ",\"channel_utilization\":" << dIt->second.channel_utilization;
            }
            if (dIt->second.has_air_util_tx) {
                ss << ",\"air_util_tx\":" << dIt->second.air_util_tx;
            }
            if (dIt->second.has_uptime_seconds) {
                ss << ",\"uptime_seconds\":" << dIt->second.uptime_seconds;
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

    uint32_t targetId = 0xffffffffU;
    if (!nodeQuery.empty()) {
        targetId = resolveNodeId(nodeQuery);
        if (targetId == 0xffffffffU) {
            return "{\"error\": \"node not found for query: " + jsonEscape(nodeQuery) + "\"}";
        }
    }

    stringstream ss;
    ss << "{\"positions\":[";
    bool first = true;

    for (map<uint32_t, meshtastic_Position>::const_iterator it = _client->positions().begin();
         it != _client->positions().end(); it++) {
        uint32_t id = it->first;
        if ((targetId != 0xffffffffU) && (id != targetId)) {
            continue;
        }
        if ((it->second.latitude_i == 0) && (it->second.longitude_i == 0)) {
            continue;
        }

        if (!first) {
            ss << ",";
        }
        first = false;

        char idBuf[16];
        snprintf(idBuf, sizeof(idBuf), "!%08x", id);
        double lat = it->second.latitude_i * 1e-7;
        double lon = it->second.longitude_i * 1e-7;

        ss << "{\"id\":\"" << idBuf << "\"";
        string sName = _client->lookupShortName(id);
        if (!sName.empty()) {
            ss << ",\"name\":\"" << jsonEscape(sName) << "\"";
        }
        ss << ",\"latitude\":" << lat;
        ss << ",\"longitude\":" << lon;
        if (it->second.altitude != 0) {
            ss << ",\"altitude_m\":" << it->second.altitude;
        }
        ss << "}";
    }
    ss << "]}";

    return ss.str();
}

string ChatBot::executeTool(const string &name, const string &argsJson) const
{
    if (name == "get_mesh_nodes") {
        return toolGetMeshNodes();
    } else if (name == "get_node_telemetry") {
        string node;
        size_t p = argsJson.find("\"node\"");
        if (p != string::npos) {
            size_t c = argsJson.find(":", p);
            if (c != string::npos) {
                size_t q1 = argsJson.find("\"", c);
                if (q1 != string::npos) {
                    size_t q2 = argsJson.find("\"", q1 + 1);
                    if (q2 != string::npos) {
                        node = argsJson.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
            }
        }
        return toolGetNodeTelemetry(node);
    } else if (name == "get_network_stats") {
        return toolGetNetworkStats();
    } else if (name == "get_node_positions") {
        string node;
        size_t p = argsJson.find("\"node\"");
        if (p != string::npos) {
            size_t c = argsJson.find(":", p);
            if (c != string::npos) {
                size_t q1 = argsJson.find("\"", c);
                if (q1 != string::npos) {
                    size_t q2 = argsJson.find("\"", q1 + 1);
                    if (q2 != string::npos) {
                        node = argsJson.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
            }
        }
        return toolGetNodePositions(node);
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
