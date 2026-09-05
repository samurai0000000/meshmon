<p align="center">
  <img src="doc/meshmon-logo.png" alt="MeshMon Logo" width="320"/>
</p>

# MeshMon

**MeshMon** is a daemon and monitoring gateway for [Meshtastic](https://meshtastic.org) networks running on Linux. It connects to one or more Meshtastic radio nodes via Serial, TCP, or BLE, providing real-time telemetry extraction, high-performance asynchronous packet logging, deep RF and network analytics, master clock synchronization broadcasts, AI chatbot integration (Google Gemini), and Home Assistant MQTT telemetry bridging.

---

## Key Features

- **Multi-Radio Ingestion**: Seamlessly connect and monitor multiple Meshtastic nodes across serial USB ports, TCP network streams, or Bluetooth LE.
- **Asynchronous SQLite Packet Logging**: Non-blocking background worker thread (`MeshMonDb`) with WAL (Write-Ahead Logging) mode, batch commit transactions, and configurable retention pruning.
- **Dual-Time Invariant Capture**:
  - `meshmon_time`: Host system NTP arrival timestamp representing absolute ground truth.
  - `rx_time`: Raw packet timestamp reported by the radio firmware.
- **Deep RF & Mesh Analytics**:
  - **Echo Storm & Flooding Detection**: Pinpoint packet duplication ratios and identify flood culprits across the mesh.
  - **Link Asymmetry & Noise Floor Elevation**: Compare transmit vs. receive link budgets to detect localized RF noise or antenna mismatch.
  - **Critical Repeater (SPOF) Discovery**: Identify single-point-of-failure relay nodes carrying disproportionate mesh traffic.
  - **Remote Clock Drift Analysis**: Detect misconfigured, unsynchronized, or drifting real-time clocks on remote nodes.
  - **Hop & Airtime Health Breakdown**: Track 0-hop neighbor RSSI/SNR metrics, hop distributions, portnum application ratios, and channel SNR stability.
- **Interactive Shell & Daemon Control**: Full interactive CLI (`MeshMonShell`) with colorized outputs, packet filtering, and database inspection commands (`db <query>`), accessible locally or via TCP/Telnet.
- **HomeChat Protocol Extensions**: Respond to on-air natural language sensor and RF queries (`traffic?`, `storm?`, `spof?`, `asymmetry?`, `health?`) and forward conversational queries to Gemini AI.
- **Master Clock Broadcast**: Synchronize remote mesh nodes with authoritative host wall-clock time broadcasts.
- **Home Assistant MQTT Integration**: Native MQTT Auto-Discovery publishing gateway status, packet rates, node counts, and diagnostic telemetry directly into Home Assistant without manual configuration.
- **HomeMesh Device Automation & Control**: Ingests, tracks, logs, and exposes telemetry and bidirectional controls for smart mesh nodes (`meshpump`, `meshroof`, `meshroom`) into Home Assistant via MQTT Auto-Discovery.

---

## Documentation Index

The `doc/` directory contains in-depth documentation covering all subsystems of `meshmon`:

| Document | Description |
| :--- | :--- |
| [**`doc/PacketLoggingDB.md`**](doc/PacketLoggingDB.md) | Complete SQLite packet logging architecture, table schema, background threading model, CLI `db` commands, and analytical SQL queries for deep RF telemetry. |
| [**`doc/HomeAssistantIntegration.md`**](doc/HomeAssistantIntegration.md) | Step-by-step Home Assistant setup guide, MQTT Auto-Discovery sensor specifications, state JSON schemas, and ready-to-use Lovelace dashboard YAML cards. |
| [**`doc/HomeMeshAutomation.md`**](doc/HomeMeshAutomation.md) | Architectural specification for HomeMesh device discovery, SQLite audit logging (`automation_events`), anti-spoofing mate verification, dynamic role migration, and bidirectional Home Assistant controls. |
| [**`doc/HomeChat-meshmon.md`**](doc/HomeChat-meshmon.md) | On-air `HomeChat` protocol specifications, master time synchronization broadcasts, Gemini AI chatbot gateway, and natural RF query syntax. |

---

## Building & Installation

### Prerequisites

- C++11 compatible compiler (`g++` / `clang++`)
- CMake 3.13+
- SQLite3 development libraries (`libsqlite3-dev`)
- libconfig++ (`libconfig++-dev`)
- Mosquitto MQTT client library (`libmosquitto-dev`)
- libcurl (`libcurl4-openssl-dev`)

### Compile

```bash
cmake -B build
cmake --build build
```

The resulting binary is located at `./build/meshmon`.

---

## Quick Start

### Running from Command Line

```bash
# Run with SQLite packet logging enabled at /var/log/meshmon.db with 30-day retention:
./build/meshmon -s /dev/ttyACM0 -D /var/log/meshmon.db --retention-days 30

# Launch interactive shell connected to a radio:
./build/meshmon -s /dev/ttyUSB0 --shell
```

### Configuration File Example (`meshmon.cfg`)

```libconfig
database = {
    enabled = true;
    path = "/var/log/meshmon/meshmon.db";
    retention_days = 30;
};

mqtt = {
    enabled = true;
    host = "192.168.1.100";
    port = 1883;
    topic_prefix = "homeassistant";
};
```

---

## License & Copyright

Copyright (C) 2026, Charles Chiou. All rights reserved.
