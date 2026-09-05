/*
 * MqttClient.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <unistd.h>
#include <mosquitto.h>
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <MqttClient.hxx>

#define MQTT_QUEUE_MAX 64
#define MQTT_PACKET_BUF 1024

MqttClient::MqttClient()
    : MqttClient("mqtt.meshtastic.org", 1883, "meshdev", "large4cats",
                 "mesh/TW")
{

}

MqttClient::MqttClient(const string &server, uint16_t port,
                       const string &user, const string &password,
                       const string &topic, bool tls)
    : _server(server),
      _port(port),
      _user(user),
      _password(password),
      _topic(topic),
      _tls(tls),
      _clientId(makeClientId()),
      _thread(NULL),
      _isRunning(false),
      _connected(false),
      _mosq(NULL),
      _grantedQos(0),
      _published(0),
      _publishConfirmed(0)
{

}

MqttClient::~MqttClient()
{
    stop();
    join();

    if (_mosq) {
        mosquitto_destroy(_mosq);
        _mosq = NULL;
    }
}

string MqttClient::makeClientId(void)
{
    static atomic<unsigned int> seq(0);
    stringstream ss;

    ss << "meshmon-" << getpid() << "-" << seq.fetch_add(1);

    return ss.str();
}

unsigned int MqttClient::published(void) const
{
    return _published.load();
}

unsigned int MqttClient::publishConfirmed(void) const
{
    return _publishConfirmed.load();
}

bool MqttClient::isConnected(void) const
{
    return _connected.load();
}

bool MqttClient::isRunning(void) const
{
    return _isRunning.load();
}

void MqttClient::start(void)
{
    if (!_isRunning.load()) {
        if (_thread == NULL) {
            _isRunning.store(true);
            _thread = make_shared<thread>(thread_function, this);
        }
    }
}

void MqttClient::stop(void)
{
    if (_isRunning.load()) {
        _isRunning.store(false);
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
    while (_textQueue.empty() == false) {
        _textQueue.pop();
    }
    _mutex.unlock();
}

bool MqttClient::enqueueLimited(void)
{
    if (!isRunning()) {
        return false;
    }

    if ((_proxyQueue.size() + _packetQueue.size() +
         _textQueue.size()) >= MQTT_QUEUE_MAX) {
        return false;
    }

    return true;
}

bool MqttClient::publish(const meshtastic_MqttClientProxyMessage &m)
{
    if (m.which_payload_variant !=
        meshtastic_MqttClientProxyMessage_data_tag) {
        return false;
    }

    _mutex.lock();
    if (!enqueueLimited()) {
        _mutex.unlock();
        return false;
    }
    _proxyQueue.push(m);
    _mutex.unlock();
    _cv.notify_one();

    return true;
}

bool MqttClient::publish(const meshtastic_MeshPacket &p)
{
    _mutex.lock();
    if (!enqueueLimited()) {
        _mutex.unlock();
        return false;
    }
    _packetQueue.push(p);
    _mutex.unlock();
    _cv.notify_one();

    return true;
}

bool MqttClient::publish(const string &topic, const string &payload,
                         bool retain)
{
    TextPublish t;

    if (topic.empty()) {
        return false;
    }

    t.topic = topic;
    t.payload = payload;
    t.retain = retain;

    _mutex.lock();
    if (!enqueueLimited()) {
        _mutex.unlock();
        return false;
    }
    _textQueue.push(t);
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

    mqtt->_connected.store(true);

    if (!mqtt->_topic.empty()) {
        rc = mosquitto_subscribe(mosq, NULL, mqtt->_topic.c_str(), 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            cerr << "mosquitto: " << mosquitto_strerror(rc) << endl;
            mosquitto_disconnect(mosq);
            return;
        }
    }

    {
        lock_guard<mutex> lock(mqtt->_mutex);
        for (size_t i = 0; i < mqtt->_extraSubscriptions.size(); i++) {
            mosquitto_subscribe(mosq, NULL, mqtt->_extraSubscriptions[i].c_str(), 1);
        }
    }
}

void MqttClient::onDisconnect(struct mosquitto *mosq, void *obj, int rc)
{
    MqttClient *mqtt = (MqttClient *) obj;

    (void)(mosq);
    (void)(obj);
    (void)(rc);

    mqtt->_connected.store(false);
    mqtt->_grantedQos.store(0);
}

void MqttClient::onPublish(struct mosquitto *mosq, void *obj, int mid)
{
    MqttClient *mqtt = (MqttClient *) obj;

    (void)(mosq);
    (void)(obj);
    (void)(mid);

    mqtt->_publishConfirmed.fetch_add(1);
}

void MqttClient::onSubscribe(struct mosquitto *mosq, void *obj,
                             int mid, int qos_count, const int *granted_qos)
{
    MqttClient *mqtt = (MqttClient *) obj;

    (void)(mosq);
    (void)(obj);
    (void)(mid);

    if (qos_count >= 1) {
        mqtt->_grantedQos.store((unsigned int) granted_qos[0]);
    }
}

void MqttClient::onMessage(struct mosquitto *mosq, void *obj,
                          const struct mosquitto_message *message)
{
    (void)(mosq);
    MqttClient *mqtt = (MqttClient *) obj;
    if (mqtt != NULL && message != NULL && message->topic != NULL) {
        string topic(message->topic);
        string payload;
        if (message->payload != NULL && message->payloadlen > 0) {
            payload.assign((const char *) message->payload, message->payloadlen);
        }
        MessageCallback cb;
        {
            lock_guard<mutex> lock(mqtt->_mutex);
            cb = mqtt->_messageCallback;
        }
        if (cb) {
            cb(topic, payload);
        }
    }
}

void MqttClient::thread_function(MqttClient *mqtt)
{
    mqtt->run();
}

void MqttClient::run(void)
{
    int ret;
    bool loopStarted = false;
    int qos;

    if (_mosq == NULL) {
        _mosq = mosquitto_new(_clientId.c_str(), true, this);
        if (_mosq == NULL) {
            cerr << "mosquitto_new() failed!" << endl;
            goto done;
        }
    }

    if (_user.empty()) {
        mosquitto_username_pw_set(_mosq, NULL, NULL);
    } else {
        mosquitto_username_pw_set(_mosq, _user.c_str(),
                                  _password.empty() ? NULL : _password.c_str());
    }
    mosquitto_connect_callback_set(_mosq, onConnect);
    mosquitto_disconnect_callback_set(_mosq, onDisconnect);
    mosquitto_publish_callback_set(_mosq, onPublish);
    mosquitto_subscribe_callback_set(_mosq, onSubscribe);
    mosquitto_message_callback_set(_mosq, onMessage);

    if (_tls) {
        ret = mosquitto_int_option(_mosq, MOSQ_OPT_TLS_USE_OS_CERTS, 1);
        if (ret != MOSQ_ERR_SUCCESS) {
            ret = mosquitto_tls_set(_mosq,
                                    "/etc/ssl/certs/ca-certificates.crt",
                                    NULL, NULL, NULL, NULL);
        }
        if (ret != MOSQ_ERR_SUCCESS) {
            ret = mosquitto_tls_set(_mosq, NULL, "/etc/ssl/certs",
                                    NULL, NULL, NULL);
        }
        if (ret != MOSQ_ERR_SUCCESS) {
            cerr << "mosquitto TLS: " << mosquitto_strerror(ret) << endl;
            goto done;
        }
    }

    ret = mosquitto_loop_start(_mosq);
    if (ret != MOSQ_ERR_SUCCESS) {
        cerr << "mosquitto_loop_start: " << mosquitto_strerror(ret) << endl;
        goto done;
    }
    loopStarted = true;

    ret = mosquitto_connect(_mosq, _server.c_str(), _port, 60);
    if (ret != MOSQ_ERR_SUCCESS) {
        cerr << "mosquitto_connect: " << mosquitto_strerror(ret) << endl;
        goto done;
    }

    while (_isRunning.load()) {
        meshtastic_MqttClientProxyMessage m;
        meshtastic_MeshPacket p;
        TextPublish t;
        bool haveProxy = false;
        bool havePacket = false;
        bool haveText = false;

        {
            unique_lock<mutex> lock(_mutex);
            while (_isRunning.load() &&
                   _proxyQueue.empty() && _packetQueue.empty() &&
                   _textQueue.empty()) {
                _cv.wait_for(lock, std::chrono::seconds(1));
            }
            if (!_proxyQueue.empty()) {
                m = _proxyQueue.front();
                _proxyQueue.pop();
                haveProxy = true;
            }
            if (!_packetQueue.empty()) {
                p = _packetQueue.front();
                _packetQueue.pop();
                havePacket = true;
            }
            if (!_textQueue.empty()) {
                t = _textQueue.front();
                _textQueue.pop();
                haveText = true;
            }
        }

        if (haveProxy) {
            qos = (int) _grantedQos.load();
            ret = mosquitto_publish(_mosq,
                                    NULL,
                                    m.topic,
                                    m.payload_variant.data.size,
                                    m.payload_variant.data.bytes,
                                    qos,
                                    m.retained);
            if (ret != MOSQ_ERR_SUCCESS){
                fprintf(stderr, "mosquitto_publish failed: %s\n",
                        mosquitto_strerror(ret));
            } else {
                _published.fetch_add(1);
            }
        }

        if (havePacket) {
            uint8_t buf[MQTT_PACKET_BUF];
            pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

            if (pb_encode(&stream, meshtastic_MeshPacket_fields, &p) != true) {
                fprintf(stderr, "mqtt MeshPacket encode failed\n");
            } else {
                qos = (int) _grantedQos.load();
                ret = mosquitto_publish(_mosq,
                                        NULL,
                                        _topic.c_str(),
                                        (int) stream.bytes_written,
                                        buf,
                                        qos,
                                        false);
                if (ret != MOSQ_ERR_SUCCESS) {
                    fprintf(stderr, "mosquitto_publish failed: %s\n",
                            mosquitto_strerror(ret));
                } else {
                    _published.fetch_add(1);
                }
            }
        }

        if (haveText) {
            qos = (int) _grantedQos.load();
            ret = mosquitto_publish(_mosq,
                                    NULL,
                                    t.topic.c_str(),
                                    (int) t.payload.size(),
                                    t.payload.c_str(),
                                    qos,
                                    t.retain);
            if (ret != MOSQ_ERR_SUCCESS) {
                fprintf(stderr, "mosquitto_publish failed: %s\n",
                        mosquitto_strerror(ret));
            } else {
                _published.fetch_add(1);
            }
        }
    }

done:

    _connected.store(false);
    if (_mosq != NULL) {
        mosquitto_disconnect(_mosq);
        if (loopStarted) {
            mosquitto_loop_stop(_mosq, true);
        }
    }
    _isRunning.store(false);

    return;
}

void MqttClient::setMessageCallback(MessageCallback cb)
{
    lock_guard<mutex> lock(_mutex);
    _messageCallback = cb;
}

void MqttClient::subscribe(const string &topic)
{
    lock_guard<mutex> lock(_mutex);
    _extraSubscriptions.push_back(topic);
    if (_mosq != NULL && _connected.load()) {
        mosquitto_subscribe(_mosq, NULL, topic.c_str(), 1);
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
