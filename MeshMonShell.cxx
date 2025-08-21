/*
 * MeshMonShell.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <MeshMon.hxx>
#include <MqttClient.hxx>
#include <MeshMonShell.hxx>

MeshMonShell::MeshMonShell(shared_ptr<MeshClient> client)
    : MeshShell(client)
{

}

MeshMonShell::~MeshMonShell()
{

}

int MeshMonShell::system(int argc, char **argv)
{
    shared_ptr<MeshMon> meshmon = dynamic_pointer_cast<MeshMon>(_client);
    const shared_ptr<MqttClient> meshtasticMqtt =
        meshmon->meshtasticMqtt();

    MeshShell::system(argc, argv);
    this->printf("CPU temp: %.1fC\n", meshmon->getCpuTempC());
    if (meshtasticMqtt) {
        this->printf("MQTT published: %u/%u\n",
                     meshtasticMqtt->publishConfirmed(),
                     meshtasticMqtt->published());
    }

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
