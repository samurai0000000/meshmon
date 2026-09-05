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
    _help_list.push_back("db");
    _help_list.push_back("robot");
}

MeshMonShell::~MeshMonShell()
{

}

void MeshMonShell::setDb(shared_ptr<MeshMonDb> db)
{
    _db = db;
}

shared_ptr<MeshShell> MeshMonShell::newInstance(void)
{
    shared_ptr<MeshMonShell> shell = make_shared<MeshMonShell>();
    shell->setDb(_db);
    return shell;
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
    if ((argc > 0) && (strcmp(argv[0], "db") == 0)) {
        return db(argc, argv);
    }
    if ((argc > 0) && (strcmp(argv[0], "robot") == 0)) {
        return robot(argc, argv);
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

void MeshMonShell::printDbHelp(void)
{
    this->printf("Usage:\n");
    this->printf("  db summary [hours]           - Traffic volume, bytes, and link stats\n");
    this->printf("  db toptalkers [limit] [hours]- Top active nodes by packets and airtime\n");
    this->printf("  db neighbors [hours]         - Direct RF neighbors with SNR/RSSI stats\n");
    this->printf("  db storm [limit] [hours]     - Duplicate packet floods and echo runs\n");
    this->printf("  db asymmetry [hours]         - Bidirectional SNR with direct neighbors\n");
    this->printf("  db spof [limit] [hours]      - Critical single-point-of-failure relays\n");
    this->printf("  db drift [hours]             - Radio clock drift relative to NTP\n");
    this->printf("  db hops [hours]              - Hop count distribution histogram\n");
    this->printf("  db apps [hours]              - Traffic breakdown by portnum / app\n");
    this->printf("  db node <node> [hours]       - Node timeline, RF stats, and battery\n");
    this->printf("  db fading <node> [hours]     - Hourly SNR/RSSI trends for link fading\n");
    this->printf("  db health [hours]            - Channel utilization, TX airtime, dupes\n");
    this->printf("  db auto [subcmd]             - HomeMesh automation analytics & history\n");
    this->printf("  db query <SQL>               - Execute a custom read-only SQL query\n");
    this->printf("  db help                      - Show this help message\n");
    this->printf("Note: default [hours] is 24. [hours]=0 selects all historical data.\n");
}

int MeshMonShell::db(int argc, char **argv)
{
    if (_db == NULL) {
        this->printf("Database is not enabled or not open.\n");
        return -1;
    }

    if ((argc <= 1) ||
        (strcmp(argv[1], "help") == 0) ||
        (strcmp(argv[1], "-h") == 0) ||
        (strcmp(argv[1], "--help") == 0) ||
        (strcmp(argv[1], "-help") == 0) ||
        (strcmp(argv[1], "-?") == 0) ||
        (strcmp(argv[1], "?") == 0)) {
        printDbHelp();
        return 0;
    }

    string subcmd = argv[1];

    if (subcmd == "auto" || subcmd == "automation" || subcmd == "robot") {
        return dbAuto(argc - 1, argv + 1);
    }

    if (subcmd == "summary" || subcmd == "stats") {
        int hours = 24;
        if (argc >= 3) {
            hours = atoi(argv[2]);
        }
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        TrafficSummary s;
        if (!_db->getTrafficSummary(since, s)) {
            this->printf("Failed to query traffic summary.\n");
            return -1;
        }

        this->printf("=== Packet Traffic Summary (Last %s) ===\n",
                     hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        this->printf("Total Packets:     %u\n", s.totalPackets);
        this->printf("Total Payload:     %llu bytes\n", (unsigned long long) s.totalBytes);
        this->printf("Total Nodes:       %u (lifetime registered)\n", _db->getTotalNodeCount());
        this->printf("Total Messages:    %llu text messages\n", (unsigned long long) _db->getTotalTextMessageCount());
        float bcastPct = s.totalPackets > 0 ? (s.broadcastPackets * 100.0f / s.totalPackets) : 0.0f;
        float unicastPct = s.totalPackets > 0 ? (s.unicastPackets * 100.0f / s.totalPackets) : 0.0f;
        float directPct = s.totalPackets > 0 ? (s.directPackets * 100.0f / s.totalPackets) : 0.0f;
        float relayedPct = s.totalPackets > 0 ? (s.relayedPackets * 100.0f / s.totalPackets) : 0.0f;
        this->printf("Broadcast / Uni:   %u (%.1f%%) / %u (%.1f%%)\n",
                     s.broadcastPackets, bcastPct, s.unicastPackets, unicastPct);
        this->printf("Direct / Relayed:  %u (%.1f%%) / %u (%.1f%%)\n",
                     s.directPackets, directPct, s.relayedPackets, relayedPct);
        this->printf("DB File Size:      %.2f MB\n",
                     ((float) _db->getDbFileSize()) / (1024.0f * 1024.0f));
        return 0;
    }

    if (subcmd == "toptalkers" || subcmd == "top") {
        size_t limit = 10;
        int hours = 24;
        if (argc >= 3) {
            limit = (size_t) max(1, atoi(argv[2]));
        }
        if (argc >= 4) {
            hours = atoi(argv[3]);
        }
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        vector<NodeTrafficStat> stats;
        if (!_db->getTopTalkers(since, limit, stats)) {
            this->printf("Failed to query top talkers.\n");
            return -1;
        }

        this->printf("=== Top %zu Talkers (Last %s) ===\n",
                     limit, hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        this->printf("%-10s %-16s %8s %10s %8s %8s %8s\n",
                     "Node", "Name", "Packets", "Bytes", "AvgHops", "AvgSNR", "LastSeen");
        for (size_t i = 0; i < stats.size(); i++) {
            const NodeTrafficStat &s = stats[i];
            string name = !s.shortName.empty() ? s.shortName : s.longName;
            if (name.size() > 15) name = name.substr(0, 15);
            this->printf("%-10s %-16s %8u %10llu %8.1f %+7.1fdB %8s\n",
                         s.nodeHex.c_str(), name.c_str(), s.packetCount,
                         (unsigned long long) s.totalBytes, s.avgHops, s.avgSnr,
                         formatRelativeTime(s.lastSeen).c_str());
        }
        return 0;
    }

    if (subcmd == "neighbors" || subcmd == "direct") {
        int hours = 24;
        if (argc >= 3) {
            hours = atoi(argv[2]);
        }
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        vector<NeighborStat> stats;
        if (!_db->getNeighborStats(since, stats)) {
            this->printf("Failed to query neighbor statistics.\n");
            return -1;
        }

        this->printf("=== Direct 0-Hop RF Neighbors (%zu found in last %s) ===\n",
                     stats.size(), hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        this->printf("%-10s %-16s %8s %8s %8s %8s %8s\n",
                     "Node", "Name", "Packets", "AvgSNR", "MinSNR", "MaxSNR", "LastSeen");
        for (size_t i = 0; i < stats.size(); i++) {
            const NeighborStat &s = stats[i];
            string name = !s.shortName.empty() ? s.shortName : s.longName;
            if (name.size() > 15) name = name.substr(0, 15);
            this->printf("%-10s %-16s %8u %+7.1fdB %+7.1fdB %+7.1fdB %8s\n",
                         s.nodeHex.c_str(), name.c_str(), s.packetCount,
                         s.avgSnr, s.minSnr, s.maxSnr,
                         formatRelativeTime(s.lastSeen).c_str());
        }
        return 0;
    }

    if (subcmd == "storm" || subcmd == "storms" || subcmd == "echo") {
        size_t limit = 10;
        int hours = 24;
        if (argc >= 3) {
            limit = (size_t) max(1, atoi(argv[2]));
        }
        if (argc >= 4) {
            hours = atoi(argv[3]);
        }
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        vector<EchoStormStat> stats;
        if (!_db->getEchoStorms(since, limit, stats)) {
            this->printf("Failed to query echo storm statistics.\n");
            return -1;
        }

        this->printf("=== Duplicate Packet Floods & Echo Reverberation (Last %s) ===\n",
                     hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        if (stats.empty()) {
            this->printf("No duplicate packet storms detected in this period.\n");
            return 0;
        }
        this->printf("%-12s %-10s %8s %10s %12s\n",
                     "Packet ID", "From", "Echoes", "Hop Range", "Duration");
        for (size_t i = 0; i < stats.size(); i++) {
            const EchoStormStat &s = stats[i];
            char pktBuf[16];
            snprintf(pktBuf, sizeof(pktBuf), "!%08x", s.packetId);
            char hopBuf[16];
            snprintf(hopBuf, sizeof(hopBuf), "%u..%u hops", s.minHops, s.maxHops);
            this->printf("%-12s %-10s %8u %10s %10us\n",
                         pktBuf, s.fromHex.c_str(), s.echoCount,
                         hopBuf, s.durationSec);
        }
        return 0;
    }

    if (subcmd == "asymmetry") {
        int hours = 24;
        if (argc >= 3) {
            hours = atoi(argv[2]);
        }
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        vector<LinkAsymmetryStat> stats;
        if (!_db->getLinkAsymmetry(since, stats)) {
            this->printf("Failed to query link asymmetry.\n");
            return -1;
        }

        this->printf("=== Direct Link SNR Analysis & Noise Floor Insights (Last %s) ===\n",
                     hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        this->printf("%-10s %-16s %8s %8s %8s\n",
                     "Node", "Name", "Samples", "AvgSNR", "AvgRSSI");
        for (size_t i = 0; i < stats.size(); i++) {
            const LinkAsymmetryStat &s = stats[i];
            string name = !s.shortName.empty() ? s.shortName : s.longName;
            if (name.size() > 15) name = name.substr(0, 15);
            this->printf("%-10s %-16s %8u %+7.1fdB %7.1fdBm\n",
                         s.nodeHex.c_str(), name.c_str(), s.sampleCount,
                         s.rxSnr, s.rxRssi);
        }
        return 0;
    }

    if (subcmd == "spof" || subcmd == "relays") {
        size_t limit = 10;
        int hours = 24;
        if (argc >= 3) {
            limit = (size_t) max(1, atoi(argv[2]));
        }
        if (argc >= 4) {
            hours = atoi(argv[3]);
        }
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        vector<CriticalRepeaterStat> stats;
        if (!_db->getCriticalRepeaters(since, limit, stats)) {
            this->printf("Failed to query critical repeaters.\n");
            return -1;
        }

        this->printf("=== Critical Relay Nodes / SPOF Analysis (Last %s) ===\n",
                     hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        if (stats.empty()) {
            this->printf("No multi-hop relay nodes detected in this period.\n");
            return 0;
        }
        this->printf("%-10s %-16s %12s %8s\n",
                     "Node", "Name", "RelayedPkts", "AvgSNR");
        for (size_t i = 0; i < stats.size(); i++) {
            const CriticalRepeaterStat &s = stats[i];
            string name = !s.shortName.empty() ? s.shortName : s.longName;
            if (name.size() > 15) name = name.substr(0, 15);
            this->printf("%-10s %-16s %12u %+7.1fdB\n",
                         s.repeaterHex.c_str(), name.c_str(), s.relayCount, s.avgSnr);
        }
        return 0;
    }

    if (subcmd == "drift") {
        int hours = 24;
        if (argc >= 3) {
            hours = atoi(argv[2]);
        }
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        vector<ClockDriftStat> stats;
        if (!_db->getClockDrift(since, stats)) {
            this->printf("Failed to query clock drift statistics.\n");
            return -1;
        }

        this->printf("=== Remote Node Clock Drift vs NTP Ground Truth (Last %s) ===\n",
                     hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        if (stats.empty()) {
            this->printf("No remote node timestamp packets recorded.\n");
            return 0;
        }
        this->printf("%-10s %-16s %8s %10s %10s %10s\n",
                     "Node", "Name", "Samples", "AvgSkew", "MinSkew", "MaxSkew");
        for (size_t i = 0; i < stats.size(); i++) {
            const ClockDriftStat &s = stats[i];
            string name = !s.shortName.empty() ? s.shortName : s.longName;
            if (name.size() > 15) name = name.substr(0, 15);
            this->printf("%-10s %-16s %8u %+9ds %+9ds %+9ds\n",
                         s.nodeHex.c_str(), name.c_str(), s.sampleCount,
                         s.avgSkewSec, s.minSkewSec, s.maxSkewSec);
        }
        return 0;
    }

    if (subcmd == "hops") {
        int hours = 24;
        if (argc >= 3) {
            hours = atoi(argv[2]);
        }
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        vector<HopStat> stats;
        if (!_db->getHopDistribution(since, stats)) {
            this->printf("Failed to query hop distribution.\n");
            return -1;
        }

        this->printf("=== Hop Count Distribution (Last %s) ===\n",
                     hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        this->printf("%-6s %10s %10s\n", "Hops", "Packets", "Percentage");
        for (size_t i = 0; i < stats.size(); i++) {
            this->printf("%-6d %10u %9.1f%%\n",
                         stats[i].hops, stats[i].packetCount, stats[i].pctShare);
        }
        return 0;
    }

    if (subcmd == "apps") {
        int hours = 24;
        if (argc >= 3) {
            hours = atoi(argv[2]);
        }
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        vector<AppStat> stats;
        if (!_db->getPortnumDistribution(since, stats)) {
            this->printf("Failed to query application distribution.\n");
            return -1;
        }

        this->printf("=== Application / PortNum Traffic Composition (Last %s) ===\n",
                     hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        this->printf("%-32s %10s %12s %8s\n", "Application", "Packets", "Bytes", "Share");
        for (size_t i = 0; i < stats.size(); i++) {
            this->printf("%-32s %10u %12llu %7.1f%%\n",
                         stats[i].appName.c_str(), stats[i].packetCount,
                         (unsigned long long) stats[i].totalBytes, stats[i].pctShare);
        }
        return 0;
    }

    if (subcmd == "node") {
        if (argc < 3) {
            this->printf("Usage: db node <node> [hours]\n");
            return -1;
        }
        uint32_t nodeId = resolveNode(argv[2]);
        if (nodeId == 0) {
            this->printf("Cannot resolve node '%s'\n", argv[2]);
            return -1;
        }
        int hours = 24;
        if (argc >= 4) {
            hours = atoi(argv[3]);
        }
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        NodeDetail d;
        if (!_db->getNodeDetail(nodeId, since, d)) {
            this->printf("Failed to query node detail.\n");
            return -1;
        }

        this->printf("=== Node Profile: %s (%s) ===\n",
                     d.nodeHex.c_str(), !d.longName.empty() ? d.longName.c_str() : "unknown");
        this->printf("Short Name:     %s\n", !d.shortName.empty() ? d.shortName.c_str() : "none");
        this->printf("First Seen:     %s ago\n", formatRelativeTime(d.firstSeen).c_str());
        this->printf("Last Seen:      %s ago\n", formatRelativeTime(d.lastSeen).c_str());
        this->printf("Packets (%s):   %u\n",
                     hours > 0 ? (to_string(hours) + "h").c_str() : "all", d.totalPackets);
        if (d.totalPackets > 0) {
            this->printf("SNR (Avg/Min/Max): %+0.1f / %+0.1f / %+0.1f dB\n",
                         d.avgSnr, d.minSnr, d.maxSnr);
            this->printf("Average RSSI:   %0.1f dBm\n", d.avgRssi);
            this->printf("Last Hop Count: %d hops\n", d.lastHops);
        }
        if (d.hasTelemetry) {
            this->printf("Battery / Volt: %d%% / %.2fV\n", d.lastBattery, d.lastVoltage);
            this->printf("Channel / Air:  %.1f%% / %.1f%%\n", d.lastChannelUtil, d.lastAirUtilTx);
        }
        if (d.hasPosition) {
            this->printf("Last Position:  %.6f, %.6f (alt %dm)\n",
                         d.lastLat, d.lastLon, d.lastAlt);
        }
        return 0;
    }

    if (subcmd == "fading") {
        if (argc < 3) {
            this->printf("Usage: db fading <node> [hours]\n");
            return -1;
        }
        uint32_t nodeId = resolveNode(argv[2]);
        if (nodeId == 0) {
            this->printf("Cannot resolve node '%s'\n", argv[2]);
            return -1;
        }
        int hours = 24;
        if (argc >= 4) {
            hours = atoi(argv[3]);
        }
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        vector<LinkFadingPoint> points;
        if (!_db->getLinkFading(nodeId, since, points)) {
            this->printf("Failed to query link fading data.\n");
            return -1;
        }

        this->printf("=== Hourly Link Quality / Fading Curve for !%08x ===\n", nodeId);
        if (points.empty()) {
            this->printf("No packet records found for this node in the specified timeframe.\n");
            return 0;
        }
        this->printf("%-20s %8s %8s %8s %8s %8s\n",
                     "Time (UTC)", "Packets", "AvgSNR", "MinSNR", "MaxSNR", "AvgRSSI");
        for (size_t i = 0; i < points.size(); i++) {
            const LinkFadingPoint &p = points[i];
            struct tm tm;
            char timeBuf[32];
            gmtime_r(&p.timestamp, &tm);
            strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:00", &tm);
            this->printf("%-20s %8u %+7.1fdB %+7.1fdB %+7.1fdB %7.1fdBm\n",
                         timeBuf, p.count, p.avgSnr, p.minSnr, p.maxSnr, p.avgRssi);
        }
        return 0;
    }

    if (subcmd == "health") {
        int hours = 24;
        if (argc >= 3) {
            hours = atoi(argv[2]);
        }
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        ChannelHealthStat h;
        if (!_db->getChannelHealth(since, h)) {
            this->printf("Failed to query channel health.\n");
            return -1;
        }

        this->printf("=== Mesh & RF Channel Health Statistics (Last %s) ===\n",
                     hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        this->printf("Avg Channel Utilization: %.1f%% (Peak: %.1f%%)\n",
                     h.avgChannelUtil, h.maxChannelUtil);
        this->printf("Avg Node TX Airtime:     %.1f%% (Peak: %.1f%%)\n",
                     h.avgAirUtilTx, h.maxAirUtilTx);
        this->printf("Total Packets Received:  %u\n", h.totalPackets);
        float dupePct = h.totalPackets > 0 ? (h.duplicatePackets * 100.0f / h.totalPackets) : 0.0f;
        this->printf("Duplicate Echo Packets:  %u (%.1f%%)\n",
                     h.duplicatePackets, dupePct);
        return 0;
    }

    if (subcmd == "query" || subcmd == "sql") {
        if (argc < 3) {
            this->printf("Usage: db query <SQL statement>\n");
            return -1;
        }

        string sql;
        for (int i = 2; i < argc; i++) {
            if (i > 2) sql += " ";
            sql += argv[i];
        }

        QueryResult r;
        if (!_db->executeRawQuery(sql, r)) {
            this->printf("SQL Error: %s\n", r.error.c_str());
            return -1;
        }

        for (size_t c = 0; c < r.columns.size(); c++) {
            if (c > 0) this->printf(" | ");
            this->printf("%s", r.columns[c].c_str());
        }
        this->printf("\n");
        for (size_t row = 0; row < r.rows.size(); row++) {
            for (size_t col = 0; col < r.rows[row].size(); col++) {
                if (col > 0) this->printf(" | ");
                this->printf("%s", r.rows[row][col].c_str());
            }
            this->printf("\n");
        }
        this->printf("(%zu rows returned)\n", r.rows.size());
        return 0;
    }

    this->printf("Unknown db subcommand '%s'. Type 'db help' for usage.\n", argv[1]);
    return -1;
}

void MeshMonShell::printDbAutoHelp(void)
{
    this->printf("Usage:\n");
    this->printf("  db auto pump [hours]         - Fish/Upper pump runtimes, duty, moisture\n");
    this->printf("  db auto roof [hours]         - RF PA runtime, WiFi metrics, CPU temp\n");
    this->printf("  db auto room [node] [hours]  - AC/TV operational hours, board temp\n");
    this->printf("  db auto latency [node] [hrs] - Response latency (RTT) trend & stats\n");
    this->printf("  db auto history [limit]      - Recent automation commands audit trail\n");
    this->printf("  db auto help                 - Show this help message\n");
}

int MeshMonShell::dbAuto(int argc, char **argv)
{
    if (_db == NULL) {
        this->printf("Database is not enabled or not open.\n");
        return -1;
    }

    if ((argc <= 1) ||
        (strcmp(argv[1], "help") == 0) ||
        (strcmp(argv[1], "-h") == 0) ||
        (strcmp(argv[1], "--help") == 0) ||
        (strcmp(argv[1], "?") == 0)) {
        printDbAutoHelp();
        return 0;
    }

    string subcmd = argv[1];

    if (subcmd == "pump" || subcmd == "meshpump") {
        int hours = 24;
        if (argc >= 3) hours = atoi(argv[2]);
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        PumpAnalytics a;
        if (!_db->getPumpAnalytics(0, since, a)) {
            this->printf("Failed to query meshpump analytics.\n");
            return -1;
        }

        this->printf("=== MeshPump Automation Analytics (Last %s) ===\n",
                     hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        this->printf("Fish Tank Pump Runtime:  %u sec (%.1f%% duty cycle)\n", a.fishRunSec, a.fishDutyPct);
        this->printf("Upper Pump Runs:         %u cycles\n", a.upRunCount);
        this->printf("Upper Pump Runtime:      %u sec\n", a.upRunSec);
        this->printf("Cutoff Auto-Triggers:    %u times\n", a.upCutoffTriggers);
        this->printf("Soil Moisture Reports:   %u events (avg %.1f%%)\n", a.moistureEvents, a.avgMoisture);
        this->printf("Total Commands / ACKs:   %u / %u\n", a.totalCommands, a.ackedCommands);
        return 0;
    }

    if (subcmd == "roof" || subcmd == "meshroof") {
        int hours = 24;
        if (argc >= 3) hours = atoi(argv[2]);
        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        RoofAnalytics a;
        if (!_db->getRoofAnalytics(0, since, a)) {
            this->printf("Failed to query meshroof analytics.\n");
            return -1;
        }

        this->printf("=== MeshRoof Automation Analytics (Last %s) ===\n",
                     hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        this->printf("RF Power Amp Runtime:    %u sec (%.1f%% active)\n", a.amplifyRunSec, a.amplifyDutyPct);
        this->printf("WiFi Reports / RSSI:     %u events (avg %.1f dBm, min %.1f, max %.1f)\n",
                     a.wifiReports, a.avgWifiRssi, a.minWifiRssi, a.maxWifiRssi);
        this->printf("ESP32 CPU Temperature:   avg %.1f C (max %.1f C)\n", a.avgCpuTemp, a.maxCpuTemp);
        this->printf("Reset / Reboot Events:   %u events\n", a.resetEvents);
        this->printf("Total Commands Sent:     %u\n", a.totalCommands);
        return 0;
    }

    if (subcmd == "room" || subcmd == "meshroom") {
        uint32_t targetNode = 0;
        int hours = 24;
        if (argc == 3) {
            if (isdigit(argv[2][0])) {
                hours = atoi(argv[2]);
            } else {
                targetNode = resolveNode(argv[2]);
            }
        } else if (argc >= 4) {
            targetNode = resolveNode(argv[2]);
            hours = atoi(argv[3]);
        }

        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        RoomAnalytics a;
        if (!_db->getRoomAnalytics(targetNode, since, a)) {
            this->printf("Failed to query meshroom analytics.\n");
            return -1;
        }

        this->printf("=== MeshRoom Automation Analytics (Last %s) ===\n",
                     hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        this->printf("AC Active Runtime:       %u sec (%.1f%% duty)\n", a.acRunSec, a.acDutyPct);
        this->printf("AC Target Temp:          avg %.1f C\n", a.avgAcTargetTemp);
        this->printf("TV Active Runtime:       %u sec (%.1f%% active)\n", a.tvRunSec, a.tvDutyPct);
        this->printf("TV Channel Changes:      %u times\n", a.tvChanChanges);
        this->printf("RP2040 Board Temp:       avg %.1f C (max %.1f C)\n", a.avgBoardTemp, a.maxBoardTemp);
        this->printf("Total Commands Sent:     %u\n", a.totalCommands);
        return 0;
    }

    if (subcmd == "latency" || subcmd == "rtt") {
        uint32_t targetNode = 0;
        int hours = 24;
        if (argc == 3) {
            if (isdigit(argv[2][0])) {
                hours = atoi(argv[2]);
            } else {
                targetNode = resolveNode(argv[2]);
            }
        } else if (argc >= 4) {
            targetNode = resolveNode(argv[2]);
            hours = atoi(argv[3]);
        }

        time_t since = (hours > 0) ? (time(NULL) - (hours * 3600)) : 0;
        vector<LatencyTrendPoint> points;
        if (!_db->getLatencyTrend(targetNode, since, points)) {
            this->printf("Failed to query latency trend.\n");
            return -1;
        }

        if (points.empty()) {
            this->printf("No latency / RTT samples recorded for the specified period.\n");
            return 0;
        }

        this->printf("=== Automation Response Latency (RTT) Trend (Last %s) ===\n",
                     hours > 0 ? (to_string(hours) + " hours").c_str() : "All time");
        this->printf("%-16s | %-8s | %-8s | %-8s | %-6s\n",
                     "Time (Local)", "Avg RTT", "Min RTT", "Max RTT", "Count");
        this->printf("-----------------+----------+----------+----------+-------\n");

        for (size_t i = 0; i < points.size(); i++) {
            const LatencyTrendPoint &pt = points[i];
            char timeBuf[32];
            struct tm tm;
            localtime_r(&pt.timestamp, &tm);
            strftime(timeBuf, sizeof(timeBuf), "%m-%d %H:%M", &tm);

            char avgBuf[16], minBuf[16], maxBuf[16];
            snprintf(avgBuf, sizeof(avgBuf), "%.1f ms", pt.avgRtt);
            snprintf(minBuf, sizeof(minBuf), "%u ms", pt.minRtt);
            snprintf(maxBuf, sizeof(maxBuf), "%u ms", pt.maxRtt);

            this->printf("%-16s | %-8s | %-8s | %-8s | %-6u\n",
                         timeBuf, avgBuf, minBuf, maxBuf, pt.count);
        }
        return 0;
    }

    if (subcmd == "history") {
        size_t limit = 20;
        if (argc >= 3) limit = (size_t) atoi(argv[2]);
        vector<AutomationEvent> events;
        if (!_db->getAutomationHistory(limit, events)) {
            this->printf("Failed to query automation history.\n");
            return -1;
        }

        this->printf("=== Recent Automation Commands & Events (Last %zu) ===\n", events.size());
        this->printf("%-8s | %-9s | %-8s | %-4s | %-16s | %-10s | %-5s\n",
                     "Time", "Node", "App", "Dir", "Command", "Param", "Stat");
        this->printf("---------+-----------+----------+------+------------------+------------+------\n");

        for (size_t i = 0; i < events.size(); i++) {
            const AutomationEvent &e = events[i];
            char timeBuf[32];
            struct tm tm;
            localtime_r(&e.meshmonTime, &tm);
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm);

            string dirShort = (e.direction == "TX_CMD") ? "TX" : (e.direction == "RX_STATE" ? "RX" : e.direction.substr(0, 4));
            string statShort = (e.status == "EXECUTED") ? "OK" : (e.status == "FAILED" ? "FAIL" : e.status.substr(0, 5));

            this->printf("%-8s | %-9s | %-8s | %-4s | %-16s | %-10s | %-5s\n",
                         timeBuf, e.nodeHex.c_str(), e.deviceType.substr(0, 8).c_str(),
                         dirShort.c_str(), e.commandName.substr(0, 16).c_str(),
                         e.actionParam.substr(0, 10).c_str(), statShort.c_str());
        }
        return 0;
    }

    this->printf("Unknown db auto subcommand '%s'. Type 'db auto help' for usage.\n", argv[1]);
    return -1;
}

void MeshMonShell::printRobotHelp(void)
{
    this->printf("Usage:\n");
    this->printf("  robot                    - Show live HomeMesh automation fleet status table\n");
    this->printf("  robot <node>             - Show detailed operational status for a specific node\n");
    this->printf("  robot help               - Show this help message\n");
}

int MeshMonShell::robot(int argc, char **argv)
{
    if ((argc >= 2) &&
        ((strcmp(argv[1], "help") == 0) ||
         (strcmp(argv[1], "-h") == 0) ||
         (strcmp(argv[1], "--help") == 0) ||
         (strcmp(argv[1], "?") == 0))) {
        printRobotHelp();
        return 0;
    }

    if (argc <= 1) {
        printFleetRobotStatus();
        return 0;
    }

    string nodeArg;
    for (int i = 1; i < argc; i++) {
        if (i > 1) nodeArg += " ";
        nodeArg += argv[i];
    }

    uint32_t nodeId = resolveNode(nodeArg);
    if (nodeId == 0xffffffffU) {
        this->printf("Node '%s' not found.\n", nodeArg.c_str());
        return -1;
    }

    printNodeRobotStatus(nodeId);
    return 0;
}

void MeshMonShell::printFleetRobotStatus(void)
{
    shared_ptr<MeshMon> meshmon = dynamic_pointer_cast<MeshMon>(_client);
    if (meshmon == NULL) {
        this->printf("MeshMon core is not available.\n");
        return;
    }

    map<uint32_t, AutomationNode> nodes = meshmon->getAutomationNodes();
    if (nodes.empty()) {
        this->printf("No HomeMesh automation nodes discovered yet.\n");
        return;
    }

    this->printf("=== HomeMesh Smart Automation Fleet (%zu Nodes) ===\n", nodes.size());
    this->printf("%-9s | %-8s | %-8s | %-4s | %-7s | %-20s | %-6s\n",
                 "Node", "Name", "App", "Stat", "Uptime", "State / Telemetry", "Seen");
    this->printf("----------+----------+----------+------+---------+----------------------+--------\n");

    for (map<uint32_t, AutomationNode>::const_iterator it = nodes.begin(); it != nodes.end(); ++it) {
        const AutomationNode &n = it->second;
        string statusStr = n.online ? "ON" : "OFF";
        string upStr = (n.uptimeSec > 0) ? (to_string(n.uptimeSec / 3600) + "h " + to_string((n.uptimeSec % 3600) / 60) + "m") : "-";
        string stateSummary = "-";

        if (n.deviceType == "meshpump") {
            stateSummary = "Fish:" + string(n.fishPumpState ? "ON" : "OFF") +
                           " Up:" + string(n.upPumpState ? "ON" : "OFF");
            if (n.soilMoisture > 0.0f) {
                char buf[16];
                snprintf(buf, sizeof(buf), " M:%.0f%%", n.soilMoisture);
                stateSummary += buf;
            }
        } else if (n.deviceType == "meshroof") {
            stateSummary = "Amp:" + string(n.amplifyState ? "ON" : "OFF");
            if (n.cpuTempC > 0.0f) {
                char buf[16];
                snprintf(buf, sizeof(buf), " CPU:%.1fC", n.cpuTempC);
                stateSummary += buf;
            }
        } else if (n.deviceType == "meshroom") {
            stateSummary = "AC:" + string(n.acPower ? "ON" : "OFF");
            if (n.acPower) {
                char buf[16];
                snprintf(buf, sizeof(buf), "(%.0fC)", n.acTargetTemp);
                stateSummary += buf;
            }
            stateSummary += " TV:" + string(n.tvPower ? "ON" : "OFF");
        }

        string lastSeenStr = formatRelativeTime(n.lastSeen);

        this->printf("%-9s | %-8s | %-8s | %-4s | %-7s | %-20s | %-6s\n",
                     n.nodeHex.c_str(), n.shortName.substr(0, 8).c_str(),
                     n.deviceType.empty() ? "-" : n.deviceType.substr(0, 8).c_str(),
                     statusStr.c_str(), upStr.substr(0, 7).c_str(),
                     stateSummary.substr(0, 20).c_str(), lastSeenStr.substr(0, 6).c_str());
    }
}

void MeshMonShell::printNodeRobotStatus(uint32_t nodeId)
{
    shared_ptr<MeshMon> meshmon = dynamic_pointer_cast<MeshMon>(_client);
    if (meshmon == NULL) {
        this->printf("MeshMon core is not available.\n");
        return;
    }

    AutomationNode node;
    if (!meshmon->getAutomationNode(nodeId, node)) {
        this->printf("Node !%08x is not registered as a HomeMesh automation device.\n", nodeId);
        return;
    }

    this->printf("=== HomeMesh Node Status: %s (!%08x) ===\n",
                 node.shortName.empty() ? node.nodeHex.c_str() : node.shortName.c_str(),
                 nodeId);
    this->printf("  Device Type:     %s\n", node.deviceType.empty() ? "unknown" : node.deviceType.c_str());
    this->printf("  Firmware:        %s (HW: %s)\n",
                 node.version.empty() ? "-" : node.version.c_str(),
                 node.hardware.empty() ? "-" : node.hardware.c_str());
    if (!node.capabilities.empty()) {
        this->printf("  Capabilities:    %s\n", node.capabilities.c_str());
    }
    this->printf("  Liveness:        %s (Last seen: %s)\n",
                 node.online ? "ONLINE" : "OFFLINE", formatRelativeTime(node.lastSeen).c_str());
    this->printf("  Uptime:          %u seconds (%u reboot count)\n", node.uptimeSec, node.rebootCount);
    if (node.lastRttMs > 0 || node.avgRttMs > 0) {
        this->printf("  Response RTT:    %u ms (Avg: %u ms, %u samples)\n",
                     node.lastRttMs, node.avgRttMs, node.rttSampleCount);
    }
    this->printf("  HA Discovered:   %s\n", node.haDiscovered ? "Yes" : "No");

    if (node.deviceType == "meshpump") {
        this->printf("  MeshPump States:\n");
        this->printf("    Fish Tank Pump:     %s\n", node.fishPumpState ? "ON" : "OFF");
        this->printf("    Upper Plant Pump:   %s (Cutoff: %u sec)\n", node.upPumpState ? "ON" : "OFF", node.upPumpCutoffSec);
        this->printf("    Soil Moisture:      %.1f %%\n", node.soilMoisture);
        this->printf("    Water Reservoir:    %s\n", node.reservoirEmpty ? "EMPTY (Warning)" : "OK");
        if (!node.ledMessage.empty()) {
            this->printf("    LED Display:        '%s'\n", node.ledMessage.c_str());
        }
    } else if (node.deviceType == "meshroof") {
        this->printf("  MeshRoof States:\n");
        this->printf("    RF Power Amplifier: %s\n", node.amplifyState ? "ON" : "OFF");
        if (!node.wifiStatus.empty()) {
            this->printf("    WiFi Status:        %s\n", node.wifiStatus.c_str());
        }
        if (!node.ipAddress.empty()) {
            this->printf("    IP Address:         %s\n", node.ipAddress.c_str());
        }
        if (node.cpuTempC > 0.0f) {
            this->printf("    ESP32 CPU Temp:     %.1f C\n", node.cpuTempC);
        }
        this->printf("    Reset Count:        %u\n", node.resetCount);
    } else if (node.deviceType == "meshroom") {
        this->printf("  MeshRoom States:\n");
        this->printf("    AC Power:           %s\n", node.acPower ? "ON" : "OFF");
        this->printf("    AC Mode / Target:   %s, %.1f C\n", node.acMode.c_str(), node.acTargetTemp);
        this->printf("    AC Fan / Vane:      %s, %s\n", node.acFan.c_str(), node.acVane.c_str());
        this->printf("    TV Power / Volume:  %s, Vol %d, Chan %d\n", node.tvPower ? "ON" : "OFF", node.tvVolume, node.tvChannel);
        this->printf("    TV Mute / Input:    %s, %s\n", node.tvMute ? "MUTED" : "UNMUTED", node.tvInput.c_str());
        if (node.boardTempC > 0.0f) {
            this->printf("    RP2040 Board Temp:  %.1f C\n", node.boardTempC);
        }
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
