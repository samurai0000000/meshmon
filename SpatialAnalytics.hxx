/*
 * SpatialAnalytics.hxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#ifndef SPATIAL_ANALYTICS_HXX
#define SPATIAL_ANALYTICS_HXX

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <ctime>
#include <nlohmann/json.hpp>

class MeshMon;
class MeshMonDb;

using namespace std;
using json = nlohmann::json;

struct RemoteNodeProfile {
    uint32_t nodeId;
    string nodeHex;
    string shortName;
    string longName;
    int hwModel;
    int role;
    bool hasGps;
    double lat;
    double lon;
    int alt;
    double distanceMeters;
    double bearingDeg;
    string compassDir;
    int altDeltaMeters;
    float avgSnr;
    float minSnr;
    float maxSnr;
    float avgRssi;
    float avgHops;
    uint32_t directPackets;
    uint32_t relayedPackets;
    uint32_t totalPackets;
    uint64_t totalBytes;
    string mobility; // "Stationary", "Mobile Tracker", "RF-Only"
    double maxDisplacementM;
    time_t firstSeen;
    time_t lastSeen;
    map<int, uint32_t> portnumCounts;

    RemoteNodeProfile()
        : nodeId(0), hwModel(0), role(0), hasGps(false),
          lat(0.0), lon(0.0), alt(0), distanceMeters(0.0),
          bearingDeg(0.0), altDeltaMeters(0), avgSnr(0.0f),
          minSnr(0.0f), maxSnr(0.0f), avgRssi(0.0f), avgHops(0.0f),
          directPackets(0), relayedPackets(0), totalPackets(0),
          totalBytes(0), mobility("RF-Only"), maxDisplacementM(0.0),
          firstSeen(0), lastSeen(0) {}
};

struct TopologyRouteInfo {
    int64_t id;
    time_t meshmonTime;
    uint32_t fromNode;
    string fromHex;
    string fromShort;
    uint32_t toNode;
    string toHex;
    string toShort;
    int routeCount;
    vector<uint32_t> routeNodes;
    vector<string> routeHexes;
    vector<string> routeShorts;
    vector<float> routeSnrs;

    TopologyRouteInfo()
        : id(0), meshmonTime(0), fromNode(0), toNode(0), routeCount(0) {}
};

struct LinkAsymmetryInfo {
    uint32_t remoteNodeId;
    string remoteHex;
    string remoteShort;
    float forwardSnr;
    float reverseSnr;
    float snrDelta;
    float forwardRssi;
    uint32_t sampleCount;

    LinkAsymmetryInfo()
        : remoteNodeId(0), forwardSnr(0.0f), reverseSnr(0.0f),
          snrDelta(0.0f), forwardRssi(0.0f), sampleCount(0) {}
};

struct CentroidEstimate {
    uint32_t nodeId;
    string nodeHex;
    string shortName;
    double estLat;
    double estLon;
    double confidenceRadiusM;
    int hearingNeighborCount;

    CentroidEstimate()
        : nodeId(0), estLat(0.0), estLon(0.0),
          confidenceRadiusM(0.0), hearingNeighborCount(0) {}
};

class SpatialAnalytics {

public:

    SpatialAnalytics(shared_ptr<MeshMon> mon, shared_ptr<MeshMonDb> db);
    ~SpatialAnalytics();

    // Origin Reference Management
    void getReferenceCoords(double &refLat, double &refLon, int &refAlt, string &refName) const;

    // Spatial & Geographic Mathematics
    static double haversineMeters(double lat1, double lon1, double lat2, double lon2);
    static double calculateBearing(double lat1, double lon1, double lat2, double lon2);
    static string bearingToCompass(double bearingDeg);
    static string formatNodeHex(uint32_t nodeId);

    // Authority-Aware Node Classification
    bool isNodeOnPremise(uint32_t nodeId, double lat, double lon, double thresholdM = 50.0) const;

    // High-Level JSON Analytics Export
    json getSpatialJson(double thresholdM = 50.0);
    json getRemoteSummaryJson(double thresholdM = 50.0);
    json getRemoteNodesJson(double thresholdM = 50.0);
    json getTopologyRoutesJson(size_t limit = 50);
    json getTopologyAsymmetryJson();
    json getTopologyCentroidsJson();

private:

    shared_ptr<MeshMon> _mon;
    shared_ptr<MeshMonDb> _db;
    mutable mutex _mutex;

    void calculateNodeMobility(uint32_t nodeId, string &mobility, double &maxDisplacementM);
};

#endif /* SPATIAL_ANALYTICS_HXX */

/*
 * Local variables:
 * mode: C++
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
