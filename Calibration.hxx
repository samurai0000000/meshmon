/*
 * Calibration.hxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#ifndef CALIBRATION_HXX
#define CALIBRATION_HXX

#include <libconfig.h++>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <cstdint>

using namespace std;

enum SensorType {
    SENSOR_TEMPERATURE = 0,
    SENSOR_HUMIDITY,
    SENSOR_PRESSURE,
};

struct CalibrationPoint {
    double raw;
    double cal;

    CalibrationPoint(double r = 0.0, double c = 0.0)
        : raw(r), cal(c) {}
};

class CalibrationCurve {

public:

    CalibrationCurve();
    ~CalibrationCurve();

    double calibrate(double raw) const;
    void setPoint(double raw, double cal);
    bool delPoint(double raw);
    void clear(void);
    bool empty(void) const;
    const vector<CalibrationPoint> &getPoints(void) const;

private:

    vector<CalibrationPoint> _points;

};

class NodeCalibration {

public:

    NodeCalibration();
    ~NodeCalibration();

    CalibrationCurve temperature;
    CalibrationCurve humidity;
    CalibrationCurve pressure;

    CalibrationCurve *getCurve(SensorType type);
    const CalibrationCurve *getCurve(SensorType type) const;
    bool empty(void) const;
    void clear(void);

};

class Calibration {

public:

    Calibration();
    ~Calibration();

    float calibrateTemperature(uint32_t nodeId, float raw) const;
    float calibrateHumidity(uint32_t nodeId, float raw) const;
    float calibratePressure(uint32_t nodeId, float raw) const;

    float calibrate(uint32_t nodeId, SensorType type, float raw) const;
    float calibrate(const string &nodeKey, SensorType type, float raw) const;

    void setPoint(const string &nodeKey, SensorType type,
                  double raw, double cal);
    bool delPoint(const string &nodeKey, SensorType type, double raw);
    bool clear(const string &nodeKey, SensorType type);
    bool clearNode(const string &nodeKey);
    void clearAll(void);

    bool hasNode(const string &nodeKey) const;
    bool getNode(const string &nodeKey, NodeCalibration &out) const;
    map<string, NodeCalibration> getAll(void) const;

    void setConfigPath(const string &path);
    const string &getConfigPath(void) const;

    bool readConfig(const libconfig::Config &cfg);
    bool writeConfig(libconfig::Config &cfg) const;
    bool loadFile(const string &path = "");
    bool saveFile(const string &path = "") const;

    static string normalizeNodeKey(const string &key);
    static string normalizeNodeKey(uint32_t nodeId);
    static bool parseSensorType(const string &str, SensorType &type);
    static const char *sensorTypeName(SensorType type);
    static const char *sensorTypeUnit(SensorType type);

private:

    map<string, NodeCalibration> _nodes;
    string _configPath;
    mutable mutex _mutex;

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
