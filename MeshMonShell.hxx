/*
 * MeshMonShell.hxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#ifndef MESHMONSHELL_HXX
#define MESHMONSHELL_HXX

#include <MeshShell.hxx>
#include <Calibration.hxx>
#include <MeshMonDb.hxx>

using namespace std;

class MeshMonShell : public MeshShell {

public:

    MeshMonShell(shared_ptr<MeshClient> client = NULL);
    ~MeshMonShell();

    void setDb(shared_ptr<MeshMonDb> db);
    inline const shared_ptr<MeshMonDb> db(void) const {
        return _db;
    }

protected:

    virtual shared_ptr<MeshShell> newInstance(void);
    virtual int help(int argc, char **argv);
    virtual int system(int argc, char **argv);
    virtual int status(int argc, char **argv);
    virtual int unknown_command(int argc, char **argv);
    virtual int calib(int argc, char **argv);
    virtual int chatbot(int argc, char **argv);
    virtual int db(int argc, char **argv);
    virtual int robot(int argc, char **argv);

private:

    void printDbHelp(void);
    void printDbAutoHelp(void);
    int dbAuto(int argc, char **argv);
    void printChatbotHelp(void);
    void printRobotHelp(void);
    void printFleetRobotStatus(void);
    void printNodeRobotStatus(uint32_t nodeId);
    void printCurve(const char *name, const CalibrationCurve &curve,
                    const char *unit);
    void printNode(const string &nodeKey, const NodeCalibration &nodeCal);
    bool resolveNodeKey(const string &nodeArg, string &outKey,
                        string &outName);

    uint32_t resolveNode(const string &nodeArg) const;
    void printStatusHelp(void);
    void printNodeStatus(uint32_t nodeId);
    static string formatRelativeTime(time_t timestamp);
    static const char *hardwareModelString(meshtastic_HardwareModel model);
    static const char *roleString(meshtastic_Config_DeviceConfig_Role role);

    shared_ptr<MeshMonDb> _db;

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
