/*
 * MeshMonShell.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <MeshMon.hxx>
#include <MqttClient.hxx>
#include <MeshMonShell.hxx>
#include <Calibration.hxx>
#include <GeminiChat.hxx>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cerrno>

#include <ctime>
#include <string>

MeshMonShell::MeshMonShell(shared_ptr<MeshClient> client)
    : MeshShell(client)
{
    _help_list.push_back("calib");
    _help_list.push_back("chatbot");
}

MeshMonShell::~MeshMonShell()
{

}

shared_ptr<MeshShell> MeshMonShell::newInstance(void)
{
    return make_shared<MeshMonShell>();
}

void MeshMonShell::printStatusHelp(void)
{
    this->printf("Usage:\n");
    this->printf("  status                   - Show general system and mesh status\n");
    this->printf("  status <node>            - Show details for a specific node\n");
    this->printf("  status help              - Show this help message\n");
    this->printf("Nodes: node shortname, longname, hex ID (e.g. !2bf941d4, 2bf941d4) or 'me'\n");
}

int MeshMonShell::help(int argc, char **argv)
{
    return MeshShell::help(argc, argv);
}

int MeshMonShell::system(int argc, char **argv)
{
    if ((argc >= 2) &&
        ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))) {
        return MeshShell::system(argc, argv);
    }

    shared_ptr<MeshMon> meshmon = dynamic_pointer_cast<MeshMon>(_client);
    const shared_ptr<MqttClient> meshtasticMqtt =
        meshmon ? meshmon->meshtasticMqtt() : NULL;
    const shared_ptr<MqttClient> myownMqtt =
        meshmon ? meshmon->myownMqtt() : NULL;

    MeshShell::system(argc, argv);
    if (meshmon == NULL) {
        return 0;
    }

    this->printf("CPU temp: %.1fC\n", meshmon->getCpuTempC());
    if (meshtasticMqtt) {
        this->printf("MQTT public published: %u/%u\n",
                     meshtasticMqtt->publishConfirmed(),
                     meshtasticMqtt->published());
    }
    if (myownMqtt) {
        this->printf("MQTT own published: %u/%u\n",
                     myownMqtt->publishConfirmed(),
                     myownMqtt->published());
    }

    return 0;
}

int MeshMonShell::status(int argc, char **argv)
{
    if (argc <= 1) {
        return MeshShell::status(argc, argv);
    }

    if ((argc == 2) &&
        ((strcmp(argv[1], "help") == 0) ||
         (strcmp(argv[1], "-h") == 0) ||
         (strcmp(argv[1], "--help") == 0) ||
         (strcmp(argv[1], "-?") == 0) ||
         (strcmp(argv[1], "?") == 0))) {
        printStatusHelp();
        return 0;
    }

    string nodeArg;
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            nodeArg += " ";
        }
        nodeArg += argv[i];
    }

    uint32_t nodeId = resolveNode(nodeArg);
    if (nodeId == 0xffffffffU) {
        this->printf("Node '%s' not found (no matching short name, long name, or node ID).\n",
                     nodeArg.c_str());
        return -1;
    }

    printNodeStatus(nodeId);
    return 0;
}

int MeshMonShell::unknown_command(int argc, char **argv)
{
    if ((argc > 0) && (strcmp(argv[0], "calib") == 0)) {
        return calib(argc, argv);
    }
    if ((argc > 0) && (strcmp(argv[0], "chatbot") == 0)) {
        return chatbot(argc, argv);
    }

    return MeshShell::unknown_command(argc, argv);
}

string MeshMonShell::formatRelativeTime(time_t timestamp)
{
    if (timestamp == 0) {
        return "never";
    }

    time_t now = time(NULL);
    long diff = (long)(now - timestamp);
    if (diff < 0) {
        diff = 0;
    }

    if (diff < 60) {
        return to_string(diff) + "s ago";
    } else if (diff < 3600) {
        long min = diff / 60;
        long sec = diff % 60;
        if (sec > 0) {
            return to_string(min) + "m " + to_string(sec) + "s ago";
        }
        return to_string(min) + "m ago";
    } else if (diff < 86400) {
        long hour = diff / 3600;
        long min = (diff % 3600) / 60;
        if (min > 0) {
            return to_string(hour) + "h " + to_string(min) + "m ago";
        }
        return to_string(hour) + "h ago";
    } else {
        long day = diff / 86400;
        long hour = (diff % 86400) / 3600;
        if (hour > 0) {
            return to_string(day) + "d " + to_string(hour) + "h ago";
        }
        return to_string(day) + "d ago";
    }
}

const char *MeshMonShell::hardwareModelString(meshtastic_HardwareModel model)
{
    switch (model) {
    case meshtastic_HardwareModel_UNSET:
        return "UNSET";
    case meshtastic_HardwareModel_TLORA_V2:
        return "TLORA_V2";
    case meshtastic_HardwareModel_TLORA_V1:
        return "TLORA_V1";
    case meshtastic_HardwareModel_TLORA_V2_1_1P6:
        return "TLORA_V2_1_1P6";
    case meshtastic_HardwareModel_TBEAM:
        return "TBEAM";
    case meshtastic_HardwareModel_HELTEC_V2_0:
        return "HELTEC_V2_0";
    case meshtastic_HardwareModel_TBEAM_V0P7:
        return "TBEAM_V0P7";
    case meshtastic_HardwareModel_T_ECHO:
        return "T_ECHO";
    case meshtastic_HardwareModel_TLORA_V1_1P3:
        return "TLORA_V1_1P3";
    case meshtastic_HardwareModel_RAK4631:
        return "RAK4631";
    case meshtastic_HardwareModel_HELTEC_V2_1:
        return "HELTEC_V2_1";
    case meshtastic_HardwareModel_HELTEC_V1:
        return "HELTEC_V1";
    case meshtastic_HardwareModel_LILYGO_TBEAM_S3_CORE:
        return "LILYGO_TBEAM_S3_CORE";
    case meshtastic_HardwareModel_RAK11200:
        return "RAK11200";
    case meshtastic_HardwareModel_NANO_G1:
        return "NANO_G1";
    case meshtastic_HardwareModel_TLORA_V2_1_1P8:
        return "TLORA_V2_1_1P8";
    case meshtastic_HardwareModel_TLORA_T3_S3:
        return "TLORA_T3_S3";
    case meshtastic_HardwareModel_NANO_G1_EXPLORER:
        return "NANO_G1_EXPLORER";
    case meshtastic_HardwareModel_NANO_G2_ULTRA:
        return "NANO_G2_ULTRA";
    case meshtastic_HardwareModel_LORA_TYPE:
        return "LORA_TYPE";
    case meshtastic_HardwareModel_WIPHONE:
        return "WIPHONE";
    case meshtastic_HardwareModel_WIO_WM1110:
        return "WIO_WM1110";
    case meshtastic_HardwareModel_RAK2560:
        return "RAK2560";
    case meshtastic_HardwareModel_HELTEC_HRU_3601:
        return "HELTEC_HRU_3601";
    case meshtastic_HardwareModel_HELTEC_WIRELESS_BRIDGE:
        return "HELTEC_WIRELESS_BRIDGE";
    case meshtastic_HardwareModel_STATION_G1:
        return "STATION_G1";
    case meshtastic_HardwareModel_RAK11310:
        return "RAK11310";
    case meshtastic_HardwareModel_SENSELORA_RP2040:
        return "SENSELORA_RP2040";
    case meshtastic_HardwareModel_SENSELORA_S3:
        return "SENSELORA_S3";
    case meshtastic_HardwareModel_CANARYONE:
        return "CANARYONE";
    case meshtastic_HardwareModel_RP2040_LORA:
        return "RP2040_LORA";
    case meshtastic_HardwareModel_STATION_G2:
        return "STATION_G2";
    case meshtastic_HardwareModel_LORA_RELAY_V1:
        return "LORA_RELAY_V1";
    case meshtastic_HardwareModel_NRF52840DK:
        return "NRF52840DK";
    case meshtastic_HardwareModel_PPR:
        return "PPR";
    case meshtastic_HardwareModel_GENIEBLOCKS:
        return "GENIEBLOCKS";
    case meshtastic_HardwareModel_NRF52_UNKNOWN:
        return "NRF52_UNKNOWN";
    case meshtastic_HardwareModel_PORTDUINO:
        return "PORTDUINO";
    case meshtastic_HardwareModel_ANDROID_SIM:
        return "ANDROID_SIM";
    case meshtastic_HardwareModel_DIY_V1:
        return "DIY_V1";
    case meshtastic_HardwareModel_NRF52840_PCA10059:
        return "NRF52840_PCA10059";
    case meshtastic_HardwareModel_DR_DEV:
        return "DR_DEV";
    case meshtastic_HardwareModel_M5STACK:
        return "M5STACK";
    case meshtastic_HardwareModel_HELTEC_V3:
        return "HELTEC_V3";
    case meshtastic_HardwareModel_HELTEC_WSL_V3:
        return "HELTEC_WSL_V3";
    case meshtastic_HardwareModel_BETAFPV_2400_TX:
        return "BETAFPV_2400_TX";
    case meshtastic_HardwareModel_BETAFPV_900_NANO_TX:
        return "BETAFPV_900_NANO_TX";
    case meshtastic_HardwareModel_RPI_PICO:
        return "RPI_PICO";
    case meshtastic_HardwareModel_HELTEC_WIRELESS_TRACKER:
        return "HELTEC_WIRELESS_TRACKER";
    case meshtastic_HardwareModel_HELTEC_WIRELESS_PAPER:
        return "HELTEC_WIRELESS_PAPER";
    case meshtastic_HardwareModel_T_DECK:
        return "T_DECK";
    case meshtastic_HardwareModel_T_WATCH_S3:
        return "T_WATCH_S3";
    case meshtastic_HardwareModel_PICOMPUTER_S3:
        return "PICOMPUTER_S3";
    case meshtastic_HardwareModel_HELTEC_HT62:
        return "HELTEC_HT62";
    case meshtastic_HardwareModel_EBYTE_ESP32_S3:
        return "EBYTE_ESP32_S3";
    case meshtastic_HardwareModel_ESP32_S3_PICO:
        return "ESP32_S3_PICO";
    case meshtastic_HardwareModel_CHATTER_2:
        return "CHATTER_2";
    case meshtastic_HardwareModel_HELTEC_WIRELESS_PAPER_V1_0:
        return "HELTEC_WIRELESS_PAPER_V1_0";
    case meshtastic_HardwareModel_HELTEC_WIRELESS_TRACKER_V1_0:
        return "HELTEC_WIRELESS_TRACKER_V1_0";
    case meshtastic_HardwareModel_UNPHONE:
        return "UNPHONE";
    case meshtastic_HardwareModel_TD_LORAC:
        return "TD_LORAC";
    case meshtastic_HardwareModel_CDEBYTE_EORA_S3:
        return "CDEBYTE_EORA_S3";
    case meshtastic_HardwareModel_TWC_MESH_V4:
        return "TWC_MESH_V4";
    case meshtastic_HardwareModel_NRF52_PROMICRO_DIY:
        return "NRF52_PROMICRO_DIY";
    case meshtastic_HardwareModel_RADIOMASTER_900_BANDIT_NANO:
        return "RADIOMASTER_900_BANDIT_NANO";
    case meshtastic_HardwareModel_HELTEC_CAPSULE_SENSOR_V3:
        return "HELTEC_CAPSULE_SENSOR_V3";
    case meshtastic_HardwareModel_HELTEC_VISION_MASTER_T190:
        return "HELTEC_VISION_MASTER_T190";
    case meshtastic_HardwareModel_HELTEC_VISION_MASTER_E213:
        return "HELTEC_VISION_MASTER_E213";
    case meshtastic_HardwareModel_HELTEC_VISION_MASTER_E290:
        return "HELTEC_VISION_MASTER_E290";
    case meshtastic_HardwareModel_HELTEC_MESH_NODE_T114:
        return "HELTEC_MESH_NODE_T114";
    case meshtastic_HardwareModel_SENSECAP_INDICATOR:
        return "SENSECAP_INDICATOR";
    case meshtastic_HardwareModel_TRACKER_T1000_E:
        return "TRACKER_T1000_E";
    case meshtastic_HardwareModel_RAK3172:
        return "RAK3172";
    case meshtastic_HardwareModel_WIO_E5:
        return "WIO_E5";
    case meshtastic_HardwareModel_RADIOMASTER_900_BANDIT:
        return "RADIOMASTER_900_BANDIT";
    case meshtastic_HardwareModel_ME25LS01_4Y10TD:
        return "ME25LS01_4Y10TD";
    case meshtastic_HardwareModel_RP2040_FEATHER_RFM95:
        return "RP2040_FEATHER_RFM95";
    case meshtastic_HardwareModel_M5STACK_COREBASIC:
        return "M5STACK_COREBASIC";
    case meshtastic_HardwareModel_M5STACK_CORE2:
        return "M5STACK_CORE2";
    case meshtastic_HardwareModel_RPI_PICO2:
        return "RPI_PICO2";
    case meshtastic_HardwareModel_M5STACK_CORES3:
        return "M5STACK_CORES3";
    case meshtastic_HardwareModel_SEEED_XIAO_S3:
        return "SEEED_XIAO_S3";
    case meshtastic_HardwareModel_MS24SF1:
        return "MS24SF1";
    case meshtastic_HardwareModel_TLORA_C6:
        return "TLORA_C6";
    case meshtastic_HardwareModel_WISMESH_TAP:
        return "WISMESH_TAP";
    case meshtastic_HardwareModel_ROUTASTIC:
        return "ROUTASTIC";
    case meshtastic_HardwareModel_MESH_TAB:
        return "MESH_TAB";
    case meshtastic_HardwareModel_MESHLINK:
        return "MESHLINK";
    case meshtastic_HardwareModel_XIAO_NRF52_KIT:
        return "XIAO_NRF52_KIT";
    case meshtastic_HardwareModel_THINKNODE_M1:
        return "THINKNODE_M1";
    case meshtastic_HardwareModel_THINKNODE_M2:
        return "THINKNODE_M2";
    case meshtastic_HardwareModel_T_ETH_ELITE:
        return "T_ETH_ELITE";
    case meshtastic_HardwareModel_HELTEC_SENSOR_HUB:
        return "HELTEC_SENSOR_HUB";
    case meshtastic_HardwareModel_RESERVED_FRIED_CHICKEN:
        return "RESERVED_FRIED_CHICKEN";
    case meshtastic_HardwareModel_HELTEC_MESH_POCKET:
        return "HELTEC_MESH_POCKET";
    case meshtastic_HardwareModel_SEEED_SOLAR_NODE:
        return "SEEED_SOLAR_NODE";
    case meshtastic_HardwareModel_NOMADSTAR_METEOR_PRO:
        return "NOMADSTAR_METEOR_PRO";
    case meshtastic_HardwareModel_CROWPANEL:
        return "CROWPANEL";
    case meshtastic_HardwareModel_PRIVATE_HW:
        return "PRIVATE_HW";
    default:
        return "UNKNOWN";
    }
}

const char *MeshMonShell::roleString(meshtastic_Config_DeviceConfig_Role role)
{
    switch (role) {
    case meshtastic_Config_DeviceConfig_Role_CLIENT:
        return "CLIENT";
    case meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE:
        return "CLIENT_MUTE";
    case meshtastic_Config_DeviceConfig_Role_ROUTER:
        return "ROUTER";
    case meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT:
        return "ROUTER_CLIENT";
    case meshtastic_Config_DeviceConfig_Role_REPEATER:
        return "REPEATER";
    case meshtastic_Config_DeviceConfig_Role_TRACKER:
        return "TRACKER";
    case meshtastic_Config_DeviceConfig_Role_SENSOR:
        return "SENSOR";
    case meshtastic_Config_DeviceConfig_Role_TAK:
        return "TAK";
    case meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN:
        return "CLIENT_HIDDEN";
    case meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND:
        return "LOST_AND_FOUND";
    case meshtastic_Config_DeviceConfig_Role_TAK_TRACKER:
        return "TAK_TRACKER";
    case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:
        return "ROUTER_LATE";
    default:
        return "UNKNOWN";
    }
}

uint32_t MeshMonShell::resolveNode(const string &nodeArg) const
{
    if (nodeArg.empty() || (_client == NULL)) {
        return 0xffffffffU;
    }

    if ((strcasecmp(nodeArg.c_str(), "me") == 0) ||
        (strcasecmp(nodeArg.c_str(), "self") == 0) ||
        (strcasecmp(nodeArg.c_str(), "local") == 0)) {
        return _client->whoami();
    }

    // 1. Try parsing hex ID: !hex, 0xhex, or 8-char hex
    string hexStr = nodeArg;
    if (hexStr.size() > 0 && hexStr[0] == '!') {
        hexStr = hexStr.substr(1);
    } else if (hexStr.size() > 2 && hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X')) {
        hexStr = hexStr.substr(2);
    }

    bool isHex = (!hexStr.empty() && hexStr.size() <= 8);
    if (isHex) {
        for (size_t i = 0; i < hexStr.size(); i++) {
            if (!isxdigit(static_cast<unsigned char>(hexStr[i]))) {
                isHex = false;
                break;
            }
        }
    }

    if (isHex && (nodeArg[0] == '!' || (nodeArg.size() > 2 && nodeArg[0] == '0' && (nodeArg[1] == 'x' || nodeArg[1] == 'X')) || hexStr.size() == 8)) {
        char *end = NULL;
        errno = 0;
        unsigned long val = strtoul(hexStr.c_str(), &end, 16);
        if ((errno == 0) && (end != hexStr.c_str()) && (*end == '\0')) {
            return (uint32_t) val;
        }
    }

    // 2. Try matching known nodes by short name (case-insensitive)
    const map<uint32_t, meshtastic_NodeInfo> &nodes = _client->nodeInfos();
    for (map<uint32_t, meshtastic_NodeInfo>::const_iterator it = nodes.begin();
         it != nodes.end(); it++) {
        if (it->second.has_user) {
            const char *sn = it->second.user.short_name;
            if ((sn != NULL) && (strcasecmp(sn, nodeArg.c_str()) == 0)) {
                return it->first;
            }
        }
    }

    // 3. Try matching known nodes by long name (case-insensitive)
    for (map<uint32_t, meshtastic_NodeInfo>::const_iterator it = nodes.begin();
         it != nodes.end(); it++) {
        if (it->second.has_user) {
            const char *ln = it->second.user.long_name;
            if ((ln != NULL) && (strcasecmp(ln, nodeArg.c_str()) == 0)) {
                return it->first;
            }
        }
    }

    // 4. Try matching by decimal node ID
    bool isDec = !nodeArg.empty();
    for (size_t i = 0; i < nodeArg.size(); i++) {
        if (!isdigit(static_cast<unsigned char>(nodeArg[i]))) {
            isDec = false;
            break;
        }
    }
    if (isDec) {
        char *end = NULL;
        errno = 0;
        unsigned long val = strtoul(nodeArg.c_str(), &end, 10);
        if ((errno == 0) && (end != nodeArg.c_str()) && (*end == '\0')) {
            if (nodes.find((uint32_t) val) != nodes.end() || ((uint32_t) val == _client->whoami())) {
                return (uint32_t) val;
            }
        }
    }

    // 5. Fallback to SimpleClient::getId
    uint32_t idByName = _client->getId(nodeArg);
    if (idByName != 0xffffffffU) {
        return idByName;
    }

    // 6. If it was a valid hex string of any length <= 8, try parsing as hex as last resort
    if (isHex) {
        char *end = NULL;
        errno = 0;
        unsigned long val = strtoul(hexStr.c_str(), &end, 16);
        if ((errno == 0) && (end != hexStr.c_str()) && (*end == '\0')) {
            return (uint32_t) val;
        }
    }

    return 0xffffffffU;
}

void MeshMonShell::printNodeStatus(uint32_t nodeId)
{
    if (_client == NULL) {
        this->printf("Client not connected.\n");
        return;
    }

    const map<uint32_t, meshtastic_NodeInfo> &nodes = _client->nodeInfos();
    map<uint32_t, meshtastic_NodeInfo>::const_iterator nIt = nodes.find(nodeId);

    string shortName = _client->lookupShortName(nodeId);
    string longName = _client->lookupLongName(nodeId);
    bool isSelf = (nodeId == _client->whoami());

    char idHex[16];
    snprintf(idHex, sizeof(idHex), "!%08x", nodeId);

    this->printf("Node %s (num: %u, hex: %s)%s:\n",
                 shortName.c_str(), nodeId, idHex,
                 isSelf ? " [Self/Local]" : "");

    if (!longName.empty() && (longName != shortName)) {
        this->printf("  Long name:        %s\n", longName.c_str());
    }

    if (nIt != nodes.end() && nIt->second.has_user) {
        const meshtastic_User &user = nIt->second.user;
        if (user.hw_model != meshtastic_HardwareModel_UNSET) {
            this->printf("  Hardware:         %s\n", hardwareModelString(user.hw_model));
        }
        this->printf("  Role:             %s\n", roleString(user.role));
        if (user.is_licensed) {
            this->printf("  Licensed:         yes\n");
        }
        if (user.macaddr[0] != 0 || user.macaddr[1] != 0 || user.macaddr[2] != 0 ||
            user.macaddr[3] != 0 || user.macaddr[4] != 0 || user.macaddr[5] != 0) {
            this->printf("  MAC Address:      %02x:%02x:%02x:%02x:%02x:%02x\n",
                         (unsigned char) user.macaddr[0], (unsigned char) user.macaddr[1],
                         (unsigned char) user.macaddr[2], (unsigned char) user.macaddr[3],
                         (unsigned char) user.macaddr[4], (unsigned char) user.macaddr[5]);
        }
        if (user.has_is_unmessagable && user.is_unmessagable) {
            this->printf("  Unmessagable:     yes\n");
        }
    }

    if (nIt != nodes.end()) {
        const meshtastic_NodeInfo &info = nIt->second;
        if (info.is_favorite) {
            this->printf("  Favorite:         yes\n");
        }
        if (info.is_ignored) {
            this->printf("  Ignored:          yes\n");
        }
        if (info.has_hops_away) {
            if (info.hops_away == 0) {
                this->printf("  Hops away:        0 (direct neighbor)\n");
            } else {
                this->printf("  Hops away:        %u\n", (unsigned int) info.hops_away);
            }
        }
        if (info.snr != 0.0f) {
            this->printf("  SNR:              %.2f dB\n", info.snr);
        }
        if (info.channel != 0) {
            string chanName = _client->getChannelName((uint8_t) info.channel);
            if (!chanName.empty()) {
                this->printf("  Channel:          %u (%s)\n", (unsigned int) info.channel, chanName.c_str());
            } else {
                this->printf("  Channel:          %u\n", (unsigned int) info.channel);
            }
        }
        this->printf("  Last heard:       %s\n", formatRelativeTime(info.last_heard).c_str());
    }

    // Device metrics
    const map<uint32_t, meshtastic_DeviceMetrics> &dMap = _client->deviceMetrics();
    map<uint32_t, meshtastic_DeviceMetrics>::const_iterator dIt = dMap.find(nodeId);
    const meshtastic_DeviceMetrics *dm = NULL;
    if (dIt != dMap.end()) {
        dm = &dIt->second;
    } else if (nIt != nodes.end() && nIt->second.has_device_metrics) {
        dm = &nIt->second.device_metrics;
    }
    if (dm != NULL) {
        this->printf("  Device Metrics:\n");
        if (dm->has_battery_level) {
            if (dm->battery_level > 100) {
                this->printf("    Battery:        Powered / USB\n");
            } else {
                this->printf("    Battery:        %u%%\n", (unsigned int) dm->battery_level);
            }
        }
        if (dm->has_voltage) {
            this->printf("    Voltage:        %.2f V\n", dm->voltage);
        }
        if (dm->has_channel_utilization) {
            this->printf("    Channel util:   %.2f%%\n", dm->channel_utilization);
        }
        if (dm->has_air_util_tx) {
            this->printf("    Air util (Tx):  %.2f%%\n", dm->air_util_tx);
        }
        if (dm->has_uptime_seconds) {
            unsigned int ut = dm->uptime_seconds;
            unsigned int days = ut / 86400;
            unsigned int hours = (ut % 86400) / 3600;
            unsigned int mins = (ut % 3600) / 60;
            unsigned int secs = ut % 60;
            if (days > 0) {
                this->printf("    Uptime:         %ud %02u:%02u:%02u\n", days, hours, mins, secs);
            } else {
                this->printf("    Uptime:         %02u:%02u:%02u\n", hours, mins, secs);
            }
        }
    }

    // Position
    const map<uint32_t, meshtastic_Position> &pMap = _client->positions();
    map<uint32_t, meshtastic_Position>::const_iterator pIt = pMap.find(nodeId);
    const meshtastic_Position *pos = NULL;
    if (pIt != pMap.end() && (pIt->second.latitude_i != 0 || pIt->second.longitude_i != 0)) {
        pos = &pIt->second;
    } else if (nIt != nodes.end() && nIt->second.has_position && (nIt->second.position.latitude_i != 0 || nIt->second.position.longitude_i != 0)) {
        pos = &nIt->second.position;
    }
    if (pos != NULL) {
        this->printf("  Position:\n");
        this->printf("    Latitude:       %.7f\u00b0\n", pos->latitude_i * 1e-7);
        this->printf("    Longitude:      %.7f\u00b0\n", pos->longitude_i * 1e-7);
        if (pos->altitude != 0) {
            this->printf("    Altitude:       %d m\n", pos->altitude);
        }
        if (pos->time != 0) {
            this->printf("    Fix time:       %s\n", formatRelativeTime(pos->time).c_str());
        }
        if (pos->sats_in_view != 0) {
            this->printf("    Satellites:     %u\n", (unsigned int) pos->sats_in_view);
        }
        if (pos->has_ground_speed && pos->ground_speed != 0) {
            this->printf("    Ground speed:   %.1f m/s\n", pos->ground_speed * 1e-3);
        }
    }

    // Environment metrics
    const map<uint32_t, meshtastic_EnvironmentMetrics> &eMap = _client->environmentMetrics();
    map<uint32_t, meshtastic_EnvironmentMetrics>::const_iterator eIt = eMap.find(nodeId);
    if (eIt != eMap.end()) {
        const meshtastic_EnvironmentMetrics &em = eIt->second;
        this->printf("  Environment:\n");
        if (em.has_temperature) {
            this->printf("    Temperature:    %.2f \u00b0C\n", em.temperature);
        }
        if (em.has_relative_humidity) {
            this->printf("    Humidity:       %.2f %%\n", em.relative_humidity);
        }
        if (em.has_barometric_pressure) {
            this->printf("    Pressure:       %.2f hPa\n", em.barometric_pressure);
        }
        if (em.has_iaq) {
            this->printf("    IAQ:            %u\n", (unsigned int) em.iaq);
        }
        if (em.has_gas_resistance) {
            this->printf("    Gas resistance: %.2f \u03a9\n", em.gas_resistance);
        }
        if (em.has_voltage) {
            this->printf("    Sensor voltage: %.2f V\n", em.voltage);
        }
        if (em.has_current) {
            this->printf("    Sensor current: %.2f mA\n", em.current);
        }
        if (em.has_lux) {
            this->printf("    Lux:            %.1f lx\n", em.lux);
        }
        if (em.has_uv_lux) {
            this->printf("    UV lux:         %.1f lx\n", em.uv_lux);
        }
        if (em.has_wind_speed) {
            this->printf("    Wind speed:     %.2f m/s\n", em.wind_speed);
        }
        if (em.has_wind_direction) {
            this->printf("    Wind direction: %u\u00b0\n", (unsigned int) em.wind_direction);
        }
        if (em.has_wind_gust) {
            this->printf("    Wind gust:      %.2f m/s\n", em.wind_gust);
        }
        if (em.has_radiation) {
            this->printf("    Radiation:      %.2f \u00b5Sv/h\n", em.radiation);
        }
        if (em.has_weight) {
            this->printf("    Weight:         %.2f kg\n", em.weight);
        }
        if (em.has_distance) {
            this->printf("    Distance:       %.2f mm\n", em.distance);
        }
    }

    // Air Quality metrics
    const map<uint32_t, meshtastic_AirQualityMetrics> &aqMap = _client->airQualityMetrics();
    map<uint32_t, meshtastic_AirQualityMetrics>::const_iterator aqIt = aqMap.find(nodeId);
    if (aqIt != aqMap.end()) {
        const meshtastic_AirQualityMetrics &aq = aqIt->second;
        this->printf("  Air Quality:\n");
        if (aq.has_pm10_standard) {
            this->printf("    PM1.0 (std):    %u \u00b5g/m\u00b3\n", (unsigned int) aq.pm10_standard);
        }
        if (aq.has_pm25_standard) {
            this->printf("    PM2.5 (std):    %u \u00b5g/m\u00b3\n", (unsigned int) aq.pm25_standard);
        }
        if (aq.has_pm100_standard) {
            this->printf("    PM10  (std):    %u \u00b5g/m\u00b3\n", (unsigned int) aq.pm100_standard);
        }
        if (aq.has_co2) {
            this->printf("    CO2:            %u ppm\n", (unsigned int) aq.co2);
        }
    }

    // Power metrics
    const map<uint32_t, meshtastic_PowerMetrics> &powMap = _client->powerMetrics();
    map<uint32_t, meshtastic_PowerMetrics>::const_iterator powIt = powMap.find(nodeId);
    if (powIt != powMap.end()) {
        const meshtastic_PowerMetrics &pm = powIt->second;
        this->printf("  Power Metrics:\n");
        if (pm.has_ch1_voltage || pm.has_ch1_current) {
            this->printf("    Channel 1:      %.2f V, %.2f mA\n", pm.ch1_voltage, pm.ch1_current);
        }
        if (pm.has_ch2_voltage || pm.has_ch2_current) {
            this->printf("    Channel 2:      %.2f V, %.2f mA\n", pm.ch2_voltage, pm.ch2_current);
        }
        if (pm.has_ch3_voltage || pm.has_ch3_current) {
            this->printf("    Channel 3:      %.2f V, %.2f mA\n", pm.ch3_voltage, pm.ch3_current);
        }
    }

    // Host metrics
    const map<uint32_t, meshtastic_HostMetrics> &hMap = _client->hostMetrics();
    map<uint32_t, meshtastic_HostMetrics>::const_iterator hIt = hMap.find(nodeId);
    if (hIt != hMap.end()) {
        const meshtastic_HostMetrics &hm = hIt->second;
        this->printf("  Host Metrics:\n");
        if (hm.uptime_seconds > 0) {
            unsigned int ut = hm.uptime_seconds;
            unsigned int days = ut / 86400;
            unsigned int hours = (ut % 86400) / 3600;
            unsigned int mins = (ut % 3600) / 60;
            unsigned int secs = ut % 60;
            if (days > 0) {
                this->printf("    Host uptime:    %ud %02u:%02u:%02u\n", days, hours, mins, secs);
            } else {
                this->printf("    Host uptime:    %02u:%02u:%02u\n", hours, mins, secs);
            }
        }
        if (hm.freemem_bytes > 0) {
            this->printf("    Free memory:    %lu KB\n", (unsigned long)(hm.freemem_bytes / 1024));
        }
        if (hm.diskfree1_bytes > 0) {
            this->printf("    Disk free 1:    %lu MB\n", (unsigned long)(hm.diskfree1_bytes / (1024 * 1024)));
        }
        if (hm.has_diskfree2_bytes && hm.diskfree2_bytes > 0) {
            this->printf("    Disk free 2:    %lu MB\n", (unsigned long)(hm.diskfree2_bytes / (1024 * 1024)));
        }
        if (hm.has_diskfree3_bytes && hm.diskfree3_bytes > 0) {
            this->printf("    Disk free 3:    %lu MB\n", (unsigned long)(hm.diskfree3_bytes / (1024 * 1024)));
        }
        if (hm.load1 > 0 || hm.load5 > 0 || hm.load15 > 0) {
            this->printf("    Load average:   %.2f, %.2f, %.2f\n",
                         hm.load1 / 100.0, hm.load5 / 100.0, hm.load15 / 100.0);
        }
        if (hm.has_user_string && hm.user_string[0] != '\0') {
            this->printf("    User info:      %s\n", hm.user_string);
        }
    }

    // Health metrics
    const map<uint32_t, meshtastic_HealthMetrics> &hlMap = _client->healthMetrics();
    map<uint32_t, meshtastic_HealthMetrics>::const_iterator hlIt = hlMap.find(nodeId);
    if (hlIt != hlMap.end()) {
        const meshtastic_HealthMetrics &hl = hlIt->second;
        this->printf("  Health Metrics:\n");
        if (hl.has_heart_bpm) {
            this->printf("    Heart BPM:      %u\n", (unsigned int) hl.heart_bpm);
        }
        if (hl.has_spO2) {
            this->printf("    SpO2:           %u%%\n", (unsigned int) hl.spO2);
        }
        if (hl.has_temperature) {
            this->printf("    Body temp:      %.2f \u00b0C\n", hl.temperature);
        }
    }
}

void MeshMonShell::printCurve(const char *name,
                              const CalibrationCurve &curve,
                              const char *unit)
{
    const vector<CalibrationPoint> &points = curve.getPoints();
    if (points.empty()) {
        return;
    }

    this->printf("    %s (%s):\n", name, unit);
    for (const CalibrationPoint &pt : points) {
        this->printf("      raw: %8.2f  ->  cal: %8.2f\n", pt.raw, pt.cal);
    }
}

void MeshMonShell::printNode(const string &nodeKey,
                             const NodeCalibration &nodeCal)
{
    if (nodeCal.empty()) {
        return;
    }

    string label = nodeKey;
    shared_ptr<MeshMon> meshmon = dynamic_pointer_cast<MeshMon>(_client);
    if ((nodeKey != "default") && (meshmon != NULL)) {
        char *end = NULL;
        unsigned long val = strtoul(nodeKey.c_str(), &end, 16);
        if ((end != nodeKey.c_str()) && (*end == '\0')) {
            const map<unsigned int, meshtastic_NodeInfo> &nodes = meshmon->nodeInfos();
            map<unsigned int, meshtastic_NodeInfo>::const_iterator it = nodes.find((unsigned int) val);
            if (it != nodes.end() && it->second.has_user && (it->second.user.short_name[0] != '\0')) {
                label = string(it->second.user.short_name) + " (" + nodeKey + ")";
            }
        }
    }

    this->printf("  Node %s:\n", label.c_str());
    printCurve("Temperature", nodeCal.temperature, "\u00b0C");
    printCurve("Humidity", nodeCal.humidity, "%");
    printCurve("Pressure", nodeCal.pressure, "hPa");
}

bool MeshMonShell::resolveNodeKey(const string &nodeArg, string &outKey,
                                  string &outName)
{
    if (nodeArg.empty() || (nodeArg == "default") || (nodeArg == "*") || (nodeArg == "DEFAULT")) {
        outKey = "default";
        outName = "default";
        return true;
    }

    // 1. Try matching known nodes by shortname or longname
    shared_ptr<MeshMon> meshmon = dynamic_pointer_cast<MeshMon>(_client);
    if (meshmon != NULL) {
        const map<unsigned int, meshtastic_NodeInfo> &nodes = meshmon->nodeInfos();
        for (map<unsigned int, meshtastic_NodeInfo>::const_iterator it = nodes.begin();
             it != nodes.end(); it++) {
            if (it->second.has_user) {
                const char *sn = it->second.user.short_name;
                const char *ln = it->second.user.long_name;
                if ((sn != NULL) && (strcasecmp(sn, nodeArg.c_str()) == 0)) {
                    outKey = Calibration::normalizeNodeKey((uint32_t) it->first);
                    outName = string(sn) + " (" + outKey + ")";
                    return true;
                }
                if ((ln != NULL) && (strcasecmp(ln, nodeArg.c_str()) == 0)) {
                    outKey = Calibration::normalizeNodeKey((uint32_t) it->first);
                    outName = string(ln) + " (" + outKey + ")";
                    return true;
                }
            }
        }
    }

    // 2. Try parsing as hex node ID
    string hexStr = nodeArg;
    if (hexStr.size() > 0 && hexStr[0] == '!') {
        hexStr = hexStr.substr(1);
    } else if (hexStr.size() > 2 && hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X')) {
        hexStr = hexStr.substr(2);
    }

    if (hexStr.empty() || hexStr.size() > 8) {
        return false;
    }

    for (size_t i = 0; i < hexStr.size(); i++) {
        if (!isxdigit(static_cast<unsigned char>(hexStr[i]))) {
            return false;
        }
    }

    char *end = NULL;
    errno = 0;
    unsigned long val = strtoul(hexStr.c_str(), &end, 16);
    if ((errno != 0) || (end == hexStr.c_str()) || (*end != '\0')) {
        return false;
    }

    outKey = Calibration::normalizeNodeKey((uint32_t) val);
    outName = outKey;
    if (meshmon != NULL) {
        const map<unsigned int, meshtastic_NodeInfo> &nodes = meshmon->nodeInfos();
        map<unsigned int, meshtastic_NodeInfo>::const_iterator it = nodes.find((unsigned int) val);
        if (it != nodes.end() && it->second.has_user && (it->second.user.short_name[0] != '\0')) {
            outName = string(it->second.user.short_name) + " (" + outKey + ")";
        }
    }

    return true;
}

int MeshMonShell::calib(int argc, char **argv)
{
    if ((argc >= 2) &&
        ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0) ||
         (strcmp(argv[1], "help") == 0))) {
        this->printf("Usage: %s [-h|--help] [command] [args...]\n", argv[0]);
        this->printf("  Manage telemetry sensor calibration points.\n");
        this->printf("Commands:\n");
        this->printf("  calib                                  Show all calibration points\n");
        this->printf("  calib show [<node>]                    Show calibration for a node\n");
        this->printf("  calib set <node> <sensor> <raw> <cal>  Set a calibration point\n");
        this->printf("  calib del <node> <sensor> <raw>        Delete a calibration point\n");
        this->printf("  calib clear <node> [<sensor>]          Clear calibration for a node\n");
        this->printf("  calib reload                           Reload calibration from ~/.meshmon.calib\n");
        this->printf("Sensors: temp (temperature, \u00b0C), hum (humidity, %%), press (pressure, hPa)\n");
        this->printf("Nodes: node shortname, hex ID (e.g. 2bf941d4, !2bf941d4) or 'default'\n");
        return 0;
    }

    shared_ptr<MeshMon> meshmon = dynamic_pointer_cast<MeshMon>(_client);
    shared_ptr<Calibration> calibration =
        meshmon ? meshmon->calibration() : NULL;

    if (calibration == NULL) {
        this->printf("Calibration service is not available.\n");
        return -1;
    }

    if ((argc == 1) ||
        ((argc == 2) && ((strcmp(argv[1], "show") == 0) ||
                         (strcmp(argv[1], "list") == 0)))) {
        map<string, NodeCalibration> all = calibration->getAll();
        if (all.empty()) {
            this->printf("No calibration points configured.\n");
            return 0;
        }

        this->printf("Telemetry Calibration Points:\n");
        for (map<string, NodeCalibration>::const_iterator it = all.begin();
             it != all.end(); it++) {
            printNode(it->first, it->second);
        }
        return 0;
    }

    if ((argc == 3) && (strcmp(argv[1], "show") == 0)) {
        string nodeKey, nodeName;
        if (!resolveNodeKey(argv[2], nodeKey, nodeName)) {
            this->printf("Invalid node '%s': not a known shortname or valid hex ID.\n",
                         argv[2]);
            return -1;
        }

        NodeCalibration nodeCal;
        if (!calibration->getNode(nodeKey, nodeCal) || nodeCal.empty()) {
            this->printf("No calibration configured for node %s.\n",
                         nodeName.c_str());
            return 0;
        }

        this->printf("Calibration for node %s:\n", nodeName.c_str());
        printNode(nodeKey, nodeCal);
        return 0;
    }

    if ((argc >= 6) && (strcmp(argv[1], "set") == 0)) {
        string nodeKey, nodeName;
        if (!resolveNodeKey(argv[2], nodeKey, nodeName)) {
            this->printf("Invalid node '%s': not a known shortname or valid hex ID.\n",
                         argv[2]);
            return -1;
        }

        SensorType sensor;
        if (!Calibration::parseSensorType(argv[3], sensor)) {
            this->printf("Unknown sensor '%s'. Use temp, hum, or press.\n",
                         argv[3]);
            return -1;
        }

        char *end1 = NULL, *end2 = NULL;
        double raw = strtod(argv[4], &end1);
        double cal = strtod(argv[5], &end2);
        if ((end1 == argv[4]) || (end2 == argv[5])) {
            this->printf("Invalid numeric values for raw or cal.\n");
            return -1;
        }

        calibration->setPoint(nodeKey, sensor, raw, cal);
        calibration->saveFile();
        this->printf("Set calibration for node %s %s: raw %.2f -> cal %.2f %s\n",
                     nodeName.c_str(),
                     Calibration::sensorTypeName(sensor),
                     raw, cal, Calibration::sensorTypeUnit(sensor));
        return 0;
    }

    if ((argc >= 5) && ((strcmp(argv[1], "del") == 0) ||
                        (strcmp(argv[1], "delete") == 0) ||
                        (strcmp(argv[1], "rm") == 0))) {
        string nodeKey, nodeName;
        if (!resolveNodeKey(argv[2], nodeKey, nodeName)) {
            this->printf("Invalid node '%s': not a known shortname or valid hex ID.\n",
                         argv[2]);
            return -1;
        }

        SensorType sensor;
        if (!Calibration::parseSensorType(argv[3], sensor)) {
            this->printf("Unknown sensor '%s'. Use temp, hum, or press.\n",
                         argv[3]);
            return -1;
        }

        char *end = NULL;
        double raw = strtod(argv[4], &end);
        if (end == argv[4]) {
            this->printf("Invalid numeric value for raw.\n");
            return -1;
        }

        if (calibration->delPoint(nodeKey, sensor, raw)) {
            calibration->saveFile();
            this->printf("Deleted calibration point raw %.2f for node %s %s.\n",
                         raw, nodeName.c_str(),
                         Calibration::sensorTypeName(sensor));
        } else {
            this->printf("Calibration point raw %.2f not found for node %s %s.\n",
                         raw, nodeName.c_str(),
                         Calibration::sensorTypeName(sensor));
        }
        return 0;
    }

    if ((argc >= 3) && (strcmp(argv[1], "clear") == 0)) {
        string nodeKey, nodeName;
        if (!resolveNodeKey(argv[2], nodeKey, nodeName)) {
            this->printf("Invalid node '%s': not a known shortname or valid hex ID.\n",
                         argv[2]);
            return -1;
        }

        if (argc >= 4) {
            SensorType sensor;
            if (!Calibration::parseSensorType(argv[3], sensor)) {
                this->printf("Unknown sensor '%s'. Use temp, hum, or press.\n",
                             argv[3]);
                return -1;
            }
            calibration->clear(nodeKey, sensor);
            calibration->saveFile();
            this->printf("Cleared %s calibration for node %s.\n",
                         Calibration::sensorTypeName(sensor),
                         nodeName.c_str());
        } else {
            calibration->clearNode(nodeKey);
            calibration->saveFile();
            this->printf("Cleared all calibration for node %s.\n",
                         nodeName.c_str());
        }
        return 0;
    }

    if ((argc == 2) && (strcmp(argv[1], "reload") == 0)) {
        if (calibration->loadFile()) {
            this->printf("Calibration reloaded from %s\n",
                         calibration->getConfigPath().c_str());
        } else {
            this->printf("Failed to reload calibration\n");
            return -1;
        }
        return 0;
    }

    this->printf("Usage:\n");
    this->printf("  calib                                  - Show all calibration points\n");
    this->printf("  calib show [<node>]                    - Show calibration for a node\n");
    this->printf("  calib set <node> <sensor> <raw> <cal>  - Set a calibration point\n");
    this->printf("  calib del <node> <sensor> <raw>        - Delete a calibration point\n");
    this->printf("  calib clear <node> [<sensor>]          - Clear calibration for a node\n");
    this->printf("  calib reload                           - Reload calibration from ~/.meshmon.calib\n");
    this->printf("Sensors: temp (temperature, \u00b0C), hum (humidity, %%), press (pressure, hPa)\n");
    this->printf("Nodes: node shortname, hex ID (e.g. 2bf941d4, !2bf941d4) or 'default'\n");

    return 0;
}

void MeshMonShell::printChatbotHelp(void)
{
    this->printf("Usage:\n");
    this->printf("  chatbot                              - Show chatbot status and configuration\n");
    this->printf("  chatbot status                       - Show chatbot status and configuration\n");
    this->printf("  chatbot <enable|disable|on|off>      - Enable or disable the chatbot\n");
    this->printf("  chatbot stats [reset]                - Show invocation statistics (or reset counters)\n");
    this->printf("  chatbot tokens [reset]               - Show detailed token usage (or reset counters)\n");
    this->printf("  chatbot search <on|off>              - Enable or disable Google Search grounding\n");
    this->printf("  chatbot timeout <seconds>            - Set API HTTP request timeout in seconds (1..300)\n");
    this->printf("  chatbot max_tokens <num>             - Set max output tokens limit (16..8192)\n");
    this->printf("  chatbot thinking [<num|on|off|auto>] - Set thinking budget (0=off, auto=-1, or tokens)\n");
    this->printf("  chatbot temp [<val|default>]         - Set temperature (0.0..2.0 or default)\n");
    this->printf("  chatbot top_p [<val|default>]        - Set top-p sampling (0.0..1.0 or default)\n");
    this->printf("  chatbot top_k [<val|default>]        - Set top-k sampling (1..100 or default)\n");
    this->printf("  chatbot model [name]                 - Show or change active Gemini model\n");
    this->printf("  chatbot history [<max_turns>]        - Show or set max conversation history turns\n");
    this->printf("  chatbot context [<max_turns>]        - Show or set max context turns sent to API\n");
    this->printf("  chatbot idle [<seconds>]             - Show or set conversation idle timeout\n");
    this->printf("  chatbot clear [<node>]               - Clear conversation history\n");
    this->printf("  chatbot help                         - Show this help message\n");
}

int MeshMonShell::chatbot(int argc, char **argv)
{
    shared_ptr<MeshMon> meshmon = dynamic_pointer_cast<MeshMon>(_client);
    if (!meshmon) {
        this->printf("Error: MeshMon client not available\n");
        return -1;
    }

    shared_ptr<ChatBot> bot = meshmon->chatbot();
    if (!bot) {
        this->printf("Chatbot is not configured or disabled (check gemini section in ~/.meshmon)\n");
        return 0;
    }

    shared_ptr<GeminiChat> gemini = dynamic_pointer_cast<GeminiChat>(bot);

    if ((argc >= 2) &&
        ((strcmp(argv[1], "help") == 0) ||
         (strcmp(argv[1], "-h") == 0) ||
         (strcmp(argv[1], "--help") == 0) ||
         (strcmp(argv[1], "?") == 0))) {
        printChatbotHelp();
        return 0;
    }

    if ((argc <= 1) || (strcmp(argv[1], "status") == 0)) {
        ChatBotStats stats = bot->getStats();
        this->printf("Chatbot Status:\n");
        this->printf("  Type:            %s\n", gemini ? "GeminiChat" : "Generic ChatBot");
        this->printf("  Enabled:         %s\n", bot->enabled() ? "yes" : "no");
        if (gemini) {
            this->printf("  Model:           %s\n", gemini->getModel().c_str());
            this->printf("  Timeout:         %u s\n", gemini->getTimeout());
            this->printf("  Max Tokens:      %u\n", gemini->getMaxOutputTokens());
            int32_t tb = gemini->getThinkingBudget();
            if (tb == 0) {
                this->printf("  Thinking Budget: 0 (disabled)\n");
            } else if (tb < 0) {
                this->printf("  Thinking Budget: auto (unconstrained)\n");
            } else {
                this->printf("  Thinking Budget: %d tokens\n", tb);
            }
            float temp = gemini->getTemperature();
            if (temp < 0.0f) {
                this->printf("  Temperature:     default\n");
            } else {
                this->printf("  Temperature:     %.2f\n", temp);
            }
            float topP = gemini->getTopP();
            if (topP < 0.0f) {
                this->printf("  Top-P:           default\n");
            } else {
                this->printf("  Top-P:           %.2f\n", topP);
            }
            int32_t topK = gemini->getTopK();
            if (topK < 0) {
                this->printf("  Top-K:           default\n");
            } else {
                this->printf("  Top-K:           %d\n", topK);
            }
            this->printf("  Google Search:   %s\n", gemini->getWebSearch() ? "enabled" : "disabled");
            this->printf("  Max History:     %zu turns\n", bot->getMaxHistoryTurns());
            this->printf("  Max Context:     %d turns\n", gemini->getMaxContextTurns());
            this->printf("  Idle Timeout:    %u s\n", bot->getIdleTimeout());
            GeminiTokenUsage usage = gemini->getTokenUsage();
            this->printf("  Total Tokens:    %lu (prompt: %lu, candidate: %lu, cached: %lu)\n",
                         (unsigned long) usage.totalTokens,
                         (unsigned long) usage.promptTokens,
                         (unsigned long) usage.candidateTokens,
                         (unsigned long) usage.cachedContentTokens);
        } else {
            this->printf("  Max History:     %zu turns\n", bot->getMaxHistoryTurns());
            this->printf("  Idle Timeout:    %u s\n", bot->getIdleTimeout());
        }
        double successRate = (stats.invocations > 0)
            ? ((double) stats.successes / (double) stats.invocations * 100.0) : 0.0;
        this->printf("  Invocations:     %lu (successes: %lu, failures: %lu, rate: %.1f%%)\n",
                     (unsigned long) stats.invocations,
                     (unsigned long) stats.successes,
                     (unsigned long) stats.failures,
                     successRate);
        return 0;
    }

    if ((strcmp(argv[1], "enable") == 0) || (strcmp(argv[1], "on") == 0)) {
        bot->setEnabled(true);
        this->printf("Chatbot enabled.\n");
        return 0;
    }

    if ((strcmp(argv[1], "disable") == 0) || (strcmp(argv[1], "off") == 0)) {
        bot->setEnabled(false);
        this->printf("Chatbot disabled.\n");
        return 0;
    }

    if (strcmp(argv[1], "stats") == 0) {
        if ((argc >= 3) && (strcmp(argv[2], "reset") == 0)) {
            bot->resetStats();
            if (gemini) {
                gemini->resetTokenUsage();
            }
            this->printf("Chatbot invocation and token statistics reset.\n");
            return 0;
        }

        ChatBotStats stats = bot->getStats();
        double successRate = (stats.invocations > 0)
            ? ((double) stats.successes / (double) stats.invocations * 100.0) : 0.0;

        this->printf("Chatbot Invocations:\n");
        this->printf("  Total:           %lu\n", (unsigned long) stats.invocations);
        this->printf("  Successes:       %lu\n", (unsigned long) stats.successes);
        this->printf("  Failures:        %lu\n", (unsigned long) stats.failures);
        this->printf("  Success Rate:    %.1f%%\n", successRate);

        if (gemini) {
            GeminiTokenUsage usage = gemini->getTokenUsage();
            this->printf("\nToken Usage:\n");
            this->printf("  API Calls:       %u\n", usage.callCount);
            this->printf("  Prompt Tokens:   %lu\n", (unsigned long) usage.promptTokens);
            this->printf("  Cand. Tokens:    %lu\n", (unsigned long) usage.candidateTokens);
            this->printf("  Cached Tokens:   %lu\n", (unsigned long) usage.cachedContentTokens);
            this->printf("  Total Tokens:    %lu\n", (unsigned long) usage.totalTokens);
            this->printf("  Last Request:    %lu total (%lu prompt, %lu cand, %lu cached)\n",
                         (unsigned long) usage.lastTotalTokens,
                         (unsigned long) usage.lastPromptTokens,
                         (unsigned long) usage.lastCandidateTokens,
                         (unsigned long) usage.lastCachedContentTokens);
        }
        return 0;
    }

    if (strcmp(argv[1], "tokens") == 0) {
        if (!gemini) {
            this->printf("Token statistics are only available for GeminiChat\n");
            return -1;
        }

        if ((argc >= 3) && (strcmp(argv[2], "reset") == 0)) {
            gemini->resetTokenUsage();
            this->printf("Token usage counters reset.\n");
            return 0;
        }

        GeminiTokenUsage usage = gemini->getTokenUsage();
        this->printf("Gemini Token Usage:\n");
        this->printf("  API Calls:       %u\n", usage.callCount);
        this->printf("  Prompt Tokens:   %lu\n", (unsigned long) usage.promptTokens);
        this->printf("  Cand. Tokens:    %lu\n", (unsigned long) usage.candidateTokens);
        this->printf("  Cached Tokens:   %lu\n", (unsigned long) usage.cachedContentTokens);
        this->printf("  Total Tokens:    %lu\n", (unsigned long) usage.totalTokens);
        this->printf("  Last Request:    %lu total (%lu prompt, %lu cand, %lu cached)\n",
                     (unsigned long) usage.lastTotalTokens,
                     (unsigned long) usage.lastPromptTokens,
                     (unsigned long) usage.lastCandidateTokens,
                     (unsigned long) usage.lastCachedContentTokens);
        return 0;
    }

    if (strcmp(argv[1], "search") == 0) {
        if (!gemini) {
            this->printf("Google Search grounding is only available for GeminiChat\n");
            return -1;
        }
        if (argc < 3) {
            this->printf("Google Search grounding is currently %s\n",
                         gemini->getWebSearch() ? "enabled" : "disabled");
            return 0;
        }
        if ((strcmp(argv[2], "on") == 0) || (strcmp(argv[2], "1") == 0) ||
            (strcmp(argv[2], "enable") == 0) || (strcmp(argv[2], "true") == 0)) {
            gemini->setWebSearch(true);
            this->printf("Google Search grounding enabled.\n");
        } else if ((strcmp(argv[2], "off") == 0) || (strcmp(argv[2], "0") == 0) ||
                   (strcmp(argv[2], "disable") == 0) || (strcmp(argv[2], "false") == 0)) {
            gemini->setWebSearch(false);
            this->printf("Google Search grounding disabled.\n");
        } else {
            this->printf("Invalid option '%s'. Use 'on' or 'off'.\n", argv[2]);
            return -1;
        }
        return 0;
    }

    if (strcmp(argv[1], "timeout") == 0) {
        if (!gemini) {
            this->printf("Timeout configuration is only available for GeminiChat\n");
            return -1;
        }
        if (argc < 3) {
            this->printf("Current HTTP timeout is %u seconds\n", gemini->getTimeout());
            return 0;
        }
        char *endptr = NULL;
        long val = strtol(argv[2], &endptr, 10);
        if ((endptr == argv[2]) || (*endptr != '\0') || (val <= 0) || (val > 300)) {
            this->printf("Invalid timeout '%s'. Specify seconds between 1 and 300.\n", argv[2]);
            return -1;
        }
        gemini->setTimeout((uint32_t) val);
        this->printf("HTTP timeout set to %ld seconds.\n", val);
        return 0;
    }

    if ((strcmp(argv[1], "max_tokens") == 0) ||
        (strcmp(argv[1], "max_output_tokens") == 0)) {
        if (!gemini) {
            this->printf("Max tokens configuration is only available for GeminiChat\n");
            return -1;
        }
        if (argc < 3) {
            this->printf("Current max output tokens is %u\n", gemini->getMaxOutputTokens());
            return 0;
        }
        char *endptr = NULL;
        long val = strtol(argv[2], &endptr, 10);
        if ((endptr == argv[2]) || (*endptr != '\0') || (val < 16) || (val > 8192)) {
            this->printf("Invalid max_tokens '%s'. Specify an integer between 16 and 8192.\n", argv[2]);
            return -1;
        }
        gemini->setMaxOutputTokens((uint32_t) val);
        this->printf("Max output tokens set to %ld.\n", val);
        return 0;
    }

    if ((strcmp(argv[1], "thinking") == 0) || (strcmp(argv[1], "thinking_budget") == 0)) {
        if (!gemini) {
            this->printf("Thinking budget configuration is only available for GeminiChat\n");
            return -1;
        }
        if (argc < 3) {
            int32_t tb = gemini->getThinkingBudget();
            if (tb == 0) {
                this->printf("Current thinking budget is 0 (disabled)\n");
            } else if (tb < 0) {
                this->printf("Current thinking budget is auto (unconstrained)\n");
            } else {
                this->printf("Current thinking budget is %d tokens\n", tb);
            }
            return 0;
        }
        if ((strcmp(argv[2], "off") == 0) || (strcmp(argv[2], "0") == 0) ||
            (strcmp(argv[2], "disable") == 0) || (strcmp(argv[2], "none") == 0)) {
            gemini->setThinkingBudget(0);
            this->printf("Thinking budget set to 0 (disabled).\n");
        } else if ((strcmp(argv[2], "on") == 0) || (strcmp(argv[2], "auto") == 0) ||
                   (strcmp(argv[2], "-1") == 0) || (strcmp(argv[2], "enable") == 0)) {
            gemini->setThinkingBudget(-1);
            this->printf("Thinking budget set to auto (unconstrained).\n");
        } else {
            char *endptr = NULL;
            long val = strtol(argv[2], &endptr, 10);
            if ((endptr == argv[2]) || (*endptr != '\0') || (val < -1) || (val > 65536)) {
                this->printf("Invalid thinking budget '%s'. Specify 0 (disabled), auto, or token count (e.g. 512).\n", argv[2]);
                return -1;
            }
            gemini->setThinkingBudget((int32_t) val);
            this->printf("Thinking budget set to %ld tokens.\n", val);
        }
        return 0;
    }

    if ((strcmp(argv[1], "temp") == 0) || (strcmp(argv[1], "temperature") == 0)) {
        if (!gemini) {
            this->printf("Temperature configuration is only available for GeminiChat\n");
            return -1;
        }
        if (argc < 3) {
            float temp = gemini->getTemperature();
            if (temp < 0.0f) {
                this->printf("Current temperature is default (model default)\n");
            } else {
                this->printf("Current temperature is %.2f\n", temp);
            }
            return 0;
        }
        if ((strcmp(argv[2], "default") == 0) || (strcmp(argv[2], "-1") == 0) ||
            (strcmp(argv[2], "auto") == 0) || (strcmp(argv[2], "none") == 0)) {
            gemini->setTemperature(-1.0f);
            this->printf("Temperature reset to model default.\n");
        } else {
            char *endptr = NULL;
            double val = strtod(argv[2], &endptr);
            if ((endptr == argv[2]) || (*endptr != '\0') || (val < 0.0) || (val > 2.0)) {
                this->printf("Invalid temperature '%s'. Specify a value between 0.0 and 2.0, or 'default'.\n", argv[2]);
                return -1;
            }
            gemini->setTemperature(static_cast<float>(val));
            this->printf("Temperature set to %.2f.\n", val);
        }
        return 0;
    }

    if (strcmp(argv[1], "top_p") == 0) {
        if (!gemini) {
            this->printf("Top-P configuration is only available for GeminiChat\n");
            return -1;
        }
        if (argc < 3) {
            float topP = gemini->getTopP();
            if (topP < 0.0f) {
                this->printf("Current top-p is default (model default)\n");
            } else {
                this->printf("Current top-p is %.2f\n", topP);
            }
            return 0;
        }
        if ((strcmp(argv[2], "default") == 0) || (strcmp(argv[2], "-1") == 0) ||
            (strcmp(argv[2], "auto") == 0) || (strcmp(argv[2], "none") == 0)) {
            gemini->setTopP(-1.0f);
            this->printf("Top-P reset to model default.\n");
        } else {
            char *endptr = NULL;
            double val = strtod(argv[2], &endptr);
            if ((endptr == argv[2]) || (*endptr != '\0') || (val < 0.0) || (val > 1.0)) {
                this->printf("Invalid top-p '%s'. Specify a value between 0.0 and 1.0, or 'default'.\n", argv[2]);
                return -1;
            }
            gemini->setTopP(static_cast<float>(val));
            this->printf("Top-P set to %.2f.\n", val);
        }
        return 0;
    }

    if (strcmp(argv[1], "top_k") == 0) {
        if (!gemini) {
            this->printf("Top-K configuration is only available for GeminiChat\n");
            return -1;
        }
        if (argc < 3) {
            int32_t topK = gemini->getTopK();
            if (topK < 0) {
                this->printf("Current top-k is default (model default)\n");
            } else {
                this->printf("Current top-k is %d\n", topK);
            }
            return 0;
        }
        if ((strcmp(argv[2], "default") == 0) || (strcmp(argv[2], "-1") == 0) ||
            (strcmp(argv[2], "auto") == 0) || (strcmp(argv[2], "none") == 0)) {
            gemini->setTopK(-1);
            this->printf("Top-K reset to model default.\n");
        } else {
            char *endptr = NULL;
            long val = strtol(argv[2], &endptr, 10);
            if ((endptr == argv[2]) || (*endptr != '\0') || (val < 1) || (val > 100)) {
                this->printf("Invalid top-k '%s'. Specify an integer between 1 and 100, or 'default'.\n", argv[2]);
                return -1;
            }
            gemini->setTopK((int32_t) val);
            this->printf("Top-K set to %ld.\n", val);
        }
        return 0;
    }

    if (strcmp(argv[1], "model") == 0) {
        if (!gemini) {
            this->printf("Model configuration is only available for GeminiChat\n");
            return -1;
        }
        if (argc < 3) {
            this->printf("Current Gemini model is '%s'\n", gemini->getModel().c_str());
            return 0;
        }
        string newModel = argv[2];
        gemini->setModel(newModel);
        this->printf("Gemini model set to '%s'.\n", newModel.c_str());
        return 0;
    }

    if ((strcmp(argv[1], "history") == 0) || (strcmp(argv[1], "max_history") == 0)) {
        if (argc < 3) {
            this->printf("Current max conversation history is %zu turns\n", bot->getMaxHistoryTurns());
            return 0;
        }
        char *endptr = NULL;
        long val = strtol(argv[2], &endptr, 10);
        if ((endptr == argv[2]) || (*endptr != '\0') || (val < 1) || (val > 100)) {
            this->printf("Invalid history '%s'. Specify turns between 1 and 100.\n", argv[2]);
            return -1;
        }
        bot->setMaxHistoryTurns((size_t) val);
        this->printf("Max conversation history set to %ld turns.\n", val);
        return 0;
    }

    if ((strcmp(argv[1], "context") == 0) || (strcmp(argv[1], "max_context") == 0) ||
        (strcmp(argv[1], "max_context_turns") == 0)) {
        if (!gemini) {
            this->printf("Context window configuration is only available for GeminiChat\n");
            return -1;
        }
        if (argc < 3) {
            this->printf("Current max context window is %d turns\n", gemini->getMaxContextTurns());
            return 0;
        }
        char *endptr = NULL;
        long val = strtol(argv[2], &endptr, 10);
        if ((endptr == argv[2]) || (*endptr != '\0') || (val < 1) || (val > 100)) {
            this->printf("Invalid context turns '%s'. Specify turns between 1 and 100.\n", argv[2]);
            return -1;
        }
        gemini->setMaxContextTurns((int32_t) val);
        this->printf("Max context window set to %ld turns.\n", val);
        return 0;
    }

    if ((strcmp(argv[1], "idle") == 0) || (strcmp(argv[1], "idle_timeout") == 0)) {
        if (argc < 3) {
            this->printf("Current conversation idle timeout is %u seconds\n", bot->getIdleTimeout());
            return 0;
        }
        char *endptr = NULL;
        long val = strtol(argv[2], &endptr, 10);
        if ((endptr == argv[2]) || (*endptr != '\0') || (val < 10) || (val > 86400)) {
            this->printf("Invalid idle timeout '%s'. Specify seconds between 10 and 86400.\n", argv[2]);
            return -1;
        }
        bot->setIdleTimeout((uint32_t) val);
        this->printf("Conversation idle timeout set to %ld seconds.\n", val);
        return 0;
    }

    if (strcmp(argv[1], "clear") == 0) {
        if (argc >= 3) {
            string nodeStr = argv[2];
            uint32_t nodeId = 0;
            if (nodeStr.front() == '!' || nodeStr.front() == '@') {
                nodeStr = nodeStr.substr(1);
            }
            if ((nodeStr.size() >= 2) && (nodeStr[0] == '0') &&
                ((nodeStr[1] == 'x') || (nodeStr[1] == 'X'))) {
                nodeStr = nodeStr.substr(2);
            }
            char *endptr = NULL;
            unsigned long val = strtoul(nodeStr.c_str(), &endptr, 16);
            if ((endptr != nodeStr.c_str()) && (*endptr == '\0')) {
                nodeId = (uint32_t) val;
            } else if (_client) {
                nodeId = _client->getId(argv[2]);
            }
            if (nodeId == 0) {
                this->printf("Cannot resolve node '%s'\n", argv[2]);
                return -1;
            }
            bot->clearConversations(nodeId);
            this->printf("Conversation history cleared for node !%08x (%s).\n",
                         nodeId, _client ? _client->getDisplayName(nodeId).c_str() : argv[2]);
        } else {
            bot->clearConversations(0);
            this->printf("All conversation histories cleared.\n");
        }
        return 0;
    }

    this->printf("Unknown chatbot subcommand '%s'. Type 'chatbot help' for usage.\n", argv[1]);
    return -1;
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
