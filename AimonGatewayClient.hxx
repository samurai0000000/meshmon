/*
 * AimonGatewayClient.hxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#ifndef MESHMON_AIMON_GATEWAY_CLIENT_HXX
#define MESHMON_AIMON_GATEWAY_CLIENT_HXX

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <nlohmann/json.hpp>

class MeshMon;
class MeshMonDb;

class AimonGatewayClient {

public:

    AimonGatewayClient(std::shared_ptr<MeshMon> mon = nullptr,
                       std::shared_ptr<MeshMonDb> db = nullptr);
    ~AimonGatewayClient();

    void setMeshMon(std::shared_ptr<MeshMon> mon);
    void setDb(std::shared_ptr<MeshMonDb> db);

    bool start(const std::string &host = "127.0.0.1", uint16_t port = 3885);
    void stop(void);
    void disconnect(void);
    void join(void);

    bool isConnected(void) const;

    void setWebPort(uint16_t port) { _webPort = port; }
    uint16_t getWebPort(void) const { return _webPort; }

    std::string buildRegistrationJson(void) const;
    static std::string getRegistrationJsonSchema(uint16_t webPort = 16880);

    std::string dispatchJsonRpc(const std::string &rawBody,
                                const std::string &defaultProtocolVersion = "2025-03-26");

protected:

    void run(void);
    bool connectToGateway(void);
    bool sendRegistration(void);
    void processIncoming(void);

    std::string dispatchSingleJsonRpc(const nlohmann::json &req,
                                      const std::string &defaultProtocolVersion);
    void handleRequest(const std::string &line);
    void sendResponse(const std::string &jsonResponse);

    // Tool execution handlers
    std::string toolGetNodeStatus(const std::string &nodeIdArg);
    std::string toolQueryTelemetryHistory(const std::string &nodeIdArg,
                                          const std::string &metric,
                                          int hours, int limit);
    std::string toolGetRfAnalytics(int hours);
    std::string toolSendMessage(const std::string &text,
                                const std::string &destination,
                                int channel);
    std::string toolQueryDb(const std::string &sql);
    std::string toolGetSpatialAnalytics(int premiseThresholdM, int topFarthest, int hours);

    std::shared_ptr<MeshMon> _mon;
    std::shared_ptr<MeshMonDb> _db;

    std::string _host;
    uint16_t _port;
    uint16_t _webPort = 0;

    std::atomic<bool> _running;
    std::atomic<bool> _connected;
    std::atomic<int> _sockFd;

    std::thread _thread;
    std::mutex _sendMutex;

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
