/*
 * MqttClient.hxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#ifndef MQTTCLIENT_HXX
#define MQTTCLIENT_HXX

#include <queue>
#include <LibMeshtastic.hxx>

using namespace std;

struct mosquitto;

class MqttClient {

public:

    MqttClient();
 	MqttClient(const string &server, uint16_t port,
               const string &user, const string &password,
               const string &topic);
    ~MqttClient();

    unsigned int published(void) const;
    unsigned int publishConfirmed(void) const;
    unsigned int messaged(void) const;
    bool isConnected() const;

    bool isRunning(void) const;
    void start(void);
    void stop(void);
    void join(void);

    void reset(void);
    bool publish(const meshtastic_MqttClientProxyMessage &m);
    bool publish(const meshtastic_MeshPacket &p);

private:

    static void onConnect(struct mosquitto *mosq, void *obj, int rc);
    static void onDisconnect(struct mosquitto *mosq, void *obj, int rc);
    static void onPublish(struct mosquitto *mosq, void *obj, int mid);
    static void onSubscribe(struct mosquitto *mosq, void *obj,
                            int mid, int qos_count, const int *granted_qos);
    static void onMessage(struct mosquitto *mosq, void *obj,
                          const struct mosquitto_message *msg);

    static void thread_function(MqttClient *mqtt);
    void run(void);

private:

    string _server;
    uint16_t _port;
    string _user;
    string _password;
    string _topic;

    mutex _mutex;
    condition_variable _cv;
    shared_ptr<thread> _thread;
    bool _isRunning;

    struct mosquitto *_mosq;
    queue<meshtastic_MqttClientProxyMessage> _proxyQueue;
    queue<meshtastic_MeshPacket> _packetQueue;
    unsigned int _published;
    unsigned int _publishConfirmed;
    unsigned int _messaged;

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
