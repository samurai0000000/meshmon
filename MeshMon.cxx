/*
 * MeshMon.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <sstream>
#include <iostream>
#include <iomanip>
#include <MeshMon.hxx>

MeshMon::MeshMon()
    : MeshClient()
{

}

MeshMon::~MeshMon()
{

}

void MeshMon::gotTextMessage(const meshtastic_MeshPacket &packet,
                             const string &message)
{
    MeshClient::gotTextMessage(packet, message);

    stringstream ss;
    string reply;

    if (packet.to == whoami()) {
        bool result;

        cout << getDisplayName(packet.from) << " : "
             << message << endl;

        ss << lookupShortName(packet.from) << ", you said '"
           <<  message << "'!";
        reply = ss.str();
        if (reply.size() > 200) {
            reply = "Oopsie daisie!";
        }

        result = textMessage(packet.from, packet.channel, reply);
        if (result == false) {
            cerr << "textMessage '" << ss.str() << "' failed!" << endl;
        } else {
            cout << "my reply to " << getDisplayName(packet.from) << " : "
                 << reply << endl;
        }
    } else {
        cout << getDisplayName(packet.from) << " -> "
             << getDisplayName(packet.to) << " #"
             << getChannelName(packet.channel) << " : "
             << message << endl;
    }
}

void MeshMon::gotPosition(const meshtastic_MeshPacket &packet,
                          const meshtastic_Position &position)
{
    MeshClient::gotPosition(packet, position);

    if (!verbose()) {
        cout << getDisplayName(packet.from)
             << " sent position" << endl;
    }
}

void MeshMon::gotUser(const meshtastic_MeshPacket &packet,
                      const meshtastic_User &user)
{
    MeshClient::gotUser(packet, user);

    if (!verbose()) {
        cout << getDisplayName(packet.from)
             << " sent nodeInfo.user" << endl;
    }
}

void MeshMon::gotRouting(const meshtastic_MeshPacket &packet,
                         const meshtastic_Routing &routing)
{
    MeshClient::gotRouting(packet, routing);

    if ((routing.which_variant == meshtastic_Routing_error_reason_tag) &&
        (routing.error_reason == meshtastic_Routing_Error_NONE) &&
        (packet.from != packet.to)) {
        cout << "traceroute from " << getDisplayName(packet.from) << " -> ";
        cout << getDisplayName(packet.to)
             << "[" << packet.rx_snr << "dB]" << endl;
    }
}

void MeshMon::gotDeviceMetrics(const meshtastic_MeshPacket &packet,
                               const meshtastic_DeviceMetrics &metrics)
{
    MeshClient::gotDeviceMetrics(packet, metrics);

    if (!verbose()) {
        cout << getDisplayName(packet.from)
             << " sent device metrics" << endl;
    }
}

void MeshMon::gotEnvironmentMetrics(const meshtastic_MeshPacket &packet,
                                    const meshtastic_EnvironmentMetrics &metrics)
{
    MeshClient::gotEnvironmentMetrics(packet, metrics);

    if (!verbose()) {
        cout << getDisplayName(packet.from)
             << " sent environment metrics" << endl;
    }
}

void MeshMon::gotAirQualityMetrics(const meshtastic_MeshPacket &packet,
                                   const meshtastic_AirQualityMetrics &metrics)
{
    MeshClient::gotAirQualityMetrics(packet, metrics);

    if (!verbose()) {
        cout << getDisplayName(packet.from)
             << " sent air quality metrics" << endl;
    }
}

void MeshMon::gotPowerMetrics(const meshtastic_MeshPacket &packet,
                              const meshtastic_PowerMetrics &metrics)
{
    MeshClient::gotPowerMetrics(packet, metrics);

    if (!verbose()) {
        cout << getDisplayName(packet.from)
             << " sent power metrics" << endl;
    }
}

void MeshMon::gotLocalStats(const meshtastic_MeshPacket &packet,
                            const meshtastic_LocalStats &stats)
{
    MeshClient::gotLocalStats(packet, stats);

    if (!verbose()) {
        cout << getDisplayName(packet.from)
             << " sent local stats" << endl;
    }
}

void MeshMon::gotHealthMetrics(const meshtastic_MeshPacket &packet,
                               const meshtastic_HealthMetrics &metrics)
{
    MeshClient::gotHealthMetrics(packet, metrics);

    if (!verbose()) {
        cout << getDisplayName(packet.from)
             << " sent health metrics" << endl;
    }
}

void MeshMon::gotHostMetrics(const meshtastic_MeshPacket &packet,
                             const meshtastic_HostMetrics &metrics)
{
    MeshClient::gotHostMetrics(packet, metrics);

    if (!verbose()) {
        cout << getDisplayName(packet.from)
             << "sent host metrics" << endl;
    }
}

void MeshMon::gotTraceRoute(const meshtastic_MeshPacket &packet,
                            const meshtastic_RouteDiscovery &routeDiscovery)
{
    MeshClient::gotTraceRoute(packet, routeDiscovery);
    if (!verbose()) {
        if ((routeDiscovery.route_count > 0) &&
            (routeDiscovery.route_back_count == 0)) {
            float rx_snr;
            cout << "traceroute from " << getDisplayName(packet.from)
                 << " -> ";
            for (unsigned int i = 0; i < routeDiscovery.route_count; i++) {
                if (i > 0) {
                    cout << " -> ";
                }
                cout << getDisplayName(routeDiscovery.route[i]);
                if (routeDiscovery.snr_towards[i] != INT8_MIN) {
                    rx_snr = routeDiscovery.snr_towards[i];
                    rx_snr /= 4.0;
                    cout << "[" << rx_snr << "dB]";
                } else {
                    cout << "[???dB]";
                }
            }
            rx_snr = packet.rx_snr;
            cout << " -> " << getDisplayName(packet.to)
                 << "[" << rx_snr << "dB]" << endl;
        }
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
