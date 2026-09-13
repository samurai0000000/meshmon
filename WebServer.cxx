/*
 * WebServer.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include "WebServer.hxx"
#include "WebAssets.hxx"
#include "MeshMon.hxx"
#include "MeshMonDb.hxx"
#include "MeshMonShell.hxx"
#include "version.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <random>
#include <algorithm>
#include <chrono>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wshadow"
#include <httplib.h>
#pragma GCC diagnostic pop

#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;
namespace fs = std::filesystem;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double haversineMeters(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371000.0;
    double phi1 = lat1 * M_PI / 180.0;
    double phi2 = lat2 * M_PI / 180.0;
    double dphi = (lat2 - lat1) * M_PI / 180.0;
    double dlam = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dphi / 2.0) * sin(dphi / 2.0) +
               cos(phi1) * cos(phi2) * sin(dlam / 2.0) * sin(dlam / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return R * c;
}

static string formatNodeHex(uint32_t nodeId)
{
    if (nodeId == 0) return "-";
    char buf[32];
    snprintf(buf, sizeof(buf), "!%08x", nodeId);
    return string(buf);
}

static string generateRandomToken(size_t length = 32)
{
    static const char hexChars[] = "0123456789abcdef";
    static thread_local random_device rd;
    static thread_local mt19937 gen(rd());
    static thread_local uniform_int_distribution<> dis(0, 15);
    string token;
    token.reserve(length);
    for (size_t i = 0; i < length; i++) {
        token += hexChars[dis(gen)];
    }
    return token;
}

WebServer::WebServer(shared_ptr<MeshMon> mon,
                     shared_ptr<MeshMonDb> db,
                     const WebConfig &config)
    : _mon(mon),
      _db(db),
      _config(config),
      _server(make_unique<httplib::Server>())
{
    _startTime = time(NULL);
}

WebServer::~WebServer()
{
    stop();
}

bool WebServer::start(bool async)
{
    if (_running) {
        return true;
    }

    setupRoutes();

    _running = true;
    cout << "[WebServer] Initializing embedded HTTP server on "
         << _config.host << ":" << _config.port << endl;

    if (async) {
        _thread = make_unique<thread>([this]() {
            if (!_server->listen(_config.host.c_str(), _config.port)) {
                cerr << "[WebServer] Error: Failed to bind or listen on "
                     << _config.host << ":" << _config.port << endl;
                _running = false;
            }
        });
        this_thread::sleep_for(chrono::milliseconds(50));
        return _running;
    } else {
        return _server->listen(_config.host.c_str(), _config.port);
    }
}

void WebServer::stop(void)
{
    if (!_running) {
        return;
    }

    _running = false;

    // Terminate all SSE sessions cleanly
    {
        lock_guard<mutex> lock(_sseMutex);
        for (auto &pair : _sseSessions) {
            if (pair.second) {
                pair.second->closed.store(true);
                pair.second->cv.notify_all();
            }
        }
        _sseSessions.clear();
    }

    if (_server) {
        _server->stop();
    }

    if (_thread && _thread->joinable()) {
        _thread->join();
        _thread.reset();
    }

    cout << "[WebServer] Stopped" << endl;
}

bool WebServer::isRunning(void) const
{
    return _running.load();
}

void WebServer::serveStaticFileOrFallback(const string &diskPath,
                                         const char *fallbackAsset,
                                         const string &contentType,
                                         httplib::Response &res)
{
    // Try serving directly from local disk first (for live development / hot editing)
    if (fs::exists(diskPath)) {
        ifstream f(diskPath);
        if (f.is_open()) {
            string content((istreambuf_iterator<char>(f)),
                           istreambuf_iterator<char>());
            res.set_content(content, contentType.c_str());
            return;
        }
    }

    // Fall back to compiled in-memory asset
    res.set_content(fallbackAsset, contentType.c_str());
}

string WebServer::createSessionToken(void)
{
    lock_guard<mutex> lock(_authMutex);
    purgeExpiredTokens();

    AuthSession s;
    s.token = generateRandomToken(32);
    s.createdAt = time(NULL);
    s.expiresAt = s.createdAt + 86400; // 24-hour token validity
    _authSessions[s.token] = s;
    return s.token;
}

bool WebServer::validateSessionToken(const string &token) const
{
    if (token.empty()) {
        return false;
    }

    lock_guard<mutex> lock(_authMutex);
    auto it = _authSessions.find(token);
    if (it == _authSessions.end()) {
        return false;
    }

    if (it->second.expiresAt < time(NULL)) {
        return false;
    }

    return true;
}

void WebServer::revokeSessionToken(const string &token)
{
    lock_guard<mutex> lock(_authMutex);
    _authSessions.erase(token);
}

void WebServer::purgeExpiredTokens(void)
{
    time_t now = time(NULL);
    for (auto it = _authSessions.begin(); it != _authSessions.end(); ) {
        if (it->second.expiresAt < now) {
            it = _authSessions.erase(it);
        } else {
            ++it;
        }
    }
}

string WebServer::extractToken(const httplib::Request &req) const
{
    // 1. Check Authorization header: Bearer <token>
    if (req.has_header("Authorization")) {
        string auth = req.get_header_value("Authorization");
        if (auth.rfind("Bearer ", 0) == 0) {
            return auth.substr(7);
        }
    }

    // 2. Check Cookie header: meshmon_auth=<token>
    if (req.has_header("Cookie")) {
        string cookie = req.get_header_value("Cookie");
        size_t pos = cookie.find("meshmon_auth=");
        if (pos != string::npos) {
            size_t start = pos + 13;
            size_t end = cookie.find(';', start);
            if (end == string::npos) {
                return cookie.substr(start);
            } else {
                return cookie.substr(start, end - start);
            }
        }
    }

    // 3. Check X-Auth-Token header
    if (req.has_header("X-Auth-Token")) {
        return req.get_header_value("X-Auth-Token");
    }

    // 4. Check query param ?token=
    if (req.has_param("token")) {
        return req.get_param_value("token");
    }

    return "";
}

bool WebServer::checkAuth(const httplib::Request &req) const
{
    if (_config.password.empty()) {
        return true; // No password configured; open mode
    }

    string token = extractToken(req);
    return validateSessionToken(token);
}

void WebServer::setupRoutes(void)
{
    // Enable CORS for API routes
    _server->set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type, Authorization, X-Auth-Token"}
    });

    _server->Options(".*", [](const httplib::Request&, httplib::Response &res) {
        res.status = 204;
    });

    // Static Web Dashboard Assets
    _server->Get("/", [this](const httplib::Request&, httplib::Response &res) {
        serveStaticFileOrFallback("web/index.html", assets::INDEX_HTML, "text/html", res);
    });

    _server->Get("/style.css", [this](const httplib::Request&, httplib::Response &res) {
        serveStaticFileOrFallback("web/style.css", assets::STYLE_CSS, "text/css", res);
    });

    _server->Get("/app.js", [this](const httplib::Request&, httplib::Response &res) {
        serveStaticFileOrFallback("web/app.js", assets::APP_JS, "application/javascript", res);
    });

    // Public REST Endpoints
    _server->Get("/api/status", [this](const httplib::Request &req, httplib::Response &res) {
        handleGetStatus(req, res);
    });

    _server->Get("/api/nodes", [this](const httplib::Request &req, httplib::Response &res) {
        handleGetNodes(req, res);
    });

    _server->Get("/api/node", [this](const httplib::Request &req, httplib::Response &res) {
        handleGetNodeDetail(req, res);
    });

    _server->Get("/api/packets", [this](const httplib::Request &req, httplib::Response &res) {
        handleGetPackets(req, res);
    });

    _server->Get("/api/analytics", [this](const httplib::Request &req, httplib::Response &res) {
        handleGetAnalytics(req, res);
    });

    _server->Get("/api/automation", [this](const httplib::Request &req, httplib::Response &res) {
        handleGetAutomation(req, res);
    });

    _server->Get("/api/messages", [this](const httplib::Request &req, httplib::Response &res) {
        handleGetMessages(req, res);
    });

    _server->Get("/api/spatial", [this](const httplib::Request &req, httplib::Response &res) {
        handleGetSpatial(req, res);
    });

    // Authentication Endpoints
    _server->Post("/api/auth/login", [this](const httplib::Request &req, httplib::Response &res) {
        handlePostAuthLogin(req, res);
    });

    _server->Get("/api/auth/status", [this](const httplib::Request &req, httplib::Response &res) {
        handleGetAuthStatus(req, res);
    });

    _server->Post("/api/auth/logout", [this](const httplib::Request &req, httplib::Response &res) {
        handlePostAuthLogout(req, res);
    });

    // Authenticated Mutating Endpoints
    _server->Post("/api/messages/send", [this](const httplib::Request &req, httplib::Response &res) {
        if (!checkAuth(req)) {
            res.status = 401;
            res.set_content("{\"error\":\"Authentication required to transmit radio messages\"}", "application/json");
            return;
        }
        handlePostSendMessage(req, res);
    });

    _server->Post("/api/automation/command", [this](const httplib::Request &req, httplib::Response &res) {
        if (!checkAuth(req)) {
            res.status = 401;
            res.set_content("{\"error\":\"Authentication required to send HomeMesh automation commands\"}", "application/json");
            return;
        }
        handlePostAutomationCommand(req, res);
    });

    _server->Post("/api/db/query", [this](const httplib::Request &req, httplib::Response &res) {
        if (!checkAuth(req)) {
            res.status = 401;
            res.set_content("{\"error\":\"Authentication required to execute database queries\"}", "application/json");
            return;
        }
        handlePostDbQuery(req, res);
    });

    // Server-Sent Events (SSE) live event stream
    _server->Get("/api/events", [this](const httplib::Request&, httplib::Response &res) {
        string sessionId = generateRandomToken(16);
        auto session = make_shared<SseSession>();
        session->id = sessionId;

        {
            lock_guard<mutex> lock(_sseMutex);
            _sseSessions[sessionId] = session;
        }

        auto provider = [this, session](size_t offset, httplib::DataSink &sink) -> bool {
            if (offset == 0) {
                string init = ": connected\n\n";
                if (!sink.write(init.data(), init.size())) {
                    return false;
                }
            }

            unique_lock<mutex> lock(session->mutex);
            session->cv.wait_for(lock, chrono::seconds(15), [&]() {
                return !session->messageQueue.empty() || session->closed.load() || !_running.load();
            });

            if (session->closed.load() || !_running.load()) {
                return false;
            }

            if (session->messageQueue.empty()) {
                string ping = ": keepalive\n\n";
                return sink.write(ping.data(), ping.size());
            }

            string msg = session->messageQueue.front();
            session->messageQueue.pop();
            lock.unlock();

            return sink.write(msg.data(), msg.size());
        };

        auto releaser = [this, sessionId, session](bool) {
            session->closed.store(true);
            session->cv.notify_all();
            lock_guard<mutex> lock(_sseMutex);
            _sseSessions.erase(sessionId);
        };

        res.set_chunked_content_provider("text/event-stream", provider, releaser);
    });
}

void WebServer::onPacketReceived(const meshtastic_MeshPacket &packet, time_t meshmonTime)
{
    PacketLogEntry entry;
    entry.id = _nextPacketId++;
    entry.meshmonTime = meshmonTime;
    entry.fromNode = packet.from;
    entry.fromHex = formatNodeHex(packet.from);
    entry.toNode = packet.to;
    entry.toHex = (packet.to == 0xffffffffU) ? "^all" : formatNodeHex(packet.to);
    entry.channel = packet.channel;
    entry.rxRssi = packet.rx_rssi;
    entry.rxSnr = packet.rx_snr;
    entry.hopStart = packet.hop_start;
    entry.hopLimit = packet.hop_limit;
    entry.hops = (packet.hop_start >= packet.hop_limit) ? (packet.hop_start - packet.hop_limit) : 0;
    entry.viaMqtt = packet.via_mqtt;

    if (_mon) {
        entry.fromName = _mon->lookupShortName(packet.from);
        if (packet.to != 0xffffffffU) {
            entry.toName = _mon->lookupShortName(packet.to);
        }
    }

    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        entry.portnum = packet.decoded.portnum;
        entry.appName = MeshMonDb::portnumToString(packet.decoded.portnum);
        entry.payloadSize = packet.decoded.payload.size;

        if (packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP &&
            packet.decoded.payload.size > 0) {
            string text((const char *) packet.decoded.payload.bytes, packet.decoded.payload.size);
            entry.text = text;
        }
    } else {
        entry.appName = "ENCRYPTED";
        entry.payloadSize = packet.encrypted.size;
    }

    // Add to in-memory ring buffer
    {
        lock_guard<mutex> lock(_packetRingMutex);
        _recentPackets.push_back(entry);
        while (_recentPackets.size() > _maxRecentPackets) {
            _recentPackets.pop_front();
        }
    }

    // Broadcast SSE packet event
    json j = {
        {"id", entry.id},
        {"time", entry.meshmonTime},
        {"from_hex", entry.fromHex},
        {"from_name", entry.fromName},
        {"to_hex", entry.toHex},
        {"to_name", entry.toName},
        {"channel", entry.channel},
        {"rssi", entry.rxRssi},
        {"snr", entry.rxSnr},
        {"hops", entry.hops},
        {"portnum", entry.portnum},
        {"app", entry.appName},
        {"payload_size", entry.payloadSize},
        {"text", entry.text},
        {"via_mqtt", entry.viaMqtt}
    };

    broadcastSseEvent("packet", j.dump());
}

void WebServer::broadcastSseEvent(const string &eventType, const string &jsonData)
{
    string msg = "event: " + eventType + "\ndata: " + jsonData + "\n\n";

    lock_guard<mutex> lock(_sseMutex);
    for (auto &pair : _sseSessions) {
        auto &session = pair.second;
        if (session && !session->closed.load()) {
            {
                lock_guard<mutex> sLock(session->mutex);
                session->messageQueue.push(msg);
            }
            session->cv.notify_one();
        }
    }
}

// ----------------------------------------------------------------------------
// JSON API Handlers
// ----------------------------------------------------------------------------

void WebServer::handleGetStatus(const httplib::Request&, httplib::Response &res)
{
    json j;

    uint32_t myNodeId = 0;
    string myHex = "-";
    string shortName = "-";
    string longName = "-";
    string fwVer = "";
    bool connected = false;

    if (_mon) {
        myNodeId = _mon->whoami();
        if (myNodeId != 0) {
            myHex = formatNodeHex(myNodeId);
            shortName = _mon->lookupShortName(myNodeId);
            longName = _mon->lookupLongName(myNodeId);
            connected = true;
        }
        fwVer = _mon->firmwareVersion();
    }

    j["radio"] = {
        {"whoami", myNodeId},
        {"whoami_hex", myHex},
        {"short_name", shortName},
        {"long_name", longName},
        {"firmware_version", fwVer},
        {"connected", connected}
    };

    float cpuTemp = (_mon != nullptr) ? _mon->getCpuTempC() : 0.0f;
    time_t now = time(NULL);
    time_t uptime = now - _startTime;

    j["host"] = {
        {"cpu_temp_c", cpuTemp},
        {"uptime_seconds", uptime},
        {"now", now}
    };

    if (_db) {
        j["db"] = {
            {"enabled", true},
            {"path", _db->getDbPath()},
            {"size_bytes", _db->getDbFileSize()},
            {"total_packets", _db->getTotalPacketCount()},
            {"total_nodes", _db->getTotalNodeCount()},
            {"total_messages", _db->getTotalTextMessageCount()},
            {"total_payload_bytes", _db->getTotalPayloadBytes()}
        };
    } else {
        j["db"] = {
            {"enabled", false}
        };
    }

    size_t sseCount = 0;
    {
        lock_guard<mutex> lock(_sseMutex);
        sseCount = _sseSessions.size();
    }

    j["web"] = {
        {"port", _config.port},
        {"host", _config.host},
        {"auth_required", !_config.password.empty()},
        {"sse_clients", sseCount},
        {"version", MYPROJECT_VERSION_STRING}
    };

    res.set_content(j.dump(), "application/json");
}

void WebServer::handleGetNodes(const httplib::Request&, httplib::Response &res)
{
    if (!_mon) {
        res.status = 503;
        res.set_content("{\"error\":\"MeshMon radio client unavailable\"}", "application/json");
        return;
    }

    const map<uint32_t, meshtastic_NodeInfo> &nodes = _mon->nodeInfos();
    const map<uint32_t, meshtastic_DeviceMetrics> &dMap = _mon->deviceMetrics();
    const map<uint32_t, meshtastic_EnvironmentMetrics> &eMap = _mon->environmentMetrics();
    const map<uint32_t, meshtastic_PowerMetrics> &pwMap = _mon->powerMetrics();
    const map<uint32_t, meshtastic_Position> &pMap = _mon->positions();
    map<uint32_t, AutomationNode> autoNodes = _mon->getAutomationNodes();

    time_t now = time(NULL);
    uint32_t myNode = _mon->whoami();

    json nodesArr = json::array();

    for (const auto &pair : nodes) {
        uint32_t id = pair.first;
        const meshtastic_NodeInfo &info = pair.second;

        json nodeObj;
        nodeObj["node_id"] = id;
        nodeObj["node_hex"] = formatNodeHex(id);
        nodeObj["short_name"] = _mon->lookupShortName(id);
        nodeObj["long_name"] = _mon->lookupLongName(id);
        nodeObj["is_self"] = (id == myNode);

        // Hardware & Role
        int hwModel = 0;
        int role = 0;
        if (info.has_user) {
            hwModel = info.user.hw_model;
            role = info.user.role;
        }
        nodeObj["hw_model"] = hwModel;
        nodeObj["hw_model_name"] = MeshMonShell::hardwareModelString((meshtastic_HardwareModel) hwModel);
        nodeObj["role"] = role;
        nodeObj["role_name"] = MeshMonShell::roleString((meshtastic_Config_DeviceConfig_Role) role);

        // RF Metrics
        nodeObj["snr"] = info.snr;
        nodeObj["hops"] = info.has_hops_away ? (int) info.hops_away : 0;
        nodeObj["channel"] = (int) info.channel;
        nodeObj["last_heard"] = (int64_t) info.last_heard;
        nodeObj["last_heard_rel"] = MeshMonShell::formatRelativeTime(info.last_heard);

        // Status calculation (online <= 15m, stale <= 1h, offline > 1h)
        time_t diff = now - info.last_heard;
        if (info.last_heard == 0 || diff > 3600) {
            nodeObj["status"] = "offline";
        } else if (diff > 900) {
            nodeObj["status"] = "stale";
        } else {
            nodeObj["status"] = "online";
        }

        // Device Metrics (Battery, Voltage, Channel Util)
        auto dIt = dMap.find(id);
        if (dIt != dMap.end()) {
            const auto &dm = dIt->second;
            if (dm.has_battery_level) nodeObj["battery_level"] = dm.battery_level;
            if (dm.has_voltage) nodeObj["voltage"] = dm.voltage;
            if (dm.has_channel_utilization) nodeObj["channel_util"] = dm.channel_utilization;
            if (dm.has_air_util_tx) nodeObj["air_util_tx"] = dm.air_util_tx;
            if (dm.has_uptime_seconds) nodeObj["uptime_seconds"] = dm.uptime_seconds;
        }

        // Environment Metrics
        auto eIt = eMap.find(id);
        if (eIt != eMap.end()) {
            const auto &em = eIt->second;
            if (em.has_temperature) nodeObj["temperature"] = em.temperature;
            if (em.has_relative_humidity) nodeObj["humidity"] = em.relative_humidity;
            if (em.has_barometric_pressure) nodeObj["pressure"] = em.barometric_pressure;
        }

        // Power Metrics
        auto pwIt = pwMap.find(id);
        if (pwIt != pwMap.end()) {
            const auto &pwm = pwIt->second;
            if (pwm.has_ch1_voltage) nodeObj["ch1_voltage"] = pwm.ch1_voltage;
            if (pwm.has_ch1_current) nodeObj["ch1_current"] = pwm.ch1_current;
        }

        // Position
        auto pIt = pMap.find(id);
        if (pIt != pMap.end() && (pIt->second.latitude_i != 0 || pIt->second.longitude_i != 0)) {
            nodeObj["latitude"] = pIt->second.latitude_i * 1e-7;
            nodeObj["longitude"] = pIt->second.longitude_i * 1e-7;
            if (pIt->second.altitude != 0) {
                nodeObj["altitude"] = pIt->second.altitude;
            }
        }

        // HomeMesh Automation type if mapped
        auto aIt = autoNodes.find(id);
        if (aIt != autoNodes.end() && !aIt->second.deviceType.empty()) {
            nodeObj["automation_type"] = aIt->second.deviceType;
        }

        nodesArr.push_back(nodeObj);
    }

    res.set_content(nodesArr.dump(), "application/json");
}

void WebServer::handleGetNodeDetail(const httplib::Request &req, httplib::Response &res)
{
    if (!_mon || !_db) {
        res.status = 503;
        res.set_content("{\"error\":\"Service not ready\"}", "application/json");
        return;
    }

    string idStr = req.get_param_value("id");
    if (idStr.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"Node ID parameter 'id' required\"}", "application/json");
        return;
    }

    uint32_t targetId = MeshMonShell::resolveNode(_mon.get(), idStr);
    if (targetId == 0xffffffffU) {
        res.status = 404;
        res.set_content("{\"error\":\"Node not found\"}", "application/json");
        return;
    }

    json j;
    j["node_id"] = targetId;
    j["node_hex"] = formatNodeHex(targetId);
    j["short_name"] = _mon->lookupShortName(targetId);
    j["long_name"] = _mon->lookupLongName(targetId);

    time_t since = time(NULL) - 86400; // 24-hour history
    NodeDetail detail;
    if (_db->getNodeDetail(targetId, since, detail)) {
        j["total_packets"] = detail.totalPackets;
        j["avg_snr"] = detail.avgSnr;
        j["min_snr"] = detail.minSnr;
        j["max_snr"] = detail.maxSnr;
        j["avg_rssi"] = detail.avgRssi;
        j["last_snr"] = detail.lastSnr;
        j["last_rssi"] = detail.lastRssi;
        j["last_hops"] = detail.lastHops;
        j["has_telemetry"] = detail.hasTelemetry;
        j["last_battery"] = detail.lastBattery;
        j["last_voltage"] = detail.lastVoltage;
        j["last_channel_util"] = detail.lastChannelUtil;
        j["last_air_util_tx"] = detail.lastAirUtilTx;
    }

    // Link fading history
    vector<LinkFadingPoint> points;
    if (_db->getLinkFading(targetId, since, points)) {
        json fadingArr = json::array();
        for (const auto &pt : points) {
            fadingArr.push_back(json{
                {"time", pt.timestamp},
                {"snr", pt.avgSnr},
                {"min_snr", pt.minSnr},
                {"max_snr", pt.maxSnr},
                {"rssi", pt.avgRssi},
                {"count", pt.count}
            });
        }
        j["fading_history"] = fadingArr;
    }

    res.set_content(j.dump(), "application/json");
}

void WebServer::handleGetPackets(const httplib::Request &req, httplib::Response &res)
{
    size_t limit = 50;
    if (req.has_param("limit")) {
        limit = min((size_t) max(1, atoi(req.get_param_value("limit").c_str())), (size_t) 200);
    }

    int filterPortnum = -1;
    if (req.has_param("portnum")) {
        filterPortnum = atoi(req.get_param_value("portnum").c_str());
    }

    json arr = json::array();

    lock_guard<mutex> lock(_packetRingMutex);
    size_t count = 0;
    for (auto it = _recentPackets.rbegin(); it != _recentPackets.rend() && count < limit; ++it) {
        if (filterPortnum >= 0 && it->portnum != filterPortnum) {
            continue;
        }

        arr.push_back({
            {"id", it->id},
            {"time", it->meshmonTime},
            {"from_hex", it->fromHex},
            {"from_name", it->fromName},
            {"to_hex", it->toHex},
            {"to_name", it->toName},
            {"channel", it->channel},
            {"rssi", it->rxRssi},
            {"snr", it->rxSnr},
            {"hops", it->hops},
            {"portnum", it->portnum},
            {"app", it->appName},
            {"payload_size", it->payloadSize},
            {"text", it->text},
            {"via_mqtt", it->viaMqtt}
        });
        count++;
    }

    res.set_content(arr.dump(), "application/json");
}

void WebServer::handleGetAnalytics(const httplib::Request &req, httplib::Response &res)
{
    if (!_db) {
        res.status = 503;
        res.set_content("{\"error\":\"Database not available\"}", "application/json");
        return;
    }

    int hours = 24;
    if (req.has_param("hours")) {
        hours = max(1, atoi(req.get_param_value("hours").c_str()));
    }
    time_t since = time(NULL) - (hours * 3600);

    json j;
    j["window_hours"] = hours;

    // 1. Traffic summary
    TrafficSummary ts;
    if (_db->getTrafficSummary(since, ts)) {
        float directPct = 0.0f, bcastPct = 0.0f, avgHops = 0.0f;
        _db->getTrafficRatios(since, directPct, bcastPct, avgHops);
        j["traffic"] = json{
            {"total_packets", ts.totalPackets},
            {"direct_packets", ts.directPackets},
            {"broadcast_packets", ts.broadcastPackets},
            {"direct_pct", directPct},
            {"broadcast_pct", bcastPct},
            {"avg_hops", avgHops},
            {"total_payload_bytes", ts.totalBytes}
        };
    }

    // 2. Hop distribution
    vector<HopStat> hopStats;
    if (_db->getHopDistribution(since, hopStats)) {
        json hopArr = json::array();
        for (const auto &h : hopStats) {
            hopArr.push_back(json{
                {"hops", h.hops},
                {"packet_count", h.packetCount},
                {"pct", h.pctShare}
            });
        }
        j["hop_distribution"] = hopArr;
    }

    // 3. Channel health
    ChannelHealthStat health;
    if (_db->getChannelHealth(since, health)) {
        j["channel_health"] = json{
            {"avg_channel_util", health.avgChannelUtil},
            {"max_channel_util", health.maxChannelUtil},
            {"avg_air_util_tx", health.avgAirUtilTx},
            {"max_air_util_tx", health.maxAirUtilTx}
        };
    }

    // 4. Top Talkers
    vector<NodeTrafficStat> topTalkers;
    if (_db->getTopTalkers(since, 10, topTalkers)) {
        json ttArr = json::array();
        for (const auto &tt : topTalkers) {
            ttArr.push_back(json{
                {"node_id", tt.nodeId},
                {"node_hex", tt.nodeHex},
                {"short_name", tt.shortName},
                {"packet_count", tt.packetCount},
                {"total_bytes", tt.totalBytes},
                {"avg_snr", tt.avgSnr},
                {"avg_hops", tt.avgHops},
                {"last_seen", tt.lastSeen}
            });
        }
        j["top_talkers"] = ttArr;
    }

    // 5. Critical Repeaters
    vector<CriticalRepeaterStat> repeaters;
    if (_db->getCriticalRepeaters(since, 5, repeaters)) {
        json repArr = json::array();
        for (const auto &r : repeaters) {
            repArr.push_back(json{
                {"node_id", r.repeaterId},
                {"node_hex", r.repeaterHex},
                {"short_name", r.shortName},
                {"relayed_packets", r.relayCount},
                {"avg_snr", r.avgSnr}
            });
        }
        j["critical_repeaters"] = repArr;
    }

    // 6. Echo Storms
    vector<EchoStormStat> echoStorms;
    if (_db->getEchoStorms(since, 5, echoStorms)) {
        json echoArr = json::array();
        for (const auto &es : echoStorms) {
            string sName = (_mon != nullptr) ? _mon->lookupShortName(es.fromNode) : "";
            echoArr.push_back(json{
                {"packet_id", es.packetId},
                {"from_hex", es.fromHex},
                {"short_name", sName},
                {"echo_count", es.echoCount}
            });
        }
        j["echo_storms"] = echoArr;
    }

    // 7. Protocol App Distribution
    vector<AppStat> appStats;
    if (_db->getPortnumDistribution(since, appStats)) {
        json appArr = json::array();
        for (const auto &a : appStats) {
            appArr.push_back(json{
                {"portnum", a.portnum},
                {"app", a.appName},
                {"packet_count", a.packetCount},
                {"total_bytes", a.totalBytes},
                {"pct", a.pctShare}
            });
        }
        j["app_distribution"] = appArr;
    }

    // 8. Direct Line-of-Sight RF Neighbors (0-hop links)
    vector<NeighborStat> neighbors;
    if (_db->getNeighborStats(since, neighbors)) {
        json nArr = json::array();
        for (const auto &n : neighbors) {
            nArr.push_back(json{
                {"node_id", n.nodeId},
                {"node_hex", n.nodeHex},
                {"short_name", n.shortName},
                {"long_name", n.longName},
                {"packet_count", n.packetCount},
                {"avg_snr", n.avgSnr},
                {"min_snr", n.minSnr},
                {"max_snr", n.maxSnr},
                {"avg_rssi", n.avgRssi},
                {"last_seen", n.lastSeen}
            });
        }
        j["direct_neighbors"] = nArr;
    }

    res.set_content(j.dump(), "application/json");
}

void WebServer::handleGetAutomation(const httplib::Request&, httplib::Response &res)
{
    if (!_mon) {
        res.status = 503;
        res.set_content("{\"error\":\"MeshMon radio client unavailable\"}", "application/json");
        return;
    }

    map<uint32_t, AutomationNode> autoNodes = _mon->getAutomationNodes();
    json j;
    json nodesArr = json::array();

    for (const auto &pair : autoNodes) {
        const AutomationNode &an = pair.second;
        json aObj;
        aObj["node_id"] = an.nodeId;
        aObj["node_hex"] = formatNodeHex(an.nodeId);
        aObj["short_name"] = _mon->lookupShortName(an.nodeId);
        aObj["device_type"] = an.deviceType;
        aObj["online"] = an.online;
        aObj["first_seen"] = an.firstSeen;
        aObj["last_seen"] = an.lastSeen;
        aObj["last_seen_rel"] = MeshMonShell::formatRelativeTime(an.lastSeen);
        aObj["uptime_sec"] = an.uptimeSec;
        aObj["reboot_count"] = an.rebootCount;
        aObj["last_rtt_ms"] = an.lastRttMs;
        aObj["avg_rtt_ms"] = an.avgRttMs;

        if (an.deviceType == "meshpump") {
            aObj["fish_pump_state"] = an.fishPumpState;
            aObj["up_pump_state"] = an.upPumpState;
            aObj["up_pump_cutoff_sec"] = an.upPumpCutoffSec;
            aObj["led_message"] = an.ledMessage;
        } else if (an.deviceType == "meshroof") {
            aObj["amplify_state"] = an.amplifyState;
            aObj["wifi_status"] = an.wifiStatus;
            aObj["wifi_rssi"] = an.wifiRssi;
            aObj["ip_address"] = an.ipAddress;
            aObj["cpu_temp_c"] = an.cpuTempC;
            aObj["reset_count"] = an.resetCount;
        } else if (an.deviceType == "meshroom") {
            aObj["ac_power"] = an.acPower;
            aObj["ac_target_temp"] = an.acTargetTemp;
            aObj["ac_mode"] = an.acMode;
            aObj["ac_fan"] = an.acFan;
            aObj["ac_vane"] = an.acVane;
            aObj["ac_turbo"] = an.acTurbo;
            aObj["ac_quiet"] = an.acQuiet;
            aObj["tv_power"] = an.tvPower;
            aObj["tv_volume"] = an.tvVolume;
            aObj["tv_channel"] = an.tvChannel;
            aObj["tv_mute"] = an.tvMute;
            aObj["tv_input"] = an.tvInput;
            aObj["board_temp_c"] = an.boardTempC;
            aObj["room_temp_c"] = an.roomTempC;
            aObj["ac_ir_protocol"] = an.acIrProtocol;
            aObj["tv_ir_protocol"] = an.tvIrProtocol;
        }

        nodesArr.push_back(aObj);
    }
    j["nodes"] = nodesArr;

    // Recent automation events from DB
    if (_db) {
        vector<AutomationEvent> events;
        if (_db->getAutomationHistory(20, events)) {
            json evArr = json::array();
            for (const auto &ev : events) {
                evArr.push_back({
                    {"time", ev.meshmonTime},
                    {"node_id", ev.nodeId},
                    {"node_hex", formatNodeHex(ev.nodeId)},
                    {"device_type", ev.deviceType},
                    {"direction", ev.direction},
                    {"subsystem", ev.subsystem},
                    {"command", ev.commandName},
                    {"param", ev.actionParam},
                    {"status", ev.status},
                    {"initiator", ev.initiator},
                    {"rtt_ms", ev.rttMs}
                });
            }
            j["history"] = evArr;
        }
    }

    res.set_content(j.dump(), "application/json");
}

void WebServer::handleGetMessages(const httplib::Request &req, httplib::Response &res)
{
    if (!_db) {
        res.status = 503;
        res.set_content("{\"error\":\"Database not available\"}", "application/json");
        return;
    }

    size_t limit = 50;
    if (req.has_param("limit")) {
        limit = min((size_t) max(1, atoi(req.get_param_value("limit").c_str())), (size_t) 200);
    }

    string sql = "SELECT id, meshmon_time, from_node, to_node, channel, message "
                 "FROM text_messages ORDER BY id DESC LIMIT " + to_string(limit) + ";";

    QueryResult qres;
    if (!_db->executeRawQuery(sql, qres)) {
        res.status = 500;
        res.set_content("{\"error\":\"Database query failed: " + qres.error + "\"}", "application/json");
        return;
    }

    json arr = json::array();
    for (const auto &row : qres.rows) {
        if (row.size() < 6) continue;
        uint32_t fromNode = (uint32_t) strtoul(row[2].c_str(), nullptr, 10);
        uint32_t toNode = (uint32_t) strtoul(row[3].c_str(), nullptr, 10);

        string fromName = (_mon != nullptr) ? _mon->lookupShortName(fromNode) : "";
        string toName = (_mon != nullptr && toNode != 0xffffffffU) ? _mon->lookupShortName(toNode) : "";

        arr.push_back({
            {"id", (uint32_t) strtoul(row[0].c_str(), nullptr, 10)},
            {"time", (time_t) strtoul(row[1].c_str(), nullptr, 10)},
            {"from_hex", formatNodeHex(fromNode)},
            {"from_name", fromName},
            {"to_hex", (toNode == 0xffffffffU) ? "^all" : formatNodeHex(toNode)},
            {"to_name", toName},
            {"channel", atoi(row[4].c_str())},
            {"message", row[5]}
        });
    }

    res.set_content(arr.dump(), "application/json");
}

void WebServer::handleGetSpatial(const httplib::Request &req, httplib::Response &res)
{
    if (!_db) {
        res.status = 503;
        res.set_content("{\"error\":\"Database not available\"}", "application/json");
        return;
    }

    double refLat = 24.82176;
    double refLon = 121.2383232;
    string refName = "Gateway Reference";
    int thresholdM = 50;

    if (req.has_param("premise_threshold")) {
        thresholdM = max(10, atoi(req.get_param_value("premise_threshold").c_str()));
    }

    if (_mon != nullptr) {
        uint32_t myNode = _mon->whoami();
        if (myNode != 0) {
            refName = formatNodeHex(myNode) + " (" + _mon->lookupShortName(myNode) + ")";
            const map<uint32_t, meshtastic_Position> &pMap = _mon->positions();
            auto it = pMap.find(myNode);
            if (it != pMap.end() && it->second.latitude_i != 0 && it->second.longitude_i != 0) {
                refLat = it->second.latitude_i * 1e-7;
                refLon = it->second.longitude_i * 1e-7;
            }
        }
    }

    string sql =
        "SELECT p.node_id, n.short_name, n.long_name, p.latitude, p.longitude, p.altitude, max(p.meshmon_time) "
        "FROM positions p "
        "LEFT JOIN nodes n ON p.node_id = n.node_id "
        "WHERE p.latitude != 0 AND p.longitude != 0 "
        "GROUP BY p.node_id;";

    QueryResult qres;
    if (!_db->executeRawQuery(sql, qres)) {
        res.status = 500;
        res.set_content("{\"error\":\"Spatial query failed: " + qres.error + "\"}", "application/json");
        return;
    }

    json j;
    j["reference"] = {
        {"name", refName},
        {"lat", refLat},
        {"lon", refLon},
        {"premise_threshold_m", thresholdM}
    };

    json nodesArr = json::array();
    double farthestDist = 0.0;
    string farthestNode = "-";
    size_t onPremiseCount = 0;

    for (const auto &row : qres.rows) {
        if (row.size() < 7) continue;
        uint32_t nodeId = (uint32_t) strtoul(row[0].c_str(), nullptr, 10);
        string shortName = (row[1] != "NULL" && !row[1].empty()) ? row[1] : "-";
        string longName = (row[2] != "NULL" && !row[2].empty()) ? row[2] : "-";
        double lat = strtod(row[3].c_str(), nullptr);
        double lon = strtod(row[4].c_str(), nullptr);
        int alt = (int) strtol(row[5].c_str(), nullptr, 10);
        time_t lastSeen = (time_t) strtoul(row[6].c_str(), nullptr, 10);

        double dist = haversineMeters(refLat, refLon, lat, lon);
        bool onPremise = (dist <= thresholdM);
        if (onPremise) onPremiseCount++;

        if (dist > farthestDist) {
            farthestDist = dist;
            farthestNode = formatNodeHex(nodeId) + " (" + shortName + ")";
        }

        nodesArr.push_back({
            {"node_id", nodeId},
            {"node_hex", formatNodeHex(nodeId)},
            {"short_name", shortName},
            {"long_name", longName},
            {"lat", lat},
            {"lon", lon},
            {"alt", alt},
            {"distance_meters", dist},
            {"on_premise", onPremise},
            {"last_seen", lastSeen}
        });
    }

    j["nodes"] = nodesArr;
    j["stats"] = {
        {"total_nodes_with_gps", nodesArr.size()},
        {"on_premise_count", onPremiseCount},
        {"farthest_distance_meters", farthestDist},
        {"farthest_node", farthestNode}
    };

    res.set_content(j.dump(), "application/json");
}

// ----------------------------------------------------------------------------
// Authentication Endpoints
// ----------------------------------------------------------------------------

void WebServer::handlePostAuthLogin(const httplib::Request &req, httplib::Response &res)
{
    json reqJson;
    try {
        reqJson = json::parse(req.body);
    } catch (const exception &e) {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
        return;
    }

    string password = reqJson.value("password", "");

    if (_config.password.empty() || password == _config.password) {
        string token = createSessionToken();

        // Set HttpOnly cookie and return bearer token in JSON
        res.set_header("Set-Cookie", "meshmon_auth=" + token + "; Path=/; Max-Age=86400; SameSite=Lax");
        json resp = {
            {"authenticated", true},
            {"token", token},
            {"expires_in", 86400}
        };
        res.set_content(resp.dump(), "application/json");
        cout << "[WebServer] Admin authenticated successfully" << endl;
    } else {
        res.status = 401;
        res.set_content("{\"error\":\"Invalid password\"}", "application/json");
    }
}

void WebServer::handleGetAuthStatus(const httplib::Request &req, httplib::Response &res)
{
    bool isAuth = checkAuth(req);
    json j = {
        {"authenticated", isAuth},
        {"auth_required", !_config.password.empty()}
    };
    res.set_content(j.dump(), "application/json");
}

void WebServer::handlePostAuthLogout(const httplib::Request &req, httplib::Response &res)
{
    string token = extractToken(req);
    if (!token.empty()) {
        revokeSessionToken(token);
    }
    res.set_header("Set-Cookie", "meshmon_auth=; Path=/; Max-Age=0; SameSite=Lax");
    res.set_content("{\"authenticated\":false}", "application/json");
}

// ----------------------------------------------------------------------------
// Authenticated Mutating Handlers
// ----------------------------------------------------------------------------

void WebServer::handlePostSendMessage(const httplib::Request &req, httplib::Response &res)
{
    if (!_mon) {
        res.status = 503;
        res.set_content("{\"error\":\"Radio client not available\"}", "application/json");
        return;
    }

    json reqJson;
    try {
        reqJson = json::parse(req.body);
    } catch (const exception &e) {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid JSON: " + string(e.what()) + "\"}", "application/json");
        return;
    }

    string text = reqJson.value("text", "");
    string destination = reqJson.value("destination", "^all");
    int channel = reqJson.value("channel", 0);

    if (text.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"Message text cannot be empty\"}", "application/json");
        return;
    }

    uint32_t dest = 0xffffffffU;
    if (!destination.empty() && (destination != "^all") && (destination != "broadcast")) {
        dest = MeshMonShell::resolveNode(_mon.get(), destination);
        if (dest == 0xffffffffU) {
            res.status = 400;
            res.set_content("{\"error\":\"Destination could not be resolved: " + destination + "\"}", "application/json");
            return;
        }
    }

    bool ok = _mon->textMessage(dest, static_cast<uint8_t>(channel), text);
    if (ok) {
        json j = {
            {"success", true},
            {"destination_hex", (dest == 0xffffffffU) ? "^all" : formatNodeHex(dest)},
            {"channel", channel},
            {"bytes_sent", text.size()}
        };
        res.set_content(j.dump(), "application/json");
        cout << "[WebServer] Sent message on ch " << channel << " to "
             << ((dest == 0xffffffffU) ? "^all" : formatNodeHex(dest)) << ": " << text << endl;
    } else {
        res.status = 500;
        res.set_content("{\"error\":\"Failed to transmit radio text message\"}", "application/json");
    }
}

void WebServer::handlePostAutomationCommand(const httplib::Request &req, httplib::Response &res)
{
    if (!_mon) {
        res.status = 503;
        res.set_content("{\"error\":\"Radio client not available\"}", "application/json");
        return;
    }

    json reqJson;
    try {
        reqJson = json::parse(req.body);
    } catch (const exception &e) {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
        return;
    }

    string nodeStr = "";
    if (reqJson.contains("node_id")) {
        if (reqJson["node_id"].is_string()) {
            nodeStr = reqJson["node_id"].get<string>();
        } else if (reqJson["node_id"].is_number()) {
            nodeStr = to_string(reqJson["node_id"].get<uint32_t>());
        }
    }

    string command = reqJson.value("command", "");
    int channel = reqJson.value("channel", -1);

    if (nodeStr.empty() || command.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"'node_id' and 'command' required\"}", "application/json");
        return;
    }

    uint32_t nodeId = MeshMonShell::resolveNode(_mon.get(), nodeStr);
    if (nodeId == 0xffffffffU) {
        res.status = 404;
        res.set_content("{\"error\":\"Node not found: " + nodeStr + "\"}", "application/json");
        return;
    }

    bool ok = _mon->sendAutomationCommand(nodeId, command, "WEB", channel);
    if (ok) {
        json j = {
            {"success", true},
            {"node_hex", formatNodeHex(nodeId)},
            {"command", command}
        };
        res.set_content(j.dump(), "application/json");
        cout << "[WebServer] Dispatched automation command '" << command
             << "' to " << formatNodeHex(nodeId) << endl;
    } else {
        res.status = 500;
        res.set_content("{\"error\":\"Failed to dispatch command to automation node\"}", "application/json");
    }
}

void WebServer::handlePostDbQuery(const httplib::Request &req, httplib::Response &res)
{
    if (!_db) {
        res.status = 503;
        res.set_content("{\"error\":\"Database not available\"}", "application/json");
        return;
    }

    json reqJson;
    try {
        reqJson = json::parse(req.body);
    } catch (const exception &e) {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
        return;
    }

    string query = reqJson.value("query", "");
    if (query.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"Query cannot be empty\"}", "application/json");
        return;
    }

    // Basic SQL sanity check: enforce read-only
    string lowerQuery = query;
    transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    while (!lowerQuery.empty() && isspace(lowerQuery.front())) {
        lowerQuery.erase(0, 1);
    }

    if (lowerQuery.rfind("select", 0) != 0 && lowerQuery.rfind("explain", 0) != 0 && lowerQuery.rfind("pragma", 0) != 0) {
        res.status = 403;
        res.set_content("{\"error\":\"Only SELECT, EXPLAIN, and PRAGMA read queries are permitted via Web Console\"}", "application/json");
        return;
    }

    QueryResult qres;
    if (_db->executeRawQuery(query, qres)) {
        json j;
        j["columns"] = qres.columns;
        j["rows"] = qres.rows;
        j["row_count"] = qres.rows.size();
        res.set_content(j.dump(), "application/json");
    } else {
        res.status = 400;
        json j = {
            {"error", qres.error}
        };
        res.set_content(j.dump(), "application/json");
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
