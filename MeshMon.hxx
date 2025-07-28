/*
 * MeshMon.hxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#ifndef MESHMON_HXX
#define MESHMON_HXX

#include <LibMeshtastic.hxx>

using namespace std;

class MqttClient;

class MeshMon : public MeshClient {

public:

    MeshMon();
    ~MeshMon();

    void join(void);

protected:

    virtual void gotModuleConfigMQTT(const meshtastic_ModuleConfig_MQTTConfig &c);
    virtual void gotMqttClientProxyMessage(const meshtastic_MqttClientProxyMessage &m);
    virtual void gotTextMessage(const meshtastic_MeshPacket &packet,
                                const string &message);
    virtual void gotPosition(const meshtastic_MeshPacket &packet,
                             const meshtastic_Position &position);
    virtual void gotRouting(const meshtastic_MeshPacket &packet,
                            const meshtastic_Routing &routing);
    virtual void gotUser(const meshtastic_MeshPacket &packet,
                         const meshtastic_User &user);
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

private:

    shared_ptr<MqttClient> _meshtasticMqtt;
    shared_ptr<MqttClient> _myownMqtt;

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
