/*
 * TestWebServerAuth.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>
#include <vector>
#include <memory>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wshadow"
#include <httplib.h>
#pragma GCC diagnostic pop

#include <nlohmann/json.hpp>

#include "MeshMon.hxx"
#include "MeshMonDb.hxx"
#include "WebServer.hxx"

#include <CppUTest/TestHarness.h>

using json = nlohmann::json;

namespace {

std::string createTempDbPath()
{
    char tmpl[] = "/tmp/test_meshmon_web_XXXXXX";
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

uint16_t pickFreeTcpPort()
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return 26880;
    }
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = htons(0);
    uint16_t port = 26880;
    if (bind(s, reinterpret_cast<struct sockaddr *>(&sin), sizeof(sin)) == 0) {
        socklen_t len = sizeof(sin);
        if (getsockname(s, reinterpret_cast<struct sockaddr *>(&sin), &len) == 0) {
            port = ntohs(sin.sin_port);
        }
    }
    close(s);
    return port;
}

std::string extractUiTokenFromHtml(const std::string &html)
{
    const std::string marker = "window.__UI_SESSION_TOKEN__ = \"";
    size_t pos = html.find(marker);
    if (pos == std::string::npos) {
        return "";
    }
    pos += marker.size();
    size_t end = html.find('"', pos);
    if (end == std::string::npos) {
        return "";
    }
    return html.substr(pos, end - pos);
}

struct DisableCppUTestNonThreadSafeLeakDetector {
    DisableCppUTestNonThreadSafeLeakDetector()
    {
        MemoryLeakWarningPlugin::turnOffNewDeleteOverloads();
    }
};

static DisableCppUTestNonThreadSafeLeakDetector g_disableLeakDetector;

class TestMeshMon : public MeshMon {
public:
    void injectNode(uint32_t id, const std::string &shortName, const std::string &longName)
    {
        SimpleClient::ScopedLock lock(this);
        meshtastic_NodeInfo info = meshtastic_NodeInfo_init_zero;
        info.num = id;
        info.has_user = true;
        strncpy(info.user.id, ("!" + std::to_string(id)).c_str(), sizeof(info.user.id) - 1);
        strncpy(info.user.short_name, shortName.c_str(), sizeof(info.user.short_name) - 1);
        strncpy(info.user.long_name, longName.c_str(), sizeof(info.user.long_name) - 1);
        _nodeInfos[id] = info;
    }
};

} // namespace

TEST_GROUP(WebServerSecurity)
{
    std::string _dbPath;
    std::shared_ptr<MeshMonDb> _db;
    std::shared_ptr<TestMeshMon> _mon;
    std::shared_ptr<WebServer> _server;
    std::unique_ptr<httplib::Client> _cli;
    std::vector<std::string> _uiTokens;
    std::vector<std::string> _authTokens;
    uint16_t _port = 0;

    void setup() override
    {
        _dbPath = createTempDbPath();
        _db = std::make_shared<MeshMonDb>();
        CHECK_TRUE(_db->open(_dbPath));
        CHECK_TRUE(_db->start());

        _mon = std::make_shared<TestMeshMon>();
        _port = pickFreeTcpPort();

        WebConfig cfg;
        cfg.enabled = true;
        cfg.host = "127.0.0.1";
        cfg.port = _port;
        cfg.password = "AdminPass99";
        cfg.endpointsEnabled = false;

        _server = std::make_shared<WebServer>(_mon, _db, cfg);
        _server->setAdminPassword("AdminPass99");
        CHECK_TRUE(_server->start(true));

        _cli = std::make_unique<httplib::Client>("127.0.0.1", _port);
        _cli->set_connection_timeout(2, 0);
        _cli->set_read_timeout(2, 0);
    }

    void teardown() override
    {
        _cli.reset();
        _uiTokens.clear();
        _authTokens.clear();
        if (_server) {
            _server->stop();
            _server->join();
            _server.reset();
        }
        if (_mon) {
            _mon->detach();
            _mon->join();
            _mon.reset();
        }
        if (_db) {
            _db->stop();
            _db->join();
            _db->close();
            _db.reset();
        }
        cleanupDbFiles(_dbPath);
    }
};

TEST(WebServerSecurity, UiSessionTokenCannotExecutePrivilegedAdminEndpoints)
{
    // 1. Fetch GET / and extract window.__UI_SESSION_TOKEN__
    auto indexRes = _cli->Get("/");
    CHECK_TRUE(indexRes != nullptr);
    LONGS_EQUAL(200, indexRes->status);
    std::string uiToken = extractUiTokenFromHtml(indexRes->body);
    CHECK_FALSE(uiToken.empty());

    // 2. UI session token can read GET /api/status and GET /api/messages
    httplib::Headers uiHeaders = {
        {"Authorization", "Bearer " + uiToken}
    };
    auto statusRes = _cli->Get("/api/status", uiHeaders);
    CHECK_TRUE(statusRes != nullptr);
    LONGS_EQUAL(200, statusRes->status);

    auto msgRes = _cli->Get("/api/messages", uiHeaders);
    CHECK_TRUE(msgRes != nullptr);
    LONGS_EQUAL(200, msgRes->status);

    // 3. UI session token MUST be rejected (401) on privileged POST endpoints
    auto dbRes = _cli->Post("/api/db/query", uiHeaders,
                            "{\"query\":\"SELECT count(*) FROM nodes;\",\"sql\":\"SELECT count(*) FROM nodes;\"}",
                            "application/json");
    CHECK_TRUE(dbRes != nullptr);
    LONGS_EQUAL(401, dbRes->status);

    auto sendRes = _cli->Post("/api/messages/send", uiHeaders,
                              "{\"text\":\"unauthorized broadcast\"}",
                              "application/json");
    CHECK_TRUE(sendRes != nullptr);
    LONGS_EQUAL(401, sendRes->status);

    auto autoRes = _cli->Post("/api/automation/command", uiHeaders,
                              "{\"node_id\":\"!11223344\",\"command\":\"status\"}",
                              "application/json");
    CHECK_TRUE(autoRes != nullptr);
    LONGS_EQUAL(401, autoRes->status);

    // 4. Unauthenticated GET /api/status -> 401
    auto unauthRes = _cli->Get("/api/status");
    CHECK_TRUE(unauthRes != nullptr);
    LONGS_EQUAL(401, unauthRes->status);
}

TEST(WebServerSecurity, AdminLoginLogoutAndTypedMessagesLifecycle)
{
    // 1. POST /api/auth/login with wrong password -> 401
    auto badLogin = _cli->Post("/api/auth/login",
                               "{\"password\":\"WrongPassword\"}",
                               "application/json");
    CHECK_TRUE(badLogin != nullptr);
    LONGS_EQUAL(401, badLogin->status);

    // 2. POST /api/auth/login with "AdminPass99" -> 200 and returns token
    auto goodLogin = _cli->Post("/api/auth/login",
                                "{\"password\":\"AdminPass99\"}",
                                "application/json");
    CHECK_TRUE(goodLogin != nullptr);
    LONGS_EQUAL(200, goodLogin->status);
    json loginJson = json::parse(goodLogin->body);
    CHECK_TRUE(loginJson.value("authenticated", false));
    std::string adminToken = loginJson.value("token", "");
    CHECK_FALSE(adminToken.empty());

    httplib::Headers adminHeaders = {
        {"Authorization", "Bearer " + adminToken}
    };

    // 3. Admin token on POST /api/db/query with read-only SELECT -> 200
    auto selectRes = _cli->Post("/api/db/query", adminHeaders,
                                "{\"sql\":\"SELECT count(*) FROM nodes;\"}",
                                "application/json");
    CHECK_TRUE(selectRes != nullptr);
    LONGS_EQUAL(200, selectRes->status);

    // 4. Admin token on POST /api/db/query with mutating DROP TABLE -> 400
    auto dropRes = _cli->Post("/api/db/query", adminHeaders,
                              "{\"sql\":\"DROP TABLE nodes;\"}",
                              "application/json");
    CHECK_TRUE(dropRes != nullptr);
    LONGS_EQUAL(400, dropRes->status);

    // 5. Populate a text message in _db, flush _db, call GET /api/messages?limit=10 with UI token
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 9001;
    pkt.from = 0x11223344U;
    pkt.to = 0xffffffffU;
    pkt.channel = 2;
    pkt.rx_snr = 8.5f;
    pkt.rx_rssi = -92;
    pkt.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    pkt.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    _db->enqueuePacket(pkt, 1700000500);
    _db->enqueueTextMessage(pkt, "Hello Typed Mesh", 1700000500);
    _db->flush();

    auto indexRes = _cli->Get("/");
    CHECK_TRUE(indexRes != nullptr);
    std::string uiToken = extractUiTokenFromHtml(indexRes->body);
    CHECK_FALSE(uiToken.empty());
    httplib::Headers uiHeaders = {
        {"Authorization", "Bearer " + uiToken}
    };

    auto msgRes = _cli->Get("/api/messages?limit=10", uiHeaders);
    CHECK_TRUE(msgRes != nullptr);
    LONGS_EQUAL(200, msgRes->status);
    json msgArr = json::parse(msgRes->body);
    CHECK_TRUE(msgArr.is_array());
    LONGS_EQUAL(1, msgArr.size());
    LONGS_EQUAL(0x11223344U, msgArr[0].value("from_id", 0U));
    STRCMP_EQUAL("!11223344", msgArr[0].value("from_hex", "").c_str());
    LONGS_EQUAL(2, msgArr[0].value("channel", -1));
    STRCMP_EQUAL("Hello Typed Mesh", msgArr[0].value("text", "").c_str());
    DOUBLES_EQUAL(8.5, msgArr[0].value("snr", 0.0), 0.01);
    LONGS_EQUAL(-92, msgArr[0].value("rssi", 0));

    // 6. POST /api/auth/logout with admin token -> 200, subsequent POST /api/db/query -> 401
    auto logoutRes = _cli->Post("/api/auth/logout", adminHeaders, "{}", "application/json");
    CHECK_TRUE(logoutRes != nullptr);
    LONGS_EQUAL(200, logoutRes->status);

    auto afterLogoutRes = _cli->Post("/api/db/query", adminHeaders,
                                     "{\"sql\":\"SELECT count(*) FROM nodes;\"}",
                                     "application/json");
    CHECK_TRUE(afterLogoutRes != nullptr);
    LONGS_EQUAL(401, afterLogoutRes->status);
}

TEST(WebServerSecurity, SessionMapBoundsAndEviction)
{
    // 1. Mint 266 (MAX_UI_SESSIONS + 10) UI tokens via GET /
    _uiTokens.reserve(266);
    for (size_t i = 0; i < 266; i++) {
        auto res = _cli->Get("/");
        CHECK_TRUE(res != nullptr);
        LONGS_EQUAL(200, res->status);
        std::string tok = extractUiTokenFromHtml(res->body);
        CHECK_FALSE(tok.empty());
        _uiTokens.push_back(tok);
    }

    // Earliest 10 UI tokens must be evicted (401)
    for (size_t i = 0; i < 10; i++) {
        httplib::Headers hdrs = {{"Authorization", "Bearer " + _uiTokens[i]}};
        auto res = _cli->Get("/api/status", hdrs);
        CHECK_TRUE(res != nullptr);
        LONGS_EQUAL(401, res->status);
    }

    // Newest UI tokens must remain valid (200)
    for (size_t i = 260; i < 266; i++) {
        httplib::Headers hdrs = {{"Authorization", "Bearer " + _uiTokens[i]}};
        auto res = _cli->Get("/api/status", hdrs);
        CHECK_TRUE(res != nullptr);
        LONGS_EQUAL(200, res->status);
    }

    // 2. Mint 69 (MAX_AUTH_SESSIONS + 5) admin tokens via POST /api/auth/login
    _authTokens.reserve(69);
    for (size_t i = 0; i < 69; i++) {
        auto res = _cli->Post("/api/auth/login",
                              "{\"password\":\"AdminPass99\"}",
                              "application/json");
        CHECK_TRUE(res != nullptr);
        LONGS_EQUAL(200, res->status);
        json j = json::parse(res->body);
        std::string tok = j.value("token", "");
        CHECK_FALSE(tok.empty());
        _authTokens.push_back(tok);
    }

    // Earliest 5 admin tokens must be evicted (401)
    for (size_t i = 0; i < 5; i++) {
        httplib::Headers hdrs = {{"Authorization", "Bearer " + _authTokens[i]}};
        auto res = _cli->Post("/api/db/query", hdrs,
                              "{\"sql\":\"SELECT count(*) FROM nodes;\"}",
                              "application/json");
        CHECK_TRUE(res != nullptr);
        LONGS_EQUAL(401, res->status);
    }

    // Newest admin tokens must remain valid (200)
    for (size_t i = 64; i < 69; i++) {
        httplib::Headers hdrs = {{"Authorization", "Bearer " + _authTokens[i]}};
        auto res = _cli->Post("/api/db/query", hdrs,
                              "{\"sql\":\"SELECT count(*) FROM nodes;\"}",
                              "application/json");
        CHECK_TRUE(res != nullptr);
        LONGS_EQUAL(200, res->status);
    }
}

TEST(WebServerSecurity, Utf8SafeJsonDumpOnPacketReceivedAndApi)
{
    // 1. Fetch UI token from GET /
    auto indexRes = _cli->Get("/");
    CHECK_TRUE(indexRes != nullptr);
    LONGS_EQUAL(200, indexRes->status);
    std::string uiToken = extractUiTokenFromHtml(indexRes->body);
    CHECK_FALSE(uiToken.empty());
    httplib::Headers uiHeaders = {{"Authorization", "Bearer " + uiToken}};

    // 2. Inject non-UTF-8 binary sequence into onPacketReceived
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 7788;
    pkt.from = 0xaabbccddU;
    pkt.to = 0xffffffffU;
    pkt.channel = 1;
    pkt.rx_snr = 7.2f;
    pkt.rx_rssi = -88;
    pkt.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    pkt.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    const uint8_t badPayload[] = {
        'p', 'r', 'e', 'f', 'i', 'x',
        0xff, 0xfe, 0x80,
        'm', 'i', 'd', 'd', 'l', 'e',
        0xaa, 0xbb, 0xcc,
        'e', 'n', 'd'
    };
    memcpy(pkt.decoded.payload.bytes, badPayload, sizeof(badPayload));
    pkt.decoded.payload.size = sizeof(badPayload);

    // Must not throw nlohmann::json::type_error
    _server->onPacketReceived(pkt, 1700000200);

    // 3. Verify GET /api/packets serializes cleanly without throwing
    auto packetsRes = _cli->Get("/api/packets", uiHeaders);
    CHECK_TRUE(packetsRes != nullptr);
    LONGS_EQUAL(200, packetsRes->status);
    json packetsJson = json::parse(packetsRes->body);
    CHECK_TRUE(packetsJson.is_array());
    CHECK_TRUE(packetsJson.size() >= 1);

    // 4. Inject non-UTF-8 binary sequences into node names
    std::string badShort = std::string("bad\xff\xfe");
    std::string badLong = std::string("NodeName\x80\xaa\xbb");
    _mon->injectNode(0xaabbccddU, badShort, badLong);

    // 5. Verify GET /api/nodes serializes cleanly without throwing
    auto nodesRes = _cli->Get("/api/nodes", uiHeaders);
    CHECK_TRUE(nodesRes != nullptr);
    LONGS_EQUAL(200, nodesRes->status);
    json nodesJson = json::parse(nodesRes->body);
    CHECK_TRUE(nodesJson.is_array());
    CHECK_TRUE(nodesJson.size() >= 1);

    // 6. Verify GET /api/node detail serializes cleanly without throwing
    auto nodeDetailRes = _cli->Get("/api/node?id=!aabbccdd", uiHeaders);
    CHECK_TRUE(nodeDetailRes != nullptr);
    LONGS_EQUAL(200, nodeDetailRes->status);
    json detailJson = json::parse(nodeDetailRes->body);
    CHECK_TRUE(detailJson.is_object());
}

TEST(WebServerSecurity, PostHandlersRejectMalformedAndNonObjectJson)
{
    // 1. Authenticate to obtain admin token
    auto loginRes = _cli->Post("/api/auth/login",
                               "{\"password\":\"AdminPass99\"}",
                               "application/json");
    CHECK_TRUE(loginRes != nullptr);
    LONGS_EQUAL(200, loginRes->status);
    json loginJson = json::parse(loginRes->body);
    std::string adminToken = loginJson.value("token", "");
    CHECK_FALSE(adminToken.empty());
    httplib::Headers adminHeaders = {{"Authorization", "Bearer " + adminToken}};

    // 2. /api/auth/login with non-object JSON and wrong types
    const std::vector<std::string> badLoginBodies = {
        "[]",
        "123",
        "\"string\"",
        "{\"password\": null}",
        "{\"password\": 123}",
        "{\"password\": [\"nested\"]}"
    };
    for (const auto &body : badLoginBodies) {
        auto res = _cli->Post("/api/auth/login", body, "application/json");
        CHECK_TRUE(res != nullptr);
        LONGS_EQUAL(400, res->status);
        json errJson = json::parse(res->body);
        CHECK_TRUE(errJson.contains("error"));
    }

    // 3. /api/send and /api/messages/send with non-object and wrong types
    for (const auto &ep : {"/api/send", "/api/messages/send"}) {
        auto nonObjRes1 = _cli->Post(ep, adminHeaders, "[]", "application/json");
        CHECK_TRUE(nonObjRes1 != nullptr);
        LONGS_EQUAL(400, nonObjRes1->status);

        auto nonObjRes2 = _cli->Post(ep, adminHeaders, "123", "application/json");
        CHECK_TRUE(nonObjRes2 != nullptr);
        LONGS_EQUAL(400, nonObjRes2->status);
    }

    const std::vector<std::string> badSendBodies = {
        "{\"text\": 123}",
        "{\"text\": null}",
        "{\"text\": \"hello\", \"channel\": \"bad\"}",
        "{\"text\": \"hello\", \"destination\": 123}"
    };
    for (const auto &body : badSendBodies) {
        auto res = _cli->Post("/api/send", adminHeaders, body, "application/json");
        CHECK_TRUE(res != nullptr);
        LONGS_EQUAL(400, res->status);
        json errJson = json::parse(res->body);
        CHECK_TRUE(errJson.contains("error"));
    }

    // Special-character unresolvable destination error escaping
    auto specDestRes = _cli->Post("/api/send", adminHeaders,
                                  "{\"text\":\"hi\",\"destination\":\"dest\\\"quotes\\nnewline\"}",
                                  "application/json");
    CHECK_TRUE(specDestRes != nullptr);
    LONGS_EQUAL(400, specDestRes->status);
    json specDestJson = json::parse(specDestRes->body);
    CHECK_TRUE(specDestJson.contains("error"));

    // 4. /api/automation/command with non-object and wrong types
    auto autoNonObj = _cli->Post("/api/automation/command", adminHeaders, "[]", "application/json");
    CHECK_TRUE(autoNonObj != nullptr);
    LONGS_EQUAL(400, autoNonObj->status);

    const std::vector<std::string> badAutoBodies = {
        "{\"command\": 123, \"node_id\": \"!11223344\"}",
        "{\"command\": null, \"node_id\": \"!11223344\"}",
        "{\"command\": \"status\", \"channel\": \"bad\", \"node_id\": \"!11223344\"}"
    };
    for (const auto &body : badAutoBodies) {
        auto res = _cli->Post("/api/automation/command", adminHeaders, body, "application/json");
        CHECK_TRUE(res != nullptr);
        LONGS_EQUAL(400, res->status);
        json errJson = json::parse(res->body);
        CHECK_TRUE(errJson.contains("error"));
    }

    // Special-character unresolvable node error escaping
    auto specAutoRes = _cli->Post("/api/automation/command", adminHeaders,
                                  "{\"node_id\":\"bad\\\"node\\nname\",\"command\":\"status\"}",
                                  "application/json");
    CHECK_TRUE(specAutoRes != nullptr);
    LONGS_EQUAL(404, specAutoRes->status);
    json specAutoJson = json::parse(specAutoRes->body);
    CHECK_TRUE(specAutoJson.contains("error"));

    // 5. /api/db/query with non-object and wrong types
    auto dbNonObj = _cli->Post("/api/db/query", adminHeaders, "[]", "application/json");
    CHECK_TRUE(dbNonObj != nullptr);
    LONGS_EQUAL(400, dbNonObj->status);

    const std::vector<std::string> badDbBodies = {
        "{\"query\": 123}",
        "{\"query\": null}",
        "{\"query\": [\"SELECT 1;\"]}",
        "{\"sql\": 123}"
    };
    for (const auto &body : badDbBodies) {
        auto res = _cli->Post("/api/db/query", adminHeaders, body, "application/json");
        CHECK_TRUE(res != nullptr);
        LONGS_EQUAL(400, res->status);
        json errJson = json::parse(res->body);
        CHECK_TRUE(errJson.contains("error"));
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
