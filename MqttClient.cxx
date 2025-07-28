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
}

MqttClient::~MqttClient()
{
    if (_mosq) {
        mosquitto_destroy(_mosq);
        _mosq = NULL;
    }
}

unsigned int MqttClient::published(void) const
{
    return _published;
}

unsigned int MqttClient::publishConfirmed(void) const
{
    return _publishConfirmed;
}

bool MqttClient::isConnected(void) const
{
    return (_mosq != NULL) && (_grantedQos != 0);
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
    if (_thread != NULL) {
        if (_thread->joinable()) {
            _thread->join();
        }
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

    if (m.which_payload_variant !=
        meshtastic_MqttClientProxyMessage_data_tag) {
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

    if (rc != MOSQ_ERR_SUCCESS) {
        cerr << "mosquitto: " << mosquitto_connack_string(rc) << endl;
        mosquitto_disconnect(mosq);
        return;
    }

    rc = mosquitto_subscribe(mosq, NULL, mqtt->_topic.c_str(), 1);
    if (rc != MOSQ_ERR_SUCCESS) {
        cerr << "mosquitto: " << mosquitto_connack_string(rc) << endl;
        mosquitto_disconnect(mosq);
        return;
    }
}

void MqttClient::onDisconnect(struct mosquitto *mosq, void *obj, int rc)
{
    MqttClient *mqtt = (MqttClient *) obj;

    (void)(mosq);
    (void)(obj);
    (void)(rc);

    mqtt->_grantedQos = 0;
}

void MqttClient::onPublish(struct mosquitto *mosq, void *obj, int mid)
{
    MqttClient *mqtt = (MqttClient *) obj;

    (void)(mosq);
    (void)(obj);
    (void)(mid);

    mqtt->_publishConfirmed++;
}

void MqttClient::onSubscribe(struct mosquitto *mosq, void *obj,
                             int mid, int qos_count, const int *granted_qos)
{
    MqttClient *mqtt = (MqttClient *) obj;

    (void)(mosq);
    (void)(obj);
    (void)(mid);

    if (qos_count == 1) {
        mqtt->_grantedQos = granted_qos[0];
    }
}

void MqttClient::thread_function(MqttClient *mqtt)
{
    mqtt->run();
}

void MqttClient::run(void)
{
    int ret;

    if (_mosq == NULL) {
        _mosq = mosquitto_new("MqttClient", true, this);
        if (_mosq == NULL) {
            cerr << "mosquitto_new() failed!" << endl;
            goto done;
        }
    }

    mosquitto_username_pw_set(_mosq, _user.c_str(), _password.c_str());
    mosquitto_connect_callback_set(_mosq, onConnect);
    mosquitto_disconnect_callback_set(_mosq, onDisconnect);
    mosquitto_publish_callback_set(_mosq, onPublish);
    mosquitto_subscribe_callback_set(_mosq, onSubscribe);

    ret = mosquitto_loop_start(_mosq);
    if (ret != MOSQ_ERR_SUCCESS) {
        cerr << "mosquitto_loop_start: " << mosquitto_strerror(ret) << endl;
        goto done;
    }

    ret = mosquitto_connect(_mosq, _server.c_str(), _port, 60);
    if (ret != MOSQ_ERR_SUCCESS) {
        cerr << "mosquitto_connect: " << mosquitto_strerror(ret) << endl;
        goto done;
    }

    while (_isRunning) {
        if (!_proxyQueue.empty()) {
            _mutex.lock();
            meshtastic_MqttClientProxyMessage m = _proxyQueue.front();
            _proxyQueue.pop();
            _mutex.unlock();

            ret = mosquitto_publish(_mosq,
                                    NULL,
                                    m.topic,
                                    m.payload_variant.data.size,
                                    m.payload_variant.data.bytes,
                                    _grantedQos,
                                    m.retained);
            if (ret != MOSQ_ERR_SUCCESS){
                fprintf(stderr, "mosquitto_publish failed: %s\n",
                        mosquitto_strerror(ret));
            } else {
                _published++;
            }
        }

        if (!_packetQueue.empty()) {
            _mutex.lock();
            meshtastic_MeshPacket p = _packetQueue.front();
            _packetQueue.pop();
            _mutex.unlock();

            (void)(p);
        }

        unique_lock<mutex> lock(_mutex);
        _cv.wait_for(lock, std::chrono::seconds(1));
    }

done:

    mosquitto_disconnect(_mosq);
    _isRunning = false;

    return;
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
