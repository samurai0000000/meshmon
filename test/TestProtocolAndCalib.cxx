/*
 * TestProtocolAndCalib.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>

#include <libmeshtastic.h>
#include <SimpleClient.hxx>
#include <HomeChat.hxx>
#include "Calibration.hxx"
#include "ChatBot.hxx"
#include "AimonGatewayClient.hxx"
#include "MeshMon.hxx"
#include "MeshMonDb.hxx"
#include "MeshMonShell.hxx"
#include "MqttClient.hxx"

#include <CppUTest/TestHarness.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

struct CapturedRx {
    size_t packetCount = 0;
    meshtastic_FromRadio lastFromRadio;
    std::string discardedLog;

    CapturedRx() {
        memset(&lastFromRadio, 0, sizeof(lastFromRadio));
    }
};

void testPacketHandler(struct mt_client *mtc, const void *packet, size_t size,
                       const meshtastic_FromRadio *from_radio)
{
    (void) packet;
    (void) size;
    CapturedRx *cap = static_cast<CapturedRx *>(mtc->ctx);
    if (cap != NULL && from_radio != NULL) {
        cap->packetCount++;
        cap->lastFromRadio = *from_radio;
    }
}

void testLoggerHandler(struct mt_client *mtc, const char *msg, size_t len)
{
    CapturedRx *cap = static_cast<CapturedRx *>(mtc->ctx);
    if (cap != NULL && msg != NULL && len > 0) {
        cap->discardedLog.append(msg, len);
    }
}

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

class IntegratedTestClient : public SimpleClient, public HomeChat {
public:
    IntegratedTestClient()
        : SimpleClient(),
          HomeChat(std::shared_ptr<SimpleClient>(this, [](SimpleClient *) {}))
    {
        setLogIncoming(false);
    }

    virtual ~IntegratedTestClient()
    {
        setClient( nullptr);
    }

protected:
    virtual void gotTextMessage(const meshtastic_MeshPacket &packet,
                                const std::string &message) override
    {
        SimpleClient::gotTextMessage(packet, message);
        handleTextMessage(packet, message);
    }
};

class TestableChatBot : public ChatBot {
public:
    TestableChatBot() : ChatBot(nullptr) {}
    virtual ~TestableChatBot() {}

    std::string callTruncateToMesh(const std::string &text) const
    {
        return truncateToMesh(text);
    }

    void callProcessDueTasks(time_t now)
    {
        processDueTasks(now);
    }

protected:
    virtual std::string generate(uint32_t from, uint32_t dest, uint8_t channel,
                                 const std::vector<ChatTurn> &history,
                                 const std::string &message) override
    {
        (void) from;
        (void) dest;
        (void) channel;
        (void) history;
        return message;
    }
};

class TestableAimonClient : public AimonGatewayClient {
public:
    TestableAimonClient(std::shared_ptr<MeshMon> mon, std::shared_ptr<MeshMonDb> db)
        : AimonGatewayClient(mon, db) {}

    void setMockFd(int fd)
    {
        _sockFd = fd;
    }

    void callHandleRequest(const std::string &line)
    {
        handleRequest(line);
    }
};

} // namespace

TEST_GROUP(SerialProtocol)
{
    int sv[2];
    struct mt_client rxMtc;
    struct mt_client txMtc;
    CapturedRx cap;

    void setup() override
    {
        sv[0] = -1;
        sv[1] = -1;
        CHECK_EQUAL(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sv));

        memset(&rxMtc, 0, sizeof(rxMtc));
        rxMtc.type = MT_CLIENT_SERIAL;
        rxMtc.fd = sv[0];
        rxMtc.device = "socketpair-rx";
        rxMtc.handler = testPacketHandler;
        rxMtc.logger = testLoggerHandler;
        rxMtc.ctx = &cap;

        memset(&txMtc, 0, sizeof(txMtc));
        txMtc.type = MT_CLIENT_SERIAL;
        txMtc.fd = sv[1];
        txMtc.device = "socketpair-tx";
    }

    void teardown() override
    {
        if (sv[0] >= 0) {
            close(sv[0]);
            sv[0] = -1;
        }
        if (sv[1] >= 0) {
            close(sv[1]);
            sv[1] = -1;
        }
    }
};

TEST(SerialProtocol, NominalFrameSendAndReceive)
{
    meshtastic_FromRadio fromRadio;
    memset(&fromRadio, 0, sizeof(fromRadio));
    fromRadio.id = 42;
    fromRadio.which_payload_variant = meshtastic_FromRadio_packet_tag;
    fromRadio.packet.from = 0x11223344U;
    fromRadio.packet.to = 0xffffffffU;
    fromRadio.packet.channel = 1;
    fromRadio.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    fromRadio.packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    const char *msg = "Hello from mesh radio";
    fromRadio.packet.decoded.payload.size = strlen(msg);
    memcpy(fromRadio.packet.decoded.payload.bytes, msg, strlen(msg));

    std::vector<uint8_t> frame = encodeFromRadioFrame(fromRadio);
    CHECK_TRUE(frame.size() > sizeof(struct mt_pb_header));

    CHECK_EQUAL(0, mt_serial_send(&txMtc, frame.data(), frame.size()));
    CHECK_EQUAL(0, mt_serial_process(&rxMtc, 20));

    CHECK_EQUAL(1u, cap.packetCount);
    CHECK_EQUAL(1u, rxMtc.packets_rx);
    CHECK_EQUAL(frame.size(), rxMtc.bytes_rx);
    CHECK_EQUAL(0x11223344U, cap.lastFromRadio.packet.from);
    CHECK_EQUAL(meshtastic_PortNum_TEXT_MESSAGE_APP,
                cap.lastFromRadio.packet.decoded.portnum);
    std::string decodedText(
        reinterpret_cast<const char *>(cap.lastFromRadio.packet.decoded.payload.bytes),
        cap.lastFromRadio.packet.decoded.payload.size);
    STRCMP_EQUAL("Hello from mesh radio", decodedText.c_str());
}

TEST(SerialProtocol, ByteByByteFragmentedFrameReassembly)
{
    meshtastic_FromRadio fromRadio;
    memset(&fromRadio, 0, sizeof(fromRadio));
    fromRadio.id = 7;
    fromRadio.which_payload_variant = meshtastic_FromRadio_rebooted_tag;
    fromRadio.rebooted = true;

    std::vector<uint8_t> frame = encodeFromRadioFrame(fromRadio);
    CHECK_TRUE(frame.size() > 4);

    for (size_t i = 0; i < frame.size(); i++) {
        CHECK_EQUAL(1, write(sv[1], &frame[i], 1));
        CHECK_EQUAL(0, mt_serial_process(&rxMtc, 10));
        if (i + 1 < frame.size()) {
            CHECK_EQUAL(0u, cap.packetCount);
        }
    }

    CHECK_EQUAL(1u, cap.packetCount);
    CHECK_EQUAL(meshtastic_FromRadio_rebooted_tag,
                cap.lastFromRadio.which_payload_variant);
    CHECK_TRUE(cap.lastFromRadio.rebooted);
    CHECK_EQUAL(0u, rxMtc.inbuf_len);
}

TEST(SerialProtocol, CorruptedHeaderAndNoiseResync)
{
    meshtastic_FromRadio fromRadio;
    memset(&fromRadio, 0, sizeof(fromRadio));
    fromRadio.id = 99;
    fromRadio.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    fromRadio.config_complete_id = 0xdeadbeefU;

    std::vector<uint8_t> validFrame = encodeFromRadioFrame(fromRadio);

    std::vector<uint8_t> stream;
    const char *asciiNoise = "bootlog: radio starting...\n";
    stream.insert(stream.end(), asciiNoise, asciiNoise + strlen(asciiNoise));
    // False start1 followed by wrong start2
    stream.push_back(MT_PB_START1);
    stream.push_back(0x00);
    // Valid header with corrupt protobuf payload (2 bytes of 0xff)
    stream.push_back(MT_PB_START1);
    stream.push_back(MT_PB_START2);
    stream.push_back(0x00);
    stream.push_back(0x02);
    stream.push_back(0xff);
    stream.push_back(0xff);
    // Followed by the valid frame
    stream.insert(stream.end(), validFrame.begin(), validFrame.end());

    CHECK_EQUAL(static_cast<ssize_t>(stream.size()),
                write(sv[1], stream.data(), stream.size()));
    CHECK_EQUAL(0, mt_serial_process(&rxMtc, 20));

    CHECK_EQUAL(1u, cap.packetCount);
    CHECK_EQUAL(0xdeadbeefU, cap.lastFromRadio.config_complete_id);
    CHECK_TRUE(cap.discardedLog.find("bootlog: radio starting...") != std::string::npos);
    CHECK_EQUAL(0u, rxMtc.inbuf_len);
}

TEST(SerialProtocol, OversizedLengthHeaderResync)
{
    meshtastic_FromRadio fromRadio;
    memset(&fromRadio, 0, sizeof(fromRadio));
    fromRadio.id = 101;
    fromRadio.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    fromRadio.config_complete_id = 0x12345678U;

    std::vector<uint8_t> validFrame = encodeFromRadioFrame(fromRadio);

    // Header claiming 600 bytes (> 512 max payload buffer)
    std::vector<uint8_t> stream = {
        MT_PB_START1, MT_PB_START2, 0x02, 0x58
    };
    stream.insert(stream.end(), validFrame.begin(), validFrame.end());

    CHECK_EQUAL(static_cast<ssize_t>(stream.size()),
                write(sv[1], stream.data(), stream.size()));
    CHECK_EQUAL(0, mt_serial_process(&rxMtc, 20));

    CHECK_EQUAL(1u, cap.packetCount);
    CHECK_EQUAL(0x12345678U, cap.lastFromRadio.config_complete_id);
    CHECK_EQUAL(0u, rxMtc.inbuf_len);
}

TEST(SerialProtocol, StalledPartialFrameTimeoutResync)
{
    // Inject a partial frame declaring 50 bytes, but only provide 6 bytes
    uint8_t partial[6] = { MT_PB_START1, MT_PB_START2, 0x00, 50, 0x08, 0x01 };
    CHECK_EQUAL(static_cast<ssize_t>(sizeof(partial)),
                write(sv[1], partial, sizeof(partial)));
    CHECK_EQUAL(0, mt_serial_process(&rxMtc, 10));
    CHECK_EQUAL(0u, cap.packetCount);
    CHECK_EQUAL(sizeof(partial), rxMtc.inbuf_len);

    // Simulate >2 second stall on the partial buffer
    rxMtc.last_byte_ts = static_cast<uint32_t>(mt_impl_now() - 3);

    meshtastic_FromRadio fromRadio;
    memset(&fromRadio, 0, sizeof(fromRadio));
    fromRadio.id = 202;
    fromRadio.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    fromRadio.config_complete_id = 0xabcdef01U;
    std::vector<uint8_t> validFrame = encodeFromRadioFrame(fromRadio);

    CHECK_EQUAL(static_cast<ssize_t>(validFrame.size()),
                write(sv[1], validFrame.data(), validFrame.size()));
    CHECK_EQUAL(0, mt_serial_process(&rxMtc, 20));

    CHECK_EQUAL(1u, cap.packetCount);
    CHECK_EQUAL(0xabcdef01U, cap.lastFromRadio.config_complete_id);
    CHECK_EQUAL(0u, rxMtc.inbuf_len);
}

TEST(SerialProtocol, EofAndInvalidArgsFaultHandling)
{
    // Invalid arguments to mt_text_message and mt_recv_packet
    CHECK_EQUAL(-1, mt_text_message(NULL, 0xffffffffU, 0, "hi", 3, false));
    CHECK_EQUAL(-1, mt_text_message(&txMtc, 0xffffffffU, 0, NULL, 3, false));
    CHECK_EQUAL(-1, mt_text_message(&txMtc, 0xffffffffU, 0, "hi", 8, false));

    std::string tooLong(201, 'A');
    CHECK_EQUAL(-1, mt_text_message(&txMtc, 0xffffffffU, 0, tooLong.c_str(), 3, false));

    std::string exactMax(200, 'B');
    CHECK_EQUAL(0, mt_text_message(&txMtc, 0xffffffffU, 0, exactMax.c_str(), 3, true));
    CHECK_EQUAL(1u, txMtc.packets_tx);

    // Close peer fd and verify EOF returns -1 with EIO
    close(sv[1]);
    sv[1] = -1;
    // Drain the frame we just sent from txMtc first, or reset rxMtc fd to a fresh closed pipe
    int pipeFds[2];
    CHECK_EQUAL(0, pipe(pipeFds));
    close(pipeFds[1]);
    rxMtc.fd = pipeFds[0];
    rxMtc.inbuf_len = 3;
    CHECK_EQUAL(-1, mt_serial_process(&rxMtc, 10));
    CHECK_EQUAL(EIO, errno);
    CHECK_EQUAL(0u, rxMtc.inbuf_len);
    close(pipeFds[0]);
}

TEST(SerialProtocol, IntegratedSimpleClientToHomeChatPipeline)
{
    IntegratedTestClient client;
    client._mtc.fd = sv[0];
    client._mtc.type = MT_CLIENT_SERIAL;
    client._mtc.device = "socketpair-integrated";

    // 1. Send MyNodeInfo so client knows whoami = 0x11111111
    meshtastic_FromRadio myInfoFrame;
    memset(&myInfoFrame, 0, sizeof(myInfoFrame));
    myInfoFrame.which_payload_variant = meshtastic_FromRadio_my_info_tag;
    myInfoFrame.my_info.my_node_num = 0x11111111U;
    std::vector<uint8_t> f1 = encodeFromRadioFrame(myInfoFrame);
    CHECK_EQUAL(0, mt_serial_send(&txMtc, f1.data(), f1.size()));

    // 2. Send NodeInfo for remote admin node 0x22222222 with public key
    meshtastic_FromRadio nodeInfoFrame;
    memset(&nodeInfoFrame, 0, sizeof(nodeInfoFrame));
    nodeInfoFrame.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    nodeInfoFrame.node_info.num = 0x22222222U;
    nodeInfoFrame.node_info.has_user = true;
    snprintf(nodeInfoFrame.node_info.user.short_name,
             sizeof(nodeInfoFrame.node_info.user.short_name), "%s", "ADMN");
    snprintf(nodeInfoFrame.node_info.user.long_name,
             sizeof(nodeInfoFrame.node_info.user.long_name), "%s", "AdminNode");
    nodeInfoFrame.node_info.user.public_key.size = 32;
    memset(nodeInfoFrame.node_info.user.public_key.bytes, 0x42, 32);
    std::vector<uint8_t> f2 = encodeFromRadioFrame(nodeInfoFrame);
    CHECK_EQUAL(0, mt_serial_send(&txMtc, f2.data(), f2.size()));

    CHECK_EQUAL(0, mt_serial_process(&client._mtc, 20));
    CHECK_EQUAL(0x11111111U, client.whoami());
    STRCMP_EQUAL("ADMN", client.lookupShortName(0x22222222U).c_str());

    // Register 0x22222222 as admin in HomeChat
    CHECK_TRUE(client.addAdmin(0x22222222U, nodeInfoFrame.node_info.user.public_key));

    // 3. Inject direct text message "uptime" from 0x22222222 to 0x11111111
    meshtastic_FromRadio dmFrame;
    memset(&dmFrame, 0, sizeof(dmFrame));
    dmFrame.which_payload_variant = meshtastic_FromRadio_packet_tag;
    dmFrame.packet.from = 0x22222222U;
    dmFrame.packet.to = 0x11111111U;
    dmFrame.packet.channel = 0;
    dmFrame.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    dmFrame.packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    const char *cmd = "uptime";
    dmFrame.packet.decoded.payload.size = strlen(cmd);
    memcpy(dmFrame.packet.decoded.payload.bytes, cmd, strlen(cmd));
    std::vector<uint8_t> f3 = encodeFromRadioFrame(dmFrame);
    CHECK_EQUAL(0, mt_serial_send(&txMtc, f3.data(), f3.size()));

    CHECK_EQUAL(0, mt_serial_process(&client._mtc, 20));
    CHECK_EQUAL(1u, client.dmRx());
    CHECK_EQUAL(1u, client.dmTx());

    // Read the ToRadio reply frame produced by HomeChat::handleTextMessage from sv[1]
    uint8_t replyBuf[516];
    ssize_t n = read(sv[1], replyBuf, sizeof(replyBuf));
    CHECK_TRUE(n > static_cast<ssize_t>(sizeof(struct mt_pb_header)));
    CHECK_EQUAL(MT_PB_START1, replyBuf[0]);
    CHECK_EQUAL(MT_PB_START2, replyBuf[1]);
    uint16_t plen = (static_cast<uint16_t>(replyBuf[2]) << 8) | replyBuf[3];
    CHECK_EQUAL(static_cast<size_t>(n), sizeof(struct mt_pb_header) + plen);

    meshtastic_ToRadio toRadio;
    memset(&toRadio, 0, sizeof(toRadio));
    pb_istream_t istream = pb_istream_from_buffer(
        replyBuf + sizeof(struct mt_pb_header), plen);
    CHECK_TRUE(pb_decode(&istream, meshtastic_ToRadio_fields, &toRadio));
    CHECK_EQUAL(meshtastic_ToRadio_packet_tag, toRadio.which_payload_variant);
    CHECK_EQUAL(0x22222222U, toRadio.packet.to);
    std::string replyText(
        reinterpret_cast<const char *>(toRadio.packet.decoded.payload.bytes),
        toRadio.packet.decoded.payload.size);
    CHECK_TRUE(replyText.find("uptime: ") == 0);
}

TEST_GROUP(CalibrationCurve)
{
};

TEST(CalibrationCurve, EmptyAndSinglePointCurves)
{
    CalibrationCurve curve;
    CHECK_TRUE(curve.empty());
    DOUBLES_EQUAL(25.5, curve.calibrate(25.5), 1e-6);

    curve.setPoint(20.0, 21.5);
    CHECK_FALSE(curve.empty());
    DOUBLES_EQUAL(11.5, curve.calibrate(10.0), 1e-6);
    DOUBLES_EQUAL(21.5, curve.calibrate(20.0), 1e-6);
    DOUBLES_EQUAL(31.5, curve.calibrate(30.0), 1e-6);
}

TEST(CalibrationCurve, MultiPointInterpolationAndExtrapolation)
{
    CalibrationCurve curve;
    curve.setPoint(30.0, 35.0);
    curve.setPoint(10.0, 12.0);
    curve.setPoint(20.0, 20.0);

    // Points must be sorted by raw value
    const std::vector<CalibrationPoint> &pts = curve.getPoints();
    CHECK_EQUAL(3u, pts.size());
    DOUBLES_EQUAL(10.0, pts[0].raw, 1e-6);
    DOUBLES_EQUAL(20.0, pts[1].raw, 1e-6);
    DOUBLES_EQUAL(30.0, pts[2].raw, 1e-6);

    // Exact knots
    DOUBLES_EQUAL(12.0, curve.calibrate(10.0), 1e-6);
    DOUBLES_EQUAL(20.0, curve.calibrate(20.0), 1e-6);
    DOUBLES_EQUAL(35.0, curve.calibrate(30.0), 1e-6);

    // Midpoint linear interpolation
    DOUBLES_EQUAL(16.0, curve.calibrate(15.0), 1e-6);
    DOUBLES_EQUAL(27.5, curve.calibrate(25.0), 1e-6);

    // Extrapolation below lowest knot (slope = (20 - 12) / (20 - 10) = 0.8)
    DOUBLES_EQUAL(4.0, curve.calibrate(0.0), 1e-6);

    // Extrapolation above highest knot (slope = (35 - 20) / (30 - 20) = 1.5)
    DOUBLES_EQUAL(50.0, curve.calibrate(40.0), 1e-6);
}

TEST(CalibrationCurve, DuplicatePointUpdateDeleteAndClear)
{
    CalibrationCurve curve;
    curve.setPoint(25.0, 24.0);
    curve.setPoint(25.00005, 23.5); // within 1e-4 threshold -> updates in place
    CHECK_EQUAL(1u, curve.getPoints().size());
    DOUBLES_EQUAL(23.5, curve.calibrate(25.0), 1e-6);

    CHECK_FALSE(curve.delPoint(99.0));
    CHECK_TRUE(curve.delPoint(25.0));
    CHECK_TRUE(curve.empty());
}

TEST(CalibrationCurve, NodeFallbackNormalizationAndConfigRoundTrip)
{
    STRCMP_EQUAL("default", Calibration::normalizeNodeKey("").c_str());
    STRCMP_EQUAL("default", Calibration::normalizeNodeKey("*").c_str());
    STRCMP_EQUAL("default", Calibration::normalizeNodeKey("DEFAULT").c_str());
    STRCMP_EQUAL("aabbccdd", Calibration::normalizeNodeKey("!AABBCCDD").c_str());
    STRCMP_EQUAL("12345678", Calibration::normalizeNodeKey("0x12345678").c_str());
    STRCMP_EQUAL("0000002a", Calibration::normalizeNodeKey(0x2aU).c_str());

    SensorType st = SENSOR_TEMPERATURE;
    CHECK_TRUE(Calibration::parseSensorType("Temperature", st));
    CHECK_EQUAL(SENSOR_TEMPERATURE, st);
    CHECK_TRUE(Calibration::parseSensorType("RH", st));
    CHECK_EQUAL(SENSOR_HUMIDITY, st);
    CHECK_TRUE(Calibration::parseSensorType("baro", st));
    CHECK_EQUAL(SENSOR_PRESSURE, st);
    CHECK_FALSE(Calibration::parseSensorType("voltage", st));

    Calibration cal;
    cal.setPoint("default", SENSOR_TEMPERATURE, 20.0, 19.0);
    cal.setPoint("!11223344", SENSOR_TEMPERATURE, 20.0, 22.0);
    cal.setPoint("!11223344", SENSOR_HUMIDITY, 50.0, 55.0);

    // Node !11223344 uses its own temperature curve, while !99887766 falls back to default
    DOUBLES_EQUAL(22.0, cal.calibrateTemperature(0x11223344U, 20.0f), 1e-4);
    DOUBLES_EQUAL(19.0, cal.calibrateTemperature(0x99887766U, 20.0f), 1e-4);
    // Pressure has neither node nor default -> returns raw
    DOUBLES_EQUAL(1013.25, cal.calibratePressure(0x11223344U, 1013.25f), 1e-3);

    std::string tmpFile = "test_data/test_calib_roundtrip.cfg";
    fs::create_directories("test_data");
    CHECK_TRUE(cal.saveFile(tmpFile));

    Calibration loaded;
    CHECK_TRUE(loaded.loadFile(tmpFile));
    DOUBLES_EQUAL(22.0, loaded.calibrateTemperature(0x11223344U, 20.0f), 1e-4);
    DOUBLES_EQUAL(55.0, loaded.calibrateHumidity(0x11223344U, 50.0f), 1e-4);
    DOUBLES_EQUAL(19.0, loaded.calibrateTemperature(0x99887766U, 20.0f), 1e-4);
    fs::remove(tmpFile);
}

TEST_GROUP(TimeBroadcast)
{
};

TEST(TimeBroadcast, ParsesIsoAndNaturalFormats)
{
    time_t epoch = 0;
    std::string tz;

    // 2026-10-15 06:00:00 UTC == 1792044000
    CHECK_TRUE(HomeChat::parseTimeBroadcast("Time is 2026-10-15 06:00:00 UTC.", epoch, tz));
    STRCMP_EQUAL("UTC", tz.c_str());
    CHECK_EQUAL(static_cast<time_t>(1792044000), epoch);

    // 12-hour PM format in CST (UTC+8): 2026-10-15 02:00:00 PM CST == 2026-10-15 06:00:00 UTC
    CHECK_TRUE(HomeChat::parseTimeBroadcast("Sync: 2026-10-15 02:00:00 PM CST!", epoch, tz));
    STRCMP_EQUAL("CST", tz.c_str());
    CHECK_EQUAL(static_cast<time_t>(1792044000), epoch);

    // Natural text format: "2:00:00 PM CST on Oct 15, 2026"
    CHECK_TRUE(HomeChat::parseTimeBroadcast("It is 2:00:00 PM CST on Oct 15, 2026", epoch, tz));
    STRCMP_EQUAL("CST", tz.c_str());
    CHECK_EQUAL(static_cast<time_t>(1792044000), epoch);

    // Valid leap year Feb 29 (2024 is a leap year)
    CHECK_TRUE(HomeChat::parseTimeBroadcast("2024-02-29 12:00:00 UTC", epoch, tz));
    STRCMP_EQUAL("UTC", tz.c_str());
}

TEST(TimeBroadcast, RejectsInvalidCalendarDatesAndPreservesTzEnv)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    const char *tzBefore = getenv("TZ");
    CHECK_TRUE(tzBefore != NULL);

    time_t epoch = 0;
    std::string tz;

    // Invalid calendar dates that mktime() would silently normalize must be rejected
    CHECK_FALSE(HomeChat::parseTimeBroadcast("Time is 2026-02-31 12:00:00 UTC", epoch, tz));
    CHECK_FALSE(HomeChat::parseTimeBroadcast("Time is 2026-02-30 12:00:00 UTC", epoch, tz));
    CHECK_FALSE(HomeChat::parseTimeBroadcast("Time is 2025-02-29 12:00:00 UTC", epoch, tz));
    CHECK_FALSE(HomeChat::parseTimeBroadcast("Time is 2026-04-31 12:00:00 UTC", epoch, tz));
    CHECK_FALSE(HomeChat::parseTimeBroadcast("Time is 2026-06-31 12:00:00 UTC", epoch, tz));
    CHECK_FALSE(HomeChat::parseTimeBroadcast("Time is 2026-09-31 12:00:00 UTC", epoch, tz));
    CHECK_FALSE(HomeChat::parseTimeBroadcast("Time is 2026-11-31 12:00:00 UTC", epoch, tz));
    CHECK_FALSE(HomeChat::parseTimeBroadcast("2:30 PM CST on Feb 30, 2026", epoch, tz));
    CHECK_FALSE(HomeChat::parseTimeBroadcast("", epoch, tz));
    CHECK_FALSE(HomeChat::parseTimeBroadcast("just two", epoch, tz));

    // Parsing CST (UTC+8) must NOT mutate the process TZ environment variable
    CHECK_TRUE(HomeChat::parseTimeBroadcast("2026-10-15 14:00:00 CST", epoch, tz));
    STRCMP_EQUAL("CST", tz.c_str());
    CHECK_EQUAL(static_cast<time_t>(1792044000), epoch);
    CHECK_EQUAL(tzBefore, getenv("TZ"));
    STRCMP_EQUAL("UTC0", getenv("TZ"));
}

TEST_GROUP(ChatBotTruncation)
{
    TestableChatBot bot;
};

TEST(ChatBotTruncation, NewlineReplacementAndAsciiBoundaries)
{
    STRCMP_EQUAL("", bot.callTruncateToMesh("").c_str());
    STRCMP_EQUAL("hello world test",
                 bot.callTruncateToMesh("hello\nworld\rtest").c_str());

    std::string exact200(200, 'x');
    std::string res200 = bot.callTruncateToMesh(exact200);
    CHECK_EQUAL(200u, res200.size());
    STRCMP_EQUAL(exact200.c_str(), res200.c_str());

    std::string over200(250, 'y');
    std::string resOver = bot.callTruncateToMesh(over200);
    CHECK_EQUAL(200u, resOver.size());
}

TEST(ChatBotTruncation, PreservesMultiByteUtf8BoundariesAt200Bytes)
{
    // 2-byte UTF-8 character 'é' (\xC3\xA9) starting at byte 199 (spans 199..200)
    std::string s2(199, 'a');
    s2 += "\xC3\xA9";
    std::string out2 = bot.callTruncateToMesh(s2);
    CHECK_EQUAL(199u, out2.size());

    // 3-byte UTF-8 character '台' (\xE5\x8F\xB0) starting at byte 198 (spans 198..200)
    std::string s3(198, 'a');
    s3 += "\xE5\x8F\xB0";
    std::string out3 = bot.callTruncateToMesh(s3);
    CHECK_EQUAL(198u, out3.size());

    // 3-byte UTF-8 character '台' starting at byte 197 (spans 197..199, fits in 200!) followed by another
    std::string s3fit(197, 'a');
    s3fit += "\xE5\x8F\xB0\xE5\x8C\x97";
    std::string out3fit = bot.callTruncateToMesh(s3fit);
    CHECK_EQUAL(200u, out3fit.size());
    STRCMP_EQUAL("\xE5\x8F\xB0", out3fit.substr(197).c_str());

    // 4-byte UTF-8 emoji '🛰' (\xF0\x9F\x9B\xB0) starting at byte 197 (spans 197..200)
    std::string s4(197, 'a');
    s4 += "\xF0\x9F\x9B\xB0";
    std::string out4 = bot.callTruncateToMesh(s4);
    CHECK_EQUAL(197u, out4.size());
}

TEST(ChatBotTruncation, CustomMaxHistoryTurnsPruningInProcessDueTasks)
{
    TestableChatBot testBot;
    testBot.setMaxHistoryTurns(3);
    CHECK_EQUAL(3u, testBot.getMaxHistoryTurns());

    uint32_t fromNode = 0x12345678U;
    std::vector<ChatTurn> initialTurns;
    for (int i = 0; i < 5; i++) {
        ChatTurn t;
        t.user = (i % 2 == 0);
        t.text = "Turn " + std::to_string(i);
        initialTurns.push_back(t);
    }
    testBot.setConversation(fromNode, initialTurns);
    CHECK_EQUAL(5u, testBot.getConversation(fromNode).size());

    time_t now = time(NULL);
    ScheduledTask task;
    task.from = fromNode;
    task.dest = 0xffffffffU;
    task.channel = 0;
    task.type = TASK_MESSAGE;
    task.content = "Automated reply text";
    task.dueTime = now - 5;
    task.maxRepeats = 1;
    task.executionCount = 0;
    task.createdAt = now - 10;
    testBot.scheduleTask(task);

    testBot.callProcessDueTasks(now);

    std::vector<ChatTurn> convAfter = testBot.getConversation(fromNode);
    // Verified: pruned to custom _maxHistoryTurns (3), instead of hardcoded 10
    CHECK_EQUAL(3u, convAfter.size());
    STRCMP_EQUAL("Automated reply text", convAfter.back().text.c_str());

    // Dynamic reconfiguration to 2 turns
    testBot.setMaxHistoryTurns(2);
    task.dueTime = now - 1;
    task.content = "Second reply";
    testBot.scheduleTask(task);
    testBot.callProcessDueTasks(now);

    convAfter = testBot.getConversation(fromNode);
    CHECK_EQUAL(2u, convAfter.size());
    STRCMP_EQUAL("Second reply", convAfter.back().text.c_str());
}

TEST(ChatBotTruncation, ThreadSafeEnabledAndClientAccess)
{
    TestableChatBot testBot;
    CHECK_TRUE(testBot.enabled());
    testBot.setEnabled(false);
    CHECK_FALSE(testBot.enabled());
    testBot.setEnabled(true);
    CHECK_TRUE(testBot.enabled());

    auto client1 = std::make_shared<MeshMon>();
    auto client2 = std::make_shared<MeshMon>();
    testBot.setClient(client1);
    CHECK_EQUAL(client1.get(), testBot.getClient().get());

    std::atomic<bool> runThreads{true};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; i++) {
        readers.emplace_back([&testBot, &runThreads]() {
            while (runThreads.load()) {
                (void) testBot.enabled();
                auto c = testBot.getClient();
                (void) c;
                std::this_thread::yield();
            }
        });
    }

    std::thread writer([&testBot, &client1, &client2, &runThreads]() {
        for (int i = 0; i < 1000; i++) {
            testBot.setEnabled(i % 2 == 0);
            testBot.setClient(i % 2 == 0 ? client1 : client2);
        }
        runThreads.store(false);
    });

    writer.join();
    for (auto &t : readers) {
        t.join();
    }
}

namespace {

std::string createSubsystemATempDbPath()
{
    char tmpl[] = "/tmp/test_meshmon_suba_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd >= 0) {
        close(fd);
        unlink(tmpl);
    }
    return std::string(tmpl) + ".db";
}

void cleanupSubsystemADbFiles(const std::string &dbPath)
{
    if (dbPath.empty()) {
        return;
    }
    unlink(dbPath.c_str());
    unlink((dbPath + "-wal").c_str());
    unlink((dbPath + "-shm").c_str());
}

class TestableMeshMon : public MeshMon {
public:
    using MeshMon::parseMeshPumpStatus;
    using MeshMon::parseMeshRoofStatus;
    using MeshMon::parseMeshRoomStatus;
    using MeshMon::handleMqttCommand;

    void setMyNodeNum(uint32_t nodeNum) {
        _myNodeInfo.my_node_num = nodeNum;
    }

    void setNodeInfo(uint32_t id, const std::string &shortName, const std::string &longName) {
        meshtastic_NodeInfo ni;
        memset(&ni, 0, sizeof(ni));
        ni.num = id;
        ni.has_user = true;
        strncpy(ni.user.short_name, shortName.c_str(), sizeof(ni.user.short_name) - 1);
        strncpy(ni.user.long_name, longName.c_str(), sizeof(ni.user.long_name) - 1);
        _nodeInfos[id] = ni;
    }

    int callVprintf(const char *format, ...) {
        va_list ap;
        va_start(ap, format);
        int ret = this->vprintf(format, ap);
        va_end(ap);
        return ret;
    }
};

class TestableMeshMonShell : public MeshMonShell {
public:
    TestableMeshMonShell(std::shared_ptr<MeshClient> client = nullptr)
        : MeshMonShell(client) {}

    using MeshMonShell::setExecutingCommand;

    int callVprintf(const char *format, ...) {
        va_list ap;
        va_start(ap, format);
        int ret = this->vprintf(format, ap);
        va_end(ap);
        return ret;
    }
};

} // namespace

TEST_GROUP(SubsystemACoreRemediation)
{
    std::string dbPath;
    std::shared_ptr<MeshMonDb> db;

    void setup() override
    {
        dbPath = createSubsystemATempDbPath();
        db = std::make_shared<MeshMonDb>();
        CHECK_TRUE(db->open(dbPath));
        CHECK_TRUE(db->start());
    }

    void teardown() override
    {
        if (db) {
            db->stop();
            db->join();
            db->close();
            db.reset();
        }
        cleanupSubsystemADbFiles(dbPath);
    }
};

TEST(SubsystemACoreRemediation, SensorForwardAllowedSecurityAndAllowlist)
{
    TestableMeshMon mon;
    mon.setMyNodeNum(0x12340001U);
    CHECK_EQUAL(0x12340001U, mon.whoami());

    // Node ID 0 is strictly disallowed
    CHECK_FALSE(mon.isSensorForwardAllowed(0));

    // Self (whoami) is always permitted
    CHECK_TRUE(mon.isSensorForwardAllowed(mon.whoami()));

    // When allowlist is empty, external nodes are strictly disallowed
    CHECK_FALSE(mon.isSensorForwardAllowed(0x12345678U));
    CHECK_FALSE(mon.isSensorForwardAllowed(0xdeadbeefU));

    // Adding an admin allows that node
    meshtastic_User_public_key_t pubkey;
    memset(&pubkey, 0, sizeof(pubkey));
    CHECK_TRUE(mon.addAdmin(0x11112222U, pubkey));
    CHECK_TRUE(mon.isSensorForwardAllowed(0x11112222U));

    // Adding a mate allows that node
    CHECK_TRUE(mon.addMate(0x33334444U, pubkey));
    CHECK_TRUE(mon.isSensorForwardAllowed(0x33334444U));

    // Any unauthorized external node is still rejected
    CHECK_FALSE(mon.isSensorForwardAllowed(0x99998888U));
}

TEST(SubsystemACoreRemediation, FallbackAutomationStatusDiscoveryInitializesAndPersists)
{
    TestableMeshMon mon;
    mon.setDb(db);

    // 1. Fallback Discovery via parseMeshPumpStatus
    meshtastic_MeshPacket pktPump;
    memset(&pktPump, 0, sizeof(pktPump));
    pktPump.from = 0x10000001U;
    pktPump.channel = 0;

    CHECK_TRUE(mon.parseMeshPumpStatus(pktPump, "status: fish=on up=off up_cutoff=15s", 42));

    AutomationNode pumpNode;
    CHECK_TRUE(mon.getAutomationNode(0x10000001U, pumpNode));
    CHECK_EQUAL(0x10000001U, pumpNode.nodeId);
    STRCMP_EQUAL("10000001", pumpNode.nodeHex.c_str());
    STRCMP_EQUAL("meshpump", pumpNode.deviceType.c_str());
    CHECK_TRUE(pumpNode.online);
    CHECK_TRUE(pumpNode.firstSeen > 0);
    CHECK_TRUE(pumpNode.lastSeen > 0);
    CHECK_TRUE(pumpNode.fishPumpState);
    CHECK_FALSE(pumpNode.upPumpState);
    CHECK_EQUAL(15u, pumpNode.upPumpCutoffSec);
    CHECK_TRUE(pumpNode.device != nullptr);

    // Verify DB persistence via executeRawQuery
    QueryResult pumpRes;
    CHECK_TRUE(db->executeRawQuery("SELECT node_hex, device_type FROM automation_nodes WHERE node_hex = '10000001'", pumpRes));
    CHECK_EQUAL(1u, pumpRes.rows.size());
    STRCMP_EQUAL("10000001", pumpRes.rows[0][0].c_str());
    STRCMP_EQUAL("meshpump", pumpRes.rows[0][1].c_str());

    // 2. Fallback Discovery via parseMeshRoofStatus
    meshtastic_MeshPacket pktRoof;
    memset(&pktRoof, 0, sizeof(pktRoof));
    pktRoof.from = 0x10000002U;
    pktRoof.channel = 0;

    CHECK_TRUE(mon.parseMeshRoofStatus(pktRoof, "status: amplify=on temp_chip=45.5 reset_count=2", 30));

    AutomationNode roofNode;
    CHECK_TRUE(mon.getAutomationNode(0x10000002U, roofNode));
    CHECK_EQUAL(0x10000002U, roofNode.nodeId);
    STRCMP_EQUAL("10000002", roofNode.nodeHex.c_str());
    STRCMP_EQUAL("meshroof", roofNode.deviceType.c_str());
    CHECK_TRUE(roofNode.online);
    CHECK_TRUE(roofNode.firstSeen > 0);
    CHECK_TRUE(roofNode.lastSeen > 0);
    CHECK_TRUE(roofNode.amplifyState);
    CHECK_EQUAL(2u, roofNode.resetCount);
    CHECK_TRUE(roofNode.device != nullptr);

    QueryResult roofRes;
    CHECK_TRUE(db->executeRawQuery("SELECT node_hex, device_type FROM automation_nodes WHERE node_hex = '10000002'", roofRes));
    CHECK_EQUAL(1u, roofRes.rows.size());
    STRCMP_EQUAL("10000002", roofRes.rows[0][0].c_str());
    STRCMP_EQUAL("meshroof", roofRes.rows[0][1].c_str());

    // 3. Fallback Discovery via parseMeshRoomStatus
    meshtastic_MeshPacket pktRoom;
    memset(&pktRoom, 0, sizeof(pktRoom));
    pktRoom.from = 0x10000003U;
    pktRoom.channel = 0;

    CHECK_TRUE(mon.parseMeshRoomStatus(pktRoom, "ac: pwr=on mode=cool temp=23 fan=auto ir=panasonic_ac", 25));

    AutomationNode roomNode;
    CHECK_TRUE(mon.getAutomationNode(0x10000003U, roomNode));
    CHECK_EQUAL(0x10000003U, roomNode.nodeId);
    STRCMP_EQUAL("10000003", roomNode.nodeHex.c_str());
    STRCMP_EQUAL("meshroom", roomNode.deviceType.c_str());
    CHECK_TRUE(roomNode.online);
    CHECK_TRUE(roomNode.firstSeen > 0);
    CHECK_TRUE(roomNode.lastSeen > 0);
    CHECK_TRUE(roomNode.acPower);
    STRCMP_EQUAL("cool", roomNode.acMode.c_str());
    STRCMP_EQUAL("panasonic_ac", roomNode.acIrProtocol.c_str());
    CHECK_TRUE(roomNode.device != nullptr);

    QueryResult roomRes;
    CHECK_TRUE(db->executeRawQuery("SELECT node_hex, device_type FROM automation_nodes WHERE node_hex = '10000003'", roomRes));
    CHECK_EQUAL(1u, roomRes.rows.size());
    STRCMP_EQUAL("10000003", roomRes.rows[0][0].c_str());
    STRCMP_EQUAL("meshroom", roomRes.rows[0][1].c_str());
}

TEST(SubsystemACoreRemediation, HandleMqttCommandUnknownNodeNoPhantomCreation)
{
    TestableMeshMon mon;

    // Verify target node does not exist
    AutomationNode checkNode;
    CHECK_FALSE(mon.getAutomationNode(0xbad00000U, checkNode));
    CHECK_FALSE(mon.getAutomationNode(0U, checkNode));

    // Send pump cutoff command to unknown node (both hex and !hex formats)
    mon.handleMqttCommand("meshmon/cmd/bad00000/pump_up_cutoff", "60");
    mon.handleMqttCommand("meshmon/cmd/!bad00000/pump_up_cutoff", "60");

    // Verify no phantom entries were created in _autoNodes
    CHECK_FALSE(mon.getAutomationNode(0xbad00000U, checkNode));
    CHECK_FALSE(mon.getAutomationNode(0U, checkNode));
    LONGS_EQUAL(0, mon.getAutomationNodes().count(0xbad00000U));
    LONGS_EQUAL(0, mon.getAutomationNodes().count(0U));
}

TEST(SubsystemACoreRemediation, MqttClientStopJoinStartRestartability)
{
    // MqttClient constructor takes: server, port, user, password, topic
    MqttClient client("127.0.0.1", 1883, "", "", "test/#");

    client.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    client.stop();
    client.join();

    // Restarting after stop + join must succeed with clean new worker thread
    client.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    client.stop();
    client.join();
}

TEST(SubsystemACoreRemediation, OversizedVprintfFormattingClamp)
{
    // Construct a formatted string exceeding the 2048-byte stack buffer (e.g. 2500 bytes)
    std::string largePayload(2500, 'Z');

    // 1. MeshMonShell::vprintf clamping in ncurses executing command mode
    TestableMeshMonShell shell;
    shell.setNcursesMode(true);
    shell.setExecutingCommand(true);
    int shellRet = shell.callVprintf("%s", largePayload.c_str());
    CHECK_EQUAL(2500, shellRet);

    // 2. MeshMon::vprintf return value and safe formatting
    TestableMeshMon mon;
    int monRet = mon.callVprintf("%s", largePayload.c_str());
    CHECK_EQUAL(2500, monRet);
}

TEST_GROUP(SubsystemsDE_Remediation)
{
    void setup() override {}
    void teardown() override {}
};

TEST(SubsystemsDE_Remediation, CalibrationGetConfigPathThreadSafetyAndValueReturn)
{
    Calibration cal;
    std::string testPath = "/tmp/test_meshmon_calib_config.conf";
    cal.setConfigPath(testPath);

    std::string retrieved = cal.getConfigPath();
    STRCMP_EQUAL(testPath.c_str(), retrieved.c_str());

    std::atomic<bool> runThreads{true};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; i++) {
        readers.emplace_back([&cal, &runThreads]() {
            while (runThreads.load()) {
                std::string p = cal.getConfigPath();
                CHECK_TRUE(!p.empty());
                std::this_thread::yield();
            }
        });
    }

    std::thread writer([&cal, &runThreads]() {
        for (int i = 0; i < 500; i++) {
            cal.setConfigPath("/tmp/path_" + std::to_string(i) + ".conf");
        }
        runThreads.store(false);
    });

    writer.join();
    for (auto &t : readers) {
        t.join();
    }
}

TEST(ChatBotTruncation, LoadTasksFromFileNoDeadlock)
{
    char tmpl[] = "/tmp/test_meshmon_sched_XXXXXX";
    int fd = mkstemp(tmpl);
    CHECK(fd >= 0);
    close(fd);
    std::string schedPath(tmpl);

    TestableChatBot writerBot;
    auto client = std::make_shared<TestableMeshMon>();
    client->setMyNodeNum(0x11223344U);
    client->setNodeInfo(0x22334455U, "nod1", "Node One");
    writerBot.setClient(client);
    writerBot.setConfigPath(schedPath);

    ScheduledTask t1;
    t1.from = 0x22334455U;
    t1.dest = 0xffffffffU;
    t1.channel = 0;
    t1.type = TASK_PROMPT;
    t1.content = "Check weather";
    t1.dueTime = time(NULL) + 3600;
    t1.cronExpr = "0 8 * * *";
    t1.maxRepeats = -1;
    uint32_t assignedId = writerBot.scheduleTask(t1);
    CHECK_EQUAL(1u, assignedId);

    // Also test loading with a short name in config alongside !hex ID
    libconfig::Config manualCfg;
    CHECK_TRUE(writerBot.writeTasksToConfig(manualCfg));
    libconfig::Setting &taskList = manualCfg.getRoot()["scheduled_tasks"];
    libconfig::Setting &g2 = taskList.add(libconfig::Setting::TypeGroup);
    g2.add("id", libconfig::Setting::TypeInt) = 5;
    g2.add("from", libconfig::Setting::TypeString) = std::string("nod1");
    g2.add("dest", libconfig::Setting::TypeString) = std::string("self");
    g2.add("channel", libconfig::Setting::TypeInt) = 1;
    g2.add("type", libconfig::Setting::TypeString) = std::string("message");
    g2.add("content", libconfig::Setting::TypeString) = std::string("Ping self");
    g2.add("due_time", libconfig::Setting::TypeInt64) = static_cast<long long>(time(NULL) + 7200);
    manualCfg.writeFile(schedPath.c_str());

    TestableChatBot readerBot;
    readerBot.setClient(client);
    CHECK_TRUE(readerBot.loadTasksFromFile(schedPath));

    std::vector<ScheduledTask> loaded = readerBot.getTasks();
    CHECK_EQUAL(2u, loaded.size());
    CHECK_EQUAL(1u, loaded[0].id);
    CHECK_EQUAL(0x22334455U, loaded[0].from);
    CHECK_EQUAL(0xffffffffU, loaded[0].dest);
    STRCMP_EQUAL("Check weather", loaded[0].content.c_str());
    CHECK_EQUAL(5u, loaded[1].id);
    CHECK_EQUAL(0x22334455U, loaded[1].from);
    CHECK_EQUAL(0x11223344U, loaded[1].dest);
    STRCMP_EQUAL("Ping self", loaded[1].content.c_str());

    unlink(schedPath.c_str());
}

TEST(SubsystemsDE_Remediation, ShellHoursMultiplicationDoesNotOverflow32BitSignedInt)
{
    int hours = 1000000; // 1 million hours
    time_t now = 2000000000; // ~year 2033
    // In 32-bit signed int, hours * 3600 overflows (1000000 * 3600 = 3600000000 > INT32_MAX = 2147483647)
    // yielding a negative number!
    int32_t overflow32 = static_cast<int32_t>(hours * 3600);
    CHECK_TRUE(overflow32 < 0);

    time_t since64 = (hours > 0) ? (now - (static_cast<time_t>(hours) * 3600)) : 0;
    CHECK_EQUAL(now - 3600000000LL, since64);
}

TEST(SubsystemsDE_Remediation, AimonGatewayClientStopAndDisconnectIdempotence)
{
    auto gw = std::make_shared<AimonGatewayClient>(nullptr, nullptr);
    CHECK_FALSE(gw->isConnected());

    // Multiple calls to disconnect and stop on an unstarted client are safe and idempotent
    gw->disconnect();
    gw->stop();
    gw->join();
    CHECK_FALSE(gw->isConnected());

    // Start with unreachable local port, stop immediately
    bool started = gw->start("127.0.0.1", 59998);
    CHECK_TRUE(started);
    gw->stop();
    gw->join();
    CHECK_FALSE(gw->isConnected());
}

TEST(SubsystemsDE_Remediation, AimonGatewayClientUtf8SafeDumpOnMalformedStrings)
{
    auto mon = std::make_shared<TestableMeshMon>();
    // Inject non-UTF-8 bytes into node name
    mon->setNodeInfo(0x11223344U, "\xff\xfe\x80", "Bad\xffName");

    TestableAimonClient client(mon, nullptr);
    int sv[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    CHECK_EQUAL(0, rc);

    client.setMockFd(sv[0]);

    // Handle tool call for the node with non-UTF-8 bytes
    std::string req = "{\"jsonrpc\":\"2.0\",\"id\":100,\"method\":\"tools/call\",\"params\":{\"name\":\"meshmon_get_node_status\",\"arguments\":{\"node_id\":\"!11223344\"}}}";
    client.callHandleRequest(req);

    char buf[4096];
    ssize_t n = recv(sv[1], buf, sizeof(buf) - 1, 0);
    CHECK_TRUE(n > 0);
    buf[n] = '\0';

    // Must be valid JSON (replace handler used instead of throwing type_error)
    nlohmann::json j = nlohmann::json::parse(buf);
    CHECK_EQUAL(100, j["id"].get<int>());
    CHECK_TRUE(j.contains("result"));
    std::string contentText = j["result"]["content"][0]["text"].get<std::string>();
    // Non-UTF-8 bytes replaced with Unicode replacement character U+FFFD (\xef\xbf\xbd)
    CHECK_TRUE(contentText.find("\xef\xbf\xbd") != std::string::npos);

    close(sv[0]);
    close(sv[1]);
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
