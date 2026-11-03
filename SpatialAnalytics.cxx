/*
 * SpatialAnalytics.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include "SpatialAnalytics.hxx"
#include "MeshMon.hxx"
#include "MeshMonDb.hxx"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace std;

SpatialAnalytics::SpatialAnalytics(shared_ptr<MeshMon> mon, shared_ptr<MeshMonDb> db)
    : _mon(mon), _db(db)
{
}

SpatialAnalytics::~SpatialAnalytics()
{
}

string SpatialAnalytics::formatNodeHex(uint32_t nodeId)
{
    if (nodeId == 0) return "-";
    char buf[32];
    snprintf(buf, sizeof(buf), "!%08x", nodeId);
    return string(buf);
}

void SpatialAnalytics::getReferenceCoords(double &refLat, double &refLon, int &refAlt, string &refName) const
{
    // Default fallback coordinates (calibrated base)
    refLat = 24.82176;
    refLon = 121.2383232;
    refAlt = 250;
    refName = "Gateway Reference";

    if (_mon != nullptr) {
        SimpleClient::ScopedLock lock(_mon);
        uint32_t myNode = _mon->whoami();
        if (myNode != 0) {
            string shortName = _mon->lookupShortName(myNode);
            if (shortName.empty()) shortName = "Gateway";
            refName = formatNodeHex(myNode) + " (" + shortName + ")";

            const map<uint32_t, meshtastic_Position> &pMap = _mon->positions();
            auto it = pMap.find(myNode);
            if (it != pMap.end() && it->second.latitude_i != 0 && it->second.longitude_i != 0) {
                refLat = it->second.latitude_i * 1e-7;
                refLon = it->second.longitude_i * 1e-7;
                refAlt = it->second.altitude;
            }
        }
    }
}

double SpatialAnalytics::haversineMeters(double lat1, double lon1, double lat2, double lon2)
{
    if ((lat1 == 0.0 && lon1 == 0.0) || (lat2 == 0.0 && lon2 == 0.0)) {
        return 0.0;
    }

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

double SpatialAnalytics::calculateBearing(double lat1, double lon1, double lat2, double lon2)
{
    if ((lat1 == 0.0 && lon1 == 0.0) || (lat2 == 0.0 && lon2 == 0.0)) {
        return 0.0;
    }

    double phi1 = lat1 * M_PI / 180.0;
    double phi2 = lat2 * M_PI / 180.0;
    double dlam = (lon2 - lon1) * M_PI / 180.0;

    double y = sin(dlam) * cos(phi2);
    double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dlam);
    double theta = atan2(y, x);

    double deg = theta * 180.0 / M_PI;
    double normalized = fmod(deg + 360.0, 360.0);
    return normalized;
}

string SpatialAnalytics::bearingToCompass(double bearingDeg)
{
    static const char *directions[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int index = (int) round(bearingDeg / 45.0) % 8;
    if (index < 0) index += 8;
    return string(directions[index]);
}

bool SpatialAnalytics::isNodeOnPremise(uint32_t nodeId, double lat, double lon, double thresholdM) const
{
    if (nodeId == 0) {
        return false;
    }

    // 1. Attached Radio (Gateway) is always On-Premise
    if (_mon != nullptr) {
        SimpleClient::ScopedLock lock(_mon);
        if (nodeId == _mon->whoami()) {
            return true;
        }

        // 2. Administrators & Mates are unconditionally On-Premise
        if (_mon->admins().count(nodeId) > 0) {
            return true;
        }
        if (_mon->mates().count(nodeId) > 0) {
            return true;
        }
    }

    // 3. Distance Rule for GPS-positioned nodes (<= thresholdM is On-Premise)
    if (lat != 0.0 && lon != 0.0) {
        double refLat = 0.0, refLon = 0.0;
        int refAlt = 0;
        string refName;
        getReferenceCoords(refLat, refLon, refAlt, refName);

        double dist = haversineMeters(refLat, refLon, lat, lon);
        if (dist <= thresholdM) {
            return true;
        }
    }

    // 4. Any external unpositioned node or node > thresholdM is Off-Premise (Remote)
    return false;
}

void SpatialAnalytics::calculateNodeMobility(uint32_t nodeId, string &mobility, double &maxDisplacementM)
{
    mobility = "RF-Only";
    maxDisplacementM = 0.0;

    if (_db == nullptr || nodeId == 0) {
        return;
    }

    string sql = "SELECT latitude, longitude FROM positions WHERE node_id = " +
                 to_string(nodeId) + " AND latitude != 0 AND longitude != 0 ORDER BY meshmon_time ASC;";

    QueryResult qres;
    if (!_db->executeRawQuery(sql, qres) || qres.rows.empty()) {
        mobility = "RF-Only";
        return;
    }

    if (qres.rows.size() == 1) {
        mobility = "Stationary";
        maxDisplacementM = 0.0;
        return;
    }

    // Compute maximum distance displacement between historical fixes
    double firstLat = strtod(qres.rows[0][0].c_str(), nullptr);
    double firstLon = strtod(qres.rows[0][1].c_str(), nullptr);
    double maxDist = 0.0;

    for (size_t i = 1; i < qres.rows.size(); i++) {
        double lat = strtod(qres.rows[i][0].c_str(), nullptr);
        double lon = strtod(qres.rows[i][1].c_str(), nullptr);
        double d = haversineMeters(firstLat, firstLon, lat, lon);
        if (d > maxDist) {
            maxDist = d;
        }
    }

    maxDisplacementM = maxDist;
    if (maxDist > 100.0) {
        mobility = "Mobile Tracker";
    } else {
        mobility = "Stationary";
    }
}

json SpatialAnalytics::getSpatialJson(double thresholdM)
{
    lock_guard<mutex> lock(_mutex);

    double refLat = 0.0, refLon = 0.0;
    int refAlt = 0;
    string refName;
    getReferenceCoords(refLat, refLon, refAlt, refName);

    json j;
    j["reference"] = {
        {"name", refName},
        {"lat", refLat},
        {"lon", refLon},
        {"alt", refAlt},
        {"premise_threshold_m", thresholdM}
    };

    if (_db == nullptr) {
        j["nodes"] = json::array();
        j["stats"] = {
            {"total_nodes_with_gps", 0},
            {"on_premise_count", 0},
            {"farthest_distance_meters", 0.0},
            {"farthest_node", "-"}
        };
        return j;
    }

    vector<RemoteNodeAnalyticsRow> rows;
    if (!_db->getRemoteNodeAnalytics(rows)) {
        j["error"] = "Failed to query remote node analytics";
        return j;
    }

    json nodesArr = json::array();
    double farthestDist = 0.0;
    string farthestNode = "-";
    size_t onPremiseCount = 0;

    for (const auto &row : rows) {
        if (!row.hasGps) {
            continue;
        }
        uint32_t nodeId = row.nodeId;
        const string &shortName = row.shortName;
        const string &longName = row.longName;
        double lat = row.lat;
        double lon = row.lon;
        int alt = row.alt;
        time_t lastSeen = (row.lastPosTime > 0) ? row.lastPosTime : row.lastSeen;

        double dist = haversineMeters(refLat, refLon, lat, lon);
        double bearing = calculateBearing(refLat, refLon, lat, lon);
        string compass = bearingToCompass(bearing);

        bool onPremise = isNodeOnPremise(nodeId, lat, lon, thresholdM);
        if (onPremise) {
            onPremiseCount++;
        }

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
            {"bearing_deg", bearing},
            {"compass_dir", compass},
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

    return j;
}

json SpatialAnalytics::getRemoteSummaryJson(double thresholdM)
{
    lock_guard<mutex> lock(_mutex);

    double refLat = 0.0, refLon = 0.0;
    int refAlt = 0;
    string refName;
    getReferenceCoords(refLat, refLon, refAlt, refName);

    json j;
    j["reference"] = {
        {"name", refName},
        {"lat", refLat},
        {"lon", refLon},
        {"alt", refAlt},
        {"premise_threshold_m", thresholdM}
    };

    if (_db == nullptr) {
        j["stats"] = {
            {"total_remote_nodes", 0},
            {"remote_nodes_gps", 0},
            {"remote_nodes_rf_only", 0},
            {"stationary_count", 0},
            {"mobile_tracker_count", 0},
            {"total_remote_packets", 0},
            {"total_mesh_packets", 0},
            {"remote_airtime_pct", 0.0},
            {"avg_distance_km", 0.0},
            {"farthest_distance_km", 0.0},
            {"farthest_node", "-"}
        };
        return j;
    }

    uint64_t totalMeshPackets = _db->getTotalPacketCount();
    vector<RemoteNodeAnalyticsRow> rows;
    if (!_db->getRemoteNodeAnalytics(rows)) {
        j["error"] = "Failed to query remote node analytics";
        return j;
    }

    size_t remoteGpsCount = 0;
    size_t remoteRfOnlyCount = 0;
    size_t stationaryCount = 0;
    size_t mobileCount = 0;
    uint64_t remotePackets = 0;
    double sumDistance = 0.0;
    double farthestDistance = 0.0;
    string farthestNode = "-";

    for (const auto &row : rows) {
        if (isNodeOnPremise(row.nodeId, row.lat, row.lon, thresholdM)) {
            continue;
        }

        if (row.hasGps) {
            remoteGpsCount++;
            double dist = haversineMeters(refLat, refLon, row.lat, row.lon);
            sumDistance += dist;
            if (dist > farthestDistance) {
                farthestDistance = dist;
                farthestNode = formatNodeHex(row.nodeId) + " (" + row.shortName + ")";
            }

            if (row.mobility == "Mobile Tracker") {
                mobileCount++;
            } else {
                stationaryCount++;
            }
        } else {
            remoteRfOnlyCount++;
        }

        remotePackets += row.totalPackets;
    }

    size_t totalRemote = remoteGpsCount + remoteRfOnlyCount;
    double avgDistKm = (remoteGpsCount > 0) ? (sumDistance / remoteGpsCount / 1000.0) : 0.0;
    double farthestDistKm = farthestDistance / 1000.0;
    float airtimePct = (totalMeshPackets > 0) ? ((float) remotePackets * 100.0f / (float) totalMeshPackets) : 0.0f;

    j["stats"] = {
        {"total_remote_nodes", totalRemote},
        {"remote_nodes_gps", remoteGpsCount},
        {"remote_nodes_rf_only", remoteRfOnlyCount},
        {"stationary_count", stationaryCount},
        {"mobile_tracker_count", mobileCount},
        {"total_remote_packets", remotePackets},
        {"total_mesh_packets", totalMeshPackets},
        {"remote_airtime_pct", airtimePct},
        {"avg_distance_km", avgDistKm},
        {"farthest_distance_km", farthestDistKm},
        {"farthest_node", farthestNode}
    };

    return j;
}

json SpatialAnalytics::getRemoteNodesJson(double thresholdM)
{
    lock_guard<mutex> lock(_mutex);

    double refLat = 0.0, refLon = 0.0;
    int refAlt = 0;
    string refName;
    getReferenceCoords(refLat, refLon, refAlt, refName);

    json j;
    j["reference"] = {
        {"name", refName},
        {"lat", refLat},
        {"lon", refLon},
        {"alt", refAlt},
        {"premise_threshold_m", thresholdM}
    };

    if (_db == nullptr) {
        j["nodes"] = json::array();
        return j;
    }

    vector<RemoteNodeAnalyticsRow> rows;
    if (!_db->getRemoteNodeAnalytics(rows)) {
        j["error"] = "Failed to query remote node analytics";
        return j;
    }

    json nodesArr = json::array();

    for (const auto &row : rows) {
        if (isNodeOnPremise(row.nodeId, row.lat, row.lon, thresholdM)) {
            continue;
        }

        RemoteNodeProfile prof;
        prof.nodeId = row.nodeId;
        prof.nodeHex = formatNodeHex(row.nodeId);
        prof.shortName = row.shortName;
        prof.longName = row.longName;
        prof.hwModel = row.hwModel;
        prof.role = row.role;
        prof.firstSeen = row.firstSeen;
        prof.lastSeen = row.lastSeen;
        prof.hasGps = row.hasGps;
        prof.lat = row.lat;
        prof.lon = row.lon;
        prof.alt = row.alt;
        prof.totalPackets = row.totalPackets;
        prof.totalBytes = row.totalBytes;
        prof.avgHops = row.avgHops;
        prof.avgSnr = row.avgSnr;
        prof.minSnr = row.minSnr;
        prof.maxSnr = row.maxSnr;
        prof.avgRssi = row.avgRssi;
        prof.directPackets = row.directPackets;
        prof.relayedPackets = row.relayedPackets;

        if (row.hasGps) {
            prof.distanceMeters = haversineMeters(refLat, refLon, row.lat, row.lon);
            prof.bearingDeg = calculateBearing(refLat, refLon, row.lat, row.lon);
            prof.compassDir = bearingToCompass(prof.bearingDeg);
            prof.altDeltaMeters = row.alt - refAlt;
            prof.mobility = row.mobility;
            prof.maxDisplacementM = row.maxDisplacementM;
        } else {
            prof.distanceMeters = -1.0;
            prof.bearingDeg = -1.0;
            prof.compassDir = "--";
            prof.altDeltaMeters = 0;
            prof.mobility = "RF-Only";
            prof.maxDisplacementM = 0.0;
        }

        json appObj = json::object();
        for (const auto &kv : row.portnumCounts) {
            appObj[to_string(kv.first)] = kv.second;
        }

        nodesArr.push_back({
            {"node_id", prof.nodeId},
            {"node_hex", prof.nodeHex},
            {"short_name", prof.shortName},
            {"long_name", prof.longName},
            {"hw_model", prof.hwModel},
            {"role", prof.role},
            {"has_gps", prof.hasGps},
            {"lat", prof.lat},
            {"lon", prof.lon},
            {"alt", prof.alt},
            {"distance_meters", prof.distanceMeters},
            {"bearing_deg", prof.bearingDeg},
            {"compass_dir", prof.compassDir},
            {"alt_delta_meters", prof.altDeltaMeters},
            {"avg_snr", prof.avgSnr},
            {"min_snr", prof.minSnr},
            {"max_snr", prof.maxSnr},
            {"avg_rssi", prof.avgRssi},
            {"avg_hops", prof.avgHops},
            {"direct_packets", prof.directPackets},
            {"relayed_packets", prof.relayedPackets},
            {"total_packets", prof.totalPackets},
            {"total_bytes", prof.totalBytes},
            {"mobility", prof.mobility},
            {"max_displacement_m", prof.maxDisplacementM},
            {"first_seen", prof.firstSeen},
            {"last_seen", prof.lastSeen},
            {"app_spectrum", appObj}
        });
    }

    sort(nodesArr.begin(), nodesArr.end(), [](const json &a, const json &b) {
        return a["total_packets"].get<uint32_t>() > b["total_packets"].get<uint32_t>();
    });

    j["nodes"] = nodesArr;
    return j;
}

json SpatialAnalytics::getTopologyRoutesJson(size_t limit)
{
    lock_guard<mutex> lock(_mutex);

    json j = json::array();
    if (_db == nullptr) {
        return j;
    }

    vector<TopologyRouteRow> rows;
    if (!_db->getTopologyRoutes(limit, rows)) {
        return j;
    }

    for (const auto &row : rows) {
        string fromShort = !row.fromShort.empty() ? row.fromShort : formatNodeHex(row.fromNode);
        string toShort = !row.toShort.empty() ? row.toShort : formatNodeHex(row.toNode);

        vector<uint32_t> rNodes;
        vector<string> rHexes;
        vector<string> rShorts;
        vector<float> rSnrs;

        stringstream ssNodes(row.routeNodes);
        string item;
        while (getline(ssNodes, item, ',')) {
            if (item.empty()) continue;
            uint32_t nId = 0;
            if (item[0] == '!') {
                nId = (uint32_t) strtoul(item.c_str() + 1, nullptr, 16);
            } else {
                nId = (uint32_t) strtoul(item.c_str(), nullptr, 10);
            }
            rNodes.push_back(nId);
            rHexes.push_back(formatNodeHex(nId));
            string sName;
            if (_mon != nullptr) {
                SimpleClient::ScopedLock mLock(_mon);
                sName = _mon->lookupShortName(nId);
            }
            if (sName.empty()) sName = formatNodeHex(nId);
            rShorts.push_back(sName);
        }

        stringstream ssSnrs(row.routeSnrs);
        while (getline(ssSnrs, item, ',')) {
            if (item.empty()) continue;
            rSnrs.push_back((float) strtod(item.c_str(), nullptr));
        }

        j.push_back({
            {"id", row.id},
            {"timestamp", row.meshmonTime},
            {"from_node", row.fromNode},
            {"from_hex", formatNodeHex(row.fromNode)},
            {"from_short", fromShort},
            {"to_node", row.toNode},
            {"to_hex", (row.toNode == 0xffffffffU) ? "^all" : formatNodeHex(row.toNode)},
            {"to_short", toShort},
            {"route_count", row.routeCount},
            {"route_nodes", rNodes},
            {"route_hexes", rHexes},
            {"route_shorts", rShorts},
            {"route_snrs", rSnrs}
        });
    }

    return j;
}

json SpatialAnalytics::getTopologyAsymmetryJson()
{
    lock_guard<mutex> lock(_mutex);

    json j = json::array();
    if (_db == nullptr) {
        return j;
    }

    vector<TopologyAsymmetryRow> rows;
    if (!_db->getTopologyAsymmetry(30, rows)) {
        return j;
    }

    for (const auto &row : rows) {
        uint32_t nodeId = row.nodeId;
        string shortName = !row.shortName.empty() ? row.shortName : formatNodeHex(nodeId);
        float fwdSnr = row.forwardSnr;
        float fwdRssi = row.forwardRssi;
        uint32_t count = row.sampleCount;

        float revSnr = fwdSnr;
        bool hasReverse = false;
        if (!row.reverseSnrs.empty()) {
            size_t comma = row.reverseSnrs.find(',');
            if (comma != string::npos) {
                revSnr = (float) strtod(row.reverseSnrs.substr(0, comma).c_str(), nullptr);
                hasReverse = true;
            } else {
                revSnr = (float) strtod(row.reverseSnrs.c_str(), nullptr);
                hasReverse = true;
            }
        }

        float delta = hasReverse ? fabs(fwdSnr - revSnr) : 0.0f;

        j.push_back({
            {"node_id", nodeId},
            {"node_hex", formatNodeHex(nodeId)},
            {"short_name", shortName},
            {"forward_snr", fwdSnr},
            {"reverse_snr", revSnr},
            {"snr_delta", delta},
            {"has_reverse_measurement", hasReverse},
            {"forward_rssi", fwdRssi},
            {"sample_count", count}
        });
    }

    return j;
}

json SpatialAnalytics::getTopologyCentroidsJson()
{
    lock_guard<mutex> lock(_mutex);

    json j = json::array();
    if (_db == nullptr) {
        return j;
    }

    vector<TopologyCentroidRow> rows;
    if (!_db->getTopologyCentroids(20, rows)) {
        return j;
    }

    for (const auto &row : rows) {
        if (row.neighborCount <= 0) {
            continue;
        }
        uint32_t nodeId = row.nodeId;
        string shortName = !row.shortName.empty() ? row.shortName : formatNodeHex(nodeId);
        int count = row.neighborCount;
        double estRadiusM = (count == 1) ? 2500.0 : (count == 2 ? 1500.0 : 800.0);

        j.push_back({
            {"node_id", nodeId},
            {"node_hex", formatNodeHex(nodeId)},
            {"short_name", shortName},
            {"estimated_lat", row.estLat},
            {"estimated_lon", row.estLon},
            {"confidence_radius_m", estRadiusM},
            {"neighbor_count", count}
        });
    }

    return j;
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
