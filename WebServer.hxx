/*
 * WebServer.hxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#ifndef WEBSERVER_HXX
#define WEBSERVER_HXX

#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <map>
#include <queue>
#include <deque>
#include <vector>
#include <ctime>
#include <libmeshtastic.h>

namespace httplib {
class Server;
class Request;
class Response;
}

class MeshMon;
class MeshMonDb;
class SpatialAnalytics;
class AimonGatewayClient;

struct WebConfig {
    bool enabled = true;
    uint16_t port = 16880;
    std::string host = "0.0.0.0";
    std::string password = "admin";
    bool endpointsEnabled = false;
    std::string gatewayHost = "127.0.0.1";
};

struct PacketLogEntry {
    uint32_t id = 0;
    time_t meshmonTime = 0;
    uint32_t fromNode = 0;
    std::string fromHex;
    std::string fromName;
    uint32_t toNode = 0;
    std::string toHex;
    std::string toName;
    uint32_t channel = 0;
    float rxRssi = 0.0f;
    float rxSnr = 0.0f;
    int hopStart = 0;
    int hopLimit = 0;
    int hops = 0;
    int portnum = 0;
    std::string appName;
    std::string text;
    size_t payloadSize = 0;
    bool viaMqtt = false;
};

struct SseSession {
    std::string id;
    std::queue<std::string> messageQueue;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> closed{false};
};

struct AuthSession {
    std::string token;
    time_t createdAt = 0;
    time_t expiresAt = 0;
};

class WebServer {

public:

    static constexpr size_t MAX_UI_SESSIONS = 256;
    static constexpr size_t MAX_AUTH_SESSIONS = 64;
    static constexpr size_t MAX_SSE_QUEUE_SIZE = 128;

    WebServer(std::shared_ptr<MeshMon> mon,
              std::shared_ptr<MeshMonDb> db,
              const WebConfig &config);
    ~WebServer();

    bool start(bool async = true);
    void stop(void);
    void join(void);
    bool isRunning(void) const;

    void setAdminPassword(const std::string &password);

    void onPacketReceived(const meshtastic_MeshPacket &packet, time_t meshmonTime);
    void broadcastSseEvent(const std::string &eventType, const std::string &jsonData);

    bool isAllowedMcpRequest(const httplib::Request &req) const;
    std::string createMcpSession(void);

    const WebConfig &getConfig(void) const {
        return _config;
    }

private:

    void setupRoutes(void);

    // Authentication helpers
    bool checkAuth(const httplib::Request &req) const;
    bool checkUiSession(const httplib::Request &req) const;
    bool checkBasicAuth(const httplib::Request &req) const;
    std::string createUiSessionToken(void);
    std::string createAuthSessionToken(void);
    std::string createSessionToken(void);
    bool validateUiSessionToken(const std::string &token) const;
    bool validateAuthSessionToken(const std::string &token) const;
    bool validateSessionToken(const std::string &token) const;
    void revokeSessionToken(const std::string &token);
    void purgeExpiredTokens(void) const;
    std::string extractToken(const httplib::Request &req) const;
    bool isValidUiSession(const httplib::Request &req) const;
    static std::string getMcpHintForPath(const std::string &path, const std::string &body);

    // JSON API handlers
    void handleGetStatus(const httplib::Request &req, httplib::Response &res);
    void handleGetNodes(const httplib::Request &req, httplib::Response &res);
    void handleGetNodeDetail(const httplib::Request &req, httplib::Response &res);
    void handleGetPackets(const httplib::Request &req, httplib::Response &res);
    void handleGetAnalytics(const httplib::Request &req, httplib::Response &res);
    void handleGetAutomation(const httplib::Request &req, httplib::Response &res);
    void handleGetMessages(const httplib::Request &req, httplib::Response &res);
    void handleGetSpatial(const httplib::Request &req, httplib::Response &res);
    void handleGetRemoteSummary(const httplib::Request &req, httplib::Response &res);
    void handleGetRemoteNodes(const httplib::Request &req, httplib::Response &res);
    void handleGetTopologyRoutes(const httplib::Request &req, httplib::Response &res);
    void handleGetTopologyAsymmetry(const httplib::Request &req, httplib::Response &res);
    void handleGetTopologyCentroids(const httplib::Request &req, httplib::Response &res);

    // Authenticated mutating handlers
    void handlePostAuthLogin(const httplib::Request &req, httplib::Response &res);
    void handleGetAuthStatus(const httplib::Request &req, httplib::Response &res);
    void handlePostAuthLogout(const httplib::Request &req, httplib::Response &res);
    void handlePostSendMessage(const httplib::Request &req, httplib::Response &res);
    void handlePostAutomationCommand(const httplib::Request &req, httplib::Response &res);
    void handlePostDbQuery(const httplib::Request &req, httplib::Response &res);

    // Static assets
    void serveStaticFileOrFallback(const std::string &diskPath,
                                   const char *fallbackAsset,
                                   const std::string &contentType,
                                   httplib::Response &res);

    struct McpSessionState {
        std::string sessionId;
        time_t lastActivity = 0;
        std::vector<std::string> pendingMessages;
        bool closed = false;
    };

    std::shared_ptr<MeshMon> _mon;
    std::shared_ptr<MeshMonDb> _db;
    std::shared_ptr<SpatialAnalytics> _spatial;
    WebConfig _config;

    std::unique_ptr<httplib::Server> _server;
    std::unique_ptr<std::thread> _thread;
    std::atomic<bool> _running{false};
    time_t _startTime = 0;

    std::unique_ptr<AimonGatewayClient> _mcpDispatcher;

    // MCP Streamable HTTP session registry
    mutable std::mutex _mcpMutex;
    std::condition_variable _mcpCv;
    std::map<std::string, McpSessionState> _mcpSessions;

    // SSE session registry
    mutable std::mutex _sseMutex;
    std::map<std::string, std::shared_ptr<SseSession>> _sseSessions;

    // Recent packet ring buffer
    mutable std::mutex _packetRingMutex;
    std::deque<PacketLogEntry> _recentPackets;
    size_t _maxRecentPackets = 150;
    uint32_t _nextPacketId = 1;

    // Dual-tier session maps (_uiSessions for read-only UI, _authSessions for admin)
    mutable std::mutex _authMutex;
    mutable std::map<std::string, time_t> _uiSessions;
    mutable std::deque<std::string> _uiSessionOrder;
    mutable std::map<std::string, time_t> _authSessions;
    mutable std::deque<std::string> _authSessionOrder;

};

#endif // WEBSERVER_HXX

/*
 * Local variables:
 * mode: C++
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
