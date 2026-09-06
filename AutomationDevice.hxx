/*
 * AutomationDevice.hxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#ifndef MESHMON_AUTOMATION_DEVICE_HXX
#define MESHMON_AUTOMATION_DEVICE_HXX

#include <string>
#include <vector>
#include <memory>

using namespace std;

class AutomationDevice {

public:

    AutomationDevice();
    virtual ~AutomationDevice();

    virtual const string &getDeviceType(void) const = 0;
    virtual vector<string> getProbeCommands(void) const = 0;
    virtual string getNextProbeCommand(uint32_t probeIndex) const;

    static shared_ptr<AutomationDevice> create(const string &deviceType);

};

#endif /* MESHMON_AUTOMATION_DEVICE_HXX */

/*
 * Local variables:
 * mode: C++
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
