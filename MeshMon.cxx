/*
 * MeshMon.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <MqttClient.hxx>
#include <MeshMon.hxx>

MeshMon::MeshMon()
    : MeshClient()
{

}

MeshMon::~MeshMon()
{
    if (_meshtasticMqtt != NULL) {
        _meshtasticMqtt->stop();
        _meshtasticMqtt->join();
        _meshtasticMqtt = NULL;
    }

    if (_myownMqtt != NULL) {
        _myownMqtt->stop();
        _myownMqtt->join();
        _myownMqtt = NULL;
    }
}

void MeshMon::join(void)
{
    MeshClient::join();

    if (_meshtasticMqtt != NULL) {
        _meshtasticMqtt->stop();
        _meshtasticMqtt->join();
        _meshtasticMqtt = NULL;
    }

    if (_myownMqtt != NULL) {
        _myownMqtt->stop();
        _myownMqtt->join();
        _myownMqtt = NULL;
    }
}

float MeshMon::getCpuTempC(void)
{
#define MAX_STRING        1024
#define GET_GENCMD_RESULT 0x00030080
    float tempC = 0.0;
    int fd = -1;
    int ret;
    static const char *command = "measure_temp";
    unsigned p[(MAX_STRING >> 2) + 7];
    unsigned int i = 0;
    const char *s;
    string str;

    fd = open("/dev/vcio", 0);
    if (fd == -1) {
        fprintf(stderr, "open: %s!\n", strerror(errno));
        goto done;
    }

    i = 0;
    p[i++] = 0; // size
    p[i++] = 0x00000000; // process request
    p[i++] = GET_GENCMD_RESULT; // (the tag id)
    p[i++] = MAX_STRING;// buffer_len
    p[i++] = 0; // request_len (set to response length)
    p[i++] = 0; // error repsonse
    memcpy(p + i, command, strlen(command) + 1);
    i += MAX_STRING >> 2;
    p[i++] = 0x00000000; // end tag
    p[0] = i * sizeof(*p); // actual size

    ret = ioctl(fd, _IOWR(100, 0, char *), p);
    if (ret == -1) {
        fprintf(stderr, "ioctl: %s!\n", strerror(errno));
        goto done;
    }

    s = (const char *) (p + 6);
    for (unsigned int i = 0; i < (sizeof(p) - 6); i++) {
        if (s[i] == '\'') {
            break;
        }
        if (isdigit(s[i]) || (s[i] == '.')) {
            str += s[i];
        }
    }

    try {
        tempC = stof(str);
    } catch (const invalid_argument& e) {
    } catch (const out_of_range &e) {
    }

done:

    if (fd != -1) {
        close(fd);
    }

    return tempC;
}

void MeshMon::gotModuleConfigMQTT(const meshtastic_ModuleConfig_MQTTConfig &c)
{
    if (c.proxy_to_client_enabled && (_meshtasticMqtt == NULL)) {
        // Turn on MQTT client proxy
        _meshtasticMqtt = make_shared<MqttClient>();
        _meshtasticMqtt->start();
    }
}

void MeshMon::gotMqttClientProxyMessage(const meshtastic_MqttClientProxyMessage &m)
{
    MeshClient::gotMqttClientProxyMessage(m);

    meshtastic_MeshPacket packet;
    bool found = false;

#if 0
    // Can't get this to work!
    char channel_id[32];
    char gateway_id[32];
    meshtastic_ServiceEnvelope q = {
        .packet = &packet,
        .channel_id = channel_id,
        .gateway_id = gateway_id,
    };
    pb_istream_t stream;

    stream = pb_istream_from_buffer(m.payload_variant.data.bytes,
                                    m.payload_variant.data.size);
    found = pb_decode(&stream, meshtastic_ServiceEnvelope_fields, &q);
#else
    // This is a hack... until we can directly decode ServiceEnvelope above
    const uint8_t *bytes = m.payload_variant.data.bytes;
    size_t size = m.payload_variant.data.size;
    pb_istream_t stream;

    while (isprint(bytes[size - 1])) {
        size--;
    }

    for (size_t i = 7; i < 10 && !found; i++) {
        for (size_t l = size; l > i; l--) {
            stream = pb_istream_from_buffer(bytes + i, l);
            found = pb_decode(&stream, meshtastic_MeshPacket_fields,
                              &packet);
            if (found) {
                break;
            }
        }
    }
#endif

    if (!found) {
        cerr << "pb_decode failed!" << endl;
        goto done;
    }

    if (packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        goto done;
    }

    switch (packet.decoded.portnum) {
    case meshtastic_PortNum_POSITION_APP:
    case meshtastic_PortNum_NODEINFO_APP:
    case meshtastic_PortNum_TELEMETRY_APP:
        // The list above are sanctioned for upload for the benefit of
        // meshmap.net
        if (_meshtasticMqtt != NULL) {
            _meshtasticMqtt->publish(m);
#if 0
            cout << "mqtt-proxy: " << packet.decoded.portnum << " "
                 << "published="
                 << _meshtasticMqtt->publishConfirmed() << "/"
                 << _meshtasticMqtt->published()
                 << endl;
#endif
        }
        break;
    default:
        // Don't allow any other app to upload to MQTT
        // We don't want to upload conversations to the MQTT server!
        break;
    }

done:

    return;
}

void MeshMon::gotTextMessage(const meshtastic_MeshPacket &packet,
                             const string &message)
{
    bool result = false;

    MeshClient::gotTextMessage(packet, message);
    result = handleTextMessage(packet, message);
    if (result) {
        return;
    }
}

void MeshMon::gotPosition(const meshtastic_MeshPacket &packet,
                          const meshtastic_Position &position)
{
    MeshClient::gotPosition(packet, position);

#if 0
    if (!verbose()) {
        if (packet.from != whoami()) {
            cout << getDisplayName(packet.from)
                 << " sent position"
                 << " [rssi:" << packet.rx_rssi << "]"
                 << " [hops:" << hopsAway(packet) << "]"
                 << endl;
        }
    }
#endif

    if (_myownMqtt != NULL) {
        _myownMqtt->publish(packet);
    }
}

void MeshMon::gotUser(const meshtastic_MeshPacket &packet,
                      const meshtastic_User &user)
{
    MeshClient::gotUser(packet, user);

#if 0
    if (!verbose()) {
        if (packet.from != whoami()) {
            cout << getDisplayName(packet.from)
                 << " sent nodeInfo.user"
                 << " [rssi:" << packet.rx_rssi << "]"
                 << " [hops:" << hopsAway(packet) << "]"
                 << endl;
        }
    }
#endif
}

void MeshMon::gotRouting(const meshtastic_MeshPacket &packet,
                         const meshtastic_Routing &routing)
{
    MeshClient::gotRouting(packet, routing);

#if 0
    if ((routing.which_variant == meshtastic_Routing_error_reason_tag) &&
        (routing.error_reason == meshtastic_Routing_Error_NONE) &&
        (packet.from != packet.to)) {
        cout << "traceroute from " << getDisplayName(packet.from) << " -> ";
        cout << getDisplayName(packet.to)
             << "[" << packet.rx_snr << "dB]" << endl;
    }
#endif
}

void MeshMon::gotAdminMessage(const meshtastic_MeshPacket &packet,
                              const meshtastic_AdminMessage &adminMessage)
{
    MeshClient::gotAdminMessage(packet, adminMessage);
    if (!verbose()) {
        cout << adminMessage;
        cout << "---" << endl;
        cout << packet;
    }
}

void MeshMon::gotDeviceMetrics(const meshtastic_MeshPacket &packet,
                               const meshtastic_DeviceMetrics &metrics)
{
    MeshClient::gotDeviceMetrics(packet, metrics);

#if 0
    if (!verbose()) {
        if (packet.from != whoami()) {
            cout << getDisplayName(packet.from)
                 << " sent device metrics"
                 << " [rssi:" << packet.rx_rssi << "]"
                 << " [hops:" << hopsAway(packet) << "]"
                 << endl;
        }
    }
#endif

    if (_myownMqtt != NULL) {
        _myownMqtt->publish(packet);
    }
}

void MeshMon::gotEnvironmentMetrics(const meshtastic_MeshPacket &packet,
                                    const meshtastic_EnvironmentMetrics &metrics)
{
    MeshClient::gotEnvironmentMetrics(packet, metrics);

#if 0
    if (!verbose()) {
        if (packet.from != whoami()) {
            cout << getDisplayName(packet.from)
                 << " sent environment metrics"
                 << " [rssi:" << packet.rx_rssi << "]"
                 << " [hops:" << hopsAway(packet) << "]"
                 << endl;
        }
    }
#endif

    if (_myownMqtt != NULL) {
        _myownMqtt->publish(packet);
    }
}

void MeshMon::gotAirQualityMetrics(const meshtastic_MeshPacket &packet,
                                   const meshtastic_AirQualityMetrics &metrics)
{
    MeshClient::gotAirQualityMetrics(packet, metrics);

#if 0
    if (!verbose()) {
        if (packet.from != whoami()) {
            cout << getDisplayName(packet.from)
                 << " sent air quality metrics"
                 << " [rssi:" << packet.rx_rssi << "]"
                 << " [hops:" << hopsAway(packet) << "]"
                 << endl;
        }
    }
#endif

    if (_myownMqtt != NULL) {
        _myownMqtt->publish(packet);
    }
}

void MeshMon::gotPowerMetrics(const meshtastic_MeshPacket &packet,
                              const meshtastic_PowerMetrics &metrics)
{
    MeshClient::gotPowerMetrics(packet, metrics);

#if 0
    if (!verbose()) {
        if (packet.from != whoami()) {
            cout << getDisplayName(packet.from)
                 << " sent power metrics"
                 << " [rssi:" << packet.rx_rssi << "]"
                 << " [hops:" << hopsAway(packet) << "]"
                 << endl;
        }
    }
#endif

    if (_myownMqtt != NULL) {
        _myownMqtt->publish(packet);
    }
}

void MeshMon::gotLocalStats(const meshtastic_MeshPacket &packet,
                            const meshtastic_LocalStats &stats)
{
    MeshClient::gotLocalStats(packet, stats);

#if 0
    if (!verbose()) {
        if (packet.from != whoami()) {
            cout << getDisplayName(packet.from)
                 << " sent local stats"
                 << " [rssi:" << packet.rx_rssi << "]"
                 << " [hops:" << hopsAway(packet) << "]"
                 << endl;
        }
    }
#endif
}

void MeshMon::gotHealthMetrics(const meshtastic_MeshPacket &packet,
                               const meshtastic_HealthMetrics &metrics)
{
    MeshClient::gotHealthMetrics(packet, metrics);

#if 0
    if (!verbose()) {
        if (packet.from != whoami()) {
            cout << getDisplayName(packet.from)
                 << " sent health metrics"
                 << " [rssi:" << packet.rx_rssi << "]"
                 << " [hops:" << hopsAway(packet) << "]"
                 << endl;
        }
    }
#endif

    if (_myownMqtt != NULL) {
        _myownMqtt->publish(packet);
    }
}

void MeshMon::gotHostMetrics(const meshtastic_MeshPacket &packet,
                             const meshtastic_HostMetrics &metrics)
{
    MeshClient::gotHostMetrics(packet, metrics);

#if 0
    if (!verbose()) {
        if (packet.from != whoami()) {
            cout << getDisplayName(packet.from)
                 << "sent host metrics"
                 << " [rssi:" << packet.rx_rssi << "]"
                 << " [hops:" << hopsAway(packet) << "]"
                 << endl;
        }
    }
#endif

    if (_myownMqtt != NULL) {
        _myownMqtt->publish(packet);
    }
}

void MeshMon::gotTraceRoute(const meshtastic_MeshPacket &packet,
                            const meshtastic_RouteDiscovery &routeDiscovery)
{
    MeshClient::gotTraceRoute(packet, routeDiscovery);
#if 0
    if (!verbose()) {
        if ((routeDiscovery.route_count > 0) &&
            (routeDiscovery.route_back_count == 0)) {
            float rx_snr;
            cout << "traceroute from " << getDisplayName(packet.from)
                 << " -> ";
            for (unsigned int i = 0; i < routeDiscovery.route_count; i++) {
                if (i > 0) {
                    cout << " -> ";
                }
                cout << getDisplayName(routeDiscovery.route[i]);
                if (routeDiscovery.snr_towards[i] != INT8_MIN) {
                    rx_snr = routeDiscovery.snr_towards[i];
                    rx_snr /= 4.0;
                    cout << "[" << rx_snr << "dB]";
                } else {
                    cout << "[???dB]";
                }
            }
            rx_snr = packet.rx_snr;
            cout << " -> " << getDisplayName(packet.to)
                 << "[" << rx_snr << "dB]" << endl;
        }
    }
#endif

    if (_myownMqtt != NULL) {
        _myownMqtt->publish(packet);
    }
}

bool MeshMon::loadNvm(void)
{
    bool result;

    result = MeshNvm::loadNvm();

    return result;
}

bool MeshMon::saveNvm(void)
{
    bool result;

    result = MeshNvm::saveNvm();

    return result;
}

string MeshMon::handleEnv(uint32_t node_num, string &message)
{
    stringstream ss;

    ss << HomeChat::handleEnv(node_num, message);
    if (!ss.str().empty()) {
        ss << endl;
    }

    ss << "cpu temperature: ";
    ss <<  setprecision(3) << getCpuTempC();

    return ss.str();
}

static inline int stdio_vprintf(const char *format, va_list ap)
{
    return vprintf(format, ap);
}

int MeshMon::vprintf(const char *format, va_list ap) const
{
    return stdio_vprintf(format, ap);
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
