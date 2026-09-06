/*
 * MeshRoofDevice.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include <MeshRoofDevice.hxx>

const string MeshRoofDevice::_deviceType = "meshroof";
const vector<string> MeshRoofDevice::_probeCommands = {
    "amplify",
    "wifi"
};

MeshRoofDevice::MeshRoofDevice()
{
}

MeshRoofDevice::~MeshRoofDevice()
{
}

const string &MeshRoofDevice::getDeviceType(void) const
{
    return _deviceType;
}

vector<string> MeshRoofDevice::getProbeCommands(void) const
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
