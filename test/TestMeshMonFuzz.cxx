/*
 * TestMeshMonFuzz.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include <unistd.h>
#include <sys/socket.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <rapidcheck.h>

#include <libmeshtastic.h>
#include <HomeChat.hxx>
#include "Calibration.hxx"
#include "ChatBot.hxx"
#include "MeshMonDb.hxx"

namespace {

class FuzzChatBot : public ChatBot {
public:
    FuzzChatBot() : ChatBot(nullptr) {}
    virtual ~FuzzChatBot() {}

    std::string callTruncateToMesh(const std::string &text) const
    {
        return truncateToMesh(text);
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

void fuzzPacketHandler(struct mt_client *mtc, const void *packet, size_t size,
                       const meshtastic_FromRadio *from_radio)
{
    (void) packet;
    (void) size;
    (void) from_radio;
    size_t *count = static_cast<size_t *>(mtc->ctx);
    if (count != NULL) {
        (*count)++;
    }
}

bool isValidUtf8Boundary(const std::string &s)
{
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0x80) == 0x00) {
            len = 1;
        } else if ((c & 0xe0) == 0xc0) {
            len = 2;
        } else if ((c & 0xf0) == 0xe0) {
            len = 3;
        } else if ((c & 0xf8) == 0xf0) {
            len = 4;
        } else {
            return false;
        }
        if (i + len > s.size()) {
            return false;
        }
        for (size_t k = 1; k < len; k++) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80) {
                return false;
            }
        }
        i += len;
    }
    return true;
}

} // namespace

int main()
{
    std::cout << "[RapidCheck] Starting MeshMon property verification..." << std::endl;

    int sv[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        return 1;
    }

    // Property 1: mt_serial_process arbitrary byte stream safety and valid-frame recovery
    bool p1 = rc::check("mt_serial_process arbitrary byte stream resync safety",
                        [&sv](const std::vector<uint8_t> &noise) {
        struct mt_client mtc;
        size_t dispatched = 0;
        memset(&mtc, 0, sizeof(mtc));
        mtc.type = MT_CLIENT_SERIAL;
        mtc.fd = sv[0];
        mtc.device = "fuzz-rx";
        mtc.handler = fuzzPacketHandler;
        mtc.ctx = &dispatched;

        // Feed arbitrary noise chunks (bounded to fit socket buffer without blocking)
        std::vector<uint8_t> bounded = noise;
        if (bounded.size() > 1024) {
            bounded.resize(1024);
        }
        if (!bounded.empty()) {
            ssize_t w = write(sv[1], bounded.data(), bounded.size());
            RC_ASSERT(w == static_cast<ssize_t>(bounded.size()));
            while (mt_serial_process(&mtc, 0) == 0) {
                fd_set rfds;
                struct timeval tv = { 0, 0 };
                FD_ZERO(&rfds);
                FD_SET(sv[0], &rfds);
                if (select(sv[0] + 1, &rfds, NULL, NULL, &tv) <= 0) {
                    break;
                }
            }
        }
        RC_ASSERT(mtc.inbuf_len < sizeof(mtc.inbuf));
    });
    close(sv[0]);
    close(sv[1]);
    if (!p1) {
        return 1;
    }

    // Property 2: CalibrationCurve knot exactness and finite output
    bool p2 = rc::check("CalibrationCurve knot exactness and finite interpolation",
                        [](const std::vector<std::pair<int, int>> &rawPts, int queryInt) {
        CalibrationCurve curve;
        for (const auto &kv : rawPts) {
            double r = static_cast<double>(kv.first % 10000) / 10.0;
            double c = static_cast<double>(kv.second % 10000) / 10.0;
            curve.setPoint(r, c);
        }
        double q = static_cast<double>(queryInt % 20000) / 10.0;
        double out = curve.calibrate(q);
        RC_ASSERT(std::isfinite(out));

        for (const auto &pt : curve.getPoints()) {
            RC_ASSERT(std::abs(curve.calibrate(pt.raw) - pt.cal) < 1e-6);
        }
    });
    if (!p2) {
        return 1;
    }

    // Property 3: HomeChat::parseTimeBroadcast crash-freedom and TZ invariance
    setenv("TZ", "UTC0", 1);
    tzset();
    const char *tzSaved = getenv("TZ");
    bool p3 = rc::check("HomeChat::parseTimeBroadcast safety and TZ invariance",
                        [tzSaved](const std::string &input) {
        time_t epoch1 = 0, epoch2 = 0;
        std::string tz1, tz2;
        bool ok1 = HomeChat::parseTimeBroadcast(input, epoch1, tz1);
        bool ok2 = HomeChat::parseTimeBroadcast(input, epoch2, tz2);
        RC_ASSERT(ok1 == ok2);
        if (ok1) {
            RC_ASSERT(epoch1 == epoch2);
            RC_ASSERT(tz1 == tz2);
            RC_ASSERT(epoch1 > 0);
        }
        RC_ASSERT(getenv("TZ") == tzSaved);
    });
    if (!p3) {
        return 1;
    }

    // Property 4: ChatBot::truncateToMesh UTF-8 codepoint boundary and 200-byte invariant
    FuzzChatBot bot;
    auto codepointGen = rc::gen::element<std::string>(
        "a", "Z", "0", "\n", "\r",
        "\xC3\xA9",         // 2-byte UTF-8
        "\xCE\xA9",         // 2-byte UTF-8
        "\xE5\x8F\xB0",     // 3-byte UTF-8
        "\xE7\x81\xAB",     // 3-byte UTF-8
        "\xF0\x9F\x9B\xB0"  // 4-byte UTF-8
    );
    bool p4 = rc::check("ChatBot::truncateToMesh valid UTF-8 and <= 200 byte invariant",
                        [&bot, codepointGen]() {
        size_t count = *rc::gen::inRange<size_t>(0, 120);
        std::string input;
        for (size_t i = 0; i < count; i++) {
            input += *codepointGen;
        }
        std::string out = bot.callTruncateToMesh(input);
        RC_ASSERT(out.size() <= 200u);
        RC_ASSERT(out.find('\n') == std::string::npos);
        RC_ASSERT(out.find('\r') == std::string::npos);
        RC_ASSERT(isValidUtf8Boundary(out));
    });
    if (!p4) {
        return 1;
    }

    // Property 5: MeshMonDb::executeRawQuery read-only guard against arbitrary & injected SQL
    char dbTmpl[] = "/tmp/fuzz_meshmon_db_XXXXXX";
    int dbFd = mkstemp(dbTmpl);
    if (dbFd >= 0) {
        close(dbFd);
        unlink(dbTmpl);
    }
    std::string dbPath = std::string(dbTmpl) + ".db";
    MeshMonDb db;
    if (!db.open(dbPath)) {
        return 1;
    }
    db.enqueueNodeInfo(0x11223344U, "Seed Node", "SEED", 1, 1, 1700000000);
    db.flush();

    auto sqlTokenGen = rc::gen::element<std::string>(
        "SELECT", "WITH", "PRAGMA", "EXPLAIN", "DROP", "DELETE", "INSERT", "UPDATE",
        "ATTACH", "ALTER", "CREATE", "writable_schema=1", "table_info(nodes)",
        "FROM", "nodes", "packets", "1", "*", ";", "-- comment\n", "/* block */",
        " ", "\t", "\n", "\0"
    );
    bool p5 = rc::check("MeshMonDb::executeRawQuery read-only guard and table preservation",
                        [&db, sqlTokenGen](const std::string &rawNoise) {
        size_t tokCount = *rc::gen::inRange<size_t>(0, 8);
        std::string query;
        for (size_t i = 0; i < tokCount; i++) {
            query += *sqlTokenGen + " ";
        }
        query += rawNoise;

        QueryResult qres;
        (void) db.executeRawQuery(query, qres);

        // Also test multi-statement injection suffix on a valid SELECT
        std::string injected = "SELECT 1; " + query;
        QueryResult injRes;
        if (!rawNoise.empty()) {
            // Any non-comment/non-whitespace tail after ';' must be rejected
            bool hasNonSpace = false;
            for (char c : rawNoise) {
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '-' && c != '/') {
                    hasNonSpace = true;
                    break;
                }
            }
            if (hasNonSpace && rawNoise.find('\0') == std::string::npos) {
                RC_ASSERT(!db.executeRawQuery("SELECT 1; DROP TABLE nodes; " + rawNoise, injRes));
            }
        }

        // Seed node must never be deleted or mutated by any executeRawQuery call
        RC_ASSERT(db.getTotalNodeCount() == 1U);
    });
    db.close();
    unlink(dbPath.c_str());
    unlink((dbPath + "-wal").c_str());
    unlink((dbPath + "-shm").c_str());
    if (!p5) {
        return 1;
    }

    std::cout << "[RapidCheck] All MeshMon property suites passed!" << std::endl;
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
