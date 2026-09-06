/*
 * AutomationDevice.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include <AutomationDevice.hxx>
#include <MeshPumpDevice.hxx>
#include <MeshRoofDevice.hxx>
#include <MeshRoomDevice.hxx>

AutomationDevice::AutomationDevice()
{
}

AutomationDevice::~AutomationDevice()
{
}

string AutomationDevice::getNextProbeCommand(uint32_t probeIndex) const
{
    const vector<string> cmds = getProbeCommands();
    if (cmds.empty()) {
        return "";
    }

    return cmds[probeIndex % cmds.size()];
}

shared_ptr<AutomationDevice> AutomationDevice::create(const string &deviceType)
{
    if (deviceType == "meshpump") {
        return make_shared<MeshPumpDevice>();
    } else if (deviceType == "meshroof") {
        return make_shared<MeshRoofDevice>();
    } else if (deviceType == "meshroom") {
        return make_shared<MeshRoomDevice>();
    }

    return nullptr;
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
