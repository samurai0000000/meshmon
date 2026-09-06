/*
 * MeshPumpDevice.hxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#ifndef MESHMON_MESHPUMP_DEVICE_HXX
#define MESHMON_MESHPUMP_DEVICE_HXX

#include <AutomationDevice.hxx>

class MeshPumpDevice : public AutomationDevice {

public:

    MeshPumpDevice();
    virtual ~MeshPumpDevice();

    virtual const string &getDeviceType(void) const override;
    virtual vector<string> getProbeCommands(void) const override;

private:

    static const string _deviceType;
    static const vector<string> _probeCommands;

};

#endif /* MESHMON_MESHPUMP_DEVICE_HXX */

/*
 * Local variables:
 * mode: C++
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
