/*
 * TestMeshMonDbAndAnalytics.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <memory>
#include <nlohmann/json.hpp>

#include "MeshMonDb.hxx"
#include "SpatialAnalytics.hxx"
#include "AimonGatewayClient.hxx"

#include <CppUTest/TestHarness.h>

using json = nlohmann::json;

namespace {

std::string createTempDbPath()
{
    char tmpl[] = "/tmp/test_meshmon_db_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd >= 0) {
        close(fd);
        unlink(tmpl);
    }
    return std::string(tmpl) + ".db";
}

void cleanupDbFiles(const std::string &dbPath)
{
    if (dbPath.empty()) {
        return;
    }
    unlink(dbPath.c_str());
    unlink((dbPath + "-wal").c_str());
    unlink((dbPath + "-shm").c_str());
}

std::vector<std::string> parseTableColumnsFromDescription(
    const std::string &desc, const std::string &tableName)
{
    std::vector<std::string> cols;
    std::string marker = "- " + tableName + " (";
    size_t pos = desc.find(marker);
    if (pos == std::string::npos) {
        return cols;
    }
    pos += marker.size();
    size_t end = desc.find(')', pos);
    if (end == std::string::npos) {
        return cols;
    }
    std::string colSpec = desc.substr(pos, end - pos);
    std::stringstream ss(colSpec);
    std::string item;
    while (std::getline(ss, item, ',')) {
        size_t first = item.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }
        size_t space = item.find_first_of(" \t\r\n", first);
        std::string colName = (space == std::string::npos)
            ? item.substr(first)
            : item.substr(first, space - first);
        if (!colName.empty()) {
            cols.push_back(colName);
        }
    }
    return cols;
}

} // namespace

TEST_GROUP(MeshMonDbIntegrity)
{
    std::string _dbPath;
    std::shared_ptr<MeshMonDb> _db;

    void setup() override
    {
        _dbPath = createTempDbPath();
        _db = std::make_shared<MeshMonDb>();
        CHECK_TRUE(_db->open(_dbPath));
        CHECK_TRUE(_db->start());
    }

    void teardown() override
    {
        if (_db) {
            _db->stop();
            _db->join();
            _db->close();
            _db.reset();
        }
        cleanupDbFiles(_dbPath);
    }
};

TEST(MeshMonDbIntegrity, TextMessageCountAndOutOfOrderForeignKeys)
{
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.from = 0x11223344U;
    pkt.to = 0xffffffffU;
    pkt.channel = 1;
    pkt.rx_time = 1700000099;
    pkt.rx_snr = 7.5f;
    pkt.rx_rssi = -88;

    meshtastic_DeviceMetrics dm = meshtastic_DeviceMetrics_init_zero;
    dm.has_battery_level = true;
    dm.battery_level = 85;
    dm.has_voltage = true;
    dm.voltage = 4.12f;

    meshtastic_EnvironmentMetrics em = meshtastic_EnvironmentMetrics_init_zero;
    em.has_temperature = true;
    em.temperature = 26.5f;
    em.has_relative_humidity = true;
    em.relative_humidity = 62.0f;
    em.has_barometric_pressure = true;
    em.barometric_pressure = 1012.5f;

    meshtastic_Position pos = meshtastic_Position_init_zero;
    pos.latitude_i = 250330000;
    pos.longitude_i = 1215654000;
    pos.altitude = 120;

    meshtastic_MeshPacket trPkt = meshtastic_MeshPacket_init_zero;
    trPkt.from = 0x11223344U;
    trPkt.to = 0x55667788U;
    meshtastic_RouteDiscovery rd = meshtastic_RouteDiscovery_init_zero;
    rd.route_count = 1;
    rd.route[0] = 0x99aabbccU;
    rd.snr_towards_count = 1;
    rd.snr_towards[0] = 24; // 6.0 dB

    // Enqueue child records BEFORE any node_info or packet for 0x11223344U or 0x55667788U
    _db->enqueueTextMessage(pkt, "Out-of-order hello", 1700000100);
    _db->enqueueDeviceMetrics(pkt, dm, 1700000101);
    _db->enqueueEnvironmentMetrics(pkt, em, 1700000102);
    _db->enqueuePosition(pkt, pos, 1700000103);
    _db->enqueueTraceRoute(trPkt, rd, 1700000104);

    _db->flush();
    LONGS_EQUAL(0, _db->getQueueDepth());
    LONGS_EQUAL(1, _db->getTotalTextMessageCount());

    std::vector<TextMessageRow> msgs;
    CHECK_TRUE(_db->getRecentTextMessages(10, msgs));
    LONGS_EQUAL(1, msgs.size());
    LONGS_EQUAL(1700000100, msgs[0].rxTime);
    UNSIGNED_LONGS_EQUAL(0x11223344U, msgs[0].fromNode);
    UNSIGNED_LONGS_EQUAL(0xffffffffU, msgs[0].toNode);
    UNSIGNED_LONGS_EQUAL(1U, msgs[0].channel);
    STRCMP_EQUAL("Out-of-order hello", msgs[0].text.c_str());

    QueryResult qres;
    CHECK_TRUE(_db->executeRawQuery(
        "SELECT count(*) FROM telemetry WHERE node_id = 287454020;", qres));
    LONGS_EQUAL(1, qres.rows.size());
    STRCMP_EQUAL("2", qres.rows[0][0].c_str());

    CHECK_TRUE(_db->executeRawQuery(
        "SELECT count(*) FROM positions WHERE node_id = 287454020;", qres));
    LONGS_EQUAL(1, qres.rows.size());
    STRCMP_EQUAL("1", qres.rows[0][0].c_str());

    CHECK_TRUE(_db->executeRawQuery(
        "SELECT count(*) FROM traceroutes WHERE from_node = 287454020;", qres));
    LONGS_EQUAL(1, qres.rows.size());
    STRCMP_EQUAL("1", qres.rows[0][0].c_str());

    // Now announce NodeInfo and verify node metadata updates cleanly
    _db->enqueueNodeInfo(0x11223344U, "Alpha Long", "ALPH", 1, 2, 1700000200);
    _db->flush();

    NodeDetail detail;
    CHECK_TRUE(_db->getNodeDetail(0x11223344U, 0, detail));
    STRCMP_EQUAL("ALPH", detail.shortName.c_str());
    STRCMP_EQUAL("Alpha Long", detail.longName.c_str());
    CHECK_TRUE(detail.hasPosition);
}

TEST(MeshMonDbIntegrity, ReadOnlyQueryGuardAndSqlInjection)
{
    _db->enqueueNodeInfo(0x11223344U, "Guard Node", "GRD1", 1, 1, 1700000100);
    _db->flush();

    QueryResult qres;
    // Allowed read-only queries
    CHECK_TRUE(_db->executeRawQuery("SELECT node_hex FROM nodes;", qres));
    LONGS_EQUAL(1, qres.rows.size());

    CHECK_TRUE(_db->executeRawQuery(
        "WITH cte AS (SELECT count(*) AS c FROM nodes) SELECT c FROM cte;", qres));
    LONGS_EQUAL(1, qres.rows.size());
    STRCMP_EQUAL("1", qres.rows[0][0].c_str());

    CHECK_TRUE(_db->executeRawQuery("PRAGMA table_info(telemetry);", qres));
    CHECK_FALSE(qres.rows.empty());

    CHECK_TRUE(_db->executeRawQuery("EXPLAIN SELECT * FROM nodes;", qres));

    // Rejected mutating or multi-statement queries
    CHECK_FALSE(_db->executeRawQuery("DROP TABLE nodes;", qres));
    CHECK_FALSE(_db->executeRawQuery("DELETE FROM nodes;", qres));
    CHECK_FALSE(_db->executeRawQuery(
        "INSERT INTO nodes (node_id, node_hex, first_seen, last_seen) VALUES (1, '!00000001', 1, 1);", qres));
    CHECK_FALSE(_db->executeRawQuery("UPDATE nodes SET short_name = 'PWN';", qres));
    CHECK_FALSE(_db->executeRawQuery("ATTACH DATABASE ':memory:' AS aux;", qres));
    CHECK_FALSE(_db->executeRawQuery("PRAGMA writable_schema=1;", qres));
    CHECK_FALSE(_db->executeRawQuery("PRAGMA foreign_keys=OFF;", qres));
    CHECK_FALSE(_db->executeRawQuery("SELECT 1; DROP TABLE nodes;", qres));
    CHECK_FALSE(_db->executeRawQuery("SELECT 1; SELECT 2;", qres));
    CHECK_FALSE(_db->executeRawQuery("EXPLAIN DROP TABLE nodes;", qres));
    CHECK_FALSE(_db->executeRawQuery("   ", qres));

    // Confirm nodes table is untouched
    UNSIGNED_LONGS_EQUAL(1U, _db->getTotalNodeCount());
}

TEST(MeshMonDbIntegrity, BoundedQueueCap)
{
    // Stop worker thread so queue accumulates without draining
    _db->stop();
    _db->join();

    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.from = 0x22334455U;
    pkt.to = 0xffffffffU;

    const size_t totalEnqueued = MeshMonDb::MAX_DB_QUEUE_SIZE + 250;
    for (size_t i = 0; i < totalEnqueued; i++) {
        _db->enqueueTextMessage(pkt, "Msg " + std::to_string(i), 1700001000 + (time_t) i);
    }

    LONGS_EQUAL(MeshMonDb::MAX_DB_QUEUE_SIZE, _db->getQueueDepth());

    // Flush drains all capped events synchronously
    _db->flush();
    LONGS_EQUAL(0, _db->getQueueDepth());
    LONGS_EQUAL(MeshMonDb::MAX_DB_QUEUE_SIZE, _db->getTotalTextMessageCount());
}

TEST(MeshMonDbIntegrity, AimonSchemaMatchesTelemetryColumns)
{
    QueryResult pragmaRes;
    CHECK_TRUE(_db->executeRawQuery("PRAGMA table_info(telemetry);", pragmaRes));
    std::set<std::string> actualCols;
    for (const auto &row : pragmaRes.rows) {
        if (row.size() >= 2) {
            actualCols.insert(row[1]);
        }
    }
    CHECK_TRUE(actualCols.count("rx_time") == 1);
    CHECK_TRUE(actualCols.count("meshmon_time") == 1);
    CHECK_TRUE(actualCols.count("relative_humidity") == 1);
    CHECK_TRUE(actualCols.count("barometric_pressure") == 1);

    std::string regLine = AimonGatewayClient::getRegistrationJsonSchema(16880);
    CHECK_FALSE(regLine.empty());

    json reg = json::parse(regLine);
    std::string queryDbDesc;
    for (const auto &tool : reg["params"]["tools"]) {
        if (tool.value("name", "") == "meshmon_query_db") {
            queryDbDesc = tool.value("description", "");
            break;
        }
    }
    CHECK_FALSE(queryDbDesc.empty());

    std::vector<std::string> advertisedCols =
        parseTableColumnsFromDescription(queryDbDesc, "telemetry");
    CHECK_FALSE(advertisedCols.empty());

    std::set<std::string> advSet(advertisedCols.begin(), advertisedCols.end());
    CHECK_TRUE(advSet.count("relative_humidity") == 1);
    CHECK_TRUE(advSet.count("barometric_pressure") == 1);
    CHECK_TRUE(advSet.count("rx_time") == 1);
    CHECK_TRUE(advSet.count("humidity") == 0);
    CHECK_TRUE(advSet.count("pressure") == 0);
    CHECK_TRUE(advSet.count("timestamp") == 0);

    for (const std::string &col : advertisedCols) {
        CHECK_TEXT(actualCols.count(col) == 1,
                   ("Advertised column missing in telemetry table: " + col).c_str());
    }
}

TEST(MeshMonDbIntegrity, TraceRouteBoundsZeroSnrOversizedRoute)
{
    meshtastic_MeshPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.from = 0x11111111U;
    pkt.to = 0x22222222U;
    pkt.rx_time = 1700000200;

    // Case 1: snr_towards_count == 0, route_count == 2
    meshtastic_RouteDiscovery rdZeroSnr = meshtastic_RouteDiscovery_init_zero;
    rdZeroSnr.route_count = 2;
    rdZeroSnr.route[0] = 0x33333333U;
    rdZeroSnr.route[1] = 0x44444444U;
    rdZeroSnr.snr_towards_count = 0;

    _db->enqueueTraceRoute(pkt, rdZeroSnr, 1700000201);
    _db->flush();

    std::vector<TopologyRouteRow> routes;
    CHECK_TRUE(_db->getTopologyRoutes(10, routes));
    LONGS_EQUAL(1, routes.size());
    LONGS_EQUAL(2, routes[0].routeCount);
    STRCMP_EQUAL("!33333333,!44444444", routes[0].routeNodes.c_str());
    STRCMP_EQUAL("", routes[0].routeSnrs.c_str());

    // Case 2: route_count > snr_towards_count
    meshtastic_RouteDiscovery rdPartialSnr = meshtastic_RouteDiscovery_init_zero;
    rdPartialSnr.route_count = 3;
    rdPartialSnr.route[0] = 0x55555555U;
    rdPartialSnr.route[1] = 0x66666666U;
    rdPartialSnr.route[2] = 0x77777777U;
    rdPartialSnr.snr_towards_count = 1;
    rdPartialSnr.snr_towards[0] = 20; // 5.0 dB

    _db->enqueueTraceRoute(pkt, rdPartialSnr, 1700000202);
    _db->flush();

    routes.clear();
    CHECK_TRUE(_db->getTopologyRoutes(10, routes));
    LONGS_EQUAL(2, routes.size());
    LONGS_EQUAL(3, routes[0].routeCount);
    STRCMP_EQUAL("!55555555,!66666666,!77777777", routes[0].routeNodes.c_str());
    STRCMP_EQUAL("5.0", routes[0].routeSnrs.c_str());

    // Case 3: Oversized route_count > 8 and snr_towards_count > 8
    meshtastic_RouteDiscovery rdOversized = meshtastic_RouteDiscovery_init_zero;
    rdOversized.route_count = 20;
    for (int i = 0; i < 8; i++) {
        rdOversized.route[i] = 0x10000000U + i;
        rdOversized.snr_towards[i] = 16; // 4.0 dB
    }
    rdOversized.snr_towards_count = 15;

    _db->enqueueTraceRoute(pkt, rdOversized, 1700000203);
    _db->flush();

    routes.clear();
    CHECK_TRUE(_db->getTopologyRoutes(10, routes));
    LONGS_EQUAL(3, routes.size());
    LONGS_EQUAL(8, routes[0].routeCount);
    STRCMP_EQUAL("!10000000,!10000001,!10000002,!10000003,!10000004,!10000005,!10000006,!10000007",
                 routes[0].routeNodes.c_str());
    STRCMP_EQUAL("4.0,4.0,4.0,4.0,4.0,4.0,4.0,4.0", routes[0].routeSnrs.c_str());
}

TEST(MeshMonDbIntegrity, AutomationNodesSurvivePruneAndFallback)
{
    // Node 1: Persisted via upsertAutomationNode
    DbAutomationNodeSummary pumpNode;
    pumpNode.nodeId = 0x11223344U;
    pumpNode.nodeHex = "!11223344";
    pumpNode.deviceType = "meshpump";
    pumpNode.version = "1.2.3";
    pumpNode.hardware = "esp32-s3";
    pumpNode.capabilities = "pump,float_switch";
    pumpNode.firstSeen = 1700000000;
    pumpNode.lastSeen = 1700000100;
    CHECK_TRUE(_db->upsertAutomationNode(pumpNode));

    _db->enqueueAutomationEvent(1700000050, pumpNode.nodeId, "meshpump", "RX",
                                "pump", "BOOT_UP", "", "OK", "meshmon");
    _db->enqueueAutomationEvent(1700000060, pumpNode.nodeId, "meshpump", "RX",
                                "pump", "ROLLCALL", "mode=auto", "OK", "meshmon");

    // Node 2: Legacy node only in automation_events
    uint32_t legacyNodeId = 0x55667788U;
    _db->enqueueAutomationEvent(1700000070, legacyNodeId, "meshroof", "RX",
                                "roof", "ROLLCALL", "sensors=temp,lux", "OK", "meshmon");
    _db->flush();

    // Verify both are discovered prior to prune
    std::vector<DbAutomationNodeSummary> nodes;
    CHECK_TRUE(_db->getDiscoveredAutomationNodes(nodes));
    LONGS_EQUAL(2, nodes.size());

    // Prune older than 1700000100 (removes all automation_events)
    size_t pruned = _db->pruneOlderThan(1700000100);
    CHECK_TRUE(pruned > 0);

    uint32_t autoEventCount = 0;
    CHECK_TRUE(_db->getAutomationEventCount(0, autoEventCount));
    LONGS_EQUAL(0, autoEventCount);

    // Verify pumpNode survives in getDiscoveredAutomationNodes despite pruned events
    nodes.clear();
    CHECK_TRUE(_db->getDiscoveredAutomationNodes(nodes));
    LONGS_EQUAL(1, nodes.size());
    UNSIGNED_LONGS_EQUAL(pumpNode.nodeId, nodes[0].nodeId);
    STRCMP_EQUAL("!11223344", nodes[0].nodeHex.c_str());
    STRCMP_EQUAL("meshpump", nodes[0].deviceType.c_str());
    STRCMP_EQUAL("1.2.3", nodes[0].version.c_str());
    STRCMP_EQUAL("esp32-s3", nodes[0].hardware.c_str());
    STRCMP_EQUAL("pump,float_switch", nodes[0].capabilities.c_str());
    LONGS_EQUAL(1700000000, nodes[0].firstSeen);
    LONGS_EQUAL(1700000100, nodes[0].lastSeen);
    LONGS_EQUAL(0, nodes[0].rebootCount);
    STRCMP_EQUAL("", nodes[0].rollcallPayload.c_str());

    // Node 3: Add new node directly to registry with empty automation_events
    DbAutomationNodeSummary roomNode;
    roomNode.nodeId = 0x99887766U;
    roomNode.nodeHex = "!99887766";
    roomNode.deviceType = "meshroom";
    roomNode.version = "2.0.0";
    roomNode.hardware = "esp32-c3";
    roomNode.capabilities = "ac,tv";
    roomNode.acIrProtocol = "coolix";
    roomNode.tvIrProtocol = "samsung";
    roomNode.firstSeen = 1700000200;
    roomNode.lastSeen = 1700000300;
    CHECK_TRUE(_db->upsertAutomationNode(roomNode));

    nodes.clear();
    CHECK_TRUE(_db->getDiscoveredAutomationNodes(nodes));
    LONGS_EQUAL(2, nodes.size());
    UNSIGNED_LONGS_EQUAL(pumpNode.nodeId, nodes[0].nodeId);
    UNSIGNED_LONGS_EQUAL(roomNode.nodeId, nodes[1].nodeId);
    STRCMP_EQUAL("meshroom", nodes[1].deviceType.c_str());
    STRCMP_EQUAL("coolix", nodes[1].acIrProtocol.c_str());
    STRCMP_EQUAL("samsung", nodes[1].tvIrProtocol.c_str());
}

TEST_GROUP(SpatialAnalyticsSql)
{
    std::string _dbPath;
    std::shared_ptr<MeshMonDb> _db;
    std::shared_ptr<SpatialAnalytics> _spatial;

    void setup() override
    {
        _dbPath = createTempDbPath();
        _db = std::make_shared<MeshMonDb>();
        CHECK_TRUE(_db->open(_dbPath));
        CHECK_TRUE(_db->start());
        _spatial = std::make_shared<SpatialAnalytics>(nullptr, _db);
    }

    void teardown() override
    {
        _spatial.reset();
        if (_db) {
            _db->stop();
            _db->join();
            _db->close();
            _db.reset();
        }
        cleanupDbFiles(_dbPath);
    }
};

TEST(SpatialAnalyticsSql, EmptyDbAnalyticsSafe)
{
    json sJson = _spatial->getSpatialJson(50.0);
    LONGS_EQUAL(0, sJson["nodes"].size());
    LONGS_EQUAL(0, sJson["stats"]["total_nodes_with_gps"].get<int>());

    json sumJson = _spatial->getRemoteSummaryJson(50.0);
    LONGS_EQUAL(0, sumJson["stats"]["total_remote_nodes"].get<int>());
    DOUBLES_EQUAL(0.0, sumJson["stats"]["remote_airtime_pct"].get<double>(), 1e-6);

    json remJson = _spatial->getRemoteNodesJson(50.0);
    LONGS_EQUAL(0, remJson["nodes"].size());

    json rtJson = _spatial->getTopologyGraphJson(50);
    CHECK_TRUE(rtJson.is_array());
    LONGS_EQUAL(0, rtJson.size());

    json asymJson = _spatial->getTopologyAsymmetryJson();
    CHECK_TRUE(asymJson.is_array());
    LONGS_EQUAL(0, asymJson.size());

    json centJson = _spatial->getTopologyCentroidsJson();
    CHECK_TRUE(centJson.is_array());
    LONGS_EQUAL(0, centJson.size());
}

TEST(SpatialAnalyticsSql, RemoteSummaryAndNodesJson)
{
    // 1. On-premise node (within 50m of default reference 24.82176, 121.2383232)
    uint32_t onPremNode = 0x10000001U;
    _db->enqueueNodeInfo(onPremNode, "Gateway Local", "GW01", 1, 1, 1700000010);
    meshtastic_MeshPacket pOn = meshtastic_MeshPacket_init_zero;
    pOn.from = onPremNode;
    pOn.to = 0xffffffffU;
    meshtastic_Position posOn = meshtastic_Position_init_zero;
    posOn.latitude_i = 248217600;
    posOn.longitude_i = 1212383232;
    posOn.altitude = 250;
    _db->enqueuePosition(pOn, posOn, 1700000011);

    // 2. Remote Stationary GPS node (~24 km north in Taipei) with >= 3 zero-hop packets
    uint32_t remStatNode = 0x20000002U;
    _db->enqueueNodeInfo(remStatNode, "Taipei Relay", "TPEI", 4, 2, 1700000020);
    meshtastic_MeshPacket pStat = meshtastic_MeshPacket_init_zero;
    pStat.from = remStatNode;
    pStat.to = 0xffffffffU;
    pStat.hop_start = 3;
    pStat.hop_limit = 3; // 0 hops
    pStat.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    pStat.decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
    pStat.decoded.payload.size = 16;

    for (int i = 0; i < 3; i++) {
        pStat.id = 100 + i;
        pStat.rx_snr = 8.0f + i; // 8, 9, 10 -> avg 9.0
        pStat.rx_rssi = -85 - i; // -85, -86, -87 -> avg -86.0
        _db->enqueuePacket(pStat, 1700000030 + i);
    }
    meshtastic_Position posStat = meshtastic_Position_init_zero;
    posStat.latitude_i = 250330000;
    posStat.longitude_i = 1215654000;
    posStat.altitude = 400;
    _db->enqueuePosition(pStat, posStat, 1700000040);

    // 3. Remote Mobile GPS node (2 positions > 1 km apart) with relayed packets
    uint32_t remMobNode = 0x30000003U;
    _db->enqueueNodeInfo(remMobNode, "Mobile Tracker", "MOB1", 5, 5, 1700000050);
    meshtastic_MeshPacket pMob = meshtastic_MeshPacket_init_zero;
    pMob.id = 200;
    pMob.from = remMobNode;
    pMob.to = remStatNode;
    pMob.hop_start = 3;
    pMob.hop_limit = 1; // 2 hops relayed
    pMob.rx_snr = -4.5f;
    pMob.rx_rssi = -112;
    pMob.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    pMob.decoded.portnum = meshtastic_PortNum_POSITION_APP;
    pMob.decoded.payload.size = 24;
    _db->enqueuePacket(pMob, 1700000060);

    meshtastic_Position posMob1 = meshtastic_Position_init_zero;
    posMob1.latitude_i = 249000000;
    posMob1.longitude_i = 1213000000;
    posMob1.altitude = 100;
    _db->enqueuePosition(pMob, posMob1, 1700000061);

    meshtastic_Position posMob2 = meshtastic_Position_init_zero;
    posMob2.latitude_i = 249500000; // >5.5 km displacement
    posMob2.longitude_i = 1213500000;
    posMob2.altitude = 150;
    _db->enqueuePosition(pMob, posMob2, 1700000065);

    // 4. Remote RF-only node (no GPS position, communicates with positioned neighbors)
    uint32_t rfOnlyNode = 0x40000004U;
    _db->enqueueNodeInfo(rfOnlyNode, "Hidden Sensor", "RF04", 2, 1, 1700000070);
    meshtastic_MeshPacket pRf = meshtastic_MeshPacket_init_zero;
    pRf.id = 300;
    pRf.from = rfOnlyNode;
    pRf.to = remStatNode;
    pRf.hop_start = 3;
    pRf.hop_limit = 2; // 1 hop
    pRf.rx_snr = 2.0f;
    pRf.rx_rssi = -98;
    pRf.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    pRf.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    pRf.decoded.payload.size = 10;
    _db->enqueuePacket(pRf, 1700000075);

    // 5. Traceroute from remStatNode to remMobNode
    meshtastic_RouteDiscovery rd = meshtastic_RouteDiscovery_init_zero;
    rd.route_count = 1;
    rd.route[0] = rfOnlyNode;
    rd.snr_towards_count = 1;
    rd.snr_towards[0] = 16; // 4.0 dB reverse SNR
    _db->enqueueTraceRoute(pStat, rd, 1700000080);

    _db->flush();

    // Verify getSpatialJson
    json sJson = _spatial->getSpatialJson(50.0);
    LONGS_EQUAL(3, sJson["stats"]["total_nodes_with_gps"].get<int>());
    LONGS_EQUAL(1, sJson["stats"]["on_premise_count"].get<int>());
    CHECK_TRUE(sJson["stats"]["farthest_distance_meters"].get<double>() > 20000.0);

    // Verify getRemoteSummaryJson
    json sumJson = _spatial->getRemoteSummaryJson(50.0);
    LONGS_EQUAL(3, sumJson["stats"]["total_remote_nodes"].get<int>());
    LONGS_EQUAL(2, sumJson["stats"]["remote_nodes_gps"].get<int>());
    LONGS_EQUAL(1, sumJson["stats"]["remote_nodes_rf_only"].get<int>());
    LONGS_EQUAL(1, sumJson["stats"]["stationary_count"].get<int>());
    LONGS_EQUAL(1, sumJson["stats"]["mobile_tracker_count"].get<int>());
    LONGS_EQUAL(5, sumJson["stats"]["total_remote_packets"].get<int>());

    // Verify getRemoteNodesJson
    json remJson = _spatial->getRemoteNodesJson(50.0);
    LONGS_EQUAL(3, remJson["nodes"].size());
    // Sorted descending by total_packets: remStatNode (3 pkts) first
    UNSIGNED_LONGS_EQUAL(remStatNode, remJson["nodes"][0]["node_id"].get<uint32_t>());
    LONGS_EQUAL(3, remJson["nodes"][0]["total_packets"].get<int>());
    LONGS_EQUAL(3, remJson["nodes"][0]["direct_packets"].get<int>());
    LONGS_EQUAL(0, remJson["nodes"][0]["relayed_packets"].get<int>());
    DOUBLES_EQUAL(9.0, remJson["nodes"][0]["avg_snr"].get<double>(), 0.01);
    DOUBLES_EQUAL(8.0, remJson["nodes"][0]["min_snr"].get<double>(), 0.01);
    DOUBLES_EQUAL(10.0, remJson["nodes"][0]["max_snr"].get<double>(), 0.01);
    DOUBLES_EQUAL(-86.0, remJson["nodes"][0]["avg_rssi"].get<double>(), 0.01);
    STRCMP_EQUAL("Stationary", remJson["nodes"][0]["mobility"].get<std::string>().c_str());

    // Verify getTopologyGraphJson / getTopologyRoutesJson
    json rtJson = _spatial->getTopologyGraphJson(50);
    LONGS_EQUAL(1, rtJson.size());
    UNSIGNED_LONGS_EQUAL(remStatNode, rtJson[0]["from_node"].get<uint32_t>());
    LONGS_EQUAL(1, rtJson[0]["route_nodes"].size());
    UNSIGNED_LONGS_EQUAL(rfOnlyNode, rtJson[0]["route_nodes"][0].get<uint32_t>());
    DOUBLES_EQUAL(4.0, rtJson[0]["route_snrs"][0].get<double>(), 0.01);

    // Verify getTopologyAsymmetryJson
    json asymJson = _spatial->getTopologyAsymmetryJson();
    LONGS_EQUAL(1, asymJson.size());
    UNSIGNED_LONGS_EQUAL(remStatNode, asymJson[0]["node_id"].get<uint32_t>());
    DOUBLES_EQUAL(9.0, asymJson[0]["forward_snr"].get<double>(), 0.01);
    DOUBLES_EQUAL(4.0, asymJson[0]["reverse_snr"].get<double>(), 0.01);
    DOUBLES_EQUAL(5.0, asymJson[0]["snr_delta"].get<double>(), 0.01);
    CHECK_TRUE(asymJson[0]["has_reverse_measurement"].get<bool>());

    // Verify getTopologyCentroidsJson
    json centJson = _spatial->getTopologyCentroidsJson();
    LONGS_EQUAL(1, centJson.size());
    UNSIGNED_LONGS_EQUAL(rfOnlyNode, centJson[0]["node_id"].get<uint32_t>());
    LONGS_EQUAL(1, centJson[0]["neighbor_count"].get<int>());
    DOUBLES_EQUAL(25.033, centJson[0]["estimated_lat"].get<double>(), 0.001);
    DOUBLES_EQUAL(121.5654, centJson[0]["estimated_lon"].get<double>(), 0.001);
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
