/*
 * MeshMonShell.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <MeshMon.hxx>
#include <MqttClient.hxx>
#include <MeshMonShell.hxx>
#include <Calibration.hxx>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cerrno>

MeshMonShell::MeshMonShell(shared_ptr<MeshClient> client)
    : MeshShell(client)
{

}

MeshMonShell::~MeshMonShell()
{

}

shared_ptr<MeshShell> MeshMonShell::newInstance(void)
{
    return make_shared<MeshMonShell>();
}

int MeshMonShell::help(int argc, char **argv)
{
    MeshShell::help(argc, argv);
    this->printf("\tcalib\n");
    return 0;
}

int MeshMonShell::system(int argc, char **argv)
{
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

int MeshMonShell::unknown_command(int argc, char **argv)
{
    if ((argc > 0) && (strcmp(argv[0], "calib") == 0)) {
        return calib(argc, argv);
    }

    return MeshShell::unknown_command(argc, argv);
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
    this->printf("  calib reload                           - Reload calibration from ~/.meshmon\n");
    this->printf("Sensors: temp (temperature, \u00b0C), hum (humidity, %%), press (pressure, hPa)\n");
    this->printf("Nodes: node shortname, hex ID (e.g. 2bf941d4, !2bf941d4) or 'default'\n");

    return 0;
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
