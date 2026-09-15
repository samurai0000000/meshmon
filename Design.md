# MeshMon: Architectural Design & Technical Specification

## 1. System Overview & Problem Statement

### 1.1 Overview
**MeshMon** is a Linux daemon and monitoring gateway for [Meshtastic](https://meshtastic.org) LoRa mesh networks. It interfaces directly with one or more physical Meshtastic radio transceivers via Serial (USB), TCP, or Bluetooth LE to provide continuous telemetry extraction, high-performance asynchronous packet logging, deep RF and network analytics, spatial behavior tracking, an embedded Web Command Center, smart device automation (HomeMesh), and bidirectional Home Assistant MQTT integration.

All configuration files adhere to `libconfig++` and standard XDG paths (`~/.config/meshmon/meshmon.cfg`), with automatic directory creation.

### 1.2 Hardware Placement & Radio Transceivers
`meshmon` is deployed on a Linux host physically connected to Meshtastic LoRa radios:
- **Physical Radios**: Attached via local USB serial interfaces (`/dev/ttyACM*`, `/dev/ttyUSB*`), TCP network endpoints, or BLE.
- **Compilation**: Standard native compilation via top-level `Makefile` (`make -j$(nproc)`).
- **Runtime Environment**: Operates as an interactive terminal shell (`--stdio`), headless background daemon (`--daemon`), and embedded web server (`--web-port 16880`).

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       Meshtastic LoRa Mesh Network                          │
│               (Nodes, Repeaters, HomeMesh Automation Devices)               │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ LoRa RF 915MHz / 433MHz
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                 Physical Radio Transceivers (USB Serial)                    │
│                        /dev/ttyACM0, /dev/ttyUSB0                           │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ Protobuf Stream
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                             MeshMon Core Daemon                             │
│                                                                             │
│  ┌───────────────────────┐  ┌──────────────────────┐  ┌──────────────────┐  │
│  │ MeshClient Engine     │  │ MeshMonDb            │  │ Spatial & RF     │  │
│  │ - Radio ingestion     │  │ - Async SQLite logger│  │   Analytics      │  │
│  │ - Packet demuxing     │  │ - Dual-time capture  │  │ - Polar radar    │  │
│  │ - Protobuf decoding   │  │ - WAL mode & prune   │  │ - Centroid trilat│  │
│  └───────────┬───────────┘  └──────────┬───────────┘  │ - Link asymmetry │  │
│              │                         │              │ - Echo / SPOF    │  │
│              │                         │              └────────┬─────────┘  │
│              ├─────────────────────────┴───────────────────────┤            │
│              ▼                                                 ▼            │
│  ┌───────────────────────┐                        ┌──────────────────────┐  │
│  │ MQTT / Home Assistant │                        │ Embedded Web Server  │  │
│  │ - Auto-discovery      │                        │ - Port 16880 (REST)  │  │
│  │ - State telemetry     │                        │ - SSE stream /events │  │
│  │ - Device automation   │                        │ - WebAssets (UI)     │  │
│  └───────────────────────┘                        └──────────┬───────────┘  │
│                                                              │              │
│  ┌───────────────────────┐                        ┌──────────┴───────────┐  │
│  │ Interactive Shell     │                        │ AimonGatewayClient   │  │
│  │ - MeshMonShell (CLI)  │                        │ - TCP client :3885   │  │
│  │ - Status & db queries │                        │ - Exports meshmon_*  │  │
│  └───────────────────────┘                        └──────────┬───────────┘  │
└──────────────────────────────────────────────────────────────┼──────────────┘
                                                               │ Line JSON-RPC
                                                               ▼
                                                 aimon Gateway (:3885)
```

---

## 3. Configuration Standardization (`libconfig++` & XDG Paths)

`meshmon` standardizes on `~/.config/meshmon/`:
- **Configuration File**: `~/.config/meshmon/meshmon.cfg`
- **Database File**: `~/.config/meshmon/meshmon.db`
- **Calibration File**: `~/.config/meshmon/meshmon.calib`
- **Schedule File**: `~/.config/meshmon/meshmon.sched`

The daemon automatically checks for and creates `~/.config/meshmon/` if missing.

Sample `meshmon.cfg`:
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
```

---

## 4. Core Subsystems

### 4.1 Dual-Time Invariant Packet Ingestion (`MeshMonDb`)
Every packet received by `meshmon` is logged with two distinct timestamps:
- `meshmon_time`: Host wall-clock timestamp (microsecond accuracy from NTP-synchronized Linux host). Represents absolute ground truth.
- `rx_time`: Radio firmware timestamp reported over protobuf.

This dual-time logging enables precise detection of remote RTC drift and relay transit latencies. Logging runs on an asynchronous worker thread with SQLite WAL mode to guarantee zero dropped packets during radio bursts.

### 4.2 Spatial Analytics & Remote Behavior Engine
`SpatialAnalytics` provides geographic and RF behavioral profiling of remote nodes:
- **Haversine Distance & Polar Bearing**: Computes relative displacement and cardinal azimuth from reference base station coordinates.
- **Polar Radar Visualizer**: Maps active nodes onto a polar radar canvas indexed by compass direction and distance rings.
- **Mobility Classification**: Evaluates position variance over time to categorize nodes into:
  - `Stationary`: Fixed repeaters, base stations, and static sensor nodes.
  - `Mobile Tracker`: Moving vehicles, handheld assets, or wandering personnel.
  - `RF-Only`: Non-GPS transmitting nodes tracked purely via RF telemetry.
- **Link Asymmetry & Noise Floor Analysis**: Contrasts forward vs. reverse packet SNR and RSSI across 0-hop neighbors to identify local RF interference, desense, or antenna mismatch.
- **Centroid Trilateration for GPS-less Nodes**: Estimates the geographic location of non-GPS nodes based on the known positions of hearing neighbor nodes.
- **Echo Storm & SPOF Discovery**: Computes relay traffic centrality to isolate single points of failure across the mesh and flags packet loopback floods.

### 4.3 Embedded Web Server & Command Center
`WebServer` runs an embedded HTTP server (using `cpp-httplib` and `nlohmann/json`) on port 16880:
- **Real-Time SSE Stream (`/events`)**: Streams live packet ingestion events directly to connected browsers without polling.
- **REST API Subsystem**:
  - `GET /api/status`: Daemon health, uptime, node counts, and memory stats.
  - `GET /api/nodes`: List of all discovered mesh nodes with RF and battery status.
  - `GET /api/packets`: Recent packet ring buffer inspection.
  - `GET /api/analytics`: RF health, echo storm ratios, and SPOF repeater metrics.
  - `GET /api/spatial`: Geographic coordinates, polar bearings, and distances.
  - `GET /api/remote/summary`: Aggregated remote node counts, mobility states, and RF link stats.
  - `GET /api/remote/nodes`: Detailed per-node RF behavioral profiles.
  - `GET /api/topology/routes`: Hop-by-hop traceroute paths and route SNR.
  - `GET /api/topology/asymmetry`: Forward vs. reverse SNR delta table.
  - `GET /api/topology/centroids`: Trilaterated centroid estimates for GPS-less nodes.
- **Authenticated Command Endpoints**:
  - `POST /api/auth/login`, `POST /api/auth/logout`, `GET /api/auth/status`
  - `POST /api/send`: Send direct or broadcast text messages.
  - `POST /api/automation/command`: Dispatch HomeMesh device commands.
  - `POST /api/db/query`: Execute read-only SQL queries against SQLite `MeshMonDb`.
- **Embedded Asset Serving**: HTML/CSS/JS bundled in `WebAssets.hxx` with fallback to local `web/` directory for live developer edits.

### 4.4 HomeMesh Device Automation & Home Assistant Integration
Monitors and controls smart IoT mesh devices (`meshpump`, `meshroof`, `meshroom`) using calibration curves (`Calibration.hxx`), audit logging (`automation_events`), and bidirectional Home Assistant MQTT state publishing.

---

## 5. AI Toolset Integration via `aimon` Gateway

`meshmon` integrates an **`AimonGatewayClient`** that establishes an outbound TCP connection to the central AI gateway on port `3885`.

### Dynamic Toolset Registration
Upon connection, the client registers the following 6 MCP tools:

| Tool Name | Operation Mode | Utility Description |
| :--- | :--- | :--- |
| `meshmon_get_node_status` | **Analytics** | Current list of known nodes, node names, hardware models, battery %, SNR, and last-seen timestamps. |
| `meshmon_query_telemetry_history` | **Analytics** | Historical sensor metrics (battery, voltage, temperature, humidity, channel utilization) over a given time window. |
| `meshmon_get_rf_analytics` | **Analytics** | Analyzes mesh health: duplicate packet ratios, echo storm culprits, single-point-of-failure (SPOF) repeaters, and average hop counts. |
| `meshmon_send_message` | **Management** | Transmits a direct text message to a specific node or broadcasts to the entire mesh (`^all`). |
| `meshmon_query_db` | **Analytics** | Executes read-only SQL queries against `MeshMonDb` SQLite database with schema security guards and parameterization. |
| `meshmon_get_spatial_analytics` | **Analytics** | Spatial metrics: polar coordinates, distance, mobility classification, centroid trilaterations, and link asymmetry. |

---

## 6. Build & Execution Workflow

### Build Targets
Build strictly via the top-level `Makefile` wrapper:

```bash
# Compile meshmon natively:
make -j$(nproc)

# Clean compiled objects:
make clean

# Remove build directory:
make distclean
```

The output binary is placed at `build/$(uname -m)/meshmon`.

### Running the Service
```bash
# Interactive mode with SQLite database and web dashboard:
./build/$(uname -m)/meshmon -d /dev/ttyACM0 -D ~/.config/meshmon/meshmon.db -w 16880

# Background daemon mode:
./build/$(uname -m)/meshmon -d /dev/ttyACM0 -b -D ~/.config/meshmon/meshmon.db --gateway
```

---

## License & Copyright

Copyright (C) 2026, Charles Chiou. All rights reserved.
