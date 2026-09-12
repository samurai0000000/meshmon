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

class MeshMon;
class MeshMonDb;

class AimonGatewayClient {

public:

    AimonGatewayClient(std::shared_ptr<MeshMon> mon = nullptr,
                       std::shared_ptr<MeshMonDb> db = nullptr);
    ~AimonGatewayClient();

    void setMeshMon(std::shared_ptr<MeshMon> mon);
    void setDb(std::shared_ptr<MeshMonDb> db);

    bool start(const std::string &host = "builder", uint16_t port = 3885);
    void stop(void);
    void join(void);

    bool isConnected(void) const;

private:

    void run(void);
    bool connectToGateway(void);
    void disconnect(void);
    bool sendRegistration(void);
    void processIncoming(void);

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

    std::atomic<bool> _running;
    std::atomic<bool> _connected;
    int _sockFd;

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
