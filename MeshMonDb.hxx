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
    uint32_t nodeId;
    string nodeHex;
    string longName;
    string shortName;
    int hwModel;
    int role;
    time_t firstSeen;
    time_t lastSeen;
    float lastRssi;
    float lastSnr;
    int lastHops;
    uint32_t totalPackets;
    float avgSnr;
    float minSnr;
    float maxSnr;
    float avgRssi;
    bool hasTelemetry;
    int lastBattery;
    float lastVoltage;
    float lastChannelUtil;
    float lastAirUtilTx;
    bool hasPosition;
    double lastLat;
    double lastLon;
    int lastAlt;
};

struct LinkFadingPoint {
    time_t timestamp;
    float avgSnr;
    float minSnr;
    float maxSnr;
    float avgRssi;
    uint32_t count;
};

struct ChannelHealthStat {
    float avgChannelUtil;
    float maxChannelUtil;
    float avgAirUtilTx;
    float maxAirUtilTx;
    uint32_t totalPackets;
    uint32_t badPackets;
    uint32_t duplicatePackets;
    uint32_t uptimeSeconds;
};

struct QueryResult {
    vector<string> columns;
    vector<vector<string>> rows;
    string error;
};

class MeshMonDb {

public:

    MeshMonDb(const string &dbPath = "");
    ~MeshMonDb();

    bool open(const string &dbPath);
    void close(void);

    bool start(void);
    void stop(void);
    void join(void);
    bool isRunning(void) const;

    const string &getDbPath(void) const;
    size_t getDbFileSize(void) const;
    uint64_t getTotalPacketCount(void);

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

    // Maintenance
    size_t pruneOlderThan(time_t thresholdTime);

    // High-Level Analytical Queries
    bool getTrafficSummary(time_t since, TrafficSummary &summary);
    bool getTopTalkers(time_t since, size_t limit, vector<NodeTrafficStat> &stats);
    bool getNeighborStats(time_t since, vector<NeighborStat> &stats);
    bool getEchoStorms(time_t since, size_t limit, vector<EchoStormStat> &stats);
    bool getLinkAsymmetry(time_t since, vector<LinkAsymmetryStat> &stats);
    bool getCriticalRepeaters(time_t since, size_t limit, vector<CriticalRepeaterStat> &stats);
    bool getClockDrift(time_t since, vector<ClockDriftStat> &stats);
    bool getHopDistribution(time_t since, vector<HopStat> &stats);
    bool getPortnumDistribution(time_t since, vector<AppStat> &stats);
    bool getNodeDetail(uint32_t nodeId, time_t since, NodeDetail &detail);
    bool getLinkFading(uint32_t nodeId, time_t since, vector<LinkFadingPoint> &points);
    bool getChannelHealth(time_t since, ChannelHealthStat &health);

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
        EV_TRACEROUTE
    };

    struct DbEvent {
        EventType type;
        time_t meshmonTime;
        time_t rxTime;
        uint32_t packetId;
        uint32_t fromNode;
        uint32_t toNode;
        uint32_t channel;
        float rxRssi;
        float rxSnr;
        int hopStart;
        int hopLimit;
        int hops;
        int portnum;
        int payloadVariant;
        vector<uint8_t> payload;
        bool wantAck;
        bool viaMqtt;

        // Domain event specific payload fields
        string text;
        string shortName;
        string longName;
        int hwModel;
        int role;

        // Telemetry
        string metricType;
        bool hasBattery;
        int batteryLevel;
        bool hasVoltage;
        float voltage;
        bool hasChannelUtil;
        float channelUtilization;
        bool hasAirUtilTx;
        float airUtilTx;
        bool hasTemp;
        float temperature;
        bool hasHum;
        float humidity;
        bool hasPress;
        float pressure;
        bool hasCh1Volt;
        float ch1Voltage;
        bool hasCh1Curr;
        float ch1Current;
        uint32_t uptimeSeconds;

        // Position
        double latitude;
        double longitude;
        int altitude;
        int groundSpeed;
        int groundTrack;
        int satsInView;

        // Traceroute
        uint32_t routeCount;
        vector<uint32_t> routeNodes;
        vector<float> routeSnrs;
    };

    bool initSchema(void);
    void workerLoop(void);
    void processEvent(const DbEvent &ev);

    string _dbPath;
    sqlite3 *_db;
    mutable mutex _dbMutex;

    // Async worker queue
    queue<DbEvent> _queue;
    mutex _queueMutex;
    condition_variable _queueCv;
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
