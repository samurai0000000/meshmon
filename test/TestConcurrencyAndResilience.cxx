/*
 * TestConcurrencyAndResilience.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

#include <libmeshtastic.h>
#include <SimpleClient.hxx>
#include <MeshClient.hxx>
#include <HomeChat.hxx>
#include <MeshShell.hxx>
#include "ChatBot.hxx"

#include <CppUTest/TestHarness.h>

namespace {

std::vector<uint8_t> encodeFromRadioFrame(const meshtastic_FromRadio &fromRadio)
{
    uint8_t buf[sizeof(struct mt_pb_header) + 512];
    memset(buf, 0, sizeof(buf));
    pb_ostream_t ostream = pb_ostream_from_buffer(
        buf + sizeof(struct mt_pb_header), 512);
    bool ok = pb_encode(&ostream, meshtastic_FromRadio_fields, &fromRadio);
    if (!ok) {
        return {};
    }
    struct mt_pb_header *hdr = reinterpret_cast<struct mt_pb_header *>(buf);
    hdr->start1 = MT_PB_START1;
    hdr->start2 = MT_PB_START2;
    hdr->h_len = static_cast<uint8_t>((ostream.bytes_written >> 8) & 0xffU);
    hdr->l_len = static_cast<uint8_t>(ostream.bytes_written & 0xffU);

    size_t total = sizeof(struct mt_pb_header) + ostream.bytes_written;
    return std::vector<uint8_t>(buf, buf + total);
}

bool createPtyPair(int &masterFd, std::string &slavePath)
{
    masterFd = posix_openpt(O_RDWR | O_NOCTTY);
    if (masterFd < 0) {
        return false;
    }
    if (grantpt(masterFd) != 0 || unlockpt(masterFd) != 0) {
        close(masterFd);
        masterFd = -1;
        return false;
    }
    char nameBuf[128];
    memset(nameBuf, 0, sizeof(nameBuf));
    if (ptsname_r(masterFd, nameBuf, sizeof(nameBuf)) != 0) {
        close(masterFd);
        masterFd = -1;
        return false;
    }
    struct termios tty;
    if (tcgetattr(masterFd, &tty) == 0) {
        cfmakeraw(&tty);
        tcsetattr(masterFd, TCSANOW, &tty);
    }
    slavePath = nameBuf;
    return true;
}

bool waitAndReadFrame(int fd, int timeoutMs)
{
    std::vector<uint8_t> accum;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        int ret = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ret > 0) {
            uint8_t buf[256];
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n > 0) {
                accum.insert(accum.end(), buf, buf + n);
                for (size_t i = 0; i + sizeof(struct mt_pb_header) <= accum.size(); i++) {
                    if (accum[i] == MT_PB_START1 && accum[i + 1] == MT_PB_START2) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

uint16_t pickFreeTcpPort()
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return 26876;
    }
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = htons(0);
    uint16_t port = 26876;
    if (bind(s, reinterpret_cast<struct sockaddr *>(&sin), sizeof(sin)) == 0) {
        socklen_t len = sizeof(sin);
        if (getsockname(s, reinterpret_cast<struct sockaddr *>(&sin), &len) == 0) {
            port = ntohs(sin.sin_port);
        }
    }
    close(s);
    return port;
}

std::string tcpRoundTrip(uint16_t port, const std::string &payload, int timeoutMs)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return "";
    }
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = htons(port);
    if (connect(s, reinterpret_cast<struct sockaddr *>(&sin), sizeof(sin)) != 0) {
        close(s);
        return "";
    }

    if (!payload.empty()) {
        ssize_t w = write(s, payload.data(), payload.size());
        (void) w;
    }

    std::string out;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        int ret = select(s + 1, &rfds, NULL, NULL, &tv);
        if (ret > 0) {
            char buf[256];
            ssize_t n = read(s, buf, sizeof(buf));
            if (n > 0) {
                out.append(buf, static_cast<size_t>(n));
            } else {
                break;
            }
        }
    }
    close(s);
    return out;
}

class ConcurrentClientHarness : public SimpleClient, public HomeChat {
public:
    ConcurrentClientHarness()
        : SimpleClient(),
          HomeChat(std::shared_ptr<SimpleClient>(this, [](SimpleClient *) {}))
    {
        setLogIncoming(false);
    }

    virtual ~ConcurrentClientHarness()
    {
        setClient(nullptr);
    }

    virtual int vprintf(const char *format, va_list ap) const override
    {
        (void) format;
        (void) ap;
        return 0;
    }

    void injectFromRadio(const meshtastic_FromRadio &fromRadio)
    {
        std::vector<uint8_t> frame = encodeFromRadioFrame(fromRadio);
        if (!frame.empty()) {
            lock();
            mt_recv_packet(&_mtc, frame.data(), frame.size());
            unlock();
        }
    }

    using HomeChat::printf;
    using HomeChat::getAuthority;
};

int dummyVprintfCb(void *ctx, const char *format, va_list ap)
{
    std::atomic<size_t> *counter = static_cast<std::atomic<size_t> *>(ctx);
    char buf[128];
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    if (counter != NULL && n >= 0) {
        counter->fetch_add(1, std::memory_order_relaxed);
    }
    return n;
}

} // namespace

TEST_GROUP(MeshShellSecurity)
{
    std::shared_ptr<MeshClient> client;
    std::shared_ptr<MeshShell> shell;

    void setup() override
    {
        client = std::make_shared<MeshClient>();
        client->setBanner("libmeshtastic_test_banner");
        shell = std::make_shared<MeshShell>(client);
    }

    void teardown() override
    {
        if (shell) {
            shell->detach();
            shell->join();
            shell.reset();
        }
        client.reset();
    }
};

TEST(MeshShellSecurity, TcpPasswordEnforcedAndNullNvmSafe)
{
    shell->setPassword("s3cr3tPass");

    uint16_t port = pickFreeTcpPort();
    CHECK_TRUE(shell->bindPort(port));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 1. Wrong password must be rejected without running shell commands
    std::string badResp = tcpRoundTrip(port, "wrongpass\nhelp\nexit\n", 250);
    CHECK_TRUE(badResp.find("Password:") != std::string::npos);
    CHECK_TRUE(badResp.find("Authentication failed") != std::string::npos);
    CHECK_TRUE(badResp.find("libmeshtastic_test_banner") == std::string::npos);
    CHECK_TRUE(badResp.find("Available commands:") == std::string::npos);

    // 2. Correct password must authenticate and run commands, plus handle null NVM safely
    std::string goodResp = tcpRoundTrip(
        port,
        "s3cr3tPass\nhelp\nauthchan\nauthchan add 1 AAAA\nadmin\nadmin add !11223344 AAAA\nmate\nmate add !11223344 AAAA\nexit\n",
        300);
    CHECK_TRUE(goodResp.find("Password:") != std::string::npos);
    CHECK_TRUE(goodResp.find("libmeshtastic_test_banner") != std::string::npos);
    CHECK_TRUE(goodResp.find("Available commands:") != std::string::npos);
    CHECK_TRUE(goodResp.find("nvm is not configured!") != std::string::npos);
}

TEST_GROUP(MeshClientReconnect)
{
    int master1 = -1;
    int master2 = -1;
    std::string slave1;
    std::string slave2;
    std::string symlinkPath;
    std::shared_ptr<MeshClient> client;

    void setup() override
    {
        char linkBuf[128];
        snprintf(linkBuf, sizeof(linkBuf), "/tmp/meshmon_test_pty_%d",
                 static_cast<int>(getpid()));
        symlinkPath = linkBuf;
        unlink(symlinkPath.c_str());

        CHECK_TRUE(createPtyPair(master1, slave1));
        CHECK_TRUE(createPtyPair(master2, slave2));
        CHECK_EQUAL(0, symlink(slave1.c_str(), symlinkPath.c_str()));
        client = std::make_shared<MeshClient>();
        client->setReconnectIntervalMs(50);
    }

    void teardown() override
    {
        if (client) {
            client->detach();
            client->join();
            client.reset();
        }
        if (!symlinkPath.empty()) {
            unlink(symlinkPath.c_str());
        }
        if (master1 >= 0) {
            close(master1);
            master1 = -1;
        }
        if (master2 >= 0) {
            close(master2);
            master2 = -1;
        }
    }
};

TEST(MeshClientReconnect, ReconnectsAfterPtyDisconnect)
{
    CHECK_TRUE(client->attachSerial(symlinkPath));

    // First PTY master should receive initial want_config packet
    CHECK_TRUE(waitAndReadFrame(master1, 1500));

    // Point the device path symlink at the replacement PTY slave, then drop master1
    std::string tmpLink = symlinkPath + ".tmp";
    unlink(tmpLink.c_str());
    CHECK_EQUAL(0, symlink(slave2.c_str(), tmpLink.c_str()));
    CHECK_EQUAL(0, rename(tmpLink.c_str(), symlinkPath.c_str()));

    close(master1);
    master1 = -1;

    // MeshClient::run() must stay alive, re-attach to symlinkPath (slave2),
    // and transmit a fresh want_config frame on master2
    bool reconnectedAndSentWantConfig = waitAndReadFrame(master2, 2500);
    client->detach();
    client->join();
    CHECK_TRUE(reconnectedAndSentWantConfig);
}

TEST_GROUP(HomeChatCallbacks)
{
};

TEST(HomeChatCallbacks, ConcurrentCallbackMutateAndPrintf)
{
    ConcurrentClientHarness harness;
    std::atomic<bool> printerReady(false);
    std::atomic<bool> running(true);
    std::atomic<size_t> cbCount1(0);
    std::atomic<size_t> cbCount2(0);

    harness.addPrintfCallback(&cbCount2, dummyVprintfCb);

    std::thread printer([&]() {
        printerReady.store(true);
        int iter = 0;
        while (running.load()) {
            harness.printf("concurrent log message %d\n", iter++);
            (void) harness.isAuthChannel("Ch0");
            bool isAdmin = false;
            bool isMate = false;
            harness.getAuthority(0x10000001U, isAdmin, isMate);
            (void) harness.admins().size();
            (void) harness.mates().size();
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    std::thread mutator([&]() {
        while (!printerReady.load()) {
            std::this_thread::yield();
        }
        for (int i = 0; i < 200; i++) {
            harness.addPrintfCallback(&cbCount1, dummyVprintfCb);
            harness.addPrintfCallback(&cbCount2, dummyVprintfCb);
            meshtastic_ChannelSettings_psk_t psk;
            memset(&psk, 0, sizeof(psk));
            psk.size = 16;
            harness.addAuthChannel("Ch" + std::to_string(i % 4), psk);
            meshtastic_User_public_key_t pubkey;
            memset(&pubkey, 0, sizeof(pubkey));
            pubkey.size = 32;
            harness.addAdmin(0x10000000U + static_cast<uint32_t>(i % 8), pubkey);
            harness.addMate(0x20000000U + static_cast<uint32_t>(i % 8), pubkey);
            harness.delPrintfCallback(&cbCount1, dummyVprintfCb);
            if ((i % 25) == 0) {
                harness.clearAuthchansAdminsMates();
            }
        }
        while (cbCount2.load() == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        running.store(false);
    });

    mutator.join();
    printer.join();
    harness.delPrintfCallback(&cbCount1, dummyVprintfCb);
    harness.delPrintfCallback(&cbCount2, dummyVprintfCb);
    CHECK_TRUE(cbCount2.load() > 0);
}

TEST_GROUP(SimpleClientConcurrency)
{
};

TEST(SimpleClientConcurrency, ConcurrentPacketIngestionAndLookups)
{
    ConcurrentClientHarness client;
    std::atomic<int> readersReady(0);
    std::atomic<bool> stopFlag(false);

    auto readerFn = [&]() {
        readersReady.fetch_add(1);
        while (!stopFlag.load()) {
            client.lock();
            for (const auto &kv : client.nodeInfos()) {
                uint32_t id = kv.first;
                (void) client.hopsAway(id);
                (void) client.lookupShortName(id);
                (void) client.lookupLongName(id);
            }
            (void) client.positions().size();
            (void) client.getId("N01");
            client.unlock();
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    };

    std::thread r1(readerFn);
    std::thread r2(readerFn);

    std::thread writer([&]() {
        while (readersReady.load() < 2) {
            std::this_thread::yield();
        }
        for (uint32_t i = 0; i < 150; i++) {
            uint32_t nodeNum = 0x55000000U + (i % 16);

            meshtastic_FromRadio nf;
            memset(&nf, 0, sizeof(nf));
            nf.which_payload_variant = meshtastic_FromRadio_node_info_tag;
            nf.node_info.num = nodeNum;
            nf.node_info.has_user = true;
            snprintf(nf.node_info.user.short_name,
                     sizeof(nf.node_info.user.short_name), "N%02u", i % 16);
            snprintf(nf.node_info.user.long_name,
                     sizeof(nf.node_info.user.long_name), "Node-%02u", i % 16);
            client.injectFromRadio(nf);

            meshtastic_FromRadio pf;
            memset(&pf, 0, sizeof(pf));
            pf.which_payload_variant = meshtastic_FromRadio_packet_tag;
            pf.packet.from = nodeNum;
            pf.packet.to = 0xffffffffU;
            pf.packet.hop_start = 3;
            pf.packet.hop_limit = 1;
            pf.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            pf.packet.decoded.portnum = meshtastic_PortNum_POSITION_APP;

            meshtastic_Position pos = meshtastic_Position_init_zero;
            pos.has_latitude_i = true;
            pos.latitude_i = 250330000 + static_cast<int32_t>(i * 100);
            pos.has_longitude_i = true;
            pos.longitude_i = 1215654000 + static_cast<int32_t>(i * 100);
            pb_ostream_t posStream = pb_ostream_from_buffer(
                pf.packet.decoded.payload.bytes,
                sizeof(pf.packet.decoded.payload.bytes));
            if (pb_encode(&posStream, meshtastic_Position_fields, &pos)) {
                pf.packet.decoded.payload.size = posStream.bytes_written;
                client.injectFromRadio(pf);
            }
        }
        stopFlag.store(true);
    });

    writer.join();
    r1.join();
    r2.join();

    CHECK_EQUAL(16u, client.nodeInfos().size());
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
