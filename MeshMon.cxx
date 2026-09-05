/*
 * MeshMon.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <MqttClient.hxx>
#include <MeshMon.hxx>

#ifndef DEBUG_CHATBOT
#define DEBUG_CHATBOT 0
#endif

MeshMon::MeshMon()
    : MeshClient()
{
    _verbose = false;
    _isClockSynced = true;
    _haGatewayDiscovered = false;
}

bool MeshMon::verbose(void) const
{
    return _verbose;
}

void MeshMon::setVerbose(bool verbose)
{
    _verbose = verbose;
}

MeshMon::~MeshMon()
{
    if (_chatbot != NULL) {
        _chatbot->stop();
        _chatbot->join();
        _chatbot = NULL;
    }

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

void MeshMon::setClient(shared_ptr<SimpleClient> client)
{
    if (client && (client.get() == static_cast<SimpleClient *>(this))) {
        // Non-owning: HomeChat must not keep a shared_ptr to *this
        HomeChat::setClient(shared_ptr<SimpleClient>(
                                shared_ptr<SimpleClient>(), this));
        return;
    }

    HomeChat::setClient(client);
}

void MeshMon::setNvm(shared_ptr<BaseNvm> nvm)
{
    if (nvm && (nvm.get() == static_cast<BaseNvm *>(this))) {
        // Non-owning: HomeChat and SimpleClient must not keep a shared_ptr to *this
        shared_ptr<BaseNvm> nonOwning(shared_ptr<BaseNvm>(), this);
        HomeChat::setNvm(nonOwning);
        SimpleClient::setNvm(nonOwning);
        return;
    }

    HomeChat::setNvm(nvm);
    SimpleClient::setNvm(nvm);
}

void MeshMon::syncHostClock(uint32_t epoch_seconds)
{
    (void)(epoch_seconds);
    _isClockSynced = true;
}

void MeshMon::syncRadioClock(void)
{
    if (!isConnected()) {
        return;
    }

    time_t now = time(NULL);
    adminSetTime((uint32_t) now);
}

void MeshMon::gotConfigCompleteId(uint32_t id)
{
    if (setupFor(whoami()) == true) {
        if (loadNvm() == false) {
            saveNvm();
        }
        syncFromNvm();
    }

    MeshClient::gotConfigCompleteId(id);
    syncRadioClock();
}

void MeshMon::gotDeviceConfig(const meshtastic_Config_DeviceConfig &c)
{
    _deviceConfig = c;
}

void MeshMon::gotRebooted(bool rebooted)
{
    MeshClient::gotRebooted(rebooted);
}

void MeshMon::loop(void)
{
    if (_chatbot != NULL) {
        ChatReply reply;

        while (_chatbot->pollReply(reply)) {
            if (textMessage(reply.dest, reply.channel, reply.text) == false) {
                cerr << "chatbot textMessage failed!" << endl;
#if DEBUG_CHATBOT
                cout << "chatbot: textMessage failed dest=" << reply.dest
                     << " channel=" << (unsigned int) reply.channel
                     << " bytes=" << reply.text.size() << endl;
#endif
            } else {
                if (reply.dest == 0xffffffffU) {
                    cout << "chatbot broadcast on #"
                         << (unsigned int) reply.channel << ": "
                         << reply.text << endl;
                } else {
                    cout << "chatbot_reply to "
                         << getDisplayName(reply.from) << ": "
                         << reply.text << endl;
                }
            }
        }
    }
}

void MeshMon::join(void)
{
    MeshClient::join();

    if (_chatbot != NULL) {
        _chatbot->stop();
        _chatbot->join();
        _chatbot = NULL;
    }

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
    {
        size_t slen = sizeof(p) - ((const char *) s - (const char *) p);

        for (size_t j = 0; j < slen; j++) {
            unsigned char c = (unsigned char) s[j];

            if (s[j] == '\'') {
                break;
            }
            if (isdigit(c) || (s[j] == '.')) {
                str += s[j];
            }
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

void MeshMon::setOwnMqtt(const string &server, uint16_t port,
                         const string &user, const string &password,
                         const string &topic, bool tls)
{
    if (_myownMqtt != NULL) {
        return;
    }

    _myownMqtt = make_shared<MqttClient>(server, port, user, password,
                                         topic, tls);
    _myownMqtt->start();
}

void MeshMon::setChatBot(shared_ptr<ChatBot> bot)
{
    if (_chatbot != NULL) {
        _chatbot->stop();
        _chatbot->join();
        _chatbot = NULL;
    }

    _chatbot = bot;
    if (_chatbot != NULL) {
        _chatbot->setClient(shared_ptr<MeshClient>(
                                shared_ptr<MeshClient>(), this));
        _chatbot->start();
#if DEBUG_CHATBOT
        cout << "chatbot: started enabled="
             << (_chatbot->enabled() ? 1 : 0) << endl;
#endif
    } else {
#if DEBUG_CHATBOT
        cout << "chatbot: setChatBot null" << endl;
#endif
    }
}

void MeshMon::setCalibration(shared_ptr<Calibration> calib)
{
    _calibration = calib;
}

void MeshMon::setDb(shared_ptr<MeshMonDb> db)
{
    _db = db;
}

void MeshMon::gotModuleConfigMQTT(const meshtastic_ModuleConfig_MQTTConfig &c)
{
    MeshClient::gotModuleConfigMQTT(c);

    if (c.proxy_to_client_enabled && (_meshtasticMqtt == NULL)) {
        // Public Meshtastic MQTT, used to feed meshmap.net
        _meshtasticMqtt = make_shared<MqttClient>();
        _meshtasticMqtt->start();
    }
}

static bool decodeEnvelopePacket(const meshtastic_MqttClientProxyMessage &m,
                                 meshtastic_MeshPacket &packet)
{
    pb_istream_t stream;
    bool found = false;

    if (m.which_payload_variant != meshtastic_MqttClientProxyMessage_data_tag) {
        return false;
    }

    memset(&packet, 0, sizeof(packet));
    stream = pb_istream_from_buffer(m.payload_variant.data.bytes,
                                    m.payload_variant.data.size);

    while (stream.bytes_left > 0) {
        pb_wire_type_t wire_type;
        uint32_t tag = 0;
        bool eof = false;

        if (!pb_decode_tag(&stream, &wire_type, &tag, &eof)) {
            if (eof) {
                break;
            }
            cerr << "pb_decode ServiceEnvelope tag failed: "
                 << PB_GET_ERROR(&stream) << endl;
            return false;
        }
        if (eof) {
            break;
        }

        if ((tag == meshtastic_ServiceEnvelope_packet_tag) &&
            (wire_type == PB_WT_STRING)) {
            pb_istream_t substream;

            if (!pb_make_string_substream(&stream, &substream)) {
                cerr << "pb_decode ServiceEnvelope.packet failed: "
                     << PB_GET_ERROR(&stream) << endl;
                return false;
            }
            found = pb_decode(&substream, meshtastic_MeshPacket_fields,
                              &packet);
            if (!found) {
                cerr << "pb_decode MeshPacket failed: "
                     << PB_GET_ERROR(&substream) << endl;
            }
            if (!pb_close_string_substream(&stream, &substream) || !found) {
                return false;
            }
        } else if (!pb_skip_field(&stream, wire_type)) {
            cerr << "pb_decode ServiceEnvelope skip failed: "
                 << PB_GET_ERROR(&stream) << endl;
            return false;
        }
    }

    return found;
}

void MeshMon::gotMqttClientProxyMessage(const meshtastic_MqttClientProxyMessage &m)
{
    MeshClient::gotMqttClientProxyMessage(m);

    if (_myownMqtt != NULL) {
        _myownMqtt->publish(m);
    }

    meshtastic_MeshPacket packet;

    if (!decodeEnvelopePacket(m, packet)) {
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

void MeshMon::gotPacket(const meshtastic_MeshPacket &packet)
{
    time_t meshmonTime = time(NULL);
    if (_db != NULL) {
        _db->enqueuePacket(packet, meshmonTime);
    }

    MeshClient::gotPacket(packet);
}

void MeshMon::gotTextMessage(const meshtastic_MeshPacket &packet,
                             const string &message)
{
    if (_db != NULL) {
        _db->enqueueTextMessage(packet, message, time(NULL));
    }

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
    if (_db != NULL) {
        _db->enqueuePosition(packet, position, time(NULL));
    }

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

}

void MeshMon::gotUser(const meshtastic_MeshPacket &packet,
                      const meshtastic_User &user)
{
    if (_db != NULL) {
        _db->enqueueNodeInfo(packet.from, user.long_name, user.short_name,
                             user.hw_model, user.role, time(NULL));
    }

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
#if 0
    if (!verbose()) {
        cout << adminMessage;
        cout << "---" << endl;
        cout << packet;
    }
#endif
}



static string jsonEscape(const string &s)
{
    string o;

    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char) s[i];

        if ((c == '"') || (c == '\\')) {
            o += '\\';
            o += (char) c;
        } else if (c == '\n') {
            o += "\\n";
        } else if (c == '\r') {
            o += "\\r";
        } else if (c < 0x20) {
            char u[8];
            snprintf(u, sizeof(u), "\\u%04x", (unsigned int) c);
            o += u;
        } else {
            o += (char) c;
        }
    }

    return o;
}

static string nodeHexId(uint32_t id)
{
    char buf[9];

    snprintf(buf, sizeof(buf), "%.8x", id);
    return string(buf);
}

#define HA_ENV_TEMP  1u
#define HA_ENV_HUM   2u
#define HA_ENV_PRES  4u

#define HA_POW_CH1_VOLT  (1u << 0)
#define HA_POW_CH1_CURR  (1u << 1)
#define HA_POW_CH2_VOLT  (1u << 2)
#define HA_POW_CH2_CURR  (1u << 3)
#define HA_POW_CH3_VOLT  (1u << 4)
#define HA_POW_CH3_CURR  (1u << 5)

#define HA_DEV_BATTERY   (1u << 0)
#define HA_DEV_VOLTAGE   (1u << 1)
#define HA_DEV_CH_UTIL   (1u << 2)
#define HA_DEV_AIR_UTIL  (1u << 3)

static string haDiscoveryJson(const string &name,
                              const string &uniqueId,
                              const string &stateTopic,
                              const string &deviceClass,
                              const string &unit,
                              const string &identifier,
                              const string &deviceName)
{
    ostringstream os;

    os << "{"
       << "\"name\":\"" << jsonEscape(name) << "\","
       << "\"unique_id\":\"" << jsonEscape(uniqueId) << "\","
       << "\"state_topic\":\"" << jsonEscape(stateTopic) << "\",";
    if (!deviceClass.empty()) {
        os << "\"device_class\":\"" << jsonEscape(deviceClass) << "\",";
    }
    os << "\"unit_of_measurement\":\"" << jsonEscape(unit) << "\","
       << "\"state_class\":\"measurement\","
       << "\"device\":{"
       << "\"identifiers\":[\"" << jsonEscape(identifier) << "\"],"
       << "\"name\":\"" << jsonEscape(deviceName) << "\","
       << "\"manufacturer\":\"Meshtastic\""
       << "}"
       << "}";

    return os.str();
}

bool MeshMon::isSensorForwardAllowed(uint32_t nodeId) const
{
    if (nodeId == 0) {
        return false;
    }

    if (nodeId == whoami()) {
        return true;
    }

    if (admins().find(nodeId) != admins().end()) {
        return true;
    }

    if (mates().find(nodeId) != mates().end()) {
        return true;
    }

    return false;
}

void MeshMon::gotEnvironmentMetrics(const meshtastic_MeshPacket &packet,
                                    const meshtastic_EnvironmentMetrics &metrics)
{
    if (_db != NULL) {
        _db->enqueueEnvironmentMetrics(packet, metrics, time(NULL));
    }

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

    if (_myownMqtt == NULL) {
        return;
    }

    if (!isSensorForwardAllowed(packet.from)) {
        return;
    }

    const string id = nodeHexId(packet.from);
    string shortName = SimpleClient::lookupShortName(packet.from, true);
    string longName = SimpleClient::lookupLongName(packet.from, true);
    string identifier = shortName.empty() ? id : shortName;
    string deviceName = longName.empty() ? (string("!") + id) : longName;
    string namesKey = identifier + "\n" + deviceName;
    unsigned int present = 0;
    unsigned int already = 0;
    unsigned int discover = 0;
    map<uint32_t, string>::iterator nameIt;
    map<uint32_t, unsigned int>::iterator metIt;
    bool namesChanged;

    if (metrics.has_temperature) {
        present |= HA_ENV_TEMP;
    }
    if (metrics.has_relative_humidity) {
        present |= HA_ENV_HUM;
    }
    if (metrics.has_barometric_pressure) {
        present |= HA_ENV_PRES;
    }
    if (present == 0) {
        return;
    }

    nameIt = _haEnvNames.find(packet.from);
    metIt = _haEnvMetrics.find(packet.from);
    already = (metIt == _haEnvMetrics.end()) ? 0 : metIt->second;
    namesChanged = (nameIt == _haEnvNames.end()) ||
                   (nameIt->second != namesKey);
    discover = namesChanged ? present : (present & ~already);

    if (discover & HA_ENV_TEMP) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_temperature/config",
            haDiscoveryJson("Temperature",
                            string("meshmon_") + id + "_temperature",
                            string("meshmon/") + id + "/temperature",
                            "temperature", "\u00b0C",
                            identifier, deviceName),
            true);
    }
    if (discover & HA_ENV_HUM) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_humidity/config",
            haDiscoveryJson("Humidity",
                            string("meshmon_") + id + "_humidity",
                            string("meshmon/") + id + "/humidity",
                            "humidity", "%",
                            identifier, deviceName),
            true);
    }
    if (discover & HA_ENV_PRES) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_pressure/config",
            haDiscoveryJson("Pressure",
                            string("meshmon_") + id + "_pressure",
                            string("meshmon/") + id + "/pressure",
                            "pressure", "hPa",
                            identifier, deviceName),
            true);
    }

    float temp = metrics.temperature;
    float hum = metrics.relative_humidity;
    float press = metrics.barometric_pressure;

    if (_calibration != NULL) {
        if (metrics.has_temperature) {
            temp = _calibration->calibrateTemperature(packet.from, temp);
        }
        if (metrics.has_relative_humidity) {
            hum = _calibration->calibrateHumidity(packet.from, hum);
        }
        if (metrics.has_barometric_pressure) {
            press = _calibration->calibratePressure(packet.from, press);
        }
    }

    if (metrics.has_temperature) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", temp);
        _myownMqtt->publish(string("meshmon/") + id + "/temperature",
                            string(buf), true);
    }
    if (metrics.has_relative_humidity) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", hum);
        _myownMqtt->publish(string("meshmon/") + id + "/humidity",
                            string(buf), true);
    }
    if (metrics.has_barometric_pressure) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", press);
        _myownMqtt->publish(string("meshmon/") + id + "/pressure",
                            string(buf), true);
    }

    _haEnvNames[packet.from] = namesKey;
    _haEnvMetrics[packet.from] = already | present;
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

}

void MeshMon::gotDeviceMetrics(const meshtastic_MeshPacket &packet,
                               const meshtastic_DeviceMetrics &metrics)
{
    if (_db != NULL) {
        _db->enqueueDeviceMetrics(packet, metrics, time(NULL));
    }

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

    if (_myownMqtt == NULL) {
        return;
    }

    if (!isSensorForwardAllowed(packet.from)) {
        return;
    }

    const string id = nodeHexId(packet.from);
    string shortName = SimpleClient::lookupShortName(packet.from, true);
    string longName = SimpleClient::lookupLongName(packet.from, true);
    string identifier = shortName.empty() ? id : shortName;
    string deviceName = longName.empty() ? (string("!") + id) : longName;
    string namesKey = identifier + "\n" + deviceName;
    unsigned int present = 0;
    unsigned int already = 0;
    unsigned int discover = 0;
    map<uint32_t, string>::iterator nameIt;
    map<uint32_t, unsigned int>::iterator metIt;
    bool namesChanged;

    if (metrics.has_battery_level && (metrics.battery_level > 0)) {
        present |= HA_DEV_BATTERY;
    }
    if (metrics.has_voltage && (metrics.voltage > 0.0f)) {
        present |= HA_DEV_VOLTAGE;
    }
    if (metrics.has_channel_utilization) {
        present |= HA_DEV_CH_UTIL;
    }
    if (metrics.has_air_util_tx) {
        present |= HA_DEV_AIR_UTIL;
    }
    if (present == 0) {
        return;
    }

    nameIt = _haDeviceNames.find(packet.from);
    metIt = _haDeviceMetrics.find(packet.from);
    already = (metIt == _haDeviceMetrics.end()) ? 0 : metIt->second;
    namesChanged = (nameIt == _haDeviceNames.end()) ||
                   (nameIt->second != namesKey);
    discover = namesChanged ? present : (present & ~already);

    if (discover & HA_DEV_BATTERY) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_battery/config",
            haDiscoveryJson("Battery",
                            string("meshmon_") + id + "_battery",
                            string("meshmon/") + id + "/battery",
                            "battery", "%",
                            identifier, deviceName),
            true);
    }
    if (discover & HA_DEV_VOLTAGE) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_voltage/config",
            haDiscoveryJson("Voltage",
                            string("meshmon_") + id + "_voltage",
                            string("meshmon/") + id + "/voltage",
                            "voltage", "V",
                            identifier, deviceName),
            true);
    }
    if (discover & HA_DEV_CH_UTIL) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_channel_utilization/config",
            haDiscoveryJson("Channel Utilization",
                            string("meshmon_") + id + "_channel_utilization",
                            string("meshmon/") + id + "/channel_utilization",
                            "", "%",
                            identifier, deviceName),
            true);
    }
    if (discover & HA_DEV_AIR_UTIL) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_air_util_tx/config",
            haDiscoveryJson("Air Util (Tx)",
                            string("meshmon_") + id + "_air_util_tx",
                            string("meshmon/") + id + "/air_util_tx",
                            "", "%",
                            identifier, deviceName),
            true);
    }

    if (metrics.has_battery_level && (metrics.battery_level > 0)) {
        char buf[32];
        uint32_t bat = metrics.battery_level > 100 ? 100 : metrics.battery_level;
        snprintf(buf, sizeof(buf), "%u", (unsigned int) bat);
        _myownMqtt->publish(string("meshmon/") + id + "/battery",
                            string(buf), true);
    }
    if (metrics.has_voltage && (metrics.voltage > 0.0f)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", metrics.voltage);
        _myownMqtt->publish(string("meshmon/") + id + "/voltage",
                            string(buf), true);
    }
    if (metrics.has_channel_utilization) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", metrics.channel_utilization);
        _myownMqtt->publish(string("meshmon/") + id + "/channel_utilization",
                            string(buf), true);
    }
    if (metrics.has_air_util_tx) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", metrics.air_util_tx);
        _myownMqtt->publish(string("meshmon/") + id + "/air_util_tx",
                            string(buf), true);
    }

    _haDeviceNames[packet.from] = namesKey;
    _haDeviceMetrics[packet.from] = already | present;
}

void MeshMon::gotPowerMetrics(const meshtastic_MeshPacket &packet,
                              const meshtastic_PowerMetrics &metrics)
{
    if (_db != NULL) {
        _db->enqueuePowerMetrics(packet, metrics, time(NULL));
    }

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

    if (_myownMqtt == NULL) {
        return;
    }

    if (!isSensorForwardAllowed(packet.from)) {
        return;
    }

    const string id = nodeHexId(packet.from);
    string shortName = SimpleClient::lookupShortName(packet.from, true);
    string longName = SimpleClient::lookupLongName(packet.from, true);
    string identifier = shortName.empty() ? id : shortName;
    string deviceName = longName.empty() ? (string("!") + id) : longName;
    string namesKey = identifier + "\n" + deviceName;
    unsigned int present = 0;
    unsigned int already = 0;
    unsigned int discover = 0;
    map<uint32_t, string>::iterator nameIt;
    map<uint32_t, unsigned int>::iterator metIt;
    bool namesChanged;

    if (metrics.has_ch1_voltage) {
        present |= HA_POW_CH1_VOLT;
    }
    if (metrics.has_ch1_current) {
        present |= HA_POW_CH1_CURR;
    }
    if (metrics.has_ch2_voltage) {
        present |= HA_POW_CH2_VOLT;
    }
    if (metrics.has_ch2_current) {
        present |= HA_POW_CH2_CURR;
    }
    if (metrics.has_ch3_voltage) {
        present |= HA_POW_CH3_VOLT;
    }
    if (metrics.has_ch3_current) {
        present |= HA_POW_CH3_CURR;
    }
    if (present == 0) {
        return;
    }

    nameIt = _haPowerNames.find(packet.from);
    metIt = _haPowerMetrics.find(packet.from);
    already = (metIt == _haPowerMetrics.end()) ? 0 : metIt->second;
    namesChanged = (nameIt == _haPowerNames.end()) ||
                   (nameIt->second != namesKey);
    discover = namesChanged ? present : (present & ~already);

    if (discover & HA_POW_CH1_VOLT) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_ch1_voltage/config",
            haDiscoveryJson("Channel 1 Voltage",
                            string("meshmon_") + id + "_ch1_voltage",
                            string("meshmon/") + id + "/ch1_voltage",
                            "voltage", "V",
                            identifier, deviceName),
            true);
    }
    if (discover & HA_POW_CH1_CURR) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_ch1_current/config",
            haDiscoveryJson("Channel 1 Current",
                            string("meshmon_") + id + "_ch1_current",
                            string("meshmon/") + id + "/ch1_current",
                            "current", "mA",
                            identifier, deviceName),
            true);
    }
    if (discover & HA_POW_CH2_VOLT) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_ch2_voltage/config",
            haDiscoveryJson("Channel 2 Voltage",
                            string("meshmon_") + id + "_ch2_voltage",
                            string("meshmon/") + id + "/ch2_voltage",
                            "voltage", "V",
                            identifier, deviceName),
            true);
    }
    if (discover & HA_POW_CH2_CURR) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_ch2_current/config",
            haDiscoveryJson("Channel 2 Current",
                            string("meshmon_") + id + "_ch2_current",
                            string("meshmon/") + id + "/ch2_current",
                            "current", "mA",
                            identifier, deviceName),
            true);
    }
    if (discover & HA_POW_CH3_VOLT) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_ch3_voltage/config",
            haDiscoveryJson("Channel 3 Voltage",
                            string("meshmon_") + id + "_ch3_voltage",
                            string("meshmon/") + id + "/ch3_voltage",
                            "voltage", "V",
                            identifier, deviceName),
            true);
    }
    if (discover & HA_POW_CH3_CURR) {
        _myownMqtt->publish(
            string("homeassistant/sensor/meshmon_") + id +
            "_ch3_current/config",
            haDiscoveryJson("Channel 3 Current",
                            string("meshmon_") + id + "_ch3_current",
                            string("meshmon/") + id + "/ch3_current",
                            "current", "mA",
                            identifier, deviceName),
            true);
    }

    if (metrics.has_ch1_voltage) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", metrics.ch1_voltage);
        _myownMqtt->publish(string("meshmon/") + id + "/ch1_voltage",
                            string(buf), true);
    }
    if (metrics.has_ch1_current) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", metrics.ch1_current);
        _myownMqtt->publish(string("meshmon/") + id + "/ch1_current",
                            string(buf), true);
    }
    if (metrics.has_ch2_voltage) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", metrics.ch2_voltage);
        _myownMqtt->publish(string("meshmon/") + id + "/ch2_voltage",
                            string(buf), true);
    }
    if (metrics.has_ch2_current) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", metrics.ch2_current);
        _myownMqtt->publish(string("meshmon/") + id + "/ch2_current",
                            string(buf), true);
    }
    if (metrics.has_ch3_voltage) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", metrics.ch3_voltage);
        _myownMqtt->publish(string("meshmon/") + id + "/ch3_voltage",
                            string(buf), true);
    }
    if (metrics.has_ch3_current) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", metrics.ch3_current);
        _myownMqtt->publish(string("meshmon/") + id + "/ch3_current",
                            string(buf), true);
    }

    _haPowerNames[packet.from] = namesKey;
    _haPowerMetrics[packet.from] = already | present;
}

void MeshMon::gotLocalStats(const meshtastic_MeshPacket &packet,
                            const meshtastic_LocalStats &stats)
{
    if (_db != NULL) {
        _db->enqueueLocalStats(packet, stats, time(NULL));
    }

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

}

void MeshMon::gotTraceRoute(const meshtastic_MeshPacket &packet,
                            const meshtastic_RouteDiscovery &routeDiscovery)
{
    if (_db != NULL) {
        _db->enqueueTraceRoute(packet, routeDiscovery, time(NULL));
    }

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

void MeshMon::crontab(const struct tm *now)
{
    MeshClient::crontab(now);

    publishGatewayStatsToMqtt();

    if (now != NULL && now->tm_min == 0) {
        syncRadioClock();
        topOfHourTask(now);
    }
}

void MeshMon::topOfHourTask(const struct tm *now)
{
    if (!isConnected()) {
        return;
    }

    int robotChan = getRobotChannel();
    if (robotChan < 0) {
        return;
    }

    struct tm tm_buf;
    if (now == NULL) {
        time_t t = ::time(NULL);
        localtime_r(&t, &tm_buf);
        now = &tm_buf;
    }

    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S %Z", now);
    string msg = "local-time: " + string(timeBuf);

    bool result = textMessage(0xffffffffU, (uint8_t) robotChan, msg);
    if (result == false) {
        this->printf("textMessage '%s' failed!\n", msg.c_str());
    }
}

bool MeshMon::matchBotAddressing(const string &rawMessage, bool directMessage, string &cleanQuery) const
{
    string msg = rawMessage;
    trimWhitespace(msg);
    if (msg.empty()) {
        cleanQuery.clear();
        return directMessage;
    }

    if (directMessage) {
        cleanQuery = msg;
    }

    size_t startIdx = 0;
    if (msg[0] == '@') {
        startIdx = 1;
        while (startIdx < msg.size() && isspace(static_cast<unsigned char>(msg[startIdx]))) {
            startIdx++;
        }
    }

    uint32_t myId = whoami();
    string shortName = lookupShortName(myId);
    string longName = lookupLongName(myId);
    char hexBuf[16];
    snprintf(hexBuf, sizeof(hexBuf), "%08x", myId);
    string hexId = hexBuf;
    string hexIdBang = "!" + hexId;

    vector<string> candidates;
    if (!longName.empty()) {
        candidates.push_back(longName);
    }
    candidates.push_back(hexIdBang);
    candidates.push_back(hexId);
    if (!shortName.empty()) {
        candidates.push_back(shortName);
    }
    if (!directMessage) {
        candidates.push_back("all");
    }

    sort(candidates.begin(), candidates.end(), [](const string &a, const string &b) {
        return a.size() > b.size();
    });

    for (size_t i = 0; i < candidates.size(); i++) {
        const string &cand = candidates[i];
        if (cand.empty()) {
            continue;
        }

        if (msg.size() < startIdx + cand.size()) {
            continue;
        }

        bool match = true;
        for (size_t c = 0; c < cand.size(); c++) {
            if (tolower(static_cast<unsigned char>(msg[startIdx + c])) !=
                tolower(static_cast<unsigned char>(cand[c]))) {
                match = false;
                break;
            }
        }

        if (match) {
            size_t endPos = startIdx + cand.size();
            if (endPos == msg.size()) {
                cleanQuery.clear();
                return true;
            }

            char delimiter = msg[endPos];
            if (!isalnum(static_cast<unsigned char>(delimiter))) {
                while (endPos < msg.size()) {
                    char d = msg[endPos];
                    if (isspace(static_cast<unsigned char>(d)) ||
                        d == ',' || d == ':' || d == '!' || d == '?' ||
                        d == ';' || d == '-' || d == '.' || d == '"' || d == '\'') {
                        endPos++;
                    } else {
                        break;
                    }
                }
                cleanQuery = msg.substr(endPos);
                trimWhitespace(cleanQuery);
                return true;
            }
        }
    }

    if (directMessage) {
        cleanQuery = msg;
        return true;
    }

    return false;
}

bool MeshMon::handleTextMessage(const meshtastic_MeshPacket &packet,
                                const string &_message)
{
    if (packet.from == whoami() || packet.from == 0) {
        return HomeChat::handleTextMessage(packet, _message);
    }

    bool directMessage = false;
    bool channelMessage = false;
    bool addressed2Me = false;
    string message = _message;
    string first_word;
    uint32_t dest = 0xffffffffU;
    uint8_t channel = 0xffU;

    if (packet.to == whoami()) {
        directMessage = true;
        dest = packet.from;
        channel = packet.channel;
    } else {
        channelMessage = true;
        dest = 0xffffffffU;
        channel = packet.channel;
    }

    // get first word
    trimWhitespace(message);
    first_word = message.substr(0, message.find(' '));
    toLowercase(first_word);

    if (channelMessage &&
        ((first_word == lookupShortName(whoami())) ||
         (first_word == lookupLongName(whoami())) ||
         (first_word.find(whoamiString()) != string::npos) ||
         (first_word == "all"))) {
        addressed2Me = true;
        message = message.substr(first_word.size());
        trimWhitespace(message);
    }

    if (directMessage || addressed2Me) {
        string query = message;
        trimWhitespace(query);
        toLowercase(query);
        while (!query.empty() &&
               (query.back() == '?' || query.back() == '!' ||
                query.back() == '.' || query.back() == ',')) {
            query.pop_back();
            trimWhitespace(query);
        }

        if (query == "time") {
            if (directMessage) {
                this->printf("%s:%c%s\n",
                             getDisplayName(packet.from).c_str(),
                             _message.find('\n') == string::npos ? ' ' : '\n',
                             _message.c_str());
            } else {
                this->printf("%s on #%s:%c%s\n",
                             getDisplayName(packet.from).c_str(),
                             getChannelName(packet.channel).c_str(),
                             _message.find('\n') == string::npos ? ' ' : '\n',
                             _message.c_str());
            }

            time_t now = ::time(NULL);
            struct tm tm;
            char timeBuf[64];
            localtime_r(&now, &tm);
            strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S %Z", &tm);
            string reply = "The local time is " + string(timeBuf);

            bool result = textMessage(dest, channel, reply);
            if (result == false) {
                this->printf("textMessage '%s' failed!\n",
                             reply.c_str());
            } else {
                this->printf("my_reply to %s: %s\n",
                             getDisplayName(packet.from).c_str(),
                             reply.c_str());
            }

            setLastMessageFrom(packet.from, _message);
            return true;
        }

        if (_db != NULL) {
            string reply;
            time_t dayAgo = time(NULL) - 86400;

            if (query == "traffic" || query == "stats" || query == "db stats") {
                TrafficSummary sum;
                if (_db->getTrafficSummary(dayAgo, sum)) {
                    float bcastPct = sum.totalPackets > 0 ?
                        (sum.broadcastPackets * 100.0f / sum.totalPackets) : 0.0f;
                    float directPct = sum.totalPackets > 0 ?
                        (sum.directPackets * 100.0f / sum.totalPackets) : 0.0f;
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                             "traffic(24h): %u pkts, %llu bytes, bcast=%.0f%%, direct=%.0f%%",
                             sum.totalPackets, (unsigned long long) sum.totalBytes,
                             bcastPct, directPct);
                    reply = buf;
                }
            } else if (query == "toptalkers" || query == "top" || query == "top talkers") {
                vector<NodeTrafficStat> stats;
                if (_db->getTopTalkers(dayAgo, 3, stats)) {
                    ostringstream os;
                    os << "toptalkers(24h): ";
                    for (size_t i = 0; i < stats.size(); i++) {
                        if (i > 0) os << ", ";
                        os << (stats[i].shortName.empty() ? stats[i].nodeHex : stats[i].shortName)
                           << ":" << stats[i].packetCount << "p";
                    }
                    reply = os.str();
                }
            } else if (query == "neighbors" || query == "direct" || query == "direct neighbors") {
                vector<NeighborStat> stats;
                if (_db->getNeighborStats(dayAgo, stats)) {
                    ostringstream os;
                    os << "neighbors(24h): " << stats.size() << " nodes.";
                    if (!stats.empty()) {
                        os << " best: " << (stats[0].shortName.empty() ? stats[0].nodeHex : stats[0].shortName)
                           << " (" << fixed << setprecision(1) << stats[0].avgSnr << "dB)";
                    }
                    reply = os.str();
                }
            } else if (query == "storm" || query == "storms" || query == "echo") {
                vector<EchoStormStat> stats;
                if (_db->getEchoStorms(dayAgo, 1, stats)) {
                    if (!stats.empty()) {
                        char buf[128];
                        snprintf(buf, sizeof(buf),
                                 "storm(24h): max echo=%ux pkts on pkt !%08x from %s (%us duration)",
                                 stats[0].echoCount, stats[0].packetId, stats[0].fromHex.c_str(),
                                 stats[0].durationSec);
                        reply = buf;
                    } else {
                        reply = "storm(24h): no duplicate packet floods detected.";
                    }
                }
            } else if (query == "asymmetry") {
                vector<LinkAsymmetryStat> stats;
                if (_db->getLinkAsymmetry(dayAgo, stats)) {
                    ostringstream os;
                    os << "asymmetry: " << stats.size() << " links. ";
                    if (!stats.empty()) {
                        os << "weakest: " << (stats[0].shortName.empty() ? stats[0].nodeHex : stats[0].shortName)
                           << " (" << fixed << setprecision(1) << stats[0].rxSnr << "dB)";
                    }
                    reply = os.str();
                }
            } else if (query == "spof" || query == "relays") {
                vector<CriticalRepeaterStat> stats;
                if (_db->getCriticalRepeaters(dayAgo, 2, stats)) {
                    ostringstream os;
                    os << "critical relays: ";
                    if (stats.empty()) {
                        os << "none detected.";
                    } else {
                        for (size_t i = 0; i < stats.size(); i++) {
                            if (i > 0) os << ", ";
                            os << (stats[i].shortName.empty() ? stats[i].repeaterHex : stats[i].shortName)
                               << " (" << stats[i].relayCount << " relays)";
                        }
                    }
                    reply = os.str();
                }
            } else if (query == "drift") {
                vector<ClockDriftStat> stats;
                if (_db->getClockDrift(dayAgo, stats)) {
                    if (stats.empty()) {
                        reply = "clock drift: no remote time skew detected.";
                    } else {
                        char buf[128];
                        snprintf(buf, sizeof(buf),
                                 "clock drift: max skew %s %ds (samples=%u)",
                                 (stats[0].shortName.empty() ? stats[0].nodeHex.c_str() : stats[0].shortName.c_str()),
                                 stats[0].avgSkewSec, stats[0].sampleCount);
                        reply = buf;
                    }
                }
            } else if (query == "hops") {
                vector<HopStat> stats;
                if (_db->getHopDistribution(dayAgo, stats)) {
                    ostringstream os;
                    os << "hops(24h): ";
                    for (size_t i = 0; i < stats.size(); i++) {
                        if (i > 0) os << ", ";
                        os << stats[i].hops << "h:" << fixed << setprecision(0) << stats[i].pctShare << "%";
                    }
                    reply = os.str();
                }
            } else if (query == "apps") {
                vector<AppStat> stats;
                if (_db->getPortnumDistribution(dayAgo, stats)) {
                    ostringstream os;
                    os << "apps(24h): ";
                    for (size_t i = 0; i < min((size_t) 3, stats.size()); i++) {
                        if (i > 0) os << ", ";
                        os << stats[i].appName << ":" << fixed << setprecision(0) << stats[i].pctShare << "%";
                    }
                    reply = os.str();
                }
            } else if (query == "health" || query == "channel") {
                ChannelHealthStat h;
                if (_db->getChannelHealth(dayAgo, h)) {
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                             "health(24h): ch_util=%.1f%% air_tx=%.1f%% pkts=%u dupes=%u",
                             h.avgChannelUtil, h.avgAirUtilTx, h.totalPackets, h.duplicatePackets);
                    reply = buf;
                }
            }

            if (!reply.empty()) {
                if (directMessage) {
                    this->printf("%s:%c%s\n",
                                 getDisplayName(packet.from).c_str(),
                                 _message.find('\n') == string::npos ? ' ' : '\n',
                                 _message.c_str());
                } else {
                    this->printf("%s on #%s:%c%s\n",
                                 getDisplayName(packet.from).c_str(),
                                 getChannelName(packet.channel).c_str(),
                                 _message.find('\n') == string::npos ? ' ' : '\n',
                                 _message.c_str());
                }

                bool result = textMessage(dest, channel, reply);
                if (result == false) {
                    this->printf("textMessage '%s' failed!\n", reply.c_str());
                } else {
                    this->printf("my_reply to %s: %s\n",
                                 getDisplayName(packet.from).c_str(),
                                 reply.c_str());
                }

                setLastMessageFrom(packet.from, _message);
                return true;
            }
        }
    }

    bool handled = HomeChat::handleTextMessage(packet, _message);
    if (handled) {
        return true;
    }

    // Chatbot-only relaxed addressing handoff
    if ((_chatbot != NULL) && _chatbot->enabled()) {
        string cleanQuery;
        if (matchBotAddressing(_message, directMessage, cleanQuery)) {
            if (!cleanQuery.empty()) {
                bool isAdmin = false, isMate = false;
                getAuthority(packet.from, isAdmin, isMate);
                bool fromAuthChan = isAuthChannel(getChannelName(packet.channel));

                if (isAdmin || isMate || fromAuthChan) {
#if DEBUG_CHATBOT
                    cout << "chatbot: ask (relaxed) from=" << packet.from
                         << " dest=" << dest
                         << " channel=" << (unsigned int) channel
                         << " query='" << cleanQuery << "'" << endl;
#endif
                    _chatbot->ask(packet.from, dest, channel, cleanQuery);
                    setLastMessageFrom(packet.from, _message);
                    return true;
                } else {
                    string firstWordLower = _message.substr(0, _message.find(' '));
                    toLowercase(firstWordLower);
                    if (firstWordLower != "all") {
                        if (_message != getLastMessageFrom(packet.from)) {
                            string unauthReply = lookupShortName(packet.from) +
                                ", you are not authorized to speak to me!";
                            textMessage(dest, channel, unauthReply);
                        }
                    }
                    setLastMessageFrom(packet.from, _message);
                    return true;
                }
            }
        }
    }

    return false;
}

void MeshMon::handleTimeBroadcast(const meshtastic_MeshPacket &packet,
                                  time_t epoch, const string &tz)
{
    (void)(packet);
    (void)(epoch);
    (void)(tz);
}

string MeshMon::handleEnv(uint32_t node_num, string &message)
{
    stringstream ss;

    ss << HomeChat::handleEnv(node_num, message);
    if (!ss.str().empty()) {
        ss << " ";
    }

    ss << "temp_cpu=";
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

void MeshMon::publishGatewayStatsToMqtt(void)
{
    if (_myownMqtt == NULL || _db == NULL) {
        return;
    }

    if (!_haGatewayDiscovered) {
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_packets/config",
            haDiscoveryJson("Gateway Total Packets", "meshmon_gateway_packets",
                            "meshmon/gateway/total_packets", "", "pkts",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_total_nodes/config",
            haDiscoveryJson("Gateway Total Nodes", "meshmon_gateway_total_nodes",
                            "meshmon/gateway/total_nodes", "", "nodes",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_total_bytes/config",
            haDiscoveryJson("Gateway Total Airtime Data", "meshmon_gateway_total_bytes",
                            "meshmon/gateway/total_bytes_mb", "data_size", "MB",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_direct_ratio/config",
            haDiscoveryJson("Gateway Direct Ratio", "meshmon_gateway_direct_ratio",
                            "meshmon/gateway/direct_ratio_pct", "", "%",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_broadcast_ratio/config",
            haDiscoveryJson("Gateway Broadcast Ratio", "meshmon_gateway_broadcast_ratio",
                            "meshmon/gateway/broadcast_ratio_pct", "", "%",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_avg_hops/config",
            haDiscoveryJson("Gateway Average Hops", "meshmon_gateway_avg_hops",
                            "meshmon/gateway/avg_hops", "", "hops",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_total_messages/config",
            haDiscoveryJson("Gateway Total Messages", "meshmon_gateway_total_messages",
                            "meshmon/gateway/total_messages", "", "msgs",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_active_nodes/config",
            haDiscoveryJson("Gateway Active Nodes (24h)", "meshmon_gateway_active_nodes",
                            "meshmon/gateway/active_nodes_24h", "", "nodes",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_avg_snr/config",
            haDiscoveryJson("Gateway Average SNR (1h)", "meshmon_gateway_avg_snr",
                            "meshmon/gateway/avg_snr_1h", "signal_strength", "dB",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_direct_neighbors/config",
            haDiscoveryJson("Gateway Direct Neighbors", "meshmon_gateway_direct_neighbors",
                            "meshmon/gateway/direct_neighbors", "", "nodes",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_top_talker/config",
            haDiscoveryJson("Gateway Top Talker", "meshmon_gateway_top_talker",
                            "meshmon/gateway/top_talker", "", "",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_top_talker_packets/config",
            haDiscoveryJson("Gateway Top Talker Packets", "meshmon_gateway_top_talker_packets",
                            "meshmon/gateway/top_talker_packets", "", "pkts",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_best_neighbor_snr/config",
            haDiscoveryJson("Gateway Best Neighbor SNR", "meshmon_gateway_best_neighbor_snr",
                            "meshmon/gateway/best_neighbor_snr", "signal_strength", "dB",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_duplicate_packets/config",
            haDiscoveryJson("Gateway Duplicate Packets", "meshmon_gateway_duplicate_packets",
                            "meshmon/gateway/duplicate_packets", "", "pkts",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_max_echo_mult/config",
            haDiscoveryJson("Gateway Max Echo Multiplier", "meshmon_gateway_max_echo_mult",
                            "meshmon/gateway/max_echo_mult", "", "x",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_critical_relay/config",
            haDiscoveryJson("Gateway Critical Relay", "meshmon_gateway_critical_relay",
                            "meshmon/gateway/critical_relay", "", "",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_max_clock_drift/config",
            haDiscoveryJson("Gateway Max Clock Drift", "meshmon_gateway_max_clock_drift",
                            "meshmon/gateway/max_clock_drift_sec", "duration", "s",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_avg_channel_util/config",
            haDiscoveryJson("Gateway Avg Channel Util", "meshmon_gateway_avg_channel_util",
                            "meshmon/gateway/avg_channel_util", "", "%",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_duplicate_ratio/config",
            haDiscoveryJson("Gateway Duplicate Ratio", "meshmon_gateway_duplicate_ratio",
                            "meshmon/gateway/duplicate_ratio_pct", "", "%",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _myownMqtt->publish(
            "homeassistant/sensor/meshmon_gateway_db_size/config",
            haDiscoveryJson("Gateway DB Size", "meshmon_gateway_db_size",
                            "meshmon/gateway/db_size_mb", "data_size", "MB",
                            "meshmon_gateway", "MeshMon Gateway"),
            true);
        _haGatewayDiscovered = true;
    }

    time_t now = time(NULL);
    time_t hourAgo = now - 3600;
    time_t dayAgo = now - 86400;
    char buf[64];

    uint64_t totalPkts = _db->getTotalPacketCount();
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long) totalPkts);
    _myownMqtt->publish("meshmon/gateway/total_packets", string(buf), true);

    uint32_t totalNodes = _db->getTotalNodeCount();
    snprintf(buf, sizeof(buf), "%u", totalNodes);
    _myownMqtt->publish("meshmon/gateway/total_nodes", string(buf), true);

    uint64_t totalBytes = _db->getTotalPayloadBytes();
    float totalMb = ((float) totalBytes) / (1024.0f * 1024.0f);
    snprintf(buf, sizeof(buf), "%.2f", totalMb);
    _myownMqtt->publish("meshmon/gateway/total_bytes_mb", string(buf), true);

    uint64_t totalMessages = _db->getTotalTextMessageCount();
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long) totalMessages);
    _myownMqtt->publish("meshmon/gateway/total_messages", string(buf), true);

    float directPct = 0.0f, bcastPct = 0.0f, avgHops = 0.0f;
    if (_db->getTrafficRatios(dayAgo, directPct, bcastPct, avgHops)) {
        snprintf(buf, sizeof(buf), "%.1f", directPct);
        _myownMqtt->publish("meshmon/gateway/direct_ratio_pct", string(buf), true);
        snprintf(buf, sizeof(buf), "%.1f", bcastPct);
        _myownMqtt->publish("meshmon/gateway/broadcast_ratio_pct", string(buf), true);
        snprintf(buf, sizeof(buf), "%.2f", avgHops);
        _myownMqtt->publish("meshmon/gateway/avg_hops", string(buf), true);
    }

    TrafficSummary hourSum;
    if (_db->getTrafficSummary(hourAgo, hourSum)) {
        vector<NeighborStat> hourNeighbors;
        if (_db->getNeighborStats(hourAgo, hourNeighbors) && !hourNeighbors.empty()) {
            float sumSnr = 0.0f;
            for (size_t i = 0; i < hourNeighbors.size(); i++) {
                sumSnr += hourNeighbors[i].avgSnr;
            }
            snprintf(buf, sizeof(buf), "%.1f", sumSnr / (float) hourNeighbors.size());
            _myownMqtt->publish("meshmon/gateway/avg_snr_1h", string(buf), true);
        }
    }

    vector<NodeTrafficStat> dayTalkers;
    if (_db->getTopTalkers(dayAgo, 1000, dayTalkers)) {
        snprintf(buf, sizeof(buf), "%u", (unsigned int) dayTalkers.size());
        _myownMqtt->publish("meshmon/gateway/active_nodes_24h", string(buf), true);
    }

    string topTalkerNode;
    uint32_t topTalkerPkts = 0;
    if (_db->getTopTalkerSummary(dayAgo, topTalkerNode, topTalkerPkts) && !topTalkerNode.empty()) {
        _myownMqtt->publish("meshmon/gateway/top_talker", topTalkerNode, true);
        snprintf(buf, sizeof(buf), "%u", topTalkerPkts);
        _myownMqtt->publish("meshmon/gateway/top_talker_packets", string(buf), true);
    }

    vector<NeighborStat> neighbors;
    if (_db->getNeighborStats(dayAgo, neighbors)) {
        snprintf(buf, sizeof(buf), "%u", (unsigned int) neighbors.size());
        _myownMqtt->publish("meshmon/gateway/direct_neighbors", string(buf), true);
    }

    string bestNeighborNode;
    float bestNeighborSnr = -999.0f;
    if (_db->getBestNeighborSummary(dayAgo, bestNeighborNode, bestNeighborSnr) && (bestNeighborSnr > -900.0f)) {
        snprintf(buf, sizeof(buf), "%+.1f", bestNeighborSnr);
        _myownMqtt->publish("meshmon/gateway/best_neighbor_snr", string(buf), true);
    }

    uint32_t maxEchoMult = 1;
    if (_db->getMaxEchoMultiplier(dayAgo, maxEchoMult)) {
        snprintf(buf, sizeof(buf), "%u", maxEchoMult);
        _myownMqtt->publish("meshmon/gateway/max_echo_mult", string(buf), true);
    }

    string topRelayNode;
    uint32_t topRelayPkts = 0;
    if (_db->getCriticalRelaySummary(dayAgo, topRelayNode, topRelayPkts) && !topRelayNode.empty()) {
        _myownMqtt->publish("meshmon/gateway/critical_relay", topRelayNode, true);
    }

    float maxClockDriftSec = 0.0f;
    if (_db->getMaxClockDrift(dayAgo, maxClockDriftSec)) {
        snprintf(buf, sizeof(buf), "%.0f", maxClockDriftSec);
        _myownMqtt->publish("meshmon/gateway/max_clock_drift_sec", string(buf), true);
    }

    ChannelHealthStat health;
    if (_db->getChannelHealth(dayAgo, health)) {
        snprintf(buf, sizeof(buf), "%u", (unsigned int) health.duplicatePackets);
        _myownMqtt->publish("meshmon/gateway/duplicate_packets", string(buf), true);
        snprintf(buf, sizeof(buf), "%.1f", health.avgChannelUtil);
        _myownMqtt->publish("meshmon/gateway/avg_channel_util", string(buf), true);
        float dupeRatio = (health.totalPackets > 0) ? (health.duplicatePackets * 100.0f / health.totalPackets) : 0.0f;
        snprintf(buf, sizeof(buf), "%.1f", dupeRatio);
        _myownMqtt->publish("meshmon/gateway/duplicate_ratio_pct", string(buf), true);
    }

    size_t dbBytes = _db->getDbFileSize();
    float dbMb = ((float) dbBytes) / (1024.0f * 1024.0f);
    snprintf(buf, sizeof(buf), "%.2f", dbMb);
    _myownMqtt->publish("meshmon/gateway/db_size_mb", string(buf), true);
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
