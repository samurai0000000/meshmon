/*
 * MeshPumpDevice.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include <MeshPumpDevice.hxx>

const string MeshPumpDevice::_deviceType = "meshpump";
const vector<string> MeshPumpDevice::_probeCommands = {
    /*
     * A bare "pump" answers with fish, up, and cutoff in one reply.
     * "fish" and "up" are sub-verbs and are not dispatched on their
     * own, so probing with them draws no response at all.
     */
    "pump"
};

MeshPumpDevice::MeshPumpDevice()
{
}

MeshPumpDevice::~MeshPumpDevice()
{
}

const string &MeshPumpDevice::getDeviceType(void) const
{
    return _deviceType;
}

vector<string> MeshPumpDevice::getProbeCommands(void) const
{
    return _probeCommands;
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
