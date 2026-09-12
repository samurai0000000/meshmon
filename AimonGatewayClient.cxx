/*
 * AimonGatewayClient.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include "AimonGatewayClient.hxx"
#include "MeshMon.hxx"
#include "MeshMonDb.hxx"
#include "MeshMonShell.hxx"
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <cmath>

using namespace std;
using json = nlohmann::json;

AimonGatewayClient::AimonGatewayClient(shared_ptr<MeshMon> mon,
                                       shared_ptr<MeshMonDb> db)
    : _mon(mon),
      _db(db),
      _host("builder"),
      _port(3885),
      _running(false),
      _connected(false),
      _sockFd(-1)
{
}

AimonGatewayClient::~AimonGatewayClient()
{
    stop();
    join();
}

void AimonGatewayClient::setMeshMon(shared_ptr<MeshMon> mon)
{
    _mon = mon;
}

void AimonGatewayClient::setDb(shared_ptr<MeshMonDb> db)
{
    _db = db;
}

bool AimonGatewayClient::start(const string &host, uint16_t port)
{
    if (_running) {
        return false;
    }

    _host = host;
    _port = port;
    _running = true;
    _thread = thread(&AimonGatewayClient::run, this);
    return true;
}

void AimonGatewayClient::stop(void)
{
    if (!_running) {
        return;
    }

    _running = false;
    disconnect();
}

void AimonGatewayClient::join(void)
{
    if (_thread.joinable()) {
        _thread.join();
    }
}

bool AimonGatewayClient::isConnected(void) const
{
    return _connected;
}

void AimonGatewayClient::disconnect(void)
{
    _connected = false;
    lock_guard<mutex> lock(_sendMutex);
    if (_sockFd != -1) {
        shutdown(_sockFd, SHUT_RDWR);
        close(_sockFd);
        _sockFd = -1;
    }
}

bool AimonGatewayClient::connectToGateway(void)
{
    disconnect();

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    string portStr = to_string(_port);
    int rc = getaddrinfo(_host.c_str(), portStr.c_str(), &hints, &res);
    if (rc != 0 || res == nullptr) {
        return false;
    }

    int fd = -1;
    for (struct addrinfo *rp = res; rp != nullptr; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1) {
            continue;
        }

        int flag = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(flag));
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &flag, sizeof(flag));

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);

    if (fd == -1) {
        return false;
    }

    {
        lock_guard<mutex> lock(_sendMutex);
        _sockFd = fd;
    }

    return true;
}

void AimonGatewayClient::sendResponse(const string &msg)
{
    lock_guard<mutex> lock(_sendMutex);
    if (_sockFd == -1) {
        return;
    }

    string payload = msg;
    if (payload.empty() || payload.back() != '\n') {
        payload += "\n";
    }

    const char *ptr = payload.c_str();
    size_t rem = payload.size();
    while (rem > 0) {
        ssize_t n = send(_sockFd, ptr, rem, MSG_NOSIGNAL);
        if (n <= 0) {
            break;
        }
        ptr += n;
        rem -= n;
    }
}

bool AimonGatewayClient::sendRegistration(void)
{
    json reg = {
        {"jsonrpc", "2.0"},
        {"method", "gateway/register"},
        {"params", {
            {"subsystem", "meshmon"},
            {"name", "meshmon"},
            {"description", "Meshtastic mesh network monitor, packet logger, and telemetry engine"},
            {"tools", json::array({
                {
                    {"name", "meshmon_get_node_status"},
                    {"description", "Get current status and device metrics of Meshtastic nodes in the mesh (battery, voltage, SNR, hops, last heard)."},
                    {"inputSchema", {
                        {"type", "object"},
                        {"properties", {
                            {"node_id", {
                                {"type", "string"},
                                {"description", "Specific node ID (decimal or !hex format) or short/long name. If omitted, returns all known nodes."}
                            }}
                        }}
                    }}
                },
                {
                    {"name", "meshmon_query_telemetry_history"},
                    {"description", "Query historical telemetry time-series metrics for a node (battery, voltage, channel utilization, air util, temperature, humidity, pressure)."},
                    {"inputSchema", {
                        {"type", "object"},
                        {"properties", {
                            {"node_id", {
                                {"type", "string"},
                                {"description", "Target node ID (decimal or !hex format) or short/long name."}
                            }},
                            {"metric", {
                                {"type", "string"},
                                {"description", "Specific metric to query: 'battery', 'voltage', 'channel_utilization', 'air_util_tx', 'temperature', 'humidity', 'pressure', or 'all' (default)."}
                            }},
                            {"hours", {
                                {"type", "integer"},
                                {"description", "Number of hours of history to query (default 24)."}
                            }},
                            {"limit", {
                                {"type", "integer"},
                                {"description", "Maximum number of data points to return (default 50)."}
                            }}
                        }},
                        {"required", json::array({"node_id"})}
                    }}
                },
                {
                    {"name", "meshmon_get_rf_analytics"},
                    {"description", "Get RF network health analytics, traffic summary, echo storm detection, duplicate packet ratios, and critical relay bottlenecks."},
                    {"inputSchema", {
                        {"type", "object"},
                        {"properties", {
                            {"hours", {
                                {"type", "integer"},
                                {"description", "Analysis window in hours (default 24)."}
                            }}
                        }}
                    }}
                },
                {
                    {"name", "meshmon_send_message"},
                    {"description", "Transmit a text message across the Meshtastic mesh network to a destination node or broadcast channel."},
                    {"inputSchema", {
                        {"type", "object"},
                        {"properties", {
                            {"text", {
                                {"type", "string"},
                                {"description", "The text message to transmit."}
                            }},
                            {"destination", {
                                {"type", "string"},
                                {"description", "Target node ID (decimal or !hex format) or '^all' for broadcast (default '^all')."}
                            }},
                            {"channel", {
                                {"type", "integer"},
                                {"description", "Channel index (0 for primary, default 0)."}
                            }}
                        }},
                        {"required", json::array({"text"})}
                    }}
                },
                {
                    {"name", "meshmon_query_db"},
                    {"description", "Execute a read-only SQL query against the local meshmon SQLite database. Supports complex analytics, percentiles, joins, aggregations, window functions, and time filtering.\n\nDatabase Schema:\n- nodes (node_id INTEGER PRIMARY KEY, node_hex TEXT, long_name TEXT, short_name TEXT, hw_model INTEGER, role INTEGER, first_seen INTEGER, last_seen INTEGER, last_rssi REAL, last_snr REAL, last_hops INTEGER)\n- packets (id INTEGER PRIMARY KEY, meshmon_time INTEGER, rx_time INTEGER, packet_id INTEGER, from_node INTEGER, to_node INTEGER, channel INTEGER, rx_rssi REAL, rx_snr REAL, hop_start INTEGER, hop_limit INTEGER, hops INTEGER, portnum INTEGER, payload_variant INTEGER, payload_size INTEGER, want_ack INTEGER, via_mqtt INTEGER, payload BLOB)\n- telemetry (id INTEGER PRIMARY KEY, meshmon_time INTEGER, node_id INTEGER, metric_type TEXT, battery_level INTEGER, voltage REAL, channel_utilization REAL, air_util_tx REAL, temperature REAL, humidity REAL, pressure REAL, ch1_voltage REAL, ch1_current REAL, uptime_seconds INTEGER)\n- positions (id INTEGER PRIMARY KEY, meshmon_time INTEGER, node_id INTEGER, latitude REAL, longitude REAL, altitude INTEGER, ground_speed INTEGER, ground_track INTEGER, sats_in_view INTEGER)\n- text_messages (id INTEGER PRIMARY KEY, meshmon_time INTEGER, packet_id INTEGER, from_node INTEGER, to_node INTEGER, channel INTEGER, message TEXT)\n- traceroutes (id INTEGER PRIMARY KEY, meshmon_time INTEGER, packet_id INTEGER, from_node INTEGER, to_node INTEGER, route_nodes TEXT, route_snrs TEXT)\n- automation_events (id INTEGER PRIMARY KEY, meshmon_time INTEGER, node_id INTEGER, device_type TEXT, direction TEXT, subsystem TEXT, command_name TEXT, action_param TEXT, status TEXT, initiator TEXT, rtt_ms INTEGER)\n- automation_nodes (node_id INTEGER PRIMARY KEY, node_hex TEXT, device_type TEXT, version TEXT, hardware TEXT, capabilities TEXT, ac_ir_protocol TEXT, tv_ir_protocol TEXT, first_seen INTEGER, last_seen INTEGER)"},
                    {"inputSchema", {
                        {"type", "object"},
                        {"properties", {
                            {"query", {
                                {"type", "string"},
                                {"description", "Read-only SQL query (SELECT or WITH statements only)."}
                            }}
                        }},
                        {"required", json::array({"query"})}
                    }}
                },
                {
                    {"name", "meshmon_get_spatial_analytics"},
                    {"description", "Calculate spatial and geographic analytics for nodes in the mesh network: off-premise vs on-premise node counts based on GPS distance from the gateway reference location, nearest nodes, and the farthest nodes ever heard."},
                    {"inputSchema", {
                        {"type", "object"},
                        {"properties", {
                            {"premise_threshold_m", {
                                {"type", "integer"},
                                {"description", "Radius threshold in meters to classify a node as on-premise vs off-premise (default: 50)."}
                            }},
                            {"top_farthest", {
                                {"type", "integer"},
                                {"description", "Number of farthest nodes to return in the leaderboard (default: 10)."}
                            }},
                            {"hours", {
                                {"type", "integer"},
                                {"description", "Filter nodes heard within the last N hours (0 for all-time history, default: 0)."}
                            }}
                        }}
                    }}
                }
            })},
        }}
    };

    sendResponse(reg.dump());
    return true;
}

void AimonGatewayClient::processIncoming(void)
{
    char buffer[4096];
    string lineBuffer;

    while (_running && _connected && (_sockFd != -1)) {
        struct pollfd pfd;
        pfd.fd = _sockFd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, 500);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (pr == 0) {
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        }
        if (pfd.revents & POLLIN) {
            ssize_t n = recv(_sockFd, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                break;
            }
            lineBuffer.append(buffer, n);

            size_t newlinePos;
            while ((newlinePos = lineBuffer.find('\n')) != string::npos) {
                string line = lineBuffer.substr(0, newlinePos);
                lineBuffer.erase(0, newlinePos + 1);
                if (!line.empty() && (line.back() == '\r')) {
                    line.pop_back();
                }
                if (!line.empty()) {
                    handleRequest(line);
                }
            }
        }
    }
}

void AimonGatewayClient::handleRequest(const string &line)
{
    try {
        json req = json::parse(line);
        if (!req.contains("id")) {
            return;
        }

        if (req.contains("result") || req.contains("error")) {
            return;
        }

        auto reqId = req["id"];
        string method = req.value("method", "");

        if (method == "tools/call") {
            json params = req.value("params", json::object());
            string toolName = params.value("name", "");
            json args = params.value("arguments", json::object());

            string resultText;
            if (toolName == "meshmon_get_node_status") {
                string nodeId = args.value("node_id", "");
                resultText = toolGetNodeStatus(nodeId);
            } else if (toolName == "meshmon_query_telemetry_history") {
                string nodeId = args.value("node_id", "");
                string metric = args.value("metric", "all");
                int hours = args.value("hours", 24);
                int limit = args.value("limit", 50);
                resultText = toolQueryTelemetryHistory(nodeId, metric, hours, limit);
            } else if (toolName == "meshmon_get_rf_analytics") {
                int hours = args.value("hours", 24);
                resultText = toolGetRfAnalytics(hours);
            } else if (toolName == "meshmon_send_message") {
                string text = args.value("text", "");
                string dest = args.value("destination", "^all");
                int channel = args.value("channel", 0);
                resultText = toolSendMessage(text, dest, channel);
            } else if (toolName == "meshmon_query_db") {
                string sql = args.value("query", "");
                resultText = toolQueryDb(sql);
            } else if (toolName == "meshmon_get_spatial_analytics") {
                int thresholdM = args.value("premise_threshold_m", 50);
                int topFarthest = args.value("top_farthest", 10);
                int hours = args.value("hours", 0);
                resultText = toolGetSpatialAnalytics(thresholdM, topFarthest, hours);
            } else {
                json errResp = {
                    {"jsonrpc", "2.0"},
                    {"id", reqId},
                    {"error", {
                        {"code", -32601},
                        {"message", "Unknown tool: " + toolName}
                    }}
                };
                sendResponse(errResp.dump());
                return;
            }

            json resp = {
                {"jsonrpc", "2.0"},
                {"id", reqId},
                {"result", {
                    {"content", json::array({
                        {
                            {"type", "text"},
                            {"text", resultText}
                        }
                    })}
                }}
            };
            sendResponse(resp.dump());
        } else {
            json errResp = {
                {"jsonrpc", "2.0"},
                {"id", reqId},
                {"error", {
                    {"code", -32601},
                    {"message", "Unsupported method: " + method}
                }}
            };
            sendResponse(errResp.dump());
        }
    } catch (const exception &e) {
        cerr << "[AimonGatewayClient] Error processing request: " << e.what() << endl;
    }
}

void AimonGatewayClient::run(void)
{
    uint32_t backoffMs = 1000;

    while (_running) {
        if (connectToGateway()) {
            backoffMs = 1000;
            if (sendRegistration()) {
                _connected = true;
                processIncoming();
            }
            _connected = false;
            disconnect();
        }

        if (!_running) {
            break;
        }

        this_thread::sleep_for(chrono::milliseconds(backoffMs));
        backoffMs = min(backoffMs * 2, 30000U);
    }
}

string AimonGatewayClient::toolGetNodeStatus(const string &nodeIdArg)
{
    if (_mon == nullptr) {
        return "Error: Radio client is not attached or initialized.";
    }

    const map<uint32_t, meshtastic_NodeInfo> &nodes = _mon->nodeInfos();
    const map<uint32_t, meshtastic_DeviceMetrics> &dMap = _mon->deviceMetrics();
    const map<uint32_t, meshtastic_Position> &pMap = _mon->positions();
    map<uint32_t, AutomationNode> autoNodes = _mon->getAutomationNodes();

    if (!nodeIdArg.empty()) {
        uint32_t targetId = MeshMonShell::resolveNode(_mon.get(), nodeIdArg);
        if (targetId == 0xffffffffU) {
            return "Error: Node '" + nodeIdArg + "' not found in known mesh nodes.";
        }

        char hexBuf[32];
        snprintf(hexBuf, sizeof(hexBuf), "!%08x", targetId);
        string shortName = _mon->lookupShortName(targetId);
        string longName = _mon->lookupLongName(targetId);
        bool isSelf = (targetId == _mon->whoami());

        stringstream ss;
        ss << "### Meshtastic Node Details: " << shortName << " (" << hexBuf << ")\n\n";
        ss << "- **Node ID**: `" << hexBuf << "` (" << targetId << ")"
           << (isSelf ? " **[Local Gateway Node]**" : "") << "\n";
        if (!longName.empty() && (longName != shortName)) {
            ss << "- **Long Name**: " << longName << "\n";
        }

        map<uint32_t, meshtastic_NodeInfo>::const_iterator nIt = nodes.find(targetId);
        if (nIt != nodes.end() && nIt->second.has_user) {
            const meshtastic_User &u = nIt->second.user;
            ss << "- **Hardware**: " << MeshMonShell::hardwareModelString(u.hw_model) << "\n";
            ss << "- **Role**: " << MeshMonShell::roleString(u.role) << "\n";
        }

        if (nIt != nodes.end()) {
            const meshtastic_NodeInfo &info = nIt->second;
            if (info.snr != 0.0f) {
                ss << "- **SNR**: " << fixed << setprecision(2) << info.snr << " dB\n";
            }
            if (info.has_hops_away) {
                ss << "- **Hops Away**: " << (unsigned int) info.hops_away << "\n";
            }
            if (info.channel != 0) {
                ss << "- **Channel**: " << (unsigned int) info.channel << " ("
                   << _mon->getChannelName((uint8_t) info.channel) << ")\n";
            }
            ss << "- **Last Heard**: " << MeshMonShell::formatRelativeTime(info.last_heard) << "\n";
        }

        map<uint32_t, meshtastic_DeviceMetrics>::const_iterator dIt = dMap.find(targetId);
        if (dIt != dMap.end()) {
            const meshtastic_DeviceMetrics &dm = dIt->second;
            ss << "\n**Device Metrics:**\n";
            if (dm.has_battery_level) {
                if (dm.battery_level > 100) {
                    ss << "- Battery: Powered / USB\n";
                } else {
                    ss << "- Battery: " << (unsigned int) dm.battery_level << "%\n";
                }
            }
            if (dm.has_voltage) {
                ss << "- Voltage: " << fixed << setprecision(2) << dm.voltage << " V\n";
            }
            if (dm.has_channel_utilization) {
                ss << "- Channel Utilization: " << fixed << setprecision(2) << dm.channel_utilization << "%\n";
            }
            if (dm.has_air_util_tx) {
                ss << "- Air Util (Tx): " << fixed << setprecision(2) << dm.air_util_tx << "%\n";
            }
            if (dm.has_uptime_seconds) {
                unsigned int ut = dm.uptime_seconds;
                unsigned int days = ut / 86400;
                unsigned int hours = (ut % 86400) / 3600;
                unsigned int mins = (ut % 3600) / 60;
                ss << "- Uptime: " << days << "d " << hours << "h " << mins << "m\n";
            }
        }

        map<uint32_t, meshtastic_Position>::const_iterator pIt = pMap.find(targetId);
        if (pIt != pMap.end() && (pIt->second.latitude_i != 0 || pIt->second.longitude_i != 0)) {
            ss << "\n**Position:**\n";
            ss << "- Latitude: " << fixed << setprecision(7) << (pIt->second.latitude_i * 1e-7) << "\n";
            ss << "- Longitude: " << fixed << setprecision(7) << (pIt->second.longitude_i * 1e-7) << "\n";
            if (pIt->second.altitude != 0) {
                ss << "- Altitude: " << pIt->second.altitude << " m\n";
            }
        }

        auto aIt = autoNodes.find(targetId);
        if (aIt != autoNodes.end()) {
            const AutomationNode &an = aIt->second;
            ss << "\n**HomeMesh Automation State:**\n";
            ss << "- Device Type: `" << an.deviceType << "`\n";
            ss << "- Online: " << (an.online ? "yes" : "no") << "\n";
            if (an.lastRttMs > 0) {
                ss << "- Last RTT: " << an.lastRttMs << " ms (avg: " << an.avgRttMs << " ms)\n";
            }
            if (an.deviceType == "meshpump") {
                ss << "- Fish Pump: " << (an.fishPumpState ? "ON" : "OFF") << "\n";
                ss << "- Up Pump: " << (an.upPumpState ? "ON" : "OFF") << "\n";
            } else if (an.deviceType == "meshroof") {
                ss << "- PA Amplify: " << (an.amplifyState ? "ON" : "OFF") << "\n";
                ss << "- WiFi RSSI: " << an.wifiRssi << " dBm (" << an.wifiStatus << ")\n";
                ss << "- Roof CPU Temp: " << fixed << setprecision(1) << an.cpuTempC << " C\n";
            } else if (an.deviceType == "meshroom") {
                ss << "- AC Power: " << (an.acPower ? "ON" : "OFF")
                   << " (Target: " << an.acTargetTemp << " C, Mode: " << an.acMode << ")\n";
                ss << "- TV Power: " << (an.tvPower ? "ON" : "OFF") << "\n";
                ss << "- Room Temp: " << fixed << setprecision(1) << an.roomTempC << " C\n";
            }
        }

        return ss.str();
    }

    // List all nodes
    stringstream ss;
    char whoBuf[32];
    snprintf(whoBuf, sizeof(whoBuf), "!%08x", _mon->whoami());

    ss << "### Meshtastic Nodes Status (Total: " << nodes.size() << ")\n";
    ss << "Local Gateway Node: `" << whoBuf << "` (" << _mon->lookupShortName(_mon->whoami()) << ")\n\n";

    ss << "| Node ID | Hex ID | Short Name | Long Name | Role | Hardware | Battery | SNR | Hops | Last Heard |\n";
    ss << "|---|---|---|---|---|---|---|---|---|---|\n";

    for (map<uint32_t, meshtastic_NodeInfo>::const_iterator it = nodes.begin();
         it != nodes.end(); it++) {
        uint32_t nid = it->first;
        const meshtastic_NodeInfo &info = it->second;

        char hBuf[32];
        snprintf(hBuf, sizeof(hBuf), "!%08x", nid);

        string sName = _mon->lookupShortName(nid);
        string lName = _mon->lookupLongName(nid);
        string role = "UNKNOWN";
        string hw = "UNKNOWN";

        if (info.has_user) {
            role = MeshMonShell::roleString(info.user.role);
            hw = MeshMonShell::hardwareModelString(info.user.hw_model);
        }

        string batt = "—";
        auto dIt = dMap.find(nid);
        if (dIt != dMap.end() && dIt->second.has_battery_level) {
            if (dIt->second.battery_level > 100) {
                batt = "USB";
            } else {
                batt = to_string(dIt->second.battery_level) + "%";
            }
            if (dIt->second.has_voltage) {
                char vBuf[32];
                snprintf(vBuf, sizeof(vBuf), " (%.2fV)", dIt->second.voltage);
                batt += vBuf;
            }
        }

        string snr = "—";
        if (info.snr != 0.0f) {
            char snrBuf[32];
            snprintf(snrBuf, sizeof(snrBuf), "%.1f dB", info.snr);
            snr = snrBuf;
        }

        string hops = info.has_hops_away ? to_string(info.hops_away) : "—";
        string lh = MeshMonShell::formatRelativeTime(info.last_heard);

        ss << "| " << nid << " | `" << hBuf << "` | " << sName << " | "
           << lName << " | " << role << " | " << hw << " | " << batt
           << " | " << snr << " | " << hops << " | " << lh << " |\n";
    }

    if (!autoNodes.empty()) {
        ss << "\n### HomeMesh Automation Fleet (" << autoNodes.size() << " nodes)\n\n";
        ss << "| Hex ID | Short Name | Device Type | Online | RTT | State Summary |\n";
        ss << "|---|---|---|---|---|---|\n";
        for (map<uint32_t, AutomationNode>::const_iterator aIt = autoNodes.begin();
             aIt != autoNodes.end(); aIt++) {
            const AutomationNode &an = aIt->second;
            string stateSummary;
            if (an.deviceType == "meshpump") {
                stateSummary = string("Fish: ") + (an.fishPumpState ? "ON" : "OFF") +
                               ", Up: " + (an.upPumpState ? "ON" : "OFF");
            } else if (an.deviceType == "meshroof") {
                stateSummary = string("Amplify: ") + (an.amplifyState ? "ON" : "OFF") +
                               ", WiFi: " + to_string(an.wifiRssi) + " dBm";
            } else if (an.deviceType == "meshroom") {
                stateSummary = string("AC: ") + (an.acPower ? "ON" : "OFF") +
                               " (" + to_string((int)an.acTargetTemp) + "C), TV: " +
                               (an.tvPower ? "ON" : "OFF") + ", Room: " +
                               to_string((int)an.roomTempC) + "C";
            } else {
                stateSummary = "—";
            }
            ss << "| `" << an.nodeHex << "` | " << an.shortName << " | "
               << an.deviceType << " | " << (an.online ? "Online" : "Offline")
               << " | " << (an.lastRttMs > 0 ? to_string(an.lastRttMs) + " ms" : "—")
               << " | " << stateSummary << " |\n";
        }
    }

    return ss.str();
}

string AimonGatewayClient::toolQueryTelemetryHistory(const string &nodeIdArg,
                                                     const string &metric,
                                                     int hours, int limit)
{
    (void) metric;

    if (_db == nullptr) {
        return "Error: Database is disabled or unavailable.";
    }
    if (_mon == nullptr) {
        return "Error: Radio client is not attached.";
    }

    uint32_t targetId = MeshMonShell::resolveNode(_mon.get(), nodeIdArg);
    if (targetId == 0xffffffffU) {
        return "Error: Node '" + nodeIdArg + "' not found.";
    }

    if (hours <= 0) {
        hours = 24;
    }
    if ((limit <= 0) || (limit > 500)) {
        limit = 50;
    }

    time_t since = time(NULL) - (hours * 3600);
    string sql = "SELECT datetime(meshmon_time, 'unixepoch', 'localtime') AS timestamp, "
                 "metric_type, battery_level, voltage, channel_utilization, air_util_tx, "
                 "temperature, relative_humidity, barometric_pressure, uptime_seconds "
                 "FROM telemetry "
                 "WHERE node_id = " + to_string(targetId) + " "
                 "AND meshmon_time >= " + to_string(since) + " "
                 "ORDER BY meshmon_time DESC LIMIT " + to_string(limit) + ";";

    QueryResult qres;
    if (!_db->executeRawQuery(sql, qres) || qres.rows.empty()) {
        return "No telemetry records found for node " + nodeIdArg + " in the last " +
               to_string(hours) + " hours.";
    }

    char hexBuf[32];
    snprintf(hexBuf, sizeof(hexBuf), "!%08x", targetId);
    string shortName = _mon->lookupShortName(targetId);

    stringstream ss;
    ss << "### Telemetry History for " << shortName << " (" << hexBuf << ")\n";
    ss << "Analysis Window: Last " << hours << " Hours | Records Returned: " << qres.rows.size() << "\n\n";

    ss << "| Timestamp | Metric Type | Battery | Voltage | Chan Util | Air Util (Tx) | Temp | Humidity | Pressure | Uptime |\n";
    ss << "|---|---|---|---|---|---|---|---|---|---|\n";

    for (size_t r = 0; r < qres.rows.size(); r++) {
        const vector<string> &row = qres.rows[r];
        string ts = (row.size() > 0) ? row[0] : "—";
        string mtype = (row.size() > 1) ? row[1] : "—";
        string batt = (row.size() > 2 && row[2] != "NULL") ? (row[2] + "%") : "—";
        string volt = (row.size() > 3 && row[3] != "NULL") ? (row[3] + " V") : "—";
        string cUtil = (row.size() > 4 && row[4] != "NULL") ? (row[4] + "%") : "—";
        string aUtil = (row.size() > 5 && row[5] != "NULL") ? (row[5] + "%") : "—";
        string temp = (row.size() > 6 && row[6] != "NULL") ? (row[6] + " C") : "—";
        string hum = (row.size() > 7 && row[7] != "NULL") ? (row[7] + "%") : "—";
        string press = (row.size() > 8 && row[8] != "NULL") ? (row[8] + " hPa") : "—";
        string uptime = (row.size() > 9 && row[9] != "NULL") ? (row[9] + " s") : "—";

        ss << "| " << ts << " | " << mtype << " | " << batt << " | " << volt
           << " | " << cUtil << " | " << aUtil << " | " << temp << " | "
           << hum << " | " << press << " | " << uptime << " |\n";
    }

    return ss.str();
}

string AimonGatewayClient::toolGetRfAnalytics(int hours)
{
    if (_db == nullptr) {
        return "Error: Database is disabled or unavailable.";
    }

    if (hours <= 0) {
        hours = 24;
    }

    time_t since = time(NULL) - (hours * 3600);

    TrafficSummary summary;
    _db->getTrafficSummary(since, summary);

    float directPct = 0.0f, bcastPct = 0.0f, avgHops = 0.0f;
    _db->getTrafficRatios(since, directPct, bcastPct, avgHops);

    vector<CriticalRepeaterStat> repeaters;
    _db->getCriticalRepeaters(since, 5, repeaters);

    vector<EchoStormStat> echoStorms;
    _db->getEchoStorms(since, 5, echoStorms);

    vector<NodeTrafficStat> topTalkers;
    _db->getTopTalkers(since, 5, topTalkers);

    ChannelHealthStat health;
    _db->getChannelHealth(since, health);

    stringstream ss;
    ss << "### Mesh RF Network Analytics (Last " << hours << " Hours)\n\n";

    ss << "#### Traffic & Channel Summary\n";
    ss << "- **Total Packets**: " << summary.totalPackets << " (" << summary.totalBytes << " payload bytes)\n";
    ss << "- **Broadcast / Unicast**: " << fixed << setprecision(1) << bcastPct << "% broadcast ("
       << summary.broadcastPackets << "), " << (100.0f - bcastPct) << "% unicast ("
       << summary.unicastPackets << ")\n";
    ss << "- **Direct / Relayed**: " << fixed << setprecision(1) << directPct << "% direct ("
       << summary.directPackets << "), " << (100.0f - directPct) << "% relayed ("
       << summary.relayedPackets << ")\n";
    ss << "- **Average Hops**: " << fixed << setprecision(2) << avgHops << "\n";
    ss << "- **Duplicate Packets**: " << health.duplicatePackets << "\n";
    ss << "- **Channel Utilization**: Avg: " << fixed << setprecision(2) << health.avgChannelUtil
       << "%, Max: " << health.maxChannelUtil << "%\n";
    ss << "- **Air Util (Tx)**: Avg: " << fixed << setprecision(2) << health.avgAirUtilTx
       << "%, Max: " << health.maxAirUtilTx << "%\n\n";

    if (!repeaters.empty()) {
        ss << "#### Critical Relays & Repeaters\n";
        ss << "| Repeater Hex | Name | Relayed Packets | Avg SNR |\n";
        ss << "|---|---|---|---|\n";
        for (size_t i = 0; i < repeaters.size(); i++) {
            const CriticalRepeaterStat &cr = repeaters[i];
            ss << "| `" << cr.repeaterHex << "` | " << cr.shortName << " | "
               << cr.relayCount << " | " << fixed << setprecision(1) << cr.avgSnr << " dB |\n";
        }
        ss << "\n";
    }

    if (!echoStorms.empty()) {
        ss << "#### Echo Storms & High-Duplication Events\n";
        ss << "| Origin Hex | Echo Multiplier | Hops Range | Duration | First Arrival |\n";
        ss << "|---|---|---|---|---|\n";
        for (size_t i = 0; i < echoStorms.size(); i++) {
            const EchoStormStat &es = echoStorms[i];
            ss << "| `" << es.fromHex << "` | " << es.echoCount << "x | "
               << es.minHops << " - " << es.maxHops << " hops | "
               << es.durationSec << "s | "
               << MeshMonShell::formatRelativeTime(es.firstArrival) << " |\n";
        }
        ss << "\n";
    }

    if (!topTalkers.empty()) {
        ss << "#### Top Mesh Talkers\n";
        ss << "| Node Hex | Short Name | Packets Sent | Total Bytes | Avg SNR | Avg Hops | Last Seen |\n";
        ss << "|---|---|---|---|---|---|---|\n";
        for (size_t i = 0; i < topTalkers.size(); i++) {
            const NodeTrafficStat &tt = topTalkers[i];
            ss << "| `" << tt.nodeHex << "` | " << tt.shortName << " | "
               << tt.packetCount << " | " << tt.totalBytes << " | "
               << fixed << setprecision(1) << tt.avgSnr << " dB | "
               << fixed << setprecision(2) << tt.avgHops << " | "
               << MeshMonShell::formatRelativeTime(tt.lastSeen) << " |\n";
        }
    }

    return ss.str();
}

string AimonGatewayClient::toolSendMessage(const string &text,
                                           const string &destination,
                                           int channel)
{
    if (_mon == nullptr) {
        return "Error: Radio client is not attached or available.";
    }
    if (text.empty()) {
        return "Error: Text message cannot be empty.";
    }

    uint32_t dest = 0xffffffffU;
    if (!destination.empty() && (destination != "^all") && (destination != "broadcast")) {
        dest = MeshMonShell::resolveNode(_mon.get(), destination);
        if (dest == 0xffffffffU) {
            return "Error: Destination '" + destination + "' could not be resolved.";
        }
    }

    bool ok = _mon->textMessage(dest, static_cast<uint8_t>(channel), text);
    if (ok) {
        char destBuf[32];
        if (dest == 0xffffffffU) {
            snprintf(destBuf, sizeof(destBuf), "broadcast (^all)");
        } else {
            snprintf(destBuf, sizeof(destBuf), "!%08x", dest);
        }
        return "Message queued for transmission across mesh to `" + string(destBuf) +
               "` on channel " + to_string(channel) + ":\n> " + text;
    } else {
        return "Error: Radio client failed to transmit message.";
    }
}

string AimonGatewayClient::toolQueryDb(const string &sql)
{
    if (_db == nullptr) {
        return "Error: Database is not available.";
    }

    size_t start = sql.find_first_not_of(" \t\r\n");
    if (start == string::npos) {
        return "Error: SQL query cannot be empty.";
    }

    size_t endWord = sql.find_first_of(" \t\r\n;", start);
    string firstWord = sql.substr(start, (endWord == string::npos) ? string::npos : (endWord - start));
    for (char &c : firstWord) {
        c = (char) toupper(c);
    }
    if (firstWord != "SELECT" && firstWord != "WITH" && firstWord != "EXPLAIN") {
        return "Error: Only read-only queries (SELECT or WITH) are permitted.";
    }

    size_t semi = sql.find(';');
    if (semi != string::npos) {
        size_t afterSemi = sql.find_first_not_of(" \t\r\n", semi + 1);
        if (afterSemi != string::npos) {
            return "Error: Multiple semicolon-separated SQL statements are not permitted.";
        }
    }

    QueryResult res;
    if (!_db->executeRawQuery(sql, res)) {
        return "SQL Error: " + res.error;
    }

    if (res.rows.empty()) {
        return "Query executed successfully. 0 rows returned.";
    }

    ostringstream ss;
    ss << "|";
    for (const string &col : res.columns) {
        ss << " " << col << " |";
    }
    ss << "\n|";
    for (size_t i = 0; i < res.columns.size(); i++) {
        ss << "---|";
    }
    ss << "\n";

    size_t displayCount = min(res.rows.size(), (size_t) 100);
    for (size_t r = 0; r < displayCount; r++) {
        ss << "|";
        for (const string &val : res.rows[r]) {
            ss << " " << val << " |";
        }
        ss << "\n";
    }

    if (res.rows.size() > displayCount) {
        ss << "\n*(Showing first " << displayCount << " of " << res.rows.size() << " total rows)*\n";
    }

    return ss.str();
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double haversineMeters(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371000.0; // Earth radius in meters
    double phi1 = lat1 * M_PI / 180.0;
    double phi2 = lat2 * M_PI / 180.0;
    double dphi = (lat2 - lat1) * M_PI / 180.0;
    double dlam = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dphi / 2.0) * sin(dphi / 2.0) +
               cos(phi1) * cos(phi2) * sin(dlam / 2.0) * sin(dlam / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return R * c;
}

struct SpatialNodeInfo {
    uint32_t nodeId;
    string shortName;
    string longName;
    double lat;
    double lon;
    int alt;
    time_t lastSeen;
    double distMeters;
};

string AimonGatewayClient::toolGetSpatialAnalytics(int premiseThresholdM, int topFarthest, int hours)
{
    if (_db == nullptr) {
        return "Error: Database is not available.";
    }

    if (premiseThresholdM <= 0) {
        premiseThresholdM = 50;
    }
    if (topFarthest <= 0) {
        topFarthest = 10;
    }

    // Determine reference coordinates (gateway / base station)
    double refLat = 24.82176;
    double refLon = 121.2383232;
    string refName = "Gateway Reference";

    if (_mon != nullptr) {
        uint32_t myNode = _mon->whoami();
        if (myNode != 0) {
            char myHex[32];
            snprintf(myHex, sizeof(myHex), "!%08x", myNode);
            refName = string(myHex) + " (" + _mon->lookupShortName(myNode) + ")";
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
        "WHERE p.latitude != 0 AND p.longitude != 0 ";
    if (hours > 0) {
        sql += "AND p.meshmon_time >= strftime('%s', 'now') - " + to_string(hours * 3600) + " ";
    }
    sql += "GROUP BY p.node_id;";

    QueryResult res;
    if (!_db->executeRawQuery(sql, res)) {
        return "SQL Error: " + res.error;
    }

    vector<SpatialNodeInfo> allNodes;
    vector<SpatialNodeInfo> onPremise;
    vector<SpatialNodeInfo> offPremise;

    for (const auto &row : res.rows) {
        if (row.size() < 7) continue;
        SpatialNodeInfo sn;
        sn.nodeId = (uint32_t) strtoul(row[0].c_str(), nullptr, 10);
        sn.shortName = (row[1] != "NULL" && !row[1].empty()) ? row[1] : "-";
        sn.longName = (row[2] != "NULL" && !row[2].empty()) ? row[2] : "-";
        sn.lat = strtod(row[3].c_str(), nullptr);
        sn.lon = strtod(row[4].c_str(), nullptr);
        sn.alt = (int) strtol(row[5].c_str(), nullptr, 10);
        sn.lastSeen = (time_t) strtoul(row[6].c_str(), nullptr, 10);

        sn.distMeters = haversineMeters(refLat, refLon, sn.lat, sn.lon);
        allNodes.push_back(sn);

        if (sn.distMeters <= premiseThresholdM) {
            onPremise.push_back(sn);
        } else {
            offPremise.push_back(sn);
        }
    }

    if (allNodes.empty()) {
        return "No nodes with valid GPS position coordinates found.";
    }

    // Sort all nodes descending by distance
    sort(allNodes.begin(), allNodes.end(), [](const SpatialNodeInfo &a, const SpatialNodeInfo &b) {
        return a.distMeters > b.distMeters;
    });

    // Sort offPremise ascending by distance
    sort(offPremise.begin(), offPremise.end(), [](const SpatialNodeInfo &a, const SpatialNodeInfo &b) {
        return a.distMeters < b.distMeters;
    });

    ostringstream ss;
    ss << "### Spatial & Geographic Analytics\n\n";
    ss << "* **Reference Origin**: " << refName << " at (" << fixed << setprecision(5) << refLat << ", " << refLon << ")\n";
    ss << "* **Premise Boundary Radius**: " << premiseThresholdM << " meters\n";
    if (hours > 0) {
        ss << "* **Time Window**: Last " << hours << " hours\n";
    } else {
        ss << "* **Time Window**: All-time history\n";
    }
    ss << "\n";

    double onPct = (100.0 * onPremise.size()) / allNodes.size();
    double offPct = (100.0 * offPremise.size()) / allNodes.size();

    ss << "| Metric | Count | Percentage |\n";
    ss << "| :--- | :--- | :--- |\n";
    ss << "| **Total Nodes with GPS** | **" << allNodes.size() << "** | 100.0% |\n";
    ss << "| **On-Premise (<= " << premiseThresholdM << "m)** | **" << onPremise.size() << "** | "
       << fixed << setprecision(1) << onPct << "% |\n";
    ss << "| **Off-Premise (> " << premiseThresholdM << "m)** | **" << offPremise.size() << "** | "
       << fixed << setprecision(1) << offPct << "% |\n\n";

    if (!allNodes.empty()) {
        const auto &farthest = allNodes.front();
        char hexBuf[32];
        snprintf(hexBuf, sizeof(hexBuf), "!%08x", farthest.nodeId);
        ss << "#### Farthest Node Ever Heard\n";
        ss << "* **Node**: `" << hexBuf << "` (" << farthest.shortName << " / " << farthest.longName << ")\n";
        ss << "* **Distance**: **" << fixed << setprecision(2) << (farthest.distMeters / 1000.0) << " km** ("
           << fixed << setprecision(0) << farthest.distMeters << " m)\n";
        ss << "* **Coordinates**: (" << fixed << setprecision(5) << farthest.lat << ", " << farthest.lon
           << "), Altitude: " << farthest.alt << " m\n";
        ss << "* **Last Heard**: " << MeshMonShell::formatRelativeTime(farthest.lastSeen) << "\n\n";
    }

    ss << "#### Top " << topFarthest << " Farthest Nodes\n\n";
    ss << "| Rank | Distance | Node ID | Short Name | Long Name | Coordinates | Alt | Last Heard |\n";
    ss << "|---|---|---|---|---|---|---|---|\n";
    size_t showFarthest = min((size_t) topFarthest, allNodes.size());
    for (size_t i = 0; i < showFarthest; i++) {
        const auto &n = allNodes[i];
        char hexBuf[32];
        snprintf(hexBuf, sizeof(hexBuf), "!%08x", n.nodeId);
        ss << "| #" << (i + 1) << " | "
           << fixed << setprecision(2) << (n.distMeters / 1000.0) << " km | `"
           << hexBuf << "` | " << n.shortName << " | " << n.longName << " | ("
           << fixed << setprecision(4) << n.lat << ", " << n.lon << ") | "
           << n.alt << "m | " << MeshMonShell::formatRelativeTime(n.lastSeen) << " |\n";
    }
    ss << "\n";

    if (!onPremise.empty()) {
        ss << "#### On-Premise Nodes (<= " << premiseThresholdM << "m)\n\n";
        ss << "| Distance | Node ID | Short Name | Long Name | Alt | Last Heard |\n";
        ss << "|---|---|---|---|---|---|\n";
        for (const auto &n : onPremise) {
            char hexBuf[32];
            snprintf(hexBuf, sizeof(hexBuf), "!%08x", n.nodeId);
            ss << "| " << fixed << setprecision(1) << n.distMeters << " m | `"
               << hexBuf << "` | " << n.shortName << " | " << n.longName << " | "
               << n.alt << "m | " << MeshMonShell::formatRelativeTime(n.lastSeen) << " |\n";
        }
    }

    return ss.str();
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
