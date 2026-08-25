/*
 * ChatBot.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <time.h>
#include <iostream>
#include <ChatBot.hxx>

#ifndef DEBUG_CHATBOT
#define DEBUG_CHATBOT 0
#endif

#define CHAT_HISTORY_MAX    8
#define CHAT_IDLE_SECONDS   1800
#define CHAT_MAX_BYTES      200

ChatBot::ChatBot(shared_ptr<MeshClient> client)
    : _client(client),
      _thread(NULL),
      _isRunning(false)
{

}

ChatBot::~ChatBot()
{
    stop();
    join();
}

void ChatBot::setClient(shared_ptr<MeshClient> client)
{
    _client = client;
}

bool ChatBot::enabled(void) const
{
    return true;
}

void ChatBot::ask(uint32_t from, uint32_t dest, uint8_t channel,
                  const string &message)
{
    deque<ChatJob>::iterator it;

    if (!enabled() || !_isRunning.load() || message.empty()) {
#if DEBUG_CHATBOT
        cout << "chatbot: ask dropped enabled=" << (enabled() ? 1 : 0)
             << " running=" << (_isRunning.load() ? 1 : 0)
             << " empty=" << (message.empty() ? 1 : 0) << endl;
#endif
        return;
    }

    {
        unique_lock<mutex> lock(_mutex);

        for (it = _queue.begin(); it != _queue.end(); it++) {
            if (it->from == from) {
                it->dest = dest;
                it->channel = channel;
                it->message = message;
#if DEBUG_CHATBOT
                cout << "chatbot: queue replace from=" << from
                     << " dest=" << dest
                     << " channel=" << (unsigned int) channel
                     << " msg='" << message << "'" << endl;
#endif
                return;
            }
        }

        ChatJob job;

        job.from = from;
        job.dest = dest;
        job.channel = channel;
        job.message = message;
        _queue.push_back(job);
#if DEBUG_CHATBOT
        cout << "chatbot: queued from=" << from
             << " dest=" << dest
             << " channel=" << (unsigned int) channel
             << " msg='" << message << "'"
             << " qlen=" << _queue.size() << endl;
#endif
    }

    _cv.notify_one();
}

bool ChatBot::pollReply(ChatReply &reply)
{
    unique_lock<mutex> lock(_mutex);

    if (_replies.empty()) {
        return false;
    }

    reply = _replies.front();
    _replies.pop_front();
    return true;
}

void ChatBot::start(void)
{
    if (!_isRunning.load()) {
        if (_thread == NULL) {
            _isRunning.store(true);
            _thread = make_shared<thread>(thread_function, this);
        }
    }
}

void ChatBot::stop(void)
{
    if (_isRunning.load()) {
        _isRunning.store(false);
        _cv.notify_one();
    }
}

void ChatBot::join(void)
{
    if (_thread != NULL) {
        if (_thread->joinable()) {
            _thread->join();
        }
        _thread.reset();
    }
}

void ChatBot::thread_function(ChatBot *bot)
{
    bot->run();
}

void ChatBot::run(void)
{
    while (_isRunning.load()) {
        ChatJob job;
        bool haveJob = false;

        {
            unique_lock<mutex> lock(_mutex);
            while (_isRunning.load() && _queue.empty()) {
                _cv.wait(lock);
            }
            if (!_queue.empty()) {
                job = _queue.front();
                _queue.pop_front();
                haveJob = true;
            }
        }

        if (haveJob) {
            processJob(job);
        }
    }
}

void ChatBot::expireIdle(time_t now)
{
    map<uint32_t, Conversation>::iterator it;

    it = _conversations.begin();
    while (it != _conversations.end()) {
        if ((now - it->second.lastUsed) >= (time_t) CHAT_IDLE_SECONDS) {
            map<uint32_t, Conversation>::iterator eraseIt = it;
            it++;
            _conversations.erase(eraseIt);
        } else {
            it++;
        }
    }
}

string ChatBot::truncateToMesh(const string &text) const
{
    string out = text;
    size_t i;
    size_t cut;

    for (i = 0; i < out.size(); i++) {
        if ((out[i] == '\n') || (out[i] == '\r')) {
            out[i] = ' ';
        }
    }

    if (out.size() <= CHAT_MAX_BYTES) {
        return out;
    }

    cut = CHAT_MAX_BYTES;
    while ((cut > 0) &&
           ((static_cast<unsigned char>(out[cut]) & 0xc0) == 0x80)) {
        cut--;
    }

    return out.substr(0, cut);
}

void ChatBot::processJob(const ChatJob &job)
{
    time_t now = time(NULL);
    string generated;
    string reply;
    ChatTurn userTurn;
    ChatTurn modelTurn;
    bool success = false;

    expireIdle(now);

    Conversation &conv = _conversations[job.from];

    generated = generate(job.from, conv.turns, job.message);
    success = !generated.empty();
#if DEBUG_CHATBOT
    cout << "chatbot: generate from=" << job.from
         << " history=" << conv.turns.size()
         << " empty=" << (success ? 0 : 1)
         << " bytes=" << generated.size() << endl;
#endif
    if (!success) {
        return;
    }

    reply = truncateToMesh(generated);
    if (reply.empty()) {
        return;
    }

    userTurn.user = true;
    userTurn.text = job.message;
    modelTurn.user = false;
    modelTurn.text = reply;
    conv.turns.push_back(userTurn);
    conv.turns.push_back(modelTurn);
    while (conv.turns.size() > CHAT_HISTORY_MAX) {
        conv.turns.erase(conv.turns.begin());
    }
    conv.lastUsed = now;

    {
        ChatReply out;

        out.from = job.from;
        out.dest = job.dest;
        out.channel = job.channel;
        out.text = reply;

        unique_lock<mutex> lock(_mutex);
        _replies.push_back(out);
    }
#if DEBUG_CHATBOT
    cout << "chatbot: reply ready from=" << job.from
         << " dest=" << job.dest
         << " bytes=" << reply.size() << endl;
#endif
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
