/*
 * MeshRoomDevice.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include <MeshRoomDevice.hxx>

const string MeshRoomDevice::_deviceType = "meshroom";
const vector<string> MeshRoomDevice::_probeCommands = {
    "ac",
    "tv"
};

MeshRoomDevice::MeshRoomDevice()
{
}

MeshRoomDevice::~MeshRoomDevice()
{
}

const string &MeshRoomDevice::getDeviceType(void) const
{
    return _deviceType;
}

vector<string> MeshRoomDevice::getProbeCommands(void) const
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
