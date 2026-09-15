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
        if (nodeId == _mon->whoami()) {
            return true;
        }

        // 2. Administrators & Mates are unconditionally On-Premise
        if (_mon->admins().find(nodeId) != _mon->admins().end()) {
            return true;
        }
        if (_mon->mates().find(nodeId) != _mon->mates().end()) {
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

    string sql =
        "SELECT p.node_id, n.short_name, n.long_name, p.latitude, p.longitude, p.altitude, max(p.meshmon_time) "
        "FROM positions p "
        "LEFT JOIN nodes n ON p.node_id = n.node_id "
        "WHERE p.latitude != 0 AND p.longitude != 0 "
        "GROUP BY p.node_id;";

    QueryResult qres;
    if (!_db->executeRawQuery(sql, qres)) {
        j["error"] = qres.error;
        return j;
    }

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
        double bearing = calculateBearing(refLat, refLon, lat, lon);
        string compass = bearingToCompass(bearing);

        // Use authoritative classification (respects mates & admins)
        bool onPremise = isNodeOnPremise(nodeId, lat, lon, thresholdM);
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

    // Get total mesh packet count for airtime share calculation
    uint64_t totalMeshPackets = _db->getTotalPacketCount();

    // Query all nodes with their latest GPS positions and first/last seen
    string sql =
        "SELECT n.node_id, coalesce(n.short_name, ''), coalesce(n.long_name, ''), "
        "coalesce(n.hw_model, 0), coalesce(n.role, 0), "
        "coalesce(p.latitude, 0.0), coalesce(p.longitude, 0.0), coalesce(p.altitude, 0), "
        "coalesce(n.first_seen, 0), coalesce(n.last_seen, 0) "
        "FROM nodes n "
        "LEFT JOIN (SELECT node_id, latitude, longitude, altitude, max(meshmon_time) "
        "           FROM positions WHERE latitude != 0 AND longitude != 0 GROUP BY node_id) p "
        "ON n.node_id = p.node_id;";

    QueryResult qres;
    if (!_db->executeRawQuery(sql, qres)) {
        j["error"] = qres.error;
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

    for (const auto &row : qres.rows) {
        if (row.size() < 10) continue;
        uint32_t nodeId = (uint32_t) strtoul(row[0].c_str(), nullptr, 10);
        string shortName = (row[1] != "NULL" && !row[1].empty()) ? row[1] : "-";
        double lat = strtod(row[5].c_str(), nullptr);
        double lon = strtod(row[6].c_str(), nullptr);

        // Check if On-Premise
        if (isNodeOnPremise(nodeId, lat, lon, thresholdM)) {
            continue; // Skip On-Premise
        }

        // Node is Off-Premise (Remote)
        bool hasGps = (lat != 0.0 && lon != 0.0);
        if (hasGps) {
            remoteGpsCount++;
            double dist = haversineMeters(refLat, refLon, lat, lon);
            sumDistance += dist;
            if (dist > farthestDistance) {
                farthestDistance = dist;
                farthestNode = formatNodeHex(nodeId) + " (" + shortName + ")";
            }

            string mobility;
            double displacement = 0.0;
            calculateNodeMobility(nodeId, mobility, displacement);
            if (mobility == "Mobile Tracker") {
                mobileCount++;
            } else {
                stationaryCount++;
            }
        } else {
            remoteRfOnlyCount++;
        }

        // Query packet count for this remote node
        string pktSql = "SELECT count(*) FROM packets WHERE from_node = " + to_string(nodeId) + ";";
        QueryResult pktRes;
        if (_db->executeRawQuery(pktSql, pktRes) && !pktRes.rows.empty()) {
            remotePackets += (uint64_t) strtoull(pktRes.rows[0][0].c_str(), nullptr, 10);
        }
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

    string sql =
        "SELECT n.node_id, coalesce(n.short_name, ''), coalesce(n.long_name, ''), "
        "coalesce(n.hw_model, 0), coalesce(n.role, 0), "
        "coalesce(p.latitude, 0.0), coalesce(p.longitude, 0.0), coalesce(p.altitude, 0), "
        "coalesce(n.first_seen, 0), coalesce(n.last_seen, 0) "
        "FROM nodes n "
        "LEFT JOIN (SELECT node_id, latitude, longitude, altitude, max(meshmon_time) "
        "           FROM positions WHERE latitude != 0 AND longitude != 0 GROUP BY node_id) p "
        "ON n.node_id = p.node_id;";

    QueryResult qres;
    if (!_db->executeRawQuery(sql, qres)) {
        j["error"] = qres.error;
        return j;
    }

    json nodesArr = json::array();

    for (const auto &row : qres.rows) {
        if (row.size() < 10) continue;
        uint32_t nodeId = (uint32_t) strtoul(row[0].c_str(), nullptr, 10);
        string shortName = (row[1] != "NULL" && !row[1].empty()) ? row[1] : "-";
        string longName = (row[2] != "NULL" && !row[2].empty()) ? row[2] : "-";
        int hwModel = (int) strtol(row[3].c_str(), nullptr, 10);
        int role = (int) strtol(row[4].c_str(), nullptr, 10);
        double lat = strtod(row[5].c_str(), nullptr);
        double lon = strtod(row[6].c_str(), nullptr);
        int alt = (int) strtol(row[7].c_str(), nullptr, 10);
        time_t firstSeen = (time_t) strtoul(row[8].c_str(), nullptr, 10);
        time_t lastSeen = (time_t) strtoul(row[9].c_str(), nullptr, 10);

        // Exclude On-Premise nodes
        if (isNodeOnPremise(nodeId, lat, lon, thresholdM)) {
            continue;
        }

        RemoteNodeProfile prof;
        prof.nodeId = nodeId;
        prof.nodeHex = formatNodeHex(nodeId);
        prof.shortName = shortName;
        prof.longName = longName;
        prof.hwModel = hwModel;
        prof.role = role;
        prof.firstSeen = firstSeen;
        prof.lastSeen = lastSeen;

        bool hasGps = (lat != 0.0 && lon != 0.0);
        prof.hasGps = hasGps;
        prof.lat = lat;
        prof.lon = lon;
        prof.alt = alt;

        if (hasGps) {
            prof.distanceMeters = haversineMeters(refLat, refLon, lat, lon);
            prof.bearingDeg = calculateBearing(refLat, refLon, lat, lon);
            prof.compassDir = bearingToCompass(prof.bearingDeg);
            prof.altDeltaMeters = alt - refAlt;
            calculateNodeMobility(nodeId, prof.mobility, prof.maxDisplacementM);
        } else {
            prof.distanceMeters = -1.0; // Indicates RF-Only
            prof.bearingDeg = -1.0;
            prof.compassDir = "--";
            prof.altDeltaMeters = 0;
            prof.mobility = "RF-Only";
            prof.maxDisplacementM = 0.0;
        }

        // Query packet metrics for this node
        string pktSql =
            "SELECT count(*), coalesce(sum(payload_size), 0), coalesce(avg(hops), 0.0), "
            "coalesce(avg(rx_snr), 0.0), coalesce(min(rx_snr), 0.0), coalesce(max(rx_snr), 0.0), "
            "coalesce(avg(rx_rssi), 0.0), "
            "coalesce(sum(case when hops = 0 then 1 else 0 end), 0), "
            "coalesce(sum(case when hops > 0 then 1 else 0 end), 0) "
            "FROM packets WHERE from_node = " + to_string(nodeId) + ";";

        QueryResult pRes;
        if (_db->executeRawQuery(pktSql, pRes) && !pRes.rows.empty()) {
            const auto &prow = pRes.rows[0];
            prof.totalPackets = (uint32_t) strtoul(prow[0].c_str(), nullptr, 10);
            prof.totalBytes = (uint64_t) strtoull(prow[1].c_str(), nullptr, 10);
            prof.avgHops = (float) strtod(prow[2].c_str(), nullptr);
            prof.avgSnr = (float) strtod(prow[3].c_str(), nullptr);
            prof.minSnr = (float) strtod(prow[4].c_str(), nullptr);
            prof.maxSnr = (float) strtod(prow[5].c_str(), nullptr);
            prof.avgRssi = (float) strtod(prow[6].c_str(), nullptr);
            prof.directPackets = (uint32_t) strtoul(prow[7].c_str(), nullptr, 10);
            prof.relayedPackets = (uint32_t) strtoul(prow[8].c_str(), nullptr, 10);
        }

        // Application spectrum (portnums)
        string appSql = "SELECT portnum, count(*) FROM packets WHERE from_node = " +
                        to_string(nodeId) + " GROUP BY portnum;";
        QueryResult aRes;
        json appObj = json::object();
        if (_db->executeRawQuery(appSql, aRes)) {
            for (const auto &arow : aRes.rows) {
                if (arow.size() < 2) continue;
                appObj[arow[0]] = (uint32_t) strtoul(arow[1].c_str(), nullptr, 10);
            }
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

    // Sort descending by total packets (top talkers first)
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

    string sql =
        "SELECT t.id, t.meshmon_time, t.from_node, t.to_node, t.route_count, "
        "t.route_nodes, t.route_snrs, coalesce(nf.short_name, ''), coalesce(nt.short_name, '') "
        "FROM traceroutes t "
        "LEFT JOIN nodes nf ON t.from_node = nf.node_id "
        "LEFT JOIN nodes nt ON t.to_node = nt.node_id "
        "ORDER BY t.meshmon_time DESC LIMIT " + to_string(limit) + ";";

    QueryResult qres;
    if (!_db->executeRawQuery(sql, qres)) {
        return j;
    }

    for (const auto &row : qres.rows) {
        if (row.size() < 9) continue;
        int64_t id = strtoll(row[0].c_str(), nullptr, 10);
        time_t ts = (time_t) strtoul(row[1].c_str(), nullptr, 10);
        uint32_t fromNode = (uint32_t) strtoul(row[2].c_str(), nullptr, 10);
        uint32_t toNode = (uint32_t) strtoul(row[3].c_str(), nullptr, 10);
        int routeCount = (int) strtol(row[4].c_str(), nullptr, 10);
        string routeNodesStr = row[5];
        string routeSnrsStr = row[6];
        string fromShort = (!row[7].empty() && row[7] != "NULL") ? row[7] : formatNodeHex(fromNode);
        string toShort = (!row[8].empty() && row[8] != "NULL") ? row[8] : formatNodeHex(toNode);

        // Parse comma/space separated route nodes & SNRs
        vector<uint32_t> rNodes;
        vector<string> rHexes;
        vector<string> rShorts;
        vector<float> rSnrs;

        stringstream ssNodes(routeNodesStr);
        string item;
        while (getline(ssNodes, item, ',')) {
            if (item.empty()) continue;
            uint32_t nId = (uint32_t) strtoul(item.c_str(), nullptr, 10);
            rNodes.push_back(nId);
            rHexes.push_back(formatNodeHex(nId));
            string sName = (_mon != nullptr) ? _mon->lookupShortName(nId) : "";
            if (sName.empty()) sName = formatNodeHex(nId);
            rShorts.push_back(sName);
        }

        stringstream ssSnrs(routeSnrsStr);
        while (getline(ssSnrs, item, ',')) {
            if (item.empty()) continue;
            rSnrs.push_back((float) strtod(item.c_str(), nullptr));
        }

        j.push_back({
            {"id", id},
            {"timestamp", ts},
            {"from_node", fromNode},
            {"from_hex", formatNodeHex(fromNode)},
            {"from_short", fromShort},
            {"to_node", toNode},
            {"to_hex", (toNode == 0xffffffffU) ? "^all" : formatNodeHex(toNode)},
            {"to_short", toShort},
            {"route_count", routeCount},
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

    // Contrasts 0-hop direct forward packet SNR against reverse link indicators
    string sql =
        "SELECT p.from_node, coalesce(n.short_name, ''), "
        "avg(p.rx_snr) AS fwd_snr, avg(p.rx_rssi) AS fwd_rssi, count(*) AS sample_cnt "
        "FROM packets p "
        "LEFT JOIN nodes n ON p.from_node = n.node_id "
        "WHERE p.hops = 0 AND p.rx_snr IS NOT NULL "
        "GROUP BY p.from_node "
        "HAVING count(*) >= 3 "
        "ORDER BY sample_cnt DESC LIMIT 30;";

    QueryResult qres;
    if (!_db->executeRawQuery(sql, qres)) {
        return j;
    }

    for (const auto &row : qres.rows) {
        if (row.size() < 5) continue;
        uint32_t nodeId = (uint32_t) strtoul(row[0].c_str(), nullptr, 10);
        string shortName = (!row[1].empty() && row[1] != "NULL") ? row[1] : formatNodeHex(nodeId);
        float fwdSnr = (float) strtod(row[2].c_str(), nullptr);
        float fwdRssi = (float) strtod(row[3].c_str(), nullptr);
        uint32_t count = (uint32_t) strtoul(row[4].c_str(), nullptr, 10);

        // Check if there is a reverse SNR recorded in traceroutes where this node was destination or relay
        float revSnr = fwdSnr; // Fallback parity
        bool hasReverse = false;

        string trSql =
            "SELECT route_snrs FROM traceroutes WHERE from_node = " + to_string(nodeId) +
            " OR to_node = " + to_string(nodeId) + " ORDER BY meshmon_time DESC LIMIT 1;";
        QueryResult trRes;
        if (_db->executeRawQuery(trSql, trRes) && !trRes.rows.empty()) {
            string snrStr = trRes.rows[0][0];
            size_t comma = snrStr.find(',');
            if (comma != string::npos) {
                revSnr = (float) strtod(snrStr.substr(0, comma).c_str(), nullptr);
                hasReverse = true;
            } else if (!snrStr.empty()) {
                revSnr = (float) strtod(snrStr.c_str(), nullptr);
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

    // Find nodes without GPS coordinates that have transmitted packets
    string sql =
        "SELECT n.node_id, coalesce(n.short_name, '') "
        "FROM nodes n "
        "WHERE n.node_id NOT IN (SELECT DISTINCT node_id FROM positions WHERE latitude != 0 AND longitude != 0) "
        "AND n.node_id IN (SELECT DISTINCT from_node FROM packets) "
        "LIMIT 20;";

    QueryResult qres;
    if (!_db->executeRawQuery(sql, qres)) {
        return j;
    }

    for (const auto &row : qres.rows) {
        if (row.size() < 2) continue;
        uint32_t nodeId = (uint32_t) strtoul(row[0].c_str(), nullptr, 10);
        string shortName = (!row[1].empty() && row[1] != "NULL") ? row[1] : formatNodeHex(nodeId);

        // Find hearing neighbors that have GPS
        string nSql =
            "SELECT p.latitude, p.longitude "
            "FROM packets pkt "
            "JOIN positions p ON (pkt.to_node = p.node_id OR pkt.from_node = p.node_id) "
            "WHERE (pkt.from_node = " + to_string(nodeId) + " OR pkt.to_node = " + to_string(nodeId) + ") "
            "AND p.latitude != 0 AND p.longitude != 0 "
            "GROUP BY p.node_id;";

        QueryResult nRes;
        double sumLat = 0.0, sumLon = 0.0;
        int count = 0;
        if (_db->executeRawQuery(nSql, nRes) && !nRes.rows.empty()) {
            for (const auto &nrow : nRes.rows) {
                if (nrow.size() < 2) continue;
                sumLat += strtod(nrow[0].c_str(), nullptr);
                sumLon += strtod(nrow[1].c_str(), nullptr);
                count++;
            }
        }

        if (count > 0) {
            double estLat = sumLat / count;
            double estLon = sumLon / count;
            // Estimated radius based on RF typical range
            double estRadiusM = (count == 1) ? 2500.0 : (count == 2 ? 1500.0 : 800.0);

            j.push_back({
                {"node_id", nodeId},
                {"node_hex", formatNodeHex(nodeId)},
                {"short_name", shortName},
                {"estimated_lat", estLat},
                {"estimated_lon", estLon},
                {"confidence_radius_m", estRadiusM},
                {"neighbor_count", count}
            });
        }
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
