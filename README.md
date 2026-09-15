<p align="center">
  <img src="doc/meshmon-logo.png" alt="MeshMon Logo" width="320"/>
</p>

# MeshMon

**MeshMon** (v2.1.16) is a high-performance daemon and monitoring gateway for [Meshtastic](https://meshtastic.org) LoRa mesh networks running on Linux. It connects to one or more Meshtastic radio nodes via Serial (USB), TCP, or Bluetooth LE, providing real-time telemetry extraction, high-performance asynchronous packet logging, deep RF and network analytics, spatial behavior analysis, an embedded Web Command Center, AI chatbot integration (Google Gemini), dynamic MCP AI gateway integration (`aimon`), and Home Assistant MQTT telemetry bridging.

---

## Key Features

- **Multi-Radio Ingestion**: Seamlessly connect and monitor multiple Meshtastic nodes across serial USB ports (`/dev/ttyACM*`, `/dev/ttyUSB*`), TCP network streams, or Bluetooth LE.
- **Embedded Web Command Center**: Dark glassmorphic responsive web interface running on port 16880 (default) with real-time telemetry gauges, interactive node profiles, message dispatch, and topology visualizations.
- **Real-Time SSE Packet Stream**: Server-Sent Events (`/events`) endpoint streaming live LoRa packets directly from radio ingest to connected browser dashboards and external clients.
- **Remote Behavior & Spatial Analytics**:
  - **Polar Radar & Bearing Visualizer**: Real-time polar canvas displaying remote node azimuth, distance, and signal health relative to the reference station.
  - **SNR vs. Distance Analysis**: Scatter plot analysis correlating RF link quality against physical displacement.
  - **Mobility Classification**: Automatic categorization of nodes into *Stationary*, *Mobile Tracker*, or *RF-Only* based on historical displacement.
  - **Link Asymmetry & RF Noise Floor**: Quantitative forward vs. reverse link budget comparison to identify localized RF interference or antenna mismatch.
- **Mesh Topology Analytics**:
  - **Centroid Trilateration**: Geographic centroid estimation for GPS-less nodes computed from hearing neighbors' positions.
  - **Hearing Neighbor Graph**: Direct 0-hop neighbor visibility mapping.
  - **Multi-Hop Traceroute Explorer**: Hop-by-hop route logs and SNR per hop.
- **Asynchronous SQLite Packet Logging**: Non-blocking background worker thread (`MeshMonDb`) with WAL (Write-Ahead Logging) mode, batch commit transactions, and configurable retention pruning.
- **Dual-Time Invariant Capture**:
  - `meshmon_time`: Host system NTP arrival timestamp representing absolute ground truth.
  - `rx_time`: Raw packet timestamp reported by the radio firmware.
- **Interactive SQL Console**: Web and shell accessible read-only SQLite query engine with security guards and diagnostic presets.
- **Dynamic Aimon MCP Gateway Client**: Outbound TCP client auto-registering the `meshmon_*` MCP toolset (`meshmon_get_node_status`, `meshmon_query_telemetry_history`, `meshmon_get_rf_analytics`, `meshmon_send_message`, `meshmon_query_db`, `meshmon_get_spatial_analytics`) to the central AI gateway.
- **Interactive Shell & Daemon Control**: Full interactive CLI (`MeshMonShell`) with colorized outputs, packet filtering, and database inspection commands (`db <query>`), accessible locally or via TCP/Telnet.
- **HomeChat Protocol Extensions**: Respond to on-air natural language sensor and RF queries (`traffic?`, `storm?`, `spof?`, `asymmetry?`, `health?`) and forward conversational queries to Gemini AI.
- **Master Clock Broadcast**: Synchronize remote mesh nodes with authoritative host wall-clock time broadcasts.
- **Home Assistant MQTT Integration**: Native MQTT Auto-Discovery publishing gateway status, packet rates, node counts, and diagnostic telemetry directly into Home Assistant without manual configuration.
- **HomeMesh Device Automation & Control**: Ingests, tracks, logs, and exposes telemetry and bidirectional controls for smart mesh nodes (`meshpump`, `meshroof`, `meshroom`) into Home Assistant via MQTT Auto-Discovery.

---

## Documentation Index

Comprehensive documentation covering all subsystems of `meshmon`:

| Document | Description |
| :--- | :--- |
| [**`Design.md`**](Design.md) | Comprehensive architectural design specification: ingestion pipeline, SQLite WAL threading model, spatial analytics engine, web server architecture, and MCP gateway protocol. |
| [**`doc/PacketLoggingDB.md`**](doc/PacketLoggingDB.md) | Complete SQLite packet logging architecture, table schema, background threading model, CLI `db` commands, and analytical SQL queries for deep RF telemetry. |
| [**`doc/HomeAssistantIntegration.md`**](doc/HomeAssistantIntegration.md) | Step-by-step Home Assistant setup guide, MQTT Auto-Discovery sensor specifications, state JSON schemas, and ready-to-use Lovelace dashboard YAML cards. |
| [**`doc/HomeMeshAutomation.md`**](doc/HomeMeshAutomation.md) | Architectural specification for HomeMesh device discovery, SQLite audit logging (`automation_events`), anti-spoofing mate verification, dynamic role migration, and bidirectional Home Assistant controls. |
| [**`doc/HomeChat-meshmon.md`**](doc/HomeChat-meshmon.md) | On-air `HomeChat` protocol specifications, master time synchronization broadcasts, Gemini AI chatbot gateway, and natural RF query syntax. |

---

## Building & Installation

### Prerequisites

- C++17 compatible compiler (`g++` / `clang++`)
- GNU Make
- CMake 3.13+
- SQLite3 development libraries (`libsqlite3-dev`)
- libconfig++ (`libconfig++-dev`)
- Mosquitto MQTT client library (`libmosquitto-dev`)
- libcurl (`libcurl4-openssl-dev`)

### Compilation Workflow

Always use the top-level `Makefile` wrapper to build `meshmon`:

```bash
# Compile meshmon and initialize submodules automatically:
make -j$(nproc)

# Clean compiled build targets:
make clean

# Purge build directory for a pristine rebuild:
make distclean
```

The resulting binary is located at `build/$(uname -m)/meshmon` (e.g. `build/x86_64/meshmon` or `build/aarch64/meshmon`).

---

## Quick Start

### Running from Command Line

```bash
# Run with SQLite logging to standard XDG path and launch web command center on port 16880:
./build/$(uname -m)/meshmon -d /dev/ttyACM0 -D ~/.config/meshmon/meshmon.db -w 16880

# Launch with interactive CLI shell connected to radio:
./build/$(uname -m)/meshmon -d /dev/ttyUSB0 --stdio

# Run in background daemon mode with AI gateway client enabled:
./build/$(uname -m)/meshmon -d /dev/ttyACM0 -b -D ~/.config/meshmon/meshmon.db --gateway --gateway-host <gateway-host>
```

### Command Line Options

| Option | Long Option | Description |
| :--- | :--- | :--- |
| `-d <dev>` | `--device <dev>` | Add serial (`/dev/ttyACM0`), TCP (`192.168.1.50:4403`), or BLE device |
| `-s` | `--stdio` | Enable stdio interactive shell |
| `-p <port>` | `--port <port>` | TCP port for network shell |
| `-b` | `--daemon` | Run in background daemon mode |
| `-l` | `--log` | Enable device raw logging |
| `-D <path>` | `--database <path>`, `--db <path>` | Enable SQLite packet logging to specified file |
| | `--no-database`, `--no-db` | Disable SQLite packet logging |
| | `--retention-days <days>` | Prune database records older than N days |
| | `--gateway` | Enable dynamic AI gateway client |
| | `--no-gateway` | Disable dynamic AI gateway client |
| | `--gateway-host <host>` | AI gateway host (default: `<gateway-host>`) |
| | `--gateway-port <port>` | AI gateway port (default: 3885) |
| `-w <port>` | `--web-port <port>` | Embedded web dashboard port (default: 16880) |
| | `--no-web` | Disable embedded web dashboard |
| | `--web-password <pass>` | Set web admin authentication password (default: `admin`) |
| `-h` | `--help` | Display help message |
| `-v` | `--version` | Display version information |

---

## Configuration File Example (`~/.config/meshmon/meshmon.cfg`)

`meshmon` automatically looks for its configuration file at `~/.config/meshmon/meshmon.cfg`:

```libconfig
devices = [ "/dev/ttyACM0" ];

database = {
    enabled = true;
    path = "~/.config/meshmon/meshmon.db";
    retention_days = 30;
};

web = {
    enabled = true;
    port = 16880;
    host = "0.0.0.0";
    password = "admin";
};

gateway = {
    enabled = true;
    host = "127.0.0.1";
    port = 3885;
};

mqtt = {
    enabled = true;
    host = "<mqtt-broker-ip>";
    port = 1883;
    topic_prefix = "homeassistant";
};

gemini = {
    enabled = false;
    api_key = "<your-gemini-api-key>";
    model = "gemini-2.0-flash";
};
```

---

## License & Copyright

Copyright (C) 2026, Charles Chiou. All rights reserved.
