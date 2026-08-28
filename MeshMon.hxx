/*
 * MeshMon.hxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#ifndef MESHMON_HXX
#define MESHMON_HXX

#include <LibMeshtastic.hxx>
#include <HomeChat.hxx>
#include <MeshNvm.hxx>
#include <ChatBot.hxx>
#include <Calibration.hxx>
#include <map>

using namespace std;

class MqttClient;

class MeshMon : public MeshClient, public MeshNvm, public HomeChat {

public:

    MeshMon();
    ~MeshMon();

    void join(void);

    virtual void setClient(shared_ptr<SimpleClient> client);
    virtual void setNvm(shared_ptr<BaseNvm> nvm);

    float getCpuTempC(void);

protected:

    // Extend MeshClient

    virtual void syncHostClock(uint32_t epoch_seconds);
    virtual void gotConfigCompleteId(uint32_t id);
    virtual void gotDeviceConfig(const meshtastic_Config_DeviceConfig &c);
    virtual void gotRebooted(bool rebooted);
    virtual void loop(void);
    virtual void crontab(const struct tm *now);
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

    virtual void handleTimeBroadcast(const meshtastic_MeshPacket &packet,
                                     time_t epoch, const string &tz);
    virtual string handleEnv(uint32_t node_num, string &message);
    virtual string handleUnknown(uint32_t node_num, uint32_t dest,
                                 uint8_t channel, string &message);
    virtual int vprintf(const char *format, va_list ap) const;

    void syncRadioClock(void);


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

    void setOwnMqtt(const string &server, uint16_t port,
                    const string &user, const string &password,
                    const string &topic, bool tls);

    void setChatBot(shared_ptr<ChatBot> bot);
    void setCalibration(shared_ptr<Calibration> calib);

private:

    shared_ptr<MqttClient> _meshtasticMqtt;
    shared_ptr<MqttClient> _myownMqtt;
    shared_ptr<ChatBot> _chatbot;
    shared_ptr<Calibration> _calibration;
    bool _announcedUp;
    map<uint32_t, string> _haEnvNames;
    map<uint32_t, unsigned int> _haEnvMetrics;

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
