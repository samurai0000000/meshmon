# MeshMon: Architectural Design & Technical Specification

## 1. System Overview & Problem Statement

### 1.1 Overview
**MeshMon** is a Linux daemon and monitoring gateway for [Meshtastic](https://meshtastic.org) LoRa mesh networks. It connects to one or more physical Meshtastic radio nodes via Serial (USB), TCP, or Bluetooth LE to provide continuous telemetry extraction, high-performance asynchronous packet logging, deep RF and network analytics, master clock synchronization broadcasts, smart device automation (HomeMesh), and Home Assistant MQTT integration.

All configuration files adhere to `libconfig++` and standard XDG paths (`~/.config/meshmon/meshmon.cfg`), with automatic directory creation and fallback compatibility for legacy paths.

### 1.2 Multi-Host Hardware Topology
`meshmon` is deployed specifically on the hardware node where physical LoRa radio devices are attached:
- **Host**: `fox` (Raspberry Pi, `aarch64`, IP `192.168.8.245`).
- **Physical Radios**: Attached via local USB serial interfaces (`/dev/ttyACM*`, `/dev/ttyUSB*`).
- **Compilation**: Must be compiled natively on `fox` (`ssh -n fox "cd ~/work/meshmon && make"`), generating `build/aarch64/meshmon`.
- **Runtime Environment**: Runs continuously within GNU `screen` session `meshmon`, attached via `/home/samurai/bin/attach-meshmon` (`screen -x -R meshmon`).

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
│                        MeshMon Core Daemon (fox)                            │
│                                                                             │
│  ┌───────────────────────┐  ┌──────────────────────┐  ┌──────────────────┐  │
│  │ MeshClient Engine     │  │ MeshMonDb            │  │ RF Analytics     │  │
│  │ - Radio ingestion     │  │ - Async SQLite logger│  │ - Echo storm     │  │
│  │ - Packet demuxing     │  │ - Dual-time capture  │  │ - SPOF detection │  │
│  │ - Protobuf decoding   │  │ - WAL mode & prune   │  │ - Link asymmetry │  │
│  └───────────┬───────────┘  └──────────┬───────────┘  └────────┬─────────┘  │
│              │                         │                       │            │
│              ├─────────────────────────┴───────────────────────┤            │
│              ▼                                                 ▼            │
│  ┌───────────────────────┐                        ┌──────────────────────┐  │
│  │ MQTT / Home Assistant │                        │ Interactive Shell    │  │
│  │ - Auto-discovery      │                        │ - MeshMonShell (CLI) │  │
│  │ - State telemetry     │                        │ - Status & queries   │  │
│  └───────────────────────┘                        └──────────┬───────────┘  │
│                                                              │              │
│  ┌───────────────────────────────────────────────────────────┴───────────┐  │
│  │                  AimonGatewayClient (TCP Client)                      │  │
│  │        Exports meshmon_* toolset to aimon on builder:3885             │  │
│  └───────────────────────────────────┬───────────────────────────────────┘  │
└──────────────────────────────────────┼──────────────────────────────────────┘
                                       │ Line-delimited JSON-RPC 2.0 (TCP)
                                       ▼
                         aimon Hub (builder:3885)
```

---

## 3. Configuration Standardization (`libconfig++` & XDG Paths)

`meshmon` standardizes on `~/.config/meshmon/`:
- **Configuration File**: `~/.config/meshmon/meshmon.cfg` (legacy: `$HOME/.meshmon`)
- **Database File**: `~/.config/meshmon/meshmon.db` (legacy: `$HOME/.meshmon.db`)
- **Calibration File**: `~/.config/meshmon/meshmon.calib` (legacy: `$HOME/.meshmon.calib`)

The daemon automatically checks for and creates `~/.config/meshmon/` if missing.

Sample `meshmon.cfg` Gateway configuration:
```libconfig
gateway = {
    enabled = true;
    host = "builder";
    port = 3885;
};
```

---

## 4. Core Subsystems

### 4.1 Dual-Time Invariant Packet Ingestion (`MeshMonDb`)
Every packet received by `meshmon` is logged with two distinct timestamps:
- `meshmon_time`: Host wall-clock timestamp (microsecond accuracy from NTP-synchronized Linux host). Represents absolute ground truth.
- `rx_time`: Radio firmware timestamp reported over protobuf.
This dual-time logging enables precise detection of remote RTC drift and relay transit latencies. Logging runs on an asynchronous worker thread with SQLite WAL mode to guarantee zero dropped packets during radio bursts.

### 4.2 Deep RF & Mesh Topology Analytics
- **Echo Storm Detection**: Detects packet duplication ratios and identifies flooding culprits loopback routing.
- **Critical Repeater (SPOF) Discovery**: Computes relay traffic centrality to isolate single points of failure across the physical mesh.
- **Link Asymmetry & Noise Floor Elevation**: Contrasts forward vs. reverse packet SNR and RSSI across 0-hop neighbors to diagnose localized RF interference or antenna degradation.

### 4.3 HomeMesh Device Automation
Monitors and controls smart IoT mesh devices (`meshpump`, `meshroof`, `meshroom`) using calibration curves (`Calibration.hxx`) and bidirectional MQTT state tracking.

---

## 5. AI Toolset Integration via `aimon` Gateway

Rather than exposing a standalone HTTP/SSE server or spawning an isolated process, `meshmon` integrates an **`AimonGatewayClient`**:

1. **Outbound TCP Connection**: Connects to `aimon` at `builder:3885` (or configured host/port). Reconnects automatically on disconnection.
2. **Dynamic Toolset Registration**: Upon connection, registers the following MCP tools:
   - `meshmon_get_node_status`: Queries current state, battery percentage, SNR, and last-seen timestamps for mesh nodes.
   - `meshmon_query_telemetry_history`: Queries SQLite `MeshMonDb` for sensor metrics (temperature, humidity, voltage, channel utilization).
   - `meshmon_send_message`: Transmits direct or broadcast text messages over the mesh.
   - `meshmon_get_rf_analytics`: Returns current health metrics, echo storm ratio, and SPOF repeater analysis.
   - `meshmon_control_device`: Sends control packets to HomeMesh automation nodes.
3. **RPC Execution**: Receives `tools/call` JSON-RPC requests from `aimon`, executes the query against in-memory state or `MeshMonDb`, and returns markdown-formatted results.

---

## 6. Deployment & Process Control

- **Compile**: Native compilation on `fox`:
  ```bash
  ssh -n fox "cd ~/work/meshmon && make -j$(nproc)"
  ```
- **Screen Attachment**:
  ```bash
  /home/samurai/bin/attach-meshmon
  ```
- **Process Restart via Screen**:
  ```bash
  ssh -n fox "screen -S meshmon -X stuff \$''"
  ssh -n fox "screen -S meshmon -X stuff 'cd ~/work/meshmon && ./build/aarch64/meshmon -s /dev/ttyACM0 -D ~/.config/meshmon/meshmon.db
'"
  ```

---

## 7. AI Toolset Matrix: Analytics, Management & Workflow Automation

`meshmon` exports a focused suite of MCP tools designed for real-time mesh RF observability, sensor history querying, and direct on-air message dispatch:

| Tool Name | Operation Mode | Utility Description |
| :--- | :--- | :--- |
| `meshmon_get_node_status` | **Analytics** | Returns current list of known nodes, node names, hardware models, battery %, SNR, and last-seen timestamps. |
| `meshmon_query_telemetry_history` | **Analytics** | Queries SQLite `MeshMonDb` for sensor metrics (battery, voltage, temperature, humidity, channel utilization) over a given time window (e.g. last 6h / 24h). |
| `meshmon_get_rf_analytics` | **Analytics** | Analyzes mesh health: duplicate packet ratios, echo storm culprits, single-point-of-failure (SPOF) repeaters, and average hop counts. |
| `meshmon_send_message` | **Management & Workflow** | Transmits a direct text message to a specific node or broadcasts to the entire mesh (`^all`). |

### Practical Agent Usage Scenarios
- **Analytics**:
  - *"Which nodes in the mesh are currently reporting battery levels below 20% or have not transmitted in the last 2 hours?"*
  - *"Plot the temperature and solar voltage curve for node '!1234abcd' over the last 24 hours from the database."*
  - *"Is there an echo storm or packet looping issue degrading the mesh channel right now?"*
- **Management & Operational Intervention**:
  - *"Broadcast a weather warning message to all nodes on the primary channel."*
  - *"Ping the roof repeater node to test direct 0-hop RF link quality."*
- **Workflow Automation**:
  - **Scheduled RF Audit**: An automated cron agent queries `meshmon_get_rf_analytics` every morning, detects if any repeater node has become a single point of failure (centrality > 70%), and notifies the operator.
  - **Battery & Environmental Alerting**: Autonomous agents inspect `meshmon_query_telemetry_history` periodically to detect failing solar charging circuits before nodes experience complete power loss.

---

## License & Copyright

Copyright (C) 2026, Charles Chiou. All rights reserved.
