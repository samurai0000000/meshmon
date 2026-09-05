/*
 * MeshMonDb.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include "MeshMonDb.hxx"

using namespace std;

string MeshMonDb::formatNodeHex(uint32_t nodeId)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "!%08x", nodeId);
    return string(buf);
}

string MeshMonDb::portnumToString(int portnum)
{
    switch (portnum) {
    case meshtastic_PortNum_UNKNOWN_APP:
        return "UNKNOWN_APP";
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
        return "TEXT_MESSAGE_APP";
    case meshtastic_PortNum_REMOTE_HARDWARE_APP:
        return "REMOTE_HARDWARE_APP";
    case meshtastic_PortNum_POSITION_APP:
        return "POSITION_APP";
    case meshtastic_PortNum_NODEINFO_APP:
        return "NODEINFO_APP";
    case meshtastic_PortNum_ROUTING_APP:
        return "ROUTING_APP";
    case meshtastic_PortNum_ADMIN_APP:
        return "ADMIN_APP";
    case meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP:
        return "TEXT_MESSAGE_COMPRESSED_APP";
    case meshtastic_PortNum_WAYPOINT_APP:
        return "WAYPOINT_APP";
    case meshtastic_PortNum_AUDIO_APP:
        return "AUDIO_APP";
    case meshtastic_PortNum_DETECTION_SENSOR_APP:
        return "DETECTION_SENSOR_APP";
    case meshtastic_PortNum_REPLY_APP:
        return "REPLY_APP";
    case meshtastic_PortNum_IP_TUNNEL_APP:
        return "IP_TUNNEL_APP";
    case meshtastic_PortNum_PAXCOUNTER_APP:
        return "PAXCOUNTER_APP";
    case meshtastic_PortNum_SERIAL_APP:
        return "SERIAL_APP";
    case meshtastic_PortNum_STORE_FORWARD_APP:
        return "STORE_FORWARD_APP";
    case meshtastic_PortNum_RANGE_TEST_APP:
        return "RANGE_TEST_APP";
    case meshtastic_PortNum_TELEMETRY_APP:
        return "TELEMETRY_APP";
    case meshtastic_PortNum_ZPS_APP:
        return "ZPS_APP";
    case meshtastic_PortNum_SIMULATOR_APP:
        return "SIMULATOR_APP";
    case meshtastic_PortNum_TRACEROUTE_APP:
        return "TRACEROUTE_APP";
    case meshtastic_PortNum_NEIGHBORINFO_APP:
        return "NEIGHBORINFO_APP";
    case meshtastic_PortNum_MAP_REPORT_APP:
        return "MAP_REPORT_APP";
    case meshtastic_PortNum_POWERSTRESS_APP:
        return "POWERSTRESS_APP";
    case meshtastic_PortNum_PRIVATE_APP:
        return "PRIVATE_APP";
    case meshtastic_PortNum_ATAK_FORWARDER:
        return "ATAK_FORWARDER";
    case meshtastic_PortNum_MAX:
        return "MAX";
    default:
        break;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "PORTNUM_%d", portnum);
    return string(buf);
}

MeshMonDb::MeshMonDb(const string &dbPath)
    : _db(NULL), _running(false), _stopRequested(false)
{
    if (!dbPath.empty()) {
        open(dbPath);
    }
}

MeshMonDb::~MeshMonDb()
{
    stop();
    join();
    close();
}

bool MeshMonDb::open(const string &dbPath)
{
    lock_guard<mutex> lock(_dbMutex);

    if (_db != NULL) {
        sqlite3_close(_db);
        _db = NULL;
    }

    _dbPath = dbPath;
    int rc = sqlite3_open(_dbPath.c_str(), &_db);
    if (rc != SQLITE_OK) {
        cerr << "MeshMonDb: Failed to open " << _dbPath << ": "
             << sqlite3_errmsg(_db) << endl;
        if (_db != NULL) {
            sqlite3_close(_db);
            _db = NULL;
        }
        return false;
    }

    // Enable WAL mode & normal synchronous for high-throughput non-blocking writes
    char *err = NULL;
    sqlite3_exec(_db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err);
    if (err != NULL) {
        sqlite3_free(err);
        err = NULL;
    }
    sqlite3_exec(_db, "PRAGMA synchronous=NORMAL;", NULL, NULL, &err);
    if (err != NULL) {
        sqlite3_free(err);
        err = NULL;
    }
    sqlite3_exec(_db, "PRAGMA foreign_keys=ON;", NULL, NULL, &err);
    if (err != NULL) {
        sqlite3_free(err);
        err = NULL;
    }

    return initSchema();
}

void MeshMonDb::close(void)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db != NULL) {
        sqlite3_close(_db);
        _db = NULL;
    }
}

const string &MeshMonDb::getDbPath(void) const
{
    return _dbPath;
}

size_t MeshMonDb::getDbFileSize(void) const
{
    if (_dbPath.empty()) {
        return 0;
    }

    struct stat st;
    if (stat(_dbPath.c_str(), &st) == 0) {
        return (size_t) st.st_size;
    }

    return 0;
}

uint64_t MeshMonDb::getTotalPacketCount(void)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return 0;
    }

    const char *sql = "SELECT count(*) FROM packets;";
    sqlite3_stmt *stmt = NULL;
    uint64_t count = 0;

    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = (uint64_t) sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return count;
}

uint32_t MeshMonDb::getTotalNodeCount(void)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return 0;
    }

    const char *sql = "SELECT count(*) FROM nodes;";
    sqlite3_stmt *stmt = NULL;
    uint32_t count = 0;

    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = (uint32_t) sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return count;
}

uint64_t MeshMonDb::getTotalPayloadBytes(void)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return 0;
    }

    const char *sql = "SELECT coalesce(sum(payload_size), 0) FROM packets;";
    sqlite3_stmt *stmt = NULL;
    uint64_t bytes = 0;

    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            bytes = (uint64_t) sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return bytes;
}

uint64_t MeshMonDb::getTotalTextMessageCount(void)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return 0;
    }

    const char *sql = "SELECT count(*) FROM messages;";
    sqlite3_stmt *stmt = NULL;
    uint64_t count = 0;

    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = (uint64_t) sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return count;
}

bool MeshMonDb::initSchema(void)
{
    if (_db == NULL) {
        return false;
    }

    const char *ddl =
        "CREATE TABLE IF NOT EXISTS nodes ("
        "  node_id INTEGER PRIMARY KEY,"
        "  node_hex TEXT NOT NULL,"
        "  long_name TEXT,"
        "  short_name TEXT,"
        "  hw_model INTEGER,"
        "  role INTEGER,"
        "  first_seen INTEGER NOT NULL,"
        "  last_seen INTEGER NOT NULL,"
        "  last_rssi REAL,"
        "  last_snr REAL,"
        "  last_hops INTEGER"
        ");"
        "CREATE TABLE IF NOT EXISTS packets ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  meshmon_time INTEGER NOT NULL,"
        "  rx_time INTEGER,"
        "  packet_id INTEGER NOT NULL,"
        "  from_node INTEGER NOT NULL,"
        "  to_node INTEGER NOT NULL,"
        "  channel INTEGER NOT NULL,"
        "  rx_rssi REAL,"
        "  rx_snr REAL,"
        "  hop_start INTEGER,"
        "  hop_limit INTEGER,"
        "  hops INTEGER,"
        "  portnum INTEGER,"
        "  payload_variant INTEGER,"
        "  payload_size INTEGER,"
        "  want_ack INTEGER,"
        "  via_mqtt INTEGER,"
        "  payload BLOB,"
        "  FOREIGN KEY(from_node) REFERENCES nodes(node_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_packets_meshmon_time ON packets(meshmon_time);"
        "CREATE INDEX IF NOT EXISTS idx_packets_pkt_from ON packets(packet_id, from_node);"
        "CREATE INDEX IF NOT EXISTS idx_packets_from ON packets(from_node);"
        "CREATE INDEX IF NOT EXISTS idx_packets_portnum ON packets(portnum);"
        "CREATE INDEX IF NOT EXISTS idx_packets_hops ON packets(hops);"
        "CREATE TABLE IF NOT EXISTS telemetry ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  meshmon_time INTEGER NOT NULL,"
        "  node_id INTEGER NOT NULL,"
        "  metric_type TEXT NOT NULL,"
        "  battery_level INTEGER,"
        "  voltage REAL,"
        "  channel_utilization REAL,"
        "  air_util_tx REAL,"
        "  temperature REAL,"
        "  relative_humidity REAL,"
        "  barometric_pressure REAL,"
        "  ch1_voltage REAL,"
        "  ch1_current REAL,"
        "  uptime_seconds INTEGER,"
        "  FOREIGN KEY(node_id) REFERENCES nodes(node_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_telemetry_node_time ON telemetry(node_id, meshmon_time);"
        "CREATE TABLE IF NOT EXISTS positions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  meshmon_time INTEGER NOT NULL,"
        "  node_id INTEGER NOT NULL,"
        "  latitude REAL NOT NULL,"
        "  longitude REAL NOT NULL,"
        "  altitude INTEGER,"
        "  ground_speed INTEGER,"
        "  ground_track INTEGER,"
        "  sats_in_view INTEGER,"
        "  FOREIGN KEY(node_id) REFERENCES nodes(node_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_positions_node_time ON positions(node_id, meshmon_time);"
        "CREATE TABLE IF NOT EXISTS text_messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  meshmon_time INTEGER NOT NULL,"
        "  from_node INTEGER NOT NULL,"
        "  to_node INTEGER NOT NULL,"
        "  channel INTEGER NOT NULL,"
        "  message TEXT NOT NULL,"
        "  FOREIGN KEY(from_node) REFERENCES nodes(node_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS traceroutes ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  meshmon_time INTEGER NOT NULL,"
        "  from_node INTEGER NOT NULL,"
        "  to_node INTEGER NOT NULL,"
        "  route_count INTEGER NOT NULL,"
        "  route_nodes TEXT NOT NULL,"
        "  route_snrs TEXT NOT NULL,"
        "  FOREIGN KEY(from_node) REFERENCES nodes(node_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS automation_events ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  meshmon_time INTEGER NOT NULL,"
        "  node_id INTEGER NOT NULL,"
        "  node_hex TEXT NOT NULL,"
        "  device_type TEXT NOT NULL,"
        "  direction TEXT NOT NULL,"
        "  subsystem TEXT NOT NULL,"
        "  command_name TEXT NOT NULL,"
        "  action_param TEXT,"
        "  status TEXT NOT NULL,"
        "  initiator TEXT NOT NULL,"
        "  rtt_ms INTEGER DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_auto_events_node ON automation_events(node_id, meshmon_time);"
        "CREATE INDEX IF NOT EXISTS idx_auto_events_dev ON automation_events(device_type, meshmon_time);"
        "CREATE INDEX IF NOT EXISTS idx_auto_events_sub ON automation_events(subsystem, meshmon_time);";

    char *err = NULL;
    int rc = sqlite3_exec(_db, ddl, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        cerr << "MeshMonDb: initSchema failed: " << (err ? err : "unknown") << endl;
        if (err != NULL) {
            sqlite3_free(err);
        }
        return false;
    }

    // Migration for existing tables: ensure rtt_ms column exists
    sqlite3_exec(_db, "ALTER TABLE automation_events ADD COLUMN rtt_ms INTEGER DEFAULT 0;", NULL, NULL, NULL);

    return true;
}

bool MeshMonDb::start(void)
{
    if (_running) {
        return true;
    }

    _stopRequested = false;
    _running = true;
    _workerThread = thread(&MeshMonDb::workerLoop, this);
    return true;
}

void MeshMonDb::stop(void)
{
    _stopRequested = true;
    _queueCv.notify_all();
}

void MeshMonDb::join(void)
{
    if (_workerThread.joinable()) {
        _workerThread.join();
    }
    _running = false;
}

bool MeshMonDb::isRunning(void) const
{
    return _running;
}

void MeshMonDb::enqueuePacket(const meshtastic_MeshPacket &packet, time_t meshmonTime)
{
    DbEvent ev;
    ev.type = EV_PACKET;
    ev.meshmonTime = meshmonTime;
    ev.rxTime = packet.rx_time;
    ev.packetId = packet.id;
    ev.fromNode = packet.from;
    ev.toNode = packet.to;
    ev.channel = packet.channel;
    ev.rxRssi = packet.rx_rssi;
    ev.rxSnr = packet.rx_snr;
    ev.hopStart = packet.hop_start;
    ev.hopLimit = packet.hop_limit;
    ev.hops = packet.hop_start - packet.hop_limit;
    if (ev.hops < 0) {
        ev.hops = 0;
    }
    ev.portnum = (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) ?
        packet.decoded.portnum : 0;
    ev.payloadVariant = packet.which_payload_variant;
    ev.wantAck = packet.want_ack;
    ev.viaMqtt = packet.via_mqtt;

    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        ev.payload.assign(packet.decoded.payload.bytes,
                          packet.decoded.payload.bytes + packet.decoded.payload.size);
    } else if (packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
        ev.payload.assign(packet.encrypted.bytes,
                          packet.encrypted.bytes + packet.encrypted.size);
    }

    {
        lock_guard<mutex> lock(_queueMutex);
        _queue.push(ev);
    }
    _queueCv.notify_one();
}

void MeshMonDb::enqueueNodeInfo(uint32_t nodeId, const string &longName,
                                const string &shortName, int hwModel, int role,
                                time_t meshmonTime)
{
    DbEvent ev;
    bzero(&ev, sizeof(ev));
    ev.type = EV_NODE_INFO;
    ev.meshmonTime = meshmonTime;
    ev.fromNode = nodeId;
    ev.longName = longName;
    ev.shortName = shortName;
    ev.hwModel = hwModel;
    ev.role = role;

    {
        lock_guard<mutex> lock(_queueMutex);
        _queue.push(ev);
    }
    _queueCv.notify_one();
}

void MeshMonDb::enqueueDeviceMetrics(const meshtastic_MeshPacket &packet,
                                    const meshtastic_DeviceMetrics &metrics,
                                    time_t meshmonTime)
{
    DbEvent ev;
    bzero(&ev, sizeof(ev));
    ev.type = EV_DEVICE_METRICS;
    ev.meshmonTime = meshmonTime;
    ev.fromNode = packet.from;
    ev.metricType = "device";
    ev.hasBattery = metrics.has_battery_level;
    ev.batteryLevel = metrics.battery_level;
    ev.hasVoltage = metrics.has_voltage;
    ev.voltage = metrics.voltage;
    ev.hasChannelUtil = metrics.has_channel_utilization;
    ev.channelUtilization = metrics.channel_utilization;
    ev.hasAirUtilTx = metrics.has_air_util_tx;
    ev.airUtilTx = metrics.air_util_tx;
    ev.uptimeSeconds = metrics.has_uptime_seconds ? metrics.uptime_seconds : 0;

    {
        lock_guard<mutex> lock(_queueMutex);
        _queue.push(ev);
    }
    _queueCv.notify_one();
}

void MeshMonDb::enqueueEnvironmentMetrics(const meshtastic_MeshPacket &packet,
                                         const meshtastic_EnvironmentMetrics &metrics,
                                         time_t meshmonTime)
{
    DbEvent ev;
    bzero(&ev, sizeof(ev));
    ev.type = EV_ENV_METRICS;
    ev.meshmonTime = meshmonTime;
    ev.fromNode = packet.from;
    ev.metricType = "env";
    ev.hasTemp = metrics.has_temperature;
    ev.temperature = metrics.temperature;
    ev.hasHum = metrics.has_relative_humidity;
    ev.humidity = metrics.relative_humidity;
    ev.hasPress = metrics.has_barometric_pressure;
    ev.pressure = metrics.barometric_pressure;

    {
        lock_guard<mutex> lock(_queueMutex);
        _queue.push(ev);
    }
    _queueCv.notify_one();
}

void MeshMonDb::enqueuePowerMetrics(const meshtastic_MeshPacket &packet,
                                   const meshtastic_PowerMetrics &metrics,
                                   time_t meshmonTime)
{
    DbEvent ev;
    bzero(&ev, sizeof(ev));
    ev.type = EV_POWER_METRICS;
    ev.meshmonTime = meshmonTime;
    ev.fromNode = packet.from;
    ev.metricType = "power";
    ev.hasCh1Volt = metrics.has_ch1_voltage;
    ev.ch1Voltage = metrics.ch1_voltage;
    ev.hasCh1Curr = metrics.has_ch1_current;
    ev.ch1Current = metrics.ch1_current;

    {
        lock_guard<mutex> lock(_queueMutex);
        _queue.push(ev);
    }
    _queueCv.notify_one();
}

void MeshMonDb::enqueueLocalStats(const meshtastic_MeshPacket &packet,
                                 const meshtastic_LocalStats &stats,
                                 time_t meshmonTime)
{
    DbEvent ev;
    bzero(&ev, sizeof(ev));
    ev.type = EV_LOCAL_STATS;
    ev.meshmonTime = meshmonTime;
    ev.fromNode = packet.from;
    ev.metricType = "stats";
    ev.hasChannelUtil = true;
    ev.channelUtilization = stats.channel_utilization;
    ev.hasAirUtilTx = true;
    ev.airUtilTx = stats.air_util_tx;
    ev.uptimeSeconds = stats.uptime_seconds;

    {
        lock_guard<mutex> lock(_queueMutex);
        _queue.push(ev);
    }
    _queueCv.notify_one();
}

void MeshMonDb::enqueuePosition(const meshtastic_MeshPacket &packet,
                               const meshtastic_Position &pos,
                               time_t meshmonTime)
{
    DbEvent ev;
    bzero(&ev, sizeof(ev));
    ev.type = EV_POSITION;
    ev.meshmonTime = meshmonTime;
    ev.fromNode = packet.from;
    ev.latitude = ((double) pos.latitude_i) / 1e7;
    ev.longitude = ((double) pos.longitude_i) / 1e7;
    ev.altitude = pos.altitude;
    ev.groundSpeed = pos.ground_speed;
    ev.groundTrack = pos.ground_track;
    ev.satsInView = pos.sats_in_view;

    {
        lock_guard<mutex> lock(_queueMutex);
        _queue.push(ev);
    }
    _queueCv.notify_one();
}

void MeshMonDb::enqueueTextMessage(const meshtastic_MeshPacket &packet,
                                  const string &message,
                                  time_t meshmonTime)
{
    DbEvent ev;
    bzero(&ev, sizeof(ev));
    ev.type = EV_TEXT_MESSAGE;
    ev.meshmonTime = meshmonTime;
    ev.fromNode = packet.from;
    ev.toNode = packet.to;
    ev.channel = packet.channel;
    ev.text = message;

    {
        lock_guard<mutex> lock(_queueMutex);
        _queue.push(ev);
    }
    _queueCv.notify_one();
}

void MeshMonDb::enqueueTraceRoute(const meshtastic_MeshPacket &packet,
                                 const meshtastic_RouteDiscovery &routeDiscovery,
                                 time_t meshmonTime)
{
    DbEvent ev;
    bzero(&ev, sizeof(ev));
    ev.type = EV_TRACEROUTE;
    ev.meshmonTime = meshmonTime;
    ev.fromNode = packet.from;
    ev.toNode = packet.to;
    ev.routeCount = routeDiscovery.route_count;
    for (unsigned int i = 0; i < routeDiscovery.route_count; i++) {
        ev.routeNodes.push_back(routeDiscovery.route[i]);
        if (routeDiscovery.snr_towards[i] != INT8_MIN) {
            ev.routeSnrs.push_back(((float) routeDiscovery.snr_towards[i]) / 4.0f);
        } else {
            ev.routeSnrs.push_back(-128.0f);
        }
    }

    {
        lock_guard<mutex> lock(_queueMutex);
        _queue.push(ev);
    }
    _queueCv.notify_one();
}

void MeshMonDb::enqueueAutomationEvent(time_t meshmonTime, uint32_t nodeId,
                                       const string &deviceType, const string &direction,
                                       const string &subsystem, const string &commandName,
                                       const string &actionParam, const string &status,
                                       const string &initiator, int32_t rttMs)
{
    DbEvent ev;
    ev.type = EV_AUTOMATION_EVENT;
    ev.meshmonTime = meshmonTime;
    ev.fromNode = nodeId;
    ev.deviceType = deviceType;
    ev.direction = direction;
    ev.subsystem = subsystem;
    ev.commandName = commandName;
    ev.actionParam = actionParam;
    ev.status = status;
    ev.initiator = initiator;
    ev.rttMs = rttMs;

    {
        lock_guard<mutex> lock(_queueMutex);
        _queue.push(ev);
    }
    _queueCv.notify_one();
}

void MeshMonDb::workerLoop(void)
{
    while (!_stopRequested) {
        DbEvent ev;
        {
            unique_lock<mutex> lock(_queueMutex);
            _queueCv.wait(lock, [this] {
                return !_queue.empty() || _stopRequested;
            });

            if (_stopRequested && _queue.empty()) {
                break;
            }

            if (_queue.empty()) {
                continue;
            }

            ev = _queue.front();
            _queue.pop();
        }

        processEvent(ev);
    }

    // Drain remaining items on exit
    while (true) {
        DbEvent ev;
        {
            lock_guard<mutex> lock(_queueMutex);
            if (_queue.empty()) {
                break;
            }
            ev = _queue.front();
            _queue.pop();
        }
        processEvent(ev);
    }
}

void MeshMonDb::processEvent(const DbEvent &ev)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return;
    }

    sqlite3_stmt *stmt = NULL;

    switch (ev.type) {
    case EV_PACKET:
    {
        // 1. Upsert node
        const char *nodeSql =
            "INSERT INTO nodes (node_id, node_hex, first_seen, last_seen, last_rssi, last_snr, last_hops) "
            "VALUES (?1, ?2, ?3, ?3, ?4, ?5, ?6) "
            "ON CONFLICT(node_id) DO UPDATE SET "
            "last_seen=?3, last_rssi=?4, last_snr=?5, last_hops=?6;";
        if (sqlite3_prepare_v2(_db, nodeSql, -1, &stmt, NULL) == SQLITE_OK) {
            string hexId = formatNodeHex(ev.fromNode);
            sqlite3_bind_int64(stmt, 1, ev.fromNode);
            sqlite3_bind_text(stmt, 2, hexId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, ev.meshmonTime);
            sqlite3_bind_double(stmt, 4, ev.rxRssi);
            sqlite3_bind_double(stmt, 5, ev.rxSnr);
            sqlite3_bind_int(stmt, 6, ev.hops);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            stmt = NULL;
        }

        // 2. Insert packet
        const char *pktSql =
            "INSERT INTO packets (meshmon_time, rx_time, packet_id, from_node, to_node, channel, "
            "rx_rssi, rx_snr, hop_start, hop_limit, hops, portnum, payload_variant, payload_size, "
            "want_ack, via_mqtt, payload) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17);";
        if (sqlite3_prepare_v2(_db, pktSql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, ev.meshmonTime);
            if (ev.rxTime > 0) {
                sqlite3_bind_int64(stmt, 2, ev.rxTime);
            } else {
                sqlite3_bind_null(stmt, 2);
            }
            sqlite3_bind_int64(stmt, 3, ev.packetId);
            sqlite3_bind_int64(stmt, 4, ev.fromNode);
            sqlite3_bind_int64(stmt, 5, ev.toNode);
            sqlite3_bind_int(stmt, 6, ev.channel);
            sqlite3_bind_double(stmt, 7, ev.rxRssi);
            sqlite3_bind_double(stmt, 8, ev.rxSnr);
            sqlite3_bind_int(stmt, 9, ev.hopStart);
            sqlite3_bind_int(stmt, 10, ev.hopLimit);
            sqlite3_bind_int(stmt, 11, ev.hops);
            sqlite3_bind_int(stmt, 12, ev.portnum);
            sqlite3_bind_int(stmt, 13, ev.payloadVariant);
            sqlite3_bind_int(stmt, 14, (int) ev.payload.size());
            sqlite3_bind_int(stmt, 15, ev.wantAck ? 1 : 0);
            sqlite3_bind_int(stmt, 16, ev.viaMqtt ? 1 : 0);
            if (!ev.payload.empty()) {
                sqlite3_bind_blob(stmt, 17, ev.payload.data(), (int) ev.payload.size(), SQLITE_TRANSIENT);
            } else {
                sqlite3_bind_null(stmt, 17);
            }
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
    }
        break;

    case EV_NODE_INFO:
    {
        const char *infoSql =
            "INSERT INTO nodes (node_id, node_hex, long_name, short_name, hw_model, role, first_seen, last_seen) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?7) "
            "ON CONFLICT(node_id) DO UPDATE SET "
            "long_name=?3, short_name=?4, hw_model=?5, role=?6, last_seen=?7;";
        if (sqlite3_prepare_v2(_db, infoSql, -1, &stmt, NULL) == SQLITE_OK) {
            string hexId = formatNodeHex(ev.fromNode);
            sqlite3_bind_int64(stmt, 1, ev.fromNode);
            sqlite3_bind_text(stmt, 2, hexId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, ev.longName.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, ev.shortName.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 5, ev.hwModel);
            sqlite3_bind_int(stmt, 6, ev.role);
            sqlite3_bind_int64(stmt, 7, ev.meshmonTime);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
    }
        break;

    case EV_DEVICE_METRICS:
    case EV_ENV_METRICS:
    case EV_POWER_METRICS:
    case EV_LOCAL_STATS:
    {
        const char *telSql =
            "INSERT INTO telemetry (meshmon_time, node_id, metric_type, battery_level, voltage, "
            "channel_utilization, air_util_tx, temperature, relative_humidity, barometric_pressure, "
            "ch1_voltage, ch1_current, uptime_seconds) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13);";
        if (sqlite3_prepare_v2(_db, telSql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, ev.meshmonTime);
            sqlite3_bind_int64(stmt, 2, ev.fromNode);
            sqlite3_bind_text(stmt, 3, ev.metricType.c_str(), -1, SQLITE_TRANSIENT);
            if (ev.hasBattery) sqlite3_bind_int(stmt, 4, ev.batteryLevel); else sqlite3_bind_null(stmt, 4);
            if (ev.hasVoltage) sqlite3_bind_double(stmt, 5, ev.voltage); else sqlite3_bind_null(stmt, 5);
            if (ev.hasChannelUtil) sqlite3_bind_double(stmt, 6, ev.channelUtilization); else sqlite3_bind_null(stmt, 6);
            if (ev.hasAirUtilTx) sqlite3_bind_double(stmt, 7, ev.airUtilTx); else sqlite3_bind_null(stmt, 7);
            if (ev.hasTemp) sqlite3_bind_double(stmt, 8, ev.temperature); else sqlite3_bind_null(stmt, 8);
            if (ev.hasHum) sqlite3_bind_double(stmt, 9, ev.humidity); else sqlite3_bind_null(stmt, 9);
            if (ev.hasPress) sqlite3_bind_double(stmt, 10, ev.pressure); else sqlite3_bind_null(stmt, 10);
            if (ev.hasCh1Volt) sqlite3_bind_double(stmt, 11, ev.ch1Voltage); else sqlite3_bind_null(stmt, 11);
            if (ev.hasCh1Curr) sqlite3_bind_double(stmt, 12, ev.ch1Current); else sqlite3_bind_null(stmt, 12);
            if (ev.uptimeSeconds > 0) sqlite3_bind_int64(stmt, 13, ev.uptimeSeconds); else sqlite3_bind_null(stmt, 13);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
    }
        break;

    case EV_POSITION:
    {
        const char *posSql =
            "INSERT INTO positions (meshmon_time, node_id, latitude, longitude, altitude, ground_speed, ground_track, sats_in_view) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);";
        if (sqlite3_prepare_v2(_db, posSql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, ev.meshmonTime);
            sqlite3_bind_int64(stmt, 2, ev.fromNode);
            sqlite3_bind_double(stmt, 3, ev.latitude);
            sqlite3_bind_double(stmt, 4, ev.longitude);
            sqlite3_bind_int(stmt, 5, ev.altitude);
            sqlite3_bind_int(stmt, 6, ev.groundSpeed);
            sqlite3_bind_int(stmt, 7, ev.groundTrack);
            sqlite3_bind_int(stmt, 8, ev.satsInView);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
    }
        break;

    case EV_TEXT_MESSAGE:
    {
        const char *txtSql =
            "INSERT INTO text_messages (meshmon_time, from_node, to_node, channel, message) "
            "VALUES (?1, ?2, ?3, ?4, ?5);";
        if (sqlite3_prepare_v2(_db, txtSql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, ev.meshmonTime);
            sqlite3_bind_int64(stmt, 2, ev.fromNode);
            sqlite3_bind_int64(stmt, 3, ev.toNode);
            sqlite3_bind_int(stmt, 4, ev.channel);
            sqlite3_bind_text(stmt, 5, ev.text.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
    }
        break;

    case EV_TRACEROUTE:
    {
        ostringstream nodesStr, snrsStr;
        for (size_t i = 0; i < ev.routeNodes.size(); i++) {
            if (i > 0) {
                nodesStr << ",";
                snrsStr << ",";
            }
            nodesStr << formatNodeHex(ev.routeNodes[i]);
            snrsStr << fixed << setprecision(1) << ev.routeSnrs[i];
        }

        const char *trSql =
            "INSERT INTO traceroutes (meshmon_time, from_node, to_node, route_count, route_nodes, route_snrs) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6);";
        if (sqlite3_prepare_v2(_db, trSql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, ev.meshmonTime);
            sqlite3_bind_int64(stmt, 2, ev.fromNode);
            sqlite3_bind_int64(stmt, 3, ev.toNode);
            sqlite3_bind_int(stmt, 4, ev.routeCount);
            sqlite3_bind_text(stmt, 5, nodesStr.str().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, snrsStr.str().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
    }
        break;

    case EV_AUTOMATION_EVENT:
    {
        const char *autoSql =
            "INSERT INTO automation_events (meshmon_time, node_id, node_hex, device_type, "
            "direction, subsystem, command_name, action_param, status, initiator, rtt_ms) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);";
        if (sqlite3_prepare_v2(_db, autoSql, -1, &stmt, NULL) == SQLITE_OK) {
            string hexId = formatNodeHex(ev.fromNode);
            sqlite3_bind_int64(stmt, 1, ev.meshmonTime);
            sqlite3_bind_int64(stmt, 2, ev.fromNode);
            sqlite3_bind_text(stmt, 3, hexId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, ev.deviceType.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, ev.direction.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, ev.subsystem.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 7, ev.commandName.c_str(), -1, SQLITE_TRANSIENT);
            if (!ev.actionParam.empty()) {
                sqlite3_bind_text(stmt, 8, ev.actionParam.c_str(), -1, SQLITE_TRANSIENT);
            } else {
                sqlite3_bind_null(stmt, 8);
            }
            sqlite3_bind_text(stmt, 9, ev.status.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 10, ev.initiator.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 11, ev.rttMs);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
    }
        break;
    }
}

size_t MeshMonDb::pruneOlderThan(time_t thresholdTime)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return 0;
    }

    size_t totalDeleted = 0;
    sqlite3_stmt *stmt = NULL;
    const char *queries[] = {
        "DELETE FROM packets WHERE meshmon_time < ?1;",
        "DELETE FROM telemetry WHERE meshmon_time < ?1;",
        "DELETE FROM positions WHERE meshmon_time < ?1;",
        "DELETE FROM text_messages WHERE meshmon_time < ?1;",
        "DELETE FROM traceroutes WHERE meshmon_time < ?1;",
        "DELETE FROM automation_events WHERE meshmon_time < ?1;",
        NULL
    };

    for (int i = 0; queries[i] != NULL; i++) {
        if (sqlite3_prepare_v2(_db, queries[i], -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, thresholdTime);
            sqlite3_step(stmt);
            totalDeleted += (size_t) sqlite3_changes(_db);
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
    }

    return totalDeleted;
}

bool MeshMonDb::getTrafficSummary(time_t since, TrafficSummary &summary)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    summary = TrafficSummary();

    const char *sql =
        "SELECT count(*), coalesce(sum(payload_size), 0), "
        "coalesce(sum(case when to_node = 4294967295 then 1 else 0 end), 0), "
        "coalesce(sum(case when to_node != 4294967295 then 1 else 0 end), 0), "
        "coalesce(sum(case when hops = 0 then 1 else 0 end), 0), "
        "coalesce(sum(case when hops > 0 then 1 else 0 end), 0), "
        "coalesce(min(meshmon_time), 0), coalesce(max(meshmon_time), 0) "
        "FROM packets WHERE meshmon_time >= ?1;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            summary.totalPackets = (uint32_t) sqlite3_column_int64(stmt, 0);
            summary.totalBytes = (uint64_t) sqlite3_column_int64(stmt, 1);
            summary.broadcastPackets = (uint32_t) sqlite3_column_int64(stmt, 2);
            summary.unicastPackets = (uint32_t) sqlite3_column_int64(stmt, 3);
            summary.directPackets = (uint32_t) sqlite3_column_int64(stmt, 4);
            summary.relayedPackets = (uint32_t) sqlite3_column_int64(stmt, 5);
            summary.oldestPacket = (time_t) sqlite3_column_int64(stmt, 6);
            summary.newestPacket = (time_t) sqlite3_column_int64(stmt, 7);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    const char *portSql =
        "SELECT portnum, count(*) FROM packets "
        "WHERE meshmon_time >= ?1 GROUP BY portnum ORDER BY count(*) DESC;";
    if (sqlite3_prepare_v2(_db, portSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int portnum = sqlite3_column_int(stmt, 0);
            uint32_t count = (uint32_t) sqlite3_column_int64(stmt, 1);
            summary.portnumCounts[portnum] = count;
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    return true;
}

bool MeshMonDb::getTrafficRatios(time_t since, float &directPct, float &bcastPct, float &avgHops)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    directPct = 0.0f;
    bcastPct = 0.0f;
    avgHops = 0.0f;

    const char *sql =
        "SELECT count(*), "
        "coalesce(sum(CASE WHEN hops = 0 THEN 1 ELSE 0 END), 0), "
        "coalesce(sum(CASE WHEN to_node = 4294967295 THEN 1 ELSE 0 END), 0), "
        "coalesce(avg(hops), 0.0) "
        "FROM packets WHERE meshmon_time >= ?1;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            uint64_t total = (uint64_t) sqlite3_column_int64(stmt, 0);
            uint64_t direct = (uint64_t) sqlite3_column_int64(stmt, 1);
            uint64_t bcast = (uint64_t) sqlite3_column_int64(stmt, 2);
            avgHops = (float) sqlite3_column_double(stmt, 3);
            if (total > 0) {
                directPct = (float) (direct * 100.0 / total);
                bcastPct = (float) (bcast * 100.0 / total);
            }
        }
        sqlite3_finalize(stmt);
        return true;
    }

    return false;
}

bool MeshMonDb::getTopTalkerSummary(time_t since, string &topNode, uint32_t &topPackets)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    topNode.clear();
    topPackets = 0;

    const char *sql =
        "SELECT p.from_node, coalesce(n.short_name, ''), coalesce(n.long_name, ''), count(*) AS cnt "
        "FROM packets p LEFT JOIN nodes n ON p.from_node = n.node_id "
        "WHERE p.meshmon_time >= ?1 "
        "GROUP BY p.from_node "
        "ORDER BY cnt DESC LIMIT 1;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            uint32_t nodeId = (uint32_t) sqlite3_column_int64(stmt, 0);
            string sName = (const char *) sqlite3_column_text(stmt, 1);
            string lName = (const char *) sqlite3_column_text(stmt, 2);
            topPackets = (uint32_t) sqlite3_column_int64(stmt, 3);
            if (!sName.empty()) {
                topNode = sName;
            } else if (!lName.empty()) {
                topNode = lName;
            } else {
                topNode = formatNodeHex(nodeId);
            }
        }
        sqlite3_finalize(stmt);
        return true;
    }

    return false;
}

bool MeshMonDb::getBestNeighborSummary(time_t since, string &bestNeighbor, float &bestSnr)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    bestNeighbor.clear();
    bestSnr = -999.0f;

    const char *sql =
        "SELECT p.from_node, coalesce(n.short_name, ''), coalesce(n.long_name, ''), avg(p.rx_snr) AS snr "
        "FROM packets p LEFT JOIN nodes n ON p.from_node = n.node_id "
        "WHERE p.meshmon_time >= ?1 AND p.hops = 0 "
        "GROUP BY p.from_node "
        "ORDER BY snr DESC LIMIT 1;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            uint32_t nodeId = (uint32_t) sqlite3_column_int64(stmt, 0);
            string sName = (const char *) sqlite3_column_text(stmt, 1);
            string lName = (const char *) sqlite3_column_text(stmt, 2);
            bestSnr = (float) sqlite3_column_double(stmt, 3);
            if (!sName.empty()) {
                bestNeighbor = sName;
            } else if (!lName.empty()) {
                bestNeighbor = lName;
            } else {
                bestNeighbor = formatNodeHex(nodeId);
            }
        }
        sqlite3_finalize(stmt);
        return true;
    }

    return false;
}

bool MeshMonDb::getMaxEchoMultiplier(time_t since, uint32_t &maxMultiplier)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    maxMultiplier = 1;

    const char *sql =
        "SELECT count(*) AS echo_cnt "
        "FROM packets "
        "WHERE meshmon_time >= ?1 "
        "GROUP BY packet_id, from_node "
        "ORDER BY echo_cnt DESC LIMIT 1;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            maxMultiplier = (uint32_t) sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return true;
    }

    return false;
}

bool MeshMonDb::getCriticalRelaySummary(time_t since, string &topRelay, uint32_t &relayedCount)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    topRelay.clear();
    relayedCount = 0;

    const char *sql =
        "SELECT p.from_node, coalesce(n.short_name, ''), coalesce(n.long_name, ''), count(*) AS cnt "
        "FROM packets p LEFT JOIN nodes n ON p.from_node = n.node_id "
        "WHERE p.meshmon_time >= ?1 AND p.hops > 0 "
        "GROUP BY p.from_node "
        "ORDER BY cnt DESC LIMIT 1;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            uint32_t nodeId = (uint32_t) sqlite3_column_int64(stmt, 0);
            string sName = (const char *) sqlite3_column_text(stmt, 1);
            string lName = (const char *) sqlite3_column_text(stmt, 2);
            relayedCount = (uint32_t) sqlite3_column_int64(stmt, 3);
            if (!sName.empty()) {
                topRelay = sName;
            } else if (!lName.empty()) {
                topRelay = lName;
            } else {
                topRelay = formatNodeHex(nodeId);
            }
        }
        sqlite3_finalize(stmt);
        return true;
    }

    return false;
}

bool MeshMonDb::getMaxClockDrift(time_t since, float &maxSkewSec)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    maxSkewSec = 0.0f;

    const char *sql =
        "SELECT max(abs(rx_time - meshmon_time)) "
        "FROM packets "
        "WHERE meshmon_time >= ?1 AND rx_time IS NOT NULL AND rx_time > 0;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            maxSkewSec = (float) sqlite3_column_double(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return true;
    }

    return false;
}

bool MeshMonDb::getTopTalkers(time_t since, size_t limit, vector<NodeTrafficStat> &stats)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    stats.clear();
    const char *sql =
        "SELECT p.from_node, coalesce(n.long_name, ''), coalesce(n.short_name, ''), "
        "count(*) AS pkt_count, coalesce(sum(p.payload_size), 0) AS byte_count, "
        "coalesce(avg(p.hops), 0.0), coalesce(avg(p.rx_snr), 0.0), coalesce(avg(p.rx_rssi), 0.0), "
        "max(p.meshmon_time) "
        "FROM packets p LEFT JOIN nodes n ON p.from_node = n.node_id "
        "WHERE p.meshmon_time >= ?1 "
        "GROUP BY p.from_node "
        "ORDER BY pkt_count DESC LIMIT ?2;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        sqlite3_bind_int(stmt, 2, (int) limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            NodeTrafficStat s;
            s.nodeId = (uint32_t) sqlite3_column_int64(stmt, 0);
            s.nodeHex = formatNodeHex(s.nodeId);
            s.longName = (const char *) sqlite3_column_text(stmt, 1);
            s.shortName = (const char *) sqlite3_column_text(stmt, 2);
            s.packetCount = (uint32_t) sqlite3_column_int64(stmt, 3);
            s.totalBytes = (uint64_t) sqlite3_column_int64(stmt, 4);
            s.avgHops = (float) sqlite3_column_double(stmt, 5);
            s.avgSnr = (float) sqlite3_column_double(stmt, 6);
            s.avgRssi = (float) sqlite3_column_double(stmt, 7);
            s.lastSeen = (time_t) sqlite3_column_int64(stmt, 8);
            stats.push_back(s);
        }
        sqlite3_finalize(stmt);
    }

    return true;
}

bool MeshMonDb::getNeighborStats(time_t since, vector<NeighborStat> &stats)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    stats.clear();
    const char *sql =
        "SELECT p.from_node, coalesce(n.long_name, ''), coalesce(n.short_name, ''), "
        "count(*) AS direct_count, coalesce(avg(p.rx_snr), 0.0), coalesce(min(p.rx_snr), 0.0), "
        "coalesce(max(p.rx_snr), 0.0), coalesce(avg(p.rx_rssi), 0.0), max(p.meshmon_time) "
        "FROM packets p LEFT JOIN nodes n ON p.from_node = n.node_id "
        "WHERE p.meshmon_time >= ?1 AND p.hops = 0 "
        "GROUP BY p.from_node "
        "ORDER BY direct_count DESC;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            NeighborStat s;
            s.nodeId = (uint32_t) sqlite3_column_int64(stmt, 0);
            s.nodeHex = formatNodeHex(s.nodeId);
            s.longName = (const char *) sqlite3_column_text(stmt, 1);
            s.shortName = (const char *) sqlite3_column_text(stmt, 2);
            s.packetCount = (uint32_t) sqlite3_column_int64(stmt, 3);
            s.avgSnr = (float) sqlite3_column_double(stmt, 4);
            s.minSnr = (float) sqlite3_column_double(stmt, 5);
            s.maxSnr = (float) sqlite3_column_double(stmt, 6);
            s.avgRssi = (float) sqlite3_column_double(stmt, 7);
            s.lastSeen = (time_t) sqlite3_column_int64(stmt, 8);
            stats.push_back(s);
        }
        sqlite3_finalize(stmt);
    }

    return true;
}

bool MeshMonDb::getEchoStorms(time_t since, size_t limit, vector<EchoStormStat> &stats)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    stats.clear();
    const char *sql =
        "SELECT packet_id, from_node, count(*) AS echo_count, "
        "min(hops), max(hops), (max(meshmon_time) - min(meshmon_time)) AS duration_sec, "
        "min(meshmon_time), max(meshmon_time) "
        "FROM packets "
        "WHERE meshmon_time >= ?1 "
        "GROUP BY packet_id, from_node "
        "HAVING count(*) > 1 "
        "ORDER BY echo_count DESC, duration_sec DESC "
        "LIMIT ?2;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        sqlite3_bind_int(stmt, 2, (int) limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            EchoStormStat s;
            s.packetId = (uint32_t) sqlite3_column_int64(stmt, 0);
            s.fromNode = (uint32_t) sqlite3_column_int64(stmt, 1);
            s.fromHex = formatNodeHex(s.fromNode);
            s.echoCount = (uint32_t) sqlite3_column_int64(stmt, 2);
            s.minHops = (uint32_t) sqlite3_column_int(stmt, 3);
            s.maxHops = (uint32_t) sqlite3_column_int(stmt, 4);
            s.durationSec = (uint32_t) sqlite3_column_int64(stmt, 5);
            s.firstArrival = (time_t) sqlite3_column_int64(stmt, 6);
            s.lastArrival = (time_t) sqlite3_column_int64(stmt, 7);
            stats.push_back(s);
        }
        sqlite3_finalize(stmt);
    }

    return true;
}

bool MeshMonDb::getLinkAsymmetry(time_t since, vector<LinkAsymmetryStat> &stats)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    stats.clear();
    const char *sql =
        "SELECT p.from_node, coalesce(n.long_name, ''), coalesce(n.short_name, ''), "
        "avg(p.rx_snr), avg(p.rx_rssi), count(*) "
        "FROM packets p LEFT JOIN nodes n ON p.from_node = n.node_id "
        "WHERE p.meshmon_time >= ?1 AND p.hops = 0 "
        "GROUP BY p.from_node "
        "HAVING count(*) >= 3 "
        "ORDER BY avg(p.rx_snr) ASC;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            LinkAsymmetryStat s;
            s.nodeId = (uint32_t) sqlite3_column_int64(stmt, 0);
            s.nodeHex = formatNodeHex(s.nodeId);
            s.longName = (const char *) sqlite3_column_text(stmt, 1);
            s.shortName = (const char *) sqlite3_column_text(stmt, 2);
            s.rxSnr = (float) sqlite3_column_double(stmt, 3);
            s.rxRssi = (float) sqlite3_column_double(stmt, 4);
            s.sampleCount = (uint32_t) sqlite3_column_int64(stmt, 5);
            stats.push_back(s);
        }
        sqlite3_finalize(stmt);
    }

    return true;
}

bool MeshMonDb::getCriticalRepeaters(time_t since, size_t limit, vector<CriticalRepeaterStat> &stats)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    stats.clear();
    // Analyze multi-hop packets & discovered routes
    const char *sql =
        "SELECT p.from_node, coalesce(n.long_name, ''), coalesce(n.short_name, ''), "
        "count(*) AS relayed_count, avg(p.rx_snr) "
        "FROM packets p LEFT JOIN nodes n ON p.from_node = n.node_id "
        "WHERE p.meshmon_time >= ?1 AND p.hops > 0 "
        "GROUP BY p.from_node "
        "ORDER BY relayed_count DESC "
        "LIMIT ?2;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        sqlite3_bind_int(stmt, 2, (int) limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            CriticalRepeaterStat s;
            s.repeaterId = (uint32_t) sqlite3_column_int64(stmt, 0);
            s.repeaterHex = formatNodeHex(s.repeaterId);
            s.longName = (const char *) sqlite3_column_text(stmt, 1);
            s.shortName = (const char *) sqlite3_column_text(stmt, 2);
            s.relayCount = (uint32_t) sqlite3_column_int64(stmt, 3);
            s.avgSnr = (float) sqlite3_column_double(stmt, 4);
            stats.push_back(s);
        }
        sqlite3_finalize(stmt);
    }

    return true;
}

bool MeshMonDb::getClockDrift(time_t since, vector<ClockDriftStat> &stats)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    stats.clear();
    const char *sql =
        "SELECT p.from_node, coalesce(n.long_name, ''), coalesce(n.short_name, ''), "
        "count(*), avg(p.rx_time - p.meshmon_time), "
        "min(p.rx_time - p.meshmon_time), max(p.rx_time - p.meshmon_time) "
        "FROM packets p LEFT JOIN nodes n ON p.from_node = n.node_id "
        "WHERE p.meshmon_time >= ?1 AND p.rx_time IS NOT NULL AND p.rx_time > 0 "
        "GROUP BY p.from_node "
        "HAVING count(*) >= 2 "
        "ORDER BY abs(avg(p.rx_time - p.meshmon_time)) DESC;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ClockDriftStat s;
            s.nodeId = (uint32_t) sqlite3_column_int64(stmt, 0);
            s.nodeHex = formatNodeHex(s.nodeId);
            s.longName = (const char *) sqlite3_column_text(stmt, 1);
            s.shortName = (const char *) sqlite3_column_text(stmt, 2);
            s.sampleCount = (uint32_t) sqlite3_column_int64(stmt, 3);
            s.avgSkewSec = (int32_t) sqlite3_column_int64(stmt, 4);
            s.minSkewSec = (int32_t) sqlite3_column_int64(stmt, 5);
            s.maxSkewSec = (int32_t) sqlite3_column_int64(stmt, 6);
            stats.push_back(s);
        }
        sqlite3_finalize(stmt);
    }

    return true;
}

bool MeshMonDb::getHopDistribution(time_t since, vector<HopStat> &stats)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    stats.clear();
    uint32_t total = 0;
    const char *countSql = "SELECT count(*) FROM packets WHERE meshmon_time >= ?1;";
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(_db, countSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            total = (uint32_t) sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    if (total == 0) {
        return true;
    }

    const char *sql =
        "SELECT hops, count(*) FROM packets "
        "WHERE meshmon_time >= ?1 GROUP BY hops ORDER BY hops ASC;";
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            HopStat s;
            s.hops = sqlite3_column_int(stmt, 0);
            s.packetCount = (uint32_t) sqlite3_column_int64(stmt, 1);
            s.pctShare = ((float) s.packetCount * 100.0f) / ((float) total);
            stats.push_back(s);
        }
        sqlite3_finalize(stmt);
    }

    return true;
}

bool MeshMonDb::getPortnumDistribution(time_t since, vector<AppStat> &stats)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    stats.clear();
    uint32_t total = 0;
    const char *countSql = "SELECT count(*) FROM packets WHERE meshmon_time >= ?1;";
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(_db, countSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            total = (uint32_t) sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    if (total == 0) {
        return true;
    }

    const char *sql =
        "SELECT portnum, count(*), coalesce(sum(payload_size), 0) FROM packets "
        "WHERE meshmon_time >= ?1 GROUP BY portnum ORDER BY count(*) DESC;";
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AppStat s;
            s.portnum = sqlite3_column_int(stmt, 0);
            s.appName = portnumToString(s.portnum);
            s.packetCount = (uint32_t) sqlite3_column_int64(stmt, 1);
            s.totalBytes = (uint64_t) sqlite3_column_int64(stmt, 2);
            s.pctShare = ((float) s.packetCount * 100.0f) / ((float) total);
            stats.push_back(s);
        }
        sqlite3_finalize(stmt);
    }

    return true;
}

bool MeshMonDb::getNodeDetail(uint32_t nodeId, time_t since, NodeDetail &detail)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    bzero(&detail, sizeof(detail));
    detail.nodeId = nodeId;
    detail.nodeHex = formatNodeHex(nodeId);

    // 1. Node metadata
    const char *nodeSql =
        "SELECT long_name, short_name, hw_model, role, first_seen, last_seen, "
        "last_rssi, last_snr, last_hops FROM nodes WHERE node_id = ?1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, nodeSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, nodeId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *ln = (const char *) sqlite3_column_text(stmt, 0);
            const char *sn = (const char *) sqlite3_column_text(stmt, 1);
            if (ln) detail.longName = ln;
            if (sn) detail.shortName = sn;
            detail.hwModel = sqlite3_column_int(stmt, 2);
            detail.role = sqlite3_column_int(stmt, 3);
            detail.firstSeen = (time_t) sqlite3_column_int64(stmt, 4);
            detail.lastSeen = (time_t) sqlite3_column_int64(stmt, 5);
            detail.lastRssi = (float) sqlite3_column_double(stmt, 6);
            detail.lastSnr = (float) sqlite3_column_double(stmt, 7);
            detail.lastHops = sqlite3_column_int(stmt, 8);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    // 2. Packet aggregates
    const char *pktSql =
        "SELECT count(*), coalesce(avg(rx_snr), 0.0), coalesce(min(rx_snr), 0.0), "
        "coalesce(max(rx_snr), 0.0), coalesce(avg(rx_rssi), 0.0) "
        "FROM packets WHERE from_node = ?1 AND meshmon_time >= ?2;";
    if (sqlite3_prepare_v2(_db, pktSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, nodeId);
        sqlite3_bind_int64(stmt, 2, since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            detail.totalPackets = (uint32_t) sqlite3_column_int64(stmt, 0);
            detail.avgSnr = (float) sqlite3_column_double(stmt, 1);
            detail.minSnr = (float) sqlite3_column_double(stmt, 2);
            detail.maxSnr = (float) sqlite3_column_double(stmt, 3);
            detail.avgRssi = (float) sqlite3_column_double(stmt, 4);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    // 3. Last telemetry
    const char *telSql =
        "SELECT battery_level, voltage, channel_utilization, air_util_tx "
        "FROM telemetry WHERE node_id = ?1 ORDER BY meshmon_time DESC LIMIT 1;";
    if (sqlite3_prepare_v2(_db, telSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, nodeId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            detail.hasTelemetry = true;
            detail.lastBattery = sqlite3_column_type(stmt, 0) != SQLITE_NULL ? sqlite3_column_int(stmt, 0) : -1;
            detail.lastVoltage = sqlite3_column_type(stmt, 1) != SQLITE_NULL ? (float) sqlite3_column_double(stmt, 1) : 0.0f;
            detail.lastChannelUtil = sqlite3_column_type(stmt, 2) != SQLITE_NULL ? (float) sqlite3_column_double(stmt, 2) : 0.0f;
            detail.lastAirUtilTx = sqlite3_column_type(stmt, 3) != SQLITE_NULL ? (float) sqlite3_column_double(stmt, 3) : 0.0f;
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    // 4. Last position
    const char *posSql =
        "SELECT latitude, longitude, altitude "
        "FROM positions WHERE node_id = ?1 ORDER BY meshmon_time DESC LIMIT 1;";
    if (sqlite3_prepare_v2(_db, posSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, nodeId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            detail.hasPosition = true;
            detail.lastLat = sqlite3_column_double(stmt, 0);
            detail.lastLon = sqlite3_column_double(stmt, 1);
            detail.lastAlt = sqlite3_column_int(stmt, 2);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    return true;
}

bool MeshMonDb::getLinkFading(uint32_t nodeId, time_t since, vector<LinkFadingPoint> &points)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    points.clear();
    const char *sql =
        "SELECT (meshmon_time / 3600) * 3600 AS hour_bin, "
        "avg(rx_snr), min(rx_snr), max(rx_snr), avg(rx_rssi), count(*) "
        "FROM packets "
        "WHERE from_node = ?1 AND meshmon_time >= ?2 "
        "GROUP BY hour_bin ORDER BY hour_bin ASC;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, nodeId);
        sqlite3_bind_int64(stmt, 2, since);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            LinkFadingPoint p;
            p.timestamp = (time_t) sqlite3_column_int64(stmt, 0);
            p.avgSnr = (float) sqlite3_column_double(stmt, 1);
            p.minSnr = (float) sqlite3_column_double(stmt, 2);
            p.maxSnr = (float) sqlite3_column_double(stmt, 3);
            p.avgRssi = (float) sqlite3_column_double(stmt, 4);
            p.count = (uint32_t) sqlite3_column_int64(stmt, 5);
            points.push_back(p);
        }
        sqlite3_finalize(stmt);
    }

    return true;
}

bool MeshMonDb::getChannelHealth(time_t since, ChannelHealthStat &health)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    bzero(&health, sizeof(health));

    // Telemetry channel utilization stats
    const char *telSql =
        "SELECT coalesce(avg(channel_utilization), 0.0), coalesce(max(channel_utilization), 0.0), "
        "coalesce(avg(air_util_tx), 0.0), coalesce(max(air_util_tx), 0.0), "
        "coalesce(max(uptime_seconds), 0) "
        "FROM telemetry WHERE meshmon_time >= ?1 AND channel_utilization IS NOT NULL;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, telSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            health.avgChannelUtil = (float) sqlite3_column_double(stmt, 0);
            health.maxChannelUtil = (float) sqlite3_column_double(stmt, 1);
            health.avgAirUtilTx = (float) sqlite3_column_double(stmt, 2);
            health.maxAirUtilTx = (float) sqlite3_column_double(stmt, 3);
            health.uptimeSeconds = (uint32_t) sqlite3_column_int64(stmt, 4);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    // Packet total and duplicate count
    const char *pktSql =
        "SELECT count(*), "
        "coalesce(sum(case when cnt > 1 then (cnt - 1) else 0 end), 0) "
        "FROM ("
        "  SELECT packet_id, from_node, count(*) AS cnt "
        "  FROM packets WHERE meshmon_time >= ?1 GROUP BY packet_id, from_node"
        ");";
    if (sqlite3_prepare_v2(_db, pktSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            health.totalPackets = (uint32_t) sqlite3_column_int64(stmt, 0);
            health.duplicatePackets = (uint32_t) sqlite3_column_int64(stmt, 1);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    return true;
}

bool MeshMonDb::getPumpAnalytics(uint32_t nodeId, time_t since, PumpAnalytics &analytics)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    analytics = PumpAnalytics();

    string sql =
        "SELECT command_name, action_param, status, direction, meshmon_time "
        "FROM automation_events WHERE device_type = 'meshpump' AND meshmon_time >= ?1";
    if (nodeId != 0) {
        sql += " AND node_id = ?2";
    }
    sql += " ORDER BY meshmon_time ASC;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (nodeId != 0) {
            sqlite3_bind_int64(stmt, 2, nodeId);
        }

        time_t fishOnTime = 0;
        time_t upOnTime = 0;
        float totalMoisture = 0.0f;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            string cmd = (const char *) sqlite3_column_text(stmt, 0);
            const char *paramStr = (const char *) sqlite3_column_text(stmt, 1);
            string param = paramStr ? paramStr : "";
            string status = (const char *) sqlite3_column_text(stmt, 2);
            string dir = (const char *) sqlite3_column_text(stmt, 3);
            time_t t = (time_t) sqlite3_column_int64(stmt, 4);

            if (dir == "TX_CMD") {
                analytics.totalCommands++;
                if (status == "ACKED" || status == "EXECUTED") {
                    analytics.ackedCommands++;
                }
            }

            if (cmd == "PUMP_FISH_ON") {
                if (fishOnTime == 0) fishOnTime = t;
            } else if (cmd == "PUMP_FISH_OFF") {
                if (fishOnTime > 0 && t >= fishOnTime) {
                    analytics.fishRunSec += (uint32_t)(t - fishOnTime);
                    fishOnTime = 0;
                }
            } else if (cmd == "PUMP_UP_ON") {
                analytics.upRunCount++;
                if (upOnTime == 0) upOnTime = t;
            } else if (cmd == "PUMP_UP_OFF") {
                if (upOnTime > 0 && t >= upOnTime) {
                    analytics.upRunSec += (uint32_t)(t - upOnTime);
                    upOnTime = 0;
                }
                if (param.find("cutoff") != string::npos) {
                    analytics.upCutoffTriggers++;
                }
            } else if (cmd == "SOIL_MOISTURE") {
                float m = 0.0f;
                if (sscanf(param.c_str(), "%f", &m) == 1) {
                    analytics.moistureEvents++;
                    totalMoisture += m;
                }
            }
        }

        time_t now = time(NULL);
        if (fishOnTime > 0 && now >= fishOnTime) {
            analytics.fishRunSec += (uint32_t)(now - fishOnTime);
        }
        if (upOnTime > 0 && now >= upOnTime) {
            analytics.upRunSec += (uint32_t)(now - upOnTime);
        }

        time_t windowSec = now > since ? (now - since) : 1;
        if (windowSec > 0) {
            analytics.fishDutyPct = ((float) analytics.fishRunSec * 100.0f) / (float) windowSec;
            if (analytics.fishDutyPct > 100.0f) analytics.fishDutyPct = 100.0f;
        }

        if (analytics.moistureEvents > 0) {
            analytics.avgMoisture = totalMoisture / (float) analytics.moistureEvents;
        }

        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    return true;
}

bool MeshMonDb::getRoofAnalytics(uint32_t nodeId, time_t since, RoofAnalytics &analytics)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    analytics = RoofAnalytics();

    string sql =
        "SELECT command_name, action_param, status, direction, meshmon_time "
        "FROM automation_events WHERE device_type = 'meshroof' AND meshmon_time >= ?1";
    if (nodeId != 0) {
        sql += " AND node_id = ?2";
    }
    sql += " ORDER BY meshmon_time ASC;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (nodeId != 0) {
            sqlite3_bind_int64(stmt, 2, nodeId);
        }

        time_t ampOnTime = 0;
        float totalWifiRssi = 0.0f;
        float minWifi = 0.0f, maxWifi = -999.0f;
        float totalCpuTemp = 0.0f, maxCpu = -999.0f;
        uint32_t tempReports = 0;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            string cmd = (const char *) sqlite3_column_text(stmt, 0);
            const char *paramStr = (const char *) sqlite3_column_text(stmt, 1);
            string param = paramStr ? paramStr : "";
            string dir = (const char *) sqlite3_column_text(stmt, 3);
            time_t t = (time_t) sqlite3_column_int64(stmt, 4);

            if (dir == "TX_CMD") {
                analytics.totalCommands++;
            }

            if (cmd == "AMPLIFY_ON") {
                if (ampOnTime == 0) ampOnTime = t;
            } else if (cmd == "AMPLIFY_OFF") {
                if (ampOnTime > 0 && t >= ampOnTime) {
                    analytics.amplifyRunSec += (uint32_t)(t - ampOnTime);
                    ampOnTime = 0;
                }
            } else if (cmd == "WIFI_STATUS" || cmd == "WIFI") {
                float rssi = 0.0f;
                if (sscanf(param.c_str(), "%f", &rssi) == 1 || sscanf(param.c_str(), "rssi=%f", &rssi) == 1) {
                    analytics.wifiReports++;
                    totalWifiRssi += rssi;
                    if (analytics.wifiReports == 1 || rssi < minWifi) minWifi = rssi;
                    if (analytics.wifiReports == 1 || rssi > maxWifi) maxWifi = rssi;
                }
            } else if (cmd == "CPU_TEMP") {
                float temp = 0.0f;
                if (sscanf(param.c_str(), "%f", &temp) == 1) {
                    tempReports++;
                    totalCpuTemp += temp;
                    if (temp > maxCpu) maxCpu = temp;
                }
            } else if (cmd == "RESET" || cmd == "BOOT_UP") {
                analytics.resetEvents++;
            }
        }

        time_t now = time(NULL);
        if (ampOnTime > 0 && now >= ampOnTime) {
            analytics.amplifyRunSec += (uint32_t)(now - ampOnTime);
        }

        time_t windowSec = now > since ? (now - since) : 1;
        if (windowSec > 0) {
            analytics.amplifyDutyPct = ((float) analytics.amplifyRunSec * 100.0f) / (float) windowSec;
            if (analytics.amplifyDutyPct > 100.0f) analytics.amplifyDutyPct = 100.0f;
        }

        if (analytics.wifiReports > 0) {
            analytics.avgWifiRssi = totalWifiRssi / (float) analytics.wifiReports;
            analytics.minWifiRssi = minWifi;
            analytics.maxWifiRssi = maxWifi;
        }

        if (tempReports > 0) {
            analytics.avgCpuTemp = totalCpuTemp / (float) tempReports;
            analytics.maxCpuTemp = maxCpu;
        }

        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    return true;
}

bool MeshMonDb::getRoomAnalytics(uint32_t nodeId, time_t since, RoomAnalytics &analytics)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    analytics = RoomAnalytics();

    string sql =
        "SELECT command_name, action_param, status, direction, meshmon_time "
        "FROM automation_events WHERE device_type = 'meshroom' AND meshmon_time >= ?1";
    if (nodeId != 0) {
        sql += " AND node_id = ?2";
    }
    sql += " ORDER BY meshmon_time ASC;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, since);
        if (nodeId != 0) {
            sqlite3_bind_int64(stmt, 2, nodeId);
        }

        time_t acOnTime = 0;
        time_t tvOnTime = 0;
        float totalAcTarget = 0.0f;
        uint32_t acTargetReports = 0;
        float totalBoardTemp = 0.0f;
        float maxBoard = -999.0f;
        uint32_t tempReports = 0;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            string cmd = (const char *) sqlite3_column_text(stmt, 0);
            const char *paramStr = (const char *) sqlite3_column_text(stmt, 1);
            string param = paramStr ? paramStr : "";
            string dir = (const char *) sqlite3_column_text(stmt, 3);
            time_t t = (time_t) sqlite3_column_int64(stmt, 4);

            if (dir == "TX_CMD") {
                analytics.totalCommands++;
            }

            if (cmd == "AC_ON" || cmd == "AC_POWER_ON") {
                if (acOnTime == 0) acOnTime = t;
            } else if (cmd == "AC_OFF" || cmd == "AC_POWER_OFF") {
                if (acOnTime > 0 && t >= acOnTime) {
                    analytics.acRunSec += (uint32_t)(t - acOnTime);
                    acOnTime = 0;
                }
            } else if (cmd == "AC_TEMP" || cmd == "AC_CLIMATE") {
                float target = 0.0f;
                if (sscanf(param.c_str(), "%f", &target) == 1 || sscanf(param.c_str(), "temp=%f", &target) == 1) {
                    acTargetReports++;
                    totalAcTarget += target;
                }
            } else if (cmd == "TV_ON" || cmd == "TV_POWER_ON") {
                if (tvOnTime == 0) tvOnTime = t;
            } else if (cmd == "TV_OFF" || cmd == "TV_POWER_OFF") {
                if (tvOnTime > 0 && t >= tvOnTime) {
                    analytics.tvRunSec += (uint32_t)(t - tvOnTime);
                    tvOnTime = 0;
                }
            } else if (cmd == "TV_CHAN") {
                analytics.tvChanChanges++;
            } else if (cmd == "BOARD_TEMP" || cmd == "ROOM_TEMP") {
                float temp = 0.0f;
                if (sscanf(param.c_str(), "%f", &temp) == 1) {
                    tempReports++;
                    totalBoardTemp += temp;
                    if (temp > maxBoard) maxBoard = temp;
                }
            }
        }

        time_t now = time(NULL);
        if (acOnTime > 0 && now >= acOnTime) {
            analytics.acRunSec += (uint32_t)(now - acOnTime);
        }
        if (tvOnTime > 0 && now >= tvOnTime) {
            analytics.tvRunSec += (uint32_t)(now - tvOnTime);
        }

        time_t windowSec = now > since ? (now - since) : 1;
        if (windowSec > 0) {
            analytics.acDutyPct = ((float) analytics.acRunSec * 100.0f) / (float) windowSec;
            if (analytics.acDutyPct > 100.0f) analytics.acDutyPct = 100.0f;
            analytics.tvDutyPct = ((float) analytics.tvRunSec * 100.0f) / (float) windowSec;
            if (analytics.tvDutyPct > 100.0f) analytics.tvDutyPct = 100.0f;
        }

        if (acTargetReports > 0) {
            analytics.avgAcTargetTemp = totalAcTarget / (float) acTargetReports;
        }
        if (tempReports > 0) {
            analytics.avgBoardTemp = totalBoardTemp / (float) tempReports;
            analytics.maxBoardTemp = maxBoard;
        }

        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    return true;
}

bool MeshMonDb::getAutomationHistory(size_t limit, vector<AutomationEvent> &events)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    events.clear();

    const char *sql =
        "SELECT id, meshmon_time, node_id, node_hex, device_type, direction, "
        "subsystem, command_name, action_param, status, initiator, coalesce(rtt_ms, 0) "
        "FROM automation_events ORDER BY meshmon_time DESC, id DESC LIMIT ?1;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64) limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AutomationEvent ev;
            ev.id = sqlite3_column_int64(stmt, 0);
            ev.meshmonTime = (time_t) sqlite3_column_int64(stmt, 1);
            ev.nodeId = (uint32_t) sqlite3_column_int64(stmt, 2);
            const char *hex = (const char *) sqlite3_column_text(stmt, 3);
            ev.nodeHex = hex ? hex : "";
            const char *dev = (const char *) sqlite3_column_text(stmt, 4);
            ev.deviceType = dev ? dev : "";
            const char *dir = (const char *) sqlite3_column_text(stmt, 5);
            ev.direction = dir ? dir : "";
            const char *sub = (const char *) sqlite3_column_text(stmt, 6);
            ev.subsystem = sub ? sub : "";
            const char *cmd = (const char *) sqlite3_column_text(stmt, 7);
            ev.commandName = cmd ? cmd : "";
            const char *param = (const char *) sqlite3_column_text(stmt, 8);
            ev.actionParam = param ? param : "";
            const char *st = (const char *) sqlite3_column_text(stmt, 9);
            ev.status = st ? st : "";
            const char *init = (const char *) sqlite3_column_text(stmt, 10);
            ev.initiator = init ? init : "";
            ev.rttMs = sqlite3_column_int(stmt, 11);

            events.push_back(ev);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    return true;
}

bool MeshMonDb::getLatencyTrend(uint32_t nodeId, time_t since, vector<LatencyTrendPoint> &points)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    points.clear();

    string sql;
    if (nodeId == 0) {
        sql =
            "SELECT (meshmon_time / 3600) * 3600 AS bucket, "
            "avg(rtt_ms), min(rtt_ms), max(rtt_ms), count(*) "
            "FROM automation_events "
            "WHERE meshmon_time >= ?1 AND rtt_ms > 0 "
            "GROUP BY bucket ORDER BY bucket ASC;";
    } else {
        sql =
            "SELECT (meshmon_time / 3600) * 3600 AS bucket, "
            "avg(rtt_ms), min(rtt_ms), max(rtt_ms), count(*) "
            "FROM automation_events "
            "WHERE node_id = ?1 AND meshmon_time >= ?2 AND rtt_ms > 0 "
            "GROUP BY bucket ORDER BY bucket ASC;";
    }

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK) {
        if (nodeId == 0) {
            sqlite3_bind_int64(stmt, 1, (sqlite3_int64) since);
        } else {
            sqlite3_bind_int64(stmt, 1, (sqlite3_int64) nodeId);
            sqlite3_bind_int64(stmt, 2, (sqlite3_int64) since);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            LatencyTrendPoint pt;
            pt.timestamp = (time_t) sqlite3_column_int64(stmt, 0);
            pt.avgRtt = (float) sqlite3_column_double(stmt, 1);
            pt.minRtt = (uint32_t) sqlite3_column_int(stmt, 2);
            pt.maxRtt = (uint32_t) sqlite3_column_int(stmt, 3);
            pt.count = (uint32_t) sqlite3_column_int(stmt, 4);
            points.push_back(pt);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    return true;
}

bool MeshMonDb::getAutomationEventCount(time_t since, uint32_t &count)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        return false;
    }

    count = 0;
    const char *sql = "SELECT count(*) FROM automation_events WHERE meshmon_time >= ?1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64) since);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = (uint32_t) sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    return true;
}

bool MeshMonDb::executeRawQuery(const string &sql, QueryResult &result)
{
    lock_guard<mutex> lock(_dbMutex);
    if (_db == NULL) {
        result.error = "Database not open";
        return false;
    }

    result.columns.clear();
    result.rows.clear();
    result.error.clear();

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        result.error = sqlite3_errmsg(_db);
        return false;
    }

    int colCount = sqlite3_column_count(stmt);
    for (int i = 0; i < colCount; i++) {
        const char *name = sqlite3_column_name(stmt, i);
        result.columns.push_back(name ? name : "");
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        vector<string> row;
        for (int i = 0; i < colCount; i++) {
            const char *val = (const char *) sqlite3_column_text(stmt, i);
            row.push_back(val ? val : "NULL");
        }
        result.rows.push_back(row);
    }

    if (rc != SQLITE_DONE) {
        result.error = sqlite3_errmsg(_db);
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    return true;
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
