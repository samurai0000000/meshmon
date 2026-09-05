/*
 * MeshMon.hxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#ifndef MESHMON_HXX
#define MESHMON_HXX

#include <HomeChat.hxx>
#include <MeshClient.hxx>
#include <MeshNvm.hxx>
#include <ChatBot.hxx>
#include <Calibration.hxx>
#include <MeshMonDb.hxx>
#include <map>
#include <chrono>

using namespace std;

struct AutomationNode {
    uint32_t nodeId;
    string nodeHex;
    string shortName;
    string longName;
    string deviceType;       // "meshpump", "meshroof", "meshroom"
    string version;
    string hardware;
    string capabilities;
    time_t firstSeen;
    time_t lastSeen;
    uint32_t uptimeSec;
    time_t lastUptimeReportTime;
    uint32_t rebootCount;
    bool online;
    bool haDiscovered;

    // Response Latency (RTT) tracking
    uint32_t lastRttMs;
    uint32_t avgRttMs;
    uint32_t rttSampleCount;

    // meshpump states
    bool fishPumpState;
    bool upPumpState;
    uint32_t upPumpCutoffSec;
    float soilMoisture;
    bool reservoirEmpty;
    string ledMessage;
    uint32_t ledScrollDelay;

    // meshroof states
    bool amplifyState;
    string wifiStatus;
    int wifiRssi;
    string ipAddress;
    float cpuTempC;
    uint32_t resetCount;

    // meshroom states
    bool acPower;
    float acTargetTemp;
    string acMode;
    string acFan;
    string acVane;
    bool tvPower;
    int tvVolume;
    int tvChannel;
    bool tvMute;
    string tvInput;
    float boardTempC;
    float roomTempC;

    AutomationNode() :
        nodeId(0), firstSeen(0), lastSeen(0), uptimeSec(0),
        lastUptimeReportTime(0), rebootCount(0), online(false), haDiscovered(false),
        lastRttMs(0), avgRttMs(0), rttSampleCount(0),
        fishPumpState(false), upPumpState(false), upPumpCutoffSec(0),
        soilMoisture(0.0f), reservoirEmpty(false), ledScrollDelay(0),
        amplifyState(false), wifiRssi(0), cpuTempC(0.0f), resetCount(0),
        acPower(false), acTargetTemp(24.0f), acMode("off"), acFan("auto"), acVane("auto"),
        tvPower(false), tvVolume(20), tvChannel(1), tvMute(false), tvInput("HDMI1"),
        boardTempC(0.0f), roomTempC(0.0f) {}
};

class MqttClient;

class MeshMon : public MeshClient, public MeshNvm, public HomeChat {

public:

    MeshMon();
    ~MeshMon();

    void join(void);

    bool verbose(void) const;
    void setVerbose(bool verbose);

    virtual void setClient(shared_ptr<SimpleClient> client);
    virtual void setNvm(shared_ptr<BaseNvm> nvm);

    float getCpuTempC(void);
    bool isSensorForwardAllowed(uint32_t nodeId) const;

    // HomeMesh Automation Fleet Access
    map<uint32_t, AutomationNode> getAutomationNodes(void) const;
    bool getAutomationNode(uint32_t nodeId, AutomationNode &node) const;
    bool sendAutomationCommand(uint32_t nodeId, const string &cmd,
                               const string &initiator = "SHELL",
                               int channel = -1);

protected:

    // Extend MeshClient

    virtual void gotPacket(const meshtastic_MeshPacket &packet);
    virtual void syncHostClock(uint32_t epoch_seconds);
    virtual void gotConfigCompleteId(uint32_t id);
    virtual void gotDeviceConfig(const meshtastic_Config_DeviceConfig &c);
    virtual void gotRebooted(bool rebooted);
    virtual void loop(void);
    virtual void crontab(const struct tm *now);
    virtual void topOfHourTask(const struct tm *now);
    virtual void gotModuleConfigMQTT(const meshtastic_ModuleConfig_MQTTConfig &c);
    virtual void gotMqttClientProxyMessage(const meshtastic_MqttClientProxyMessage &m);
    virtual void gotTextMessage(const meshtastic_MeshPacket &packet,
                                const string &message);
    virtual void gotPosition(const meshtastic_MeshPacket &packet,
                             const meshtastic_Position &position);
    virtual void gotUser(const meshtastic_MeshPacket &packet,
                         const meshtastic_User &user);
    virtual void gotRouting(const meshtastic_MeshPacket &packet,
                            const meshtastic_Routing &routing);
    virtual void gotAdminMessage(const meshtastic_MeshPacket &packet,
                                 const meshtastic_AdminMessage &adminMessage);
    virtual void gotDeviceMetrics(const meshtastic_MeshPacket &packet,
                                  const meshtastic_DeviceMetrics &metrics);
    virtual void gotEnvironmentMetrics(const meshtastic_MeshPacket &packet,
                                       const meshtastic_EnvironmentMetrics &metrics);
    virtual void gotAirQualityMetrics(const meshtastic_MeshPacket &packet,
                                      const meshtastic_AirQualityMetrics &metrics);
    virtual void gotPowerMetrics(const meshtastic_MeshPacket &packet,
                                       const meshtastic_PowerMetrics &metrics);
    virtual void gotLocalStats(const meshtastic_MeshPacket &packet,
                               const meshtastic_LocalStats &stats);
    virtual void gotHealthMetrics(const meshtastic_MeshPacket &packet,
                                  const meshtastic_HealthMetrics &metrics);
    virtual void gotHostMetrics(const meshtastic_MeshPacket &packet,
                                const meshtastic_HostMetrics &metrics);
    virtual void gotTraceRoute(const meshtastic_MeshPacket &packet,
                               const meshtastic_RouteDiscovery &routeDiscovery);

    inline virtual HomeChat *getHomeChat(void) {
        return this;
    }

public:

    // Extend MeshNvm

    virtual bool loadNvm(void);
    virtual bool saveNvm(void);

protected:

    // Extend HomeChat

    virtual bool handleTextMessage(const meshtastic_MeshPacket &packet,
                                   const string &message);
    virtual void handleTimeBroadcast(const meshtastic_MeshPacket &packet,
                                     time_t epoch, const string &tz);
    virtual string handleEnv(uint32_t node_num, string &message);
    virtual int vprintf(const char *format, va_list ap) const;
    bool matchBotAddressing(const string &rawMessage, bool directMessage, string &cleanQuery) const;
    void syncRadioClock(void);
    void publishGatewayStatsToMqtt(void);

    // HomeMesh Automation Handling
    bool processAutomationMessage(const meshtastic_MeshPacket &packet,
                                  const string &message);
    bool parseRollcallResponse(const meshtastic_MeshPacket &packet,
                               const string &text, uint32_t rttMs = 0);
    bool parseBootupMessage(const meshtastic_MeshPacket &packet,
                            const string &text, uint32_t rttMs = 0);
    bool parseUptimeMessage(const meshtastic_MeshPacket &packet,
                            const string &text, uint32_t rttMs = 0);
    bool parseMeshPumpStatus(const meshtastic_MeshPacket &packet,
                             const string &text, uint32_t rttMs = 0);
    bool parseMeshRoofStatus(const meshtastic_MeshPacket &packet,
                             const string &text, uint32_t rttMs = 0);
    bool parseMeshRoomStatus(const meshtastic_MeshPacket &packet,
                             const string &text, uint32_t rttMs = 0);
    void loadAutomationNodesFromDb(void);
    void publishAllDiscoveredNodes(void);
    void ensureAutomationDiscovery(uint32_t nodeId, uint32_t channel = 0);
    void publishAutomationDiscovery(AutomationNode &node);
    void revokeAutomationDiscovery(uint32_t nodeId, const string &oldDeviceType);
    void publishAutomationState(const AutomationNode &node);
    void handleMqttCommand(const string &topic, const string &payload);
    void checkAutomationWatchdog(void);

public:

    inline const shared_ptr<MqttClient> meshtasticMqtt(void) const {
        return _meshtasticMqtt;
    }

    inline const shared_ptr<MqttClient> myownMqtt(void) const {
        return _myownMqtt;
    }

    inline const shared_ptr<Calibration> calibration(void) const {
        return _calibration;
    }

    inline const shared_ptr<ChatBot> chatbot(void) const {
        return _chatbot;
    }

    inline const shared_ptr<MeshMonDb> db(void) const {
        return _db;
    }

    void setOwnMqtt(const string &server, uint16_t port,
                    const string &user, const string &password,
                    const string &topic, bool tls);

    void setChatBot(shared_ptr<ChatBot> bot);
    void setCalibration(shared_ptr<Calibration> calib);
    void setDb(shared_ptr<MeshMonDb> db);

private:

    bool _verbose;
    shared_ptr<MqttClient> _meshtasticMqtt;
    shared_ptr<MqttClient> _myownMqtt;
    shared_ptr<ChatBot> _chatbot;
    shared_ptr<Calibration> _calibration;
    shared_ptr<MeshMonDb> _db;
    bool _haGatewayDiscovered;
    map<uint32_t, string> _haEnvNames;
    map<uint32_t, unsigned int> _haEnvMetrics;
    map<uint32_t, string> _haPowerNames;
    map<uint32_t, unsigned int> _haPowerMetrics;
    map<uint32_t, string> _haDeviceNames;
    map<uint32_t, unsigned int> _haDeviceMetrics;

    mutable mutex _autoNodesMutex;
    map<uint32_t, AutomationNode> _autoNodes;

    struct PendingCommand {
        string command;
        chrono::steady_clock::time_point txTime;
    };
    map<uint32_t, PendingCommand> _pendingCommands;

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
