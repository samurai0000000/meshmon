/*
 * Calibration.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include "Calibration.hxx"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>

using namespace libconfig;

// CalibrationCurve implementation

CalibrationCurve::CalibrationCurve()
{

}

CalibrationCurve::~CalibrationCurve()
{

}

double CalibrationCurve::calibrate(double raw) const
{
    if (_points.empty()) {
        return raw;
    }

    if (_points.size() == 1) {
        return raw + (_points[0].cal - _points[0].raw);
    }

    // Extrapolate below the lowest point
    if (raw <= _points.front().raw) {
        double dx = _points[1].raw - _points[0].raw;
        if (std::abs(dx) < 1e-9) {
            return _points[0].cal;
        }
        double slope = (_points[1].cal - _points[0].cal) / dx;
        return _points[0].cal + slope * (raw - _points[0].raw);
    }

    // Extrapolate above the highest point
    if (raw >= _points.back().raw) {
        size_t n = _points.size();
        double dx = _points[n - 1].raw - _points[n - 2].raw;
        if (std::abs(dx) < 1e-9) {
            return _points[n - 1].cal;
        }
        double slope = (_points[n - 1].cal - _points[n - 2].cal) / dx;
        return _points[n - 1].cal + slope * (raw - _points[n - 1].raw);
    }

    // Interpolate between points
    for (size_t i = 0; i < _points.size() - 1; i++) {
        if ((raw >= _points[i].raw) && (raw <= _points[i + 1].raw)) {
            double dx = _points[i + 1].raw - _points[i].raw;
            if (std::abs(dx) < 1e-9) {
                return _points[i].cal;
            }
            double t = (raw - _points[i].raw) / dx;
            return _points[i].cal + t * (_points[i + 1].cal - _points[i].cal);
        }
    }

    return raw;
}

void CalibrationCurve::setPoint(double raw, double cal)
{
    for (vector<CalibrationPoint>::iterator it = _points.begin();
         it != _points.end(); it++) {
        if (std::abs(it->raw - raw) < 1e-4) {
            it->cal = cal;
            return;
        }
    }

    _points.push_back(CalibrationPoint(raw, cal));
    std::sort(_points.begin(), _points.end(),
              [](const CalibrationPoint &a, const CalibrationPoint &b) {
                  return a.raw < b.raw;
              });
}

bool CalibrationCurve::delPoint(double raw)
{
    for (vector<CalibrationPoint>::iterator it = _points.begin();
         it != _points.end(); it++) {
        if (std::abs(it->raw - raw) < 1e-4) {
            _points.erase(it);
            return true;
        }
    }

    return false;
}

void CalibrationCurve::clear(void)
{
    _points.clear();
}

bool CalibrationCurve::empty(void) const
{
    return _points.empty();
}

const vector<CalibrationPoint> &CalibrationCurve::getPoints(void) const
{
    return _points;
}

// NodeCalibration implementation

NodeCalibration::NodeCalibration()
{

}

NodeCalibration::~NodeCalibration()
{

}

CalibrationCurve *NodeCalibration::getCurve(SensorType type)
{
    switch (type) {
    case SENSOR_TEMPERATURE:
        return &temperature;
    case SENSOR_HUMIDITY:
        return &humidity;
    case SENSOR_PRESSURE:
        return &pressure;
    default:
        return NULL;
    }
}

const CalibrationCurve *NodeCalibration::getCurve(SensorType type) const
{
    switch (type) {
    case SENSOR_TEMPERATURE:
        return &temperature;
    case SENSOR_HUMIDITY:
        return &humidity;
    case SENSOR_PRESSURE:
        return &pressure;
    default:
        return NULL;
    }
}

bool NodeCalibration::empty(void) const
{
    return temperature.empty() && humidity.empty() && pressure.empty();
}

void NodeCalibration::clear(void)
{
    temperature.clear();
    humidity.clear();
    pressure.clear();
}

// Calibration implementation

Calibration::Calibration()
{

}

Calibration::~Calibration()
{

}

float Calibration::calibrateTemperature(uint32_t nodeId, float raw) const
{
    return calibrate(nodeId, SENSOR_TEMPERATURE, raw);
}

float Calibration::calibrateHumidity(uint32_t nodeId, float raw) const
{
    return calibrate(nodeId, SENSOR_HUMIDITY, raw);
}

float Calibration::calibratePressure(uint32_t nodeId, float raw) const
{
    return calibrate(nodeId, SENSOR_PRESSURE, raw);
}

float Calibration::calibrate(uint32_t nodeId, SensorType type, float raw) const
{
    string key = normalizeNodeKey(nodeId);
    return calibrate(key, type, raw);
}

float Calibration::calibrate(const string &nodeKey, SensorType type, float raw) const
{
    lock_guard<mutex> lock(_mutex);
    string key = normalizeNodeKey(nodeKey);

    map<string, NodeCalibration>::const_iterator it = _nodes.find(key);
    if (it != _nodes.end()) {
        const CalibrationCurve *curve = it->second.getCurve(type);
        if ((curve != NULL) && !curve->empty()) {
            return (float) curve->calibrate(raw);
        }
    }

    // Fall back to default calibration if node specific is not configured
    if (key != "default") {
        it = _nodes.find("default");
        if (it != _nodes.end()) {
            const CalibrationCurve *curve = it->second.getCurve(type);
            if ((curve != NULL) && !curve->empty()) {
                return (float) curve->calibrate(raw);
            }
        }
    }

    return raw;
}

void Calibration::setPoint(const string &nodeKey, SensorType type,
                           double raw, double cal)
{
    lock_guard<mutex> lock(_mutex);
    string key = normalizeNodeKey(nodeKey);
    CalibrationCurve *curve = _nodes[key].getCurve(type);
    if (curve != NULL) {
        curve->setPoint(raw, cal);
    }
}

bool Calibration::delPoint(const string &nodeKey, SensorType type, double raw)
{
    lock_guard<mutex> lock(_mutex);
    string key = normalizeNodeKey(nodeKey);
    map<string, NodeCalibration>::iterator it = _nodes.find(key);
    if (it == _nodes.end()) {
        return false;
    }

    CalibrationCurve *curve = it->second.getCurve(type);
    if (curve == NULL) {
        return false;
    }

    bool res = curve->delPoint(raw);
    if (it->second.empty()) {
        _nodes.erase(it);
    }

    return res;
}

bool Calibration::clear(const string &nodeKey, SensorType type)
{
    lock_guard<mutex> lock(_mutex);
    string key = normalizeNodeKey(nodeKey);
    map<string, NodeCalibration>::iterator it = _nodes.find(key);
    if (it == _nodes.end()) {
        return false;
    }

    CalibrationCurve *curve = it->second.getCurve(type);
    if (curve != NULL) {
        curve->clear();
    }

    if (it->second.empty()) {
        _nodes.erase(it);
    }

    return true;
}

bool Calibration::clearNode(const string &nodeKey)
{
    lock_guard<mutex> lock(_mutex);
    string key = normalizeNodeKey(nodeKey);
    return _nodes.erase(key) > 0;
}

void Calibration::clearAll(void)
{
    lock_guard<mutex> lock(_mutex);
    _nodes.clear();
}

bool Calibration::hasNode(const string &nodeKey) const
{
    lock_guard<mutex> lock(_mutex);
    string key = normalizeNodeKey(nodeKey);
    return _nodes.find(key) != _nodes.end();
}

bool Calibration::getNode(const string &nodeKey, NodeCalibration &out) const
{
    lock_guard<mutex> lock(_mutex);
    string key = normalizeNodeKey(nodeKey);
    map<string, NodeCalibration>::const_iterator it = _nodes.find(key);
    if (it != _nodes.end()) {
        out = it->second;
        return true;
    }
    return false;
}

map<string, NodeCalibration> Calibration::getAll(void) const
{
    lock_guard<mutex> lock(_mutex);
    return _nodes;
}

void Calibration::setConfigPath(const string &path)
{
    lock_guard<mutex> lock(_mutex);
    _configPath = path;
}

const string &Calibration::getConfigPath(void) const
{
    lock_guard<mutex> lock(_mutex);
    return _configPath;
}

static void parseCurveFromSetting(CalibrationCurve &curve, const Setting &setting)
{
    curve.clear();

    if (setting.getType() == Setting::TypeList || setting.getType() == Setting::TypeArray) {
        for (int i = 0; i < setting.getLength(); i++) {
            const Setting &elem = setting[i];
            double raw = 0.0, cal = 0.0;
            bool valid = false;

            if (elem.getType() == Setting::TypeGroup) {
                if (elem.exists("raw") && elem.exists("cal")) {
                    try {
                        raw = (double) elem["raw"];
                        cal = (double) elem["cal"];
                        valid = true;
                    } catch (const SettingTypeException &) {
                        try {
                            int r = elem["raw"];
                            int c = elem["cal"];
                            raw = (double) r;
                            cal = (double) c;
                            valid = true;
                        } catch (...) {
                        }
                    }
                }
            } else if (elem.getType() == Setting::TypeList || elem.getType() == Setting::TypeArray) {
                if (elem.getLength() >= 2) {
                    try {
                        raw = (double) elem[0];
                        cal = (double) elem[1];
                        valid = true;
                    } catch (...) {
                    }
                }
            }

            if (valid) {
                curve.setPoint(raw, cal);
            }
        }
    }
}

static void parseNodeSetting(NodeCalibration &nodeCal, const Setting &group)
{
    if (group.exists("temperature")) {
        parseCurveFromSetting(nodeCal.temperature, group["temperature"]);
    } else if (group.exists("temp")) {
        parseCurveFromSetting(nodeCal.temperature, group["temp"]);
    }

    if (group.exists("humidity")) {
        parseCurveFromSetting(nodeCal.humidity, group["humidity"]);
    } else if (group.exists("hum")) {
        parseCurveFromSetting(nodeCal.humidity, group["hum"]);
    }

    if (group.exists("pressure")) {
        parseCurveFromSetting(nodeCal.pressure, group["pressure"]);
    } else if (group.exists("press")) {
        parseCurveFromSetting(nodeCal.pressure, group["press"]);
    }
}

bool Calibration::readConfig(const Config &cfg)
{
    lock_guard<mutex> lock(_mutex);
    const Setting &root = cfg.getRoot();

    _nodes.clear();

    if (!root.exists("calibration")) {
        return true;
    }

    const Setting &calSetting = root["calibration"];

    if (calSetting.getType() == Setting::TypeList || calSetting.getType() == Setting::TypeArray) {
        for (int i = 0; i < calSetting.getLength(); i++) {
            const Setting &elem = calSetting[i];
            if (elem.getType() == Setting::TypeGroup) {
                string nodeKey = "default";
                if (elem.exists("node")) {
                    string s;
                    if (elem.lookupValue("node", s)) {
                        nodeKey = s;
                    }
                }
                nodeKey = normalizeNodeKey(nodeKey);
                NodeCalibration nodeCal;
                parseNodeSetting(nodeCal, elem);
                if (!nodeCal.empty()) {
                    _nodes[nodeKey] = nodeCal;
                }
            }
        }
    } else if (calSetting.getType() == Setting::TypeGroup) {
        for (int i = 0; i < calSetting.getLength(); i++) {
            const Setting &elem = calSetting[i];
            string nodeKey = normalizeNodeKey(elem.getName());
            if (elem.getType() == Setting::TypeGroup) {
                NodeCalibration nodeCal;
                parseNodeSetting(nodeCal, elem);
                if (!nodeCal.empty()) {
                    _nodes[nodeKey] = nodeCal;
                }
            }
        }
    }

    return true;
}

static void writeCurveToSetting(const CalibrationCurve &curve,
                                Setting &parent, const char *name)
{
    if (curve.empty()) {
        return;
    }

    Setting &list = parent.add(name, Setting::TypeList);
    for (const CalibrationPoint &pt : curve.getPoints()) {
        Setting &group = list.add(Setting::TypeGroup);
        group.add("raw", Setting::TypeFloat) = pt.raw;
        group.add("cal", Setting::TypeFloat) = pt.cal;
    }
}

bool Calibration::writeConfig(Config &cfg) const
{
    lock_guard<mutex> lock(_mutex);
    Setting &root = cfg.getRoot();

    if (root.exists("calibration")) {
        root.remove("calibration");
    }

    if (_nodes.empty()) {
        return true;
    }

    Setting &calList = root.add("calibration", Setting::TypeList);
    for (map<string, NodeCalibration>::const_iterator it = _nodes.begin();
         it != _nodes.end(); it++) {
        if (it->second.empty()) {
            continue;
        }

        Setting &nodeGroup = calList.add(Setting::TypeGroup);
        nodeGroup.add("node", Setting::TypeString) = it->first;

        writeCurveToSetting(it->second.temperature, nodeGroup, "temperature");
        writeCurveToSetting(it->second.humidity, nodeGroup, "humidity");
        writeCurveToSetting(it->second.pressure, nodeGroup, "pressure");
    }

    return true;
}

static string resolveConfigPath(const string &path, const string &storedPath)
{
    if (!path.empty()) {
        return path;
    }
    if (!storedPath.empty()) {
        return storedPath;
    }
    const char *homedir = getenv("HOME");
    if ((homedir != NULL) && (homedir[0] != '\0')) {
        return string(homedir) + "/.meshmon.calib";
    }
    return ".meshmon.calib";
}

bool Calibration::loadFile(const string &path)
{
    string targetPath = resolveConfigPath(path, _configPath);
    setConfigPath(targetPath);
    Config cfg;

    try {
        cfg.readFile(targetPath.c_str());
    } catch (const FileIOException &) {
        return false;
    } catch (const ParseException &e) {
        cerr << "Parse error in " << e.getFile() << " line "
             << e.getLine() << ": " << e.getError() << endl;
        return false;
    }

    return readConfig(cfg);
}

bool Calibration::saveFile(const string &path) const
{
    string targetPath = resolveConfigPath(path, _configPath);
    Config cfg;

    // Try reading existing config first to preserve other sections
    try {
        cfg.readFile(targetPath.c_str());
    } catch (...) {
        // File may be new or empty, continue with new config
    }

    if (!writeConfig(cfg)) {
        return false;
    }

    try {
        cfg.writeFile(targetPath.c_str());
    } catch (const FileIOException &e) {
        cerr << targetPath << ": " << strerror(errno) << endl;
        return false;
    }

    return true;
}

string Calibration::normalizeNodeKey(const string &key)
{
    if (key.empty() || (key == "*") || (key == "default") || (key == "DEFAULT")) {
        return "default";
    }

    string res = key;
    if (res.size() > 0 && res[0] == '!') {
        res = res.substr(1);
    } else if (res.size() > 2 && res[0] == '0' && (res[1] == 'x' || res[1] == 'X')) {
        res = res.substr(2);
    }

    for (size_t i = 0; i < res.size(); i++) {
        res[i] = tolower(static_cast<unsigned char>(res[i]));
    }

    return res;
}

string Calibration::normalizeNodeKey(uint32_t nodeId)
{
    char buf[9];
    snprintf(buf, sizeof(buf), "%.8x", nodeId);
    return string(buf);
}

bool Calibration::parseSensorType(const string &str, SensorType &type)
{
    string s = str;
    for (size_t i = 0; i < s.size(); i++) {
        s[i] = tolower(static_cast<unsigned char>(s[i]));
    }

    if ((s == "temp") || (s == "temperature") || (s == "t")) {
        type = SENSOR_TEMPERATURE;
        return true;
    } else if ((s == "hum") || (s == "humidity") || (s == "h") || (s == "rh")) {
        type = SENSOR_HUMIDITY;
        return true;
    } else if ((s == "press") || (s == "pressure") || (s == "p") || (s == "baro")) {
        type = SENSOR_PRESSURE;
        return true;
    }

    return false;
}

const char *Calibration::sensorTypeName(SensorType type)
{
    switch (type) {
    case SENSOR_TEMPERATURE:
        return "Temperature";
    case SENSOR_HUMIDITY:
        return "Humidity";
    case SENSOR_PRESSURE:
        return "Pressure";
    default:
        return "Unknown";
    }
}

const char *Calibration::sensorTypeUnit(SensorType type)
{
    switch (type) {
    case SENSOR_TEMPERATURE:
        return "\u00b0C";
    case SENSOR_HUMIDITY:
        return "%";
    case SENSOR_PRESSURE:
        return "hPa";
    default:
        return "";
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
