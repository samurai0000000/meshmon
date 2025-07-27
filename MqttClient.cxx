/*
 * MqttClient.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <mosquitto.h>
#include <iostream>
#include <MqttClient.hxx>

MqttClient::MqttClient()
    : MqttClient("mqtt.meshtastic.org", 1883, "meshdev", "large4cats",
                 "mesh/TW")
{

}

MqttClient::MqttClient(const string &server, uint16_t port,
                       const string &user, const string &password,
                       const string &topic)
{
    _server = server;
    _port = port;
    _user = user;
    _password = password;
    _topic = topic;

    if (_mosq == NULL) {
        _mosq = mosquitto_new("MqttClient", true, this);
        if (_mosq == NULL) {
            cerr << "mosquitto_new() failed!" << endl;
            exit(EXIT_FAILURE);
        }
    }

    mosquitto_connect_callback_set(_mosq, onConnect);
    mosquitto_disconnect_callback_set(_mosq, onDisconnect);
    mosquitto_publish_callback_set(_mosq, onPublish);
    mosquitto_subscribe_callback_set(_mosq, onSubscribe);
    mosquitto_message_callback_set(_mosq, onMessage);
}

MqttClient::~MqttClient()
{
    if (_mosq) {
        mosquitto_destroy(_mosq);
        _mosq = NULL;
    }
}

bool MqttClient::isRunning(void) const
{
    return _isRunning;
}

void MqttClient::start(void)
{
    if (!_isRunning) {
        if (_thread == NULL) {
            _isRunning = true;
            _thread = make_shared<thread>(thread_function, this);
        }
    }
}

void MqttClient::stop(void)
{
    if (_isRunning) {
        _isRunning = false;
        _cv.notify_one();
    }
}

void MqttClient::join(void)
{
    if (_isRunning) {
    }
}

void MqttClient::reset(void)
{
    _mutex.lock();
    while (_proxyQueue.empty() == false) {
        _proxyQueue.pop();
    }
    while (_packetQueue.empty() == false) {
        _packetQueue.pop();
    }
    _mutex.unlock();
}

bool MqttClient::publish(const meshtastic_MqttClientProxyMessage &m)
{
    if (!isRunning() && _proxyQueue.size() > 64) {
        return false;
    }

    _mutex.lock();
    _proxyQueue.push(m);
    _mutex.unlock();
    _cv.notify_one();

    return true;
}

bool MqttClient::publish(const meshtastic_MeshPacket &p)
{
    if (!isRunning() && _packetQueue.size() > 64) {
        return false;
    }

    _mutex.lock();
    _packetQueue.push(p);
    _mutex.unlock();
    _cv.notify_one();

    return true;
}

void MqttClient::onConnect(struct mosquitto *mosq, void *obj, int rc)
{
    MqttClient *mqtt = (MqttClient *) obj;

    (void)(mqtt);
    if (rc != MOSQ_ERR_SUCCESS) {
        cerr << "mosquitto: " << mosquitto_connack_string(rc) << endl;
        mosquitto_disconnect(mosq);
        return;
    }
}

void MqttClient::onDisconnect(struct mosquitto *mosq, void *obj, int rc)
{
    (void)(mosq);
    (void)(obj);
    (void)(rc);
}

void MqttClient::onPublish(struct mosquitto *mosq, void *obj, int mid)
{
    (void)(mosq);
    (void)(obj);
    (void)(mid);
}

void MqttClient::onSubscribe(struct mosquitto *mosq, void *obj,
                             int mid, int qos_count, const int *granted_qos)
{
    (void)(mosq);
    (void)(obj);
    (void)(mid);
    (void)(qos_count);
    (void)(granted_qos);
}

void MqttClient::onMessage(struct mosquitto *mosq, void *obj,
                           const struct mosquitto_message *msg)
{
    (void)(mosq);
    (void)(obj);
    (void)(msg);
}

void MqttClient::thread_function(MqttClient *mqtt)
{
    mqtt->run();
}

void MqttClient::run(void)
{
    int ret;

    ret = mosquitto_connect(_mosq, _server.c_str(), _port, 60);
    if (ret != MOSQ_ERR_SUCCESS) {
        cerr << "mosquitto_connect: " << mosquitto_strerror(ret) << endl;
    }

    while (_isRunning) {
        unique_lock<mutex> lock(_mutex);

        if (!_proxyQueue.empty()) {
            _mutex.lock();
            meshtastic_MqttClientProxyMessage m = _proxyQueue.front();
            _proxyQueue.pop();
            _mutex.unlock();

            (void)(m);
        }

        if (!_packetQueue.empty()) {
            _mutex.lock();
            meshtastic_MeshPacket p = _packetQueue.front();
            _packetQueue.pop();
            _mutex.unlock();

            (void)(p);
        }

        _cv.wait_for(lock, std::chrono::seconds(1));
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
