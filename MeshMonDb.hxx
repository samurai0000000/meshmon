/*
 * MeshMonDb.hxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#ifndef MESHMON_DB_HXX
#define MESHMON_DB_HXX

#include <sqlite3.h>
#include <meshtastic/mesh.pb.h>
#include <meshtastic/telemetry.pb.h>
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

using namespace std;

struct TrafficSummary {
    uint32_t totalPackets;
    uint64_t totalBytes;
    uint32_t broadcastPackets;
    uint32_t unicastPackets;
    uint32_t directPackets;
    uint32_t relayedPackets;
    time_t oldestPacket;
    time_t newestPacket;
    map<int, uint32_t> portnumCounts;

    TrafficSummary()
        : totalPackets(0), totalBytes(0), broadcastPackets(0),
          unicastPackets(0), directPackets(0), relayedPackets(0),
          oldestPacket(0), newestPacket(0) {}
};

struct NodeTrafficStat {
    uint32_t nodeId;
    string nodeHex;
    string longName;
    string shortName;
    uint32_t packetCount;
    uint64_t totalBytes;
    float avgHops;
    float avgSnr;
    float avgRssi;
    time_t lastSeen;
};

struct NeighborStat {
    uint32_t nodeId;
    string nodeHex;
    string longName;
    string shortName;
    uint32_t packetCount;
    float avgSnr;
    float minSnr;
    float maxSnr;
    float avgRssi;
    time_t lastSeen;
};

struct EchoStormStat {
    uint32_t packetId;
    uint32_t fromNode;
    string fromHex;
    uint32_t echoCount;
    uint32_t minHops;
    uint32_t maxHops;
    uint32_t durationSec;
    time_t firstArrival;
    time_t lastArrival;
};

struct LinkAsymmetryStat {
    uint32_t nodeId;
    string nodeHex;
    string longName;
    string shortName;
    float rxSnr;
    float rxRssi;
    uint32_t sampleCount;
};

struct CriticalRepeaterStat {
    uint32_t repeaterId;
    string repeaterHex;
    string longName;
    string shortName;
    uint32_t relayCount;
    float avgSnr;
};

struct ClockDriftStat {
    uint32_t nodeId;
    string nodeHex;
    string longName;
    string shortName;
    uint32_t sampleCount;
    int32_t avgSkewSec;
    int32_t minSkewSec;
    int32_t maxSkewSec;
};

struct HopStat {
    int hops;
    uint32_t packetCount;
    float pctShare;
};

struct AppStat {
    int portnum;
    string appName;
    uint32_t packetCount;
    uint64_t totalBytes;
    float pctShare;
};

struct NodeDetail {
    uint32_t nodeId = 0;
    string nodeHex;
    string longName;
    string shortName;
    int hwModel = 0;
    int role = 0;
    time_t firstSeen = 0;
    time_t lastSeen = 0;
    float lastRssi = 0.0f;
    float lastSnr = 0.0f;
    int lastHops = 0;
    uint32_t totalPackets = 0;
    float avgSnr = 0.0f;
    float minSnr = 0.0f;
    float maxSnr = 0.0f;
    float avgRssi = 0.0f;
    bool hasTelemetry = false;
    int lastBattery = 0;
    float lastVoltage = 0.0f;
    float lastChannelUtil = 0.0f;
    float lastAirUtilTx = 0.0f;
    bool hasPosition = false;
    double lastLat = 0.0;
    double lastLon = 0.0;
    int lastAlt = 0;
};

struct LinkFadingPoint {
    time_t timestamp = 0;
    float avgSnr = 0.0f;
    float minSnr = 0.0f;
    float maxSnr = 0.0f;
    float avgRssi = 0.0f;
    uint32_t count = 0;
};

struct ChannelHealthStat {
    float avgChannelUtil = 0.0f;
    float maxChannelUtil = 0.0f;
    float avgAirUtilTx = 0.0f;
    float maxAirUtilTx = 0.0f;
    uint32_t totalPackets = 0;
    uint32_t badPackets = 0;
    uint32_t duplicatePackets = 0;
    uint32_t uptimeSeconds = 0;
};

struct AutomationEvent {
    int64_t id;
    time_t meshmonTime;
    uint32_t nodeId;
    string nodeHex;
    string deviceType;
    string direction;
    string subsystem;
    string commandName;
    string actionParam;
    string status;
    string initiator;
    int32_t rttMs;
};

struct LatencyTrendPoint {
    time_t timestamp;
    float avgRtt;
    uint32_t minRtt;
    uint32_t maxRtt;
    uint32_t count;
};

struct PumpAnalytics {
    uint32_t fishRunSec;
    float fishDutyPct;
    uint32_t upRunCount;
    uint32_t upRunSec;
    uint32_t upCutoffTriggers;
    uint32_t moistureEvents;
    float avgMoisture;
    uint32_t totalCommands;
    uint32_t ackedCommands;

    PumpAnalytics()
        : fishRunSec(0), fishDutyPct(0.0f), upRunCount(0), upRunSec(0),
          upCutoffTriggers(0), moistureEvents(0), avgMoisture(0.0f),
          totalCommands(0), ackedCommands(0) {}
};

struct RoofAnalytics {
    uint32_t amplifyRunSec;
    float amplifyDutyPct;
    uint32_t wifiReports;
    float avgWifiRssi;
    float minWifiRssi;
    float maxWifiRssi;
    float avgCpuTemp;
    float maxCpuTemp;
    uint32_t resetEvents;
    uint32_t totalCommands;

    RoofAnalytics()
        : amplifyRunSec(0), amplifyDutyPct(0.0f), wifiReports(0),
          avgWifiRssi(0.0f), minWifiRssi(0.0f), maxWifiRssi(0.0f),
          avgCpuTemp(0.0f), maxCpuTemp(0.0f), resetEvents(0),
          totalCommands(0) {}
};

struct RoomAnalytics {
    uint32_t acRunSec;
    float acDutyPct;
    float avgAcTargetTemp;
    uint32_t tvRunSec;
    float tvDutyPct;
    uint32_t tvChanChanges;
    float avgBoardTemp;
    float maxBoardTemp;
    uint32_t totalCommands;

    RoomAnalytics()
        : acRunSec(0), acDutyPct(0.0f), avgAcTargetTemp(0.0f),
          tvRunSec(0), tvDutyPct(0.0f), tvChanChanges(0),
          avgBoardTemp(0.0f), maxBoardTemp(0.0f), totalCommands(0) {}
};

struct DbAutomationNodeSummary {
    uint32_t nodeId;
    string nodeHex;
    string longName;
    string shortName;
    string deviceType;
    string rollcallPayload;
    time_t firstSeen;
    time_t lastSeen;
    uint32_t rebootCount;

    // Hydrated from automation_nodes, empty for rows that predate it
    string version;
    string hardware;
    string capabilities;
    string acIrProtocol;
    string tvIrProtocol;

    DbAutomationNodeSummary()
        : nodeId(0), firstSeen(0), lastSeen(0), rebootCount(0) {}
};

struct QueryResult {
    vector<string> columns;
    vector<vector<string>> rows;
    string error;
};

struct TextMessageRow {
    int64_t id;
    int64_t rxTime;
    uint32_t fromNode;
    uint32_t toNode;
    uint32_t channel;
    string text;
    double rxSnr;
    int rxRssi;

    TextMessageRow()
        : id(0), rxTime(0), fromNode(0), toNode(0), channel(0),
          rxSnr(0.0), rxRssi(0) {}
};

struct RemoteNodeAnalyticsRow {
    uint32_t nodeId;
    string shortName;
    string longName;
    int hwModel;
    int role;
    time_t firstSeen;
    time_t lastSeen;
    bool hasGps;
    double lat;
    double lon;
    int alt;
    time_t lastPosTime;
    uint32_t totalPackets;
    uint64_t totalBytes;
    float avgHops;
    float avgSnr;
    float minSnr;
    float maxSnr;
    float avgRssi;
    uint32_t directPackets;
    uint32_t relayedPackets;
    bool hasTelemetry;
    int lastBattery;
    float lastVoltage;
    double maxDisplacementM;
    string mobility;
    map<int, uint32_t> portnumCounts;

    RemoteNodeAnalyticsRow()
        : nodeId(0), hwModel(0), role(0), firstSeen(0), lastSeen(0),
          hasGps(false), lat(0.0), lon(0.0), alt(0), lastPosTime(0),
          totalPackets(0), totalBytes(0), avgHops(0.0f), avgSnr(0.0f),
          minSnr(0.0f), maxSnr(0.0f), avgRssi(0.0f), directPackets(0),
          relayedPackets(0), hasTelemetry(false), lastBattery(-1),
          lastVoltage(0.0f), maxDisplacementM(0.0), mobility("RF-Only") {}
};

struct TopologyRouteRow {
    int64_t id;
    time_t meshmonTime;
    uint32_t fromNode;
    uint32_t toNode;
    int routeCount;
    string routeNodes;
    string routeSnrs;
    string fromShort;
    string toShort;

    TopologyRouteRow()
        : id(0), meshmonTime(0), fromNode(0), toNode(0), routeCount(0) {}
};

struct TopologyAsymmetryRow {
    uint32_t nodeId;
    string shortName;
    float forwardSnr;
    float forwardRssi;
    uint32_t sampleCount;
    string reverseSnrs;

    TopologyAsymmetryRow()
        : nodeId(0), forwardSnr(0.0f), forwardRssi(0.0f), sampleCount(0) {}
};

struct TopologyCentroidRow {
    uint32_t nodeId;
    string shortName;
    double estLat;
    double estLon;
    int neighborCount;

    TopologyCentroidRow()
        : nodeId(0), estLat(0.0), estLon(0.0), neighborCount(0) {}
};

class MeshMonDb {

public:

    static constexpr size_t MAX_DB_QUEUE_SIZE = 4096;

    MeshMonDb(const string &dbPath = "");
    ~MeshMonDb();

    bool open(const string &dbPath);
    void close(void);

    bool start(void);
    void stop(void);
    void join(void);
    bool isRunning(void) const;
    void flush(void);
    size_t getQueueDepth(void) const;
    size_t getPendingQueueSize(void) const { return getQueueDepth(); }

    const string &getDbPath(void) const;
    size_t getDbFileSize(void) const;
    uint64_t getTotalPacketCount(void);
    uint32_t getTotalNodeCount(void);
    uint64_t getTotalPayloadBytes(void);
    uint64_t getTotalTextMessageCount(void);

    // Non-blocking asynchronous ingestion methods (Host NTP timestamp ground truth)
    void enqueuePacket(const meshtastic_MeshPacket &packet, time_t meshmonTime);
    void enqueueNodeInfo(uint32_t nodeId, const string &longName,
                         const string &shortName, int hwModel, int role,
                         time_t meshmonTime);
    void enqueueDeviceMetrics(const meshtastic_MeshPacket &packet,
                              const meshtastic_DeviceMetrics &metrics,
                              time_t meshmonTime);
    void enqueueEnvironmentMetrics(const meshtastic_MeshPacket &packet,
                                   const meshtastic_EnvironmentMetrics &metrics,
                                   time_t meshmonTime);
    void enqueuePowerMetrics(const meshtastic_MeshPacket &packet,
                             const meshtastic_PowerMetrics &metrics,
                             time_t meshmonTime);
    void enqueueLocalStats(const meshtastic_MeshPacket &packet,
                           const meshtastic_LocalStats &stats,
                           time_t meshmonTime);
    void enqueuePosition(const meshtastic_MeshPacket &packet,
                         const meshtastic_Position &pos,
                         time_t meshmonTime);
    void enqueueTextMessage(const meshtastic_MeshPacket &packet,
                            const string &message,
                            time_t meshmonTime);
    void enqueueTraceRoute(const meshtastic_MeshPacket &packet,
                           const meshtastic_RouteDiscovery &routeDiscovery,
                           time_t meshmonTime);
    void enqueueAutomationEvent(time_t meshmonTime, uint32_t nodeId,
                                const string &deviceType, const string &direction,
                                const string &subsystem, const string &commandName,
                                const string &actionParam, const string &status,
                                const string &initiator, int32_t rttMs = 0);

    // HomeMesh automation node registry (survives gateway restarts)
    bool upsertAutomationNode(const DbAutomationNodeSummary &node);

    // Maintenance
    size_t pruneOlderThan(time_t thresholdTime);

    // High-Level Analytical Queries
    bool getTrafficSummary(time_t since, TrafficSummary &summary);
    bool getTrafficRatios(time_t since, float &directPct, float &bcastPct, float &avgHops);
    bool getTopTalkers(time_t since, size_t limit, vector<NodeTrafficStat> &stats);
    bool getTopTalkerSummary(time_t since, string &topNode, uint32_t &topPackets);
    bool getNeighborStats(time_t since, vector<NeighborStat> &stats);
    bool getBestNeighborSummary(time_t since, string &bestNeighbor, float &bestSnr);
    bool getEchoStorms(time_t since, size_t limit, vector<EchoStormStat> &stats);
    bool getMaxEchoMultiplier(time_t since, uint32_t &maxMultiplier);
    bool getLinkAsymmetry(time_t since, vector<LinkAsymmetryStat> &stats);
    bool getCriticalRepeaters(time_t since, size_t limit, vector<CriticalRepeaterStat> &stats);
    bool getCriticalRelaySummary(time_t since, string &topRelay, uint32_t &relayedCount);
    bool getClockDrift(time_t since, vector<ClockDriftStat> &stats);
    bool getMaxClockDrift(time_t since, float &maxSkewSec);
    bool getHopDistribution(time_t since, vector<HopStat> &stats);
    bool getPortnumDistribution(time_t since, vector<AppStat> &stats);
    bool getNodeDetail(uint32_t nodeId, time_t since, NodeDetail &detail);
    bool getLinkFading(uint32_t nodeId, time_t since, vector<LinkFadingPoint> &points);
    bool getChannelHealth(time_t since, ChannelHealthStat &health);

    // Single-Pass Batch Queries for WebServer & SpatialAnalytics
    bool getRecentTextMessages(int limit, vector<TextMessageRow> &out);
    bool getRemoteNodeAnalytics(vector<RemoteNodeAnalyticsRow> &out);
    bool getTopologyRoutes(size_t limit, vector<TopologyRouteRow> &out);
    bool getTopologyAsymmetry(size_t limit, vector<TopologyAsymmetryRow> &out);
    bool getTopologyCentroids(size_t limit, vector<TopologyCentroidRow> &out);

    // Automation Queries
    bool getPumpAnalytics(uint32_t nodeId, time_t since, PumpAnalytics &analytics);
    bool getRoofAnalytics(uint32_t nodeId, time_t since, RoofAnalytics &analytics);
    bool getRoomAnalytics(uint32_t nodeId, time_t since, RoomAnalytics &analytics);
    bool getAutomationHistory(size_t limit, vector<AutomationEvent> &events);
    bool getLatencyTrend(uint32_t nodeId, time_t since, vector<LatencyTrendPoint> &points);
    bool getAutomationEventCount(time_t since, uint32_t &count);
    bool getDiscoveredAutomationNodes(vector<DbAutomationNodeSummary> &nodes);

    // Raw SQL Execution
    bool executeRawQuery(const string &sql, QueryResult &result);

    static string portnumToString(int portnum);
    static string formatNodeHex(uint32_t nodeId);

private:

    enum EventType {
        EV_PACKET,
        EV_NODE_INFO,
        EV_DEVICE_METRICS,
        EV_ENV_METRICS,
        EV_POWER_METRICS,
        EV_LOCAL_STATS,
        EV_POSITION,
        EV_TEXT_MESSAGE,
        EV_TRACEROUTE,
        EV_AUTOMATION_EVENT
    };

    struct DbEvent {
        EventType type = EV_PACKET;
        time_t meshmonTime = 0;
        time_t rxTime = 0;
        uint32_t packetId = 0;
        uint32_t fromNode = 0;
        uint32_t toNode = 0;
        uint32_t channel = 0;
        float rxRssi = 0.0f;
        float rxSnr = 0.0f;
        int hopStart = 0;
        int hopLimit = 0;
        int hops = 0;
        int portnum = 0;
        int payloadVariant = 0;
        vector<uint8_t> payload;
        bool wantAck = false;
        bool viaMqtt = false;

        // Domain event specific payload fields
        string text;
        string shortName;
        string longName;
        int hwModel = 0;
        int role = 0;

        // Telemetry
        string metricType;
        bool hasBattery = false;
        int batteryLevel = 0;
        bool hasVoltage = false;
        float voltage = 0.0f;
        bool hasChannelUtil = false;
        float channelUtilization = 0.0f;
        bool hasAirUtilTx = false;
        float airUtilTx = 0.0f;
        bool hasTemp = false;
        float temperature = 0.0f;
        bool hasHum = false;
        float humidity = 0.0f;
        bool hasPress = false;
        float pressure = 0.0f;
        bool hasCh1Volt = false;
        float ch1Voltage = 0.0f;
        bool hasCh1Curr = false;
        float ch1Current = 0.0f;
        uint32_t uptimeSeconds = 0;

        // Position
        double latitude = 0.0;
        double longitude = 0.0;
        int altitude = 0;
        int groundSpeed = 0;
        int groundTrack = 0;
        int satsInView = 0;

        // Traceroute
        uint32_t routeCount = 0;
        vector<uint32_t> routeNodes;
        vector<float> routeSnrs;

        // Automation Event
        string deviceType;
        string direction;
        string subsystem;
        string commandName;
        string actionParam;
        string status;
        string initiator;
        int32_t rttMs = 0;
    };

    bool initSchema(void);
    void prepareStatements(void);
    void finalizeStatements(void);
    void ensureNodeExists(uint32_t nodeId, int64_t ts);
    void enqueueEvent(const DbEvent &ev);
    void workerLoop(void);
    void processEvent(const DbEvent &ev);

    string _dbPath;
    sqlite3 *_db;
    mutable mutex _dbMutex;

    // Cached hot-path prepared statements
    sqlite3_stmt *_stmtEnsureNode;
    sqlite3_stmt *_stmtUpsertNodePacket;
    sqlite3_stmt *_stmtInsertPacket;
    sqlite3_stmt *_stmtUpsertNodeInfo;
    sqlite3_stmt *_stmtInsertTelemetry;
    sqlite3_stmt *_stmtInsertPosition;
    sqlite3_stmt *_stmtInsertTextMsg;
    sqlite3_stmt *_stmtInsertTraceroute;
    sqlite3_stmt *_stmtInsertAutoEvent;

    // Async worker queue
    queue<DbEvent> _queue;
    mutable mutex _queueMutex;
    condition_variable _queueCv;
    condition_variable _idleCv;
    uint64_t _enqueuedSeq;
    uint64_t _processedSeq;
    thread _workerThread;
    atomic<bool> _running;
    atomic<bool> _stopRequested;

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
