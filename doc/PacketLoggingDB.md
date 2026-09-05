# MeshMon Packet Logging Database (SQLite) & Deep RF Analytics

`meshmon` features an embedded, high-performance SQLite packet capture and analytics engine. Every packet received over the radio interface via `SimpleClient::gotPacket` is ingested asynchronously, providing a complete historical time-series of RF propagation physics, network traffic, mesh topology, node presence, and telemetry.

---

## 1. Architecture & Design Principles

### A. Non-Blocking Asynchronous Worker Pipeline
Radio packet reception in Meshtastic requires deterministic, low-latency processing to prevent hardware buffer overruns. 

```
                                  [ Meshtastic Radio ]
                                           │
                                           ▼ (Serial / SPI / TCP)
                                 SimpleClient::gotPacket()
                                           │
                        ┌──────────────────┴──────────────────┐
                        │ (Instant Enqueue - Host NTP Time)   │
                        ▼                                     ▼
             [ DbEvent Queue (Memory) ]             MeshClient Dispatch
                        │                          (gotTextMessage, etc.)
                        ▼
          [ MeshMonDb Worker Thread ]
                        │ (Prepared Statements)
                        ▼
              [ SQLite Database (WAL) ]
```

- When `MeshMon::gotPacket()` is invoked on the radio RX thread, it captures the current host NTP epoch time and pushes a lightweight event to an internal thread-safe queue (`std::queue` with `std::mutex` + `std::condition_variable`).
- The call returns immediately, guaranteeing zero blocking on the Meshtastic client dispatch loop.
- A dedicated background worker thread (`MeshMonDb::workerLoop`) drains the queue and executes batch writes against SQLite.

### B. Authoritative Timestamping Model: `meshmon_time` vs `rx_time`
LoRa nodes in the field frequently lack battery-backed Real-Time Clocks (RTC), lack GPS time synchronization, or reboot with reset clocks.
- **`meshmon_time` (Host NTP Ground Truth)**: Stamped by `meshmon` at the exact instant of packet arrival using host system time (`clock_gettime(CLOCK_REALTIME)` / `time(NULL)`). This serves as the authoritative primary time index for all time-series aggregations, rolling windows (1h, 24h), and retention queries.
- **`rx_time` (Radio Reported Timestamp)**: Preserves the remote radio's timestamp (`packet.rx_time`). Comparing `rx_time` against `meshmon_time` enables remote node clock drift analysis.

### C. SQLite Optimization & Storage Footprint
- **WAL Mode (`PRAGMA journal_mode=WAL;`)**: Write-Ahead Logging allows concurrent analytical queries from the interactive shell, HomeChat, and Home Assistant without blocking ingestion writes.
- **Normal Synchronous (`PRAGMA synchronous=NORMAL;`)**: Provides optimal write throughput on flash media and SD cards.
- **Storage Growth Math**:
  - Raw packet row: ~105 bytes (with B-Tree and indexes).
  - Telemetry / Position row: ~70 bytes.
  - Typical suburban mesh (~1,000 pkts/day): **~65–75 MB / year**.
  - Active urban mesh (~10,000 pkts/day): **~650–750 MB / year**.
- **Automated Retention Pruning**: Configurable via `database.retention_days` in `~/.meshmon` or `--retention-days <n>` CLI option.

---

## 2. Database Schema (DDL)

```sql
-- 1. Nodes Registry
CREATE TABLE IF NOT EXISTS nodes (
    node_id INTEGER PRIMARY KEY,           -- uint32 node number
    node_hex TEXT NOT NULL,                -- e.g. "!1234abcd"
    long_name TEXT,
    short_name TEXT,
    hw_model INTEGER,
    role INTEGER,
    first_seen INTEGER NOT NULL,           -- Host NTP epoch seconds
    last_seen INTEGER NOT NULL,            -- Host NTP epoch seconds
    last_rssi REAL,
    last_snr REAL,
    last_hops INTEGER
);

-- 2. Raw Packets (Logged for every gotPacket, including duplicate echoes)
CREATE TABLE IF NOT EXISTS packets (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meshmon_time INTEGER NOT NULL,         -- Host NTP arrival epoch (Ground Truth)
    rx_time INTEGER,                       -- Raw timestamp from radio (packet.rx_time)
    packet_id INTEGER NOT NULL,            -- Meshtastic packet ID
    from_node INTEGER NOT NULL,            -- Sender node ID
    to_node INTEGER NOT NULL,              -- Destination (0xffffffff = broadcast)
    channel INTEGER NOT NULL,              -- Channel index
    rx_rssi REAL,                          -- Signal strength (dBm)
    rx_snr REAL,                           -- Signal-to-noise ratio (dB)
    hop_start INTEGER,
    hop_limit INTEGER,
    hops INTEGER,                          -- hop_start - hop_limit
    portnum INTEGER,                       -- PortNum enum
    payload_variant INTEGER,               -- 1=decoded, 2=encrypted
    payload_size INTEGER,                  -- Payload byte length
    want_ack INTEGER,                      -- 1 if ACK requested
    via_mqtt INTEGER,                      -- 1 if packet arrived via MQTT gateway
    payload BLOB,                          -- Raw payload bytes
    FOREIGN KEY(from_node) REFERENCES nodes(node_id)
);

CREATE INDEX IF NOT EXISTS idx_packets_meshmon_time ON packets(meshmon_time);
CREATE INDEX IF NOT EXISTS idx_packets_pkt_from ON packets(packet_id, from_node);
CREATE INDEX IF NOT EXISTS idx_packets_from ON packets(from_node);
CREATE INDEX IF NOT EXISTS idx_packets_portnum ON packets(portnum);
CREATE INDEX IF NOT EXISTS idx_packets_hops ON packets(hops);

-- 3. Telemetry & Metrics (Device, Environment, Power, LocalStats)
CREATE TABLE IF NOT EXISTS telemetry (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meshmon_time INTEGER NOT NULL,         -- Host NTP epoch
    node_id INTEGER NOT NULL,
    metric_type TEXT NOT NULL,             -- 'device', 'env', 'power', 'stats'
    battery_level INTEGER,
    voltage REAL,
    channel_utilization REAL,
    air_util_tx REAL,
    temperature REAL,
    relative_humidity REAL,
    barometric_pressure REAL,
    ch1_voltage REAL,
    ch1_current REAL,
    uptime_seconds INTEGER,
    FOREIGN KEY(node_id) REFERENCES nodes(node_id)
);
CREATE INDEX IF NOT EXISTS idx_telemetry_node_time ON telemetry(node_id, meshmon_time);

-- 4. Positions Track Log
CREATE TABLE IF NOT EXISTS positions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meshmon_time INTEGER NOT NULL,         -- Host NTP epoch
    node_id INTEGER NOT NULL,
    latitude REAL NOT NULL,
    longitude REAL NOT NULL,
    altitude INTEGER,
    ground_speed INTEGER,
    ground_track INTEGER,
    sats_in_view INTEGER,
    FOREIGN KEY(node_id) REFERENCES nodes(node_id)
);
CREATE INDEX IF NOT EXISTS idx_positions_node_time ON positions(node_id, meshmon_time);

-- 5. Text Messages (Direct & Channel Chat History)
CREATE TABLE IF NOT EXISTS text_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meshmon_time INTEGER NOT NULL,         -- Host NTP epoch
    from_node INTEGER NOT NULL,
    to_node INTEGER NOT NULL,
    channel INTEGER NOT NULL,
    message TEXT NOT NULL,
    FOREIGN KEY(from_node) REFERENCES nodes(node_id)
);

-- 6. Traceroutes & Path Discoveries
CREATE TABLE IF NOT EXISTS traceroutes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meshmon_time INTEGER NOT NULL,         -- Host NTP epoch
    from_node INTEGER NOT NULL,
    to_node INTEGER NOT NULL,
    route_count INTEGER NOT NULL,
    route_nodes TEXT NOT NULL,             -- Comma-separated intermediate node IDs
    route_snrs TEXT NOT NULL,              -- Comma-separated SNR values
    FOREIGN KEY(from_node) REFERENCES nodes(node_id)
);

-- 7. HomeMesh Automation & Robot Command Audit Log
CREATE TABLE IF NOT EXISTS automation_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meshmon_time INTEGER NOT NULL,         -- Host arrival epoch timestamp (seconds)
    node_id INTEGER NOT NULL,              -- Meshtastic node integer ID (e.g. 0x2bf941d4)
    node_hex TEXT NOT NULL,                -- Node hex string (e.g. "!2bf941d4")
    device_type TEXT NOT NULL,             -- 'meshpump', 'meshroof', 'meshroom'
    direction TEXT NOT NULL,               -- 'TX_CMD' (MeshMon->Node) or 'RX_STATE' (Node->MeshMon)
    subsystem TEXT NOT NULL,               -- 'pump', 'led', 'amplify', 'wifi', 'ac', 'tv', 'env', 'system'
    command_name TEXT NOT NULL,            -- e.g. 'PUMP_FISH_ON', 'AC_TEMP', 'BOOT_UP', 'UPTIME', 'DEVICE_TYPE_MIGRATION'
    action_param TEXT,                     -- e.g. '24C', 'cutoff=30s', 'uptime=14d', 'meshroom -> meshroof'
    status TEXT NOT NULL,                  -- 'EXECUTED', 'ACKED', 'TIMEOUT', 'FAILED'
    initiator TEXT NOT NULL                -- 'HOMEASSISTANT', 'SHELL', 'SCHEDULE', 'RF'
);
CREATE INDEX IF NOT EXISTS idx_auto_events_node ON automation_events(node_id, meshmon_time);
CREATE INDEX IF NOT EXISTS idx_auto_events_dev ON automation_events(device_type, meshmon_time);
CREATE INDEX IF NOT EXISTS idx_auto_events_sub ON automation_events(subsystem, meshmon_time);
```

---

## 3. Deep RF & Network Analytics

The database unlocks high-resolution insights that are impossible on standard node firmware or cloud MQTT monitors:

### A. Mesh Reverberation & Echo Storm Detection (`db storm` / `storm?`)
When a broadcast packet travels across a dense mesh, multiple repeaters re-transmit it. By capturing duplicate packet arrivals `(packet_id, from_node)` with distinct hop counts and arrival timestamps:
- **Echo Multiplier**: Measures how many times your station heard the same broadcast.
- **Reverberation Duration**: Measures the time difference between the first and last received echo of a packet, identifying aggressive flooding configurations.

### B. Direct Link SNR & Noise Floor Asymmetry (`db asymmetry` / `asymmetry?`)
By isolating direct 0-hop packets (`hops = 0`):
- Evaluates raw line-of-sight signal margins without relay degradation.
- Helps identify local RF interference, desensitization (e.g. from nearby USB 3.0 or power supply noise), or transmitter power imbalances.

### C. Critical Relay / Single-Point-of-Failure (SPOF) Discovery (`db spof` / `spof?`)
- Aggregates all packets received via `hops > 0` and traceroute discoveries.
- Ranks nodes carrying the highest relayed traffic volume to pinpoint single points of failure that would partition the mesh into isolated islands.

### D. Remote Clock Drift Analysis (`db drift` / `drift?`)
- Evaluates `skew = (packet.rx_time - meshmon_time)`.
- Discovers remote nodes with uncalibrated crystal oscillators, failed RTC backup cells, or lost GPS lock.

### E. Hourly Link Fading Curves (`db fading <node>`)
- Aggregates hourly average, minimum, and maximum SNR/RSSI for a specific node.
- Reveals diurnal fading cycles (morning vs evening thermal inversions) and rainfall attenuation.

---

## 4. Interactive Shell Commands (`MeshMonShell`)

The interactive shell exposes high-level commands under the `db` command group:

| Shell Command | Description |
| :--- | :--- |
| `db summary [hours]` | Displays packet volume, byte throughput, broadcast ratio, and direct/relayed breakdown. |
| `db toptalkers [limit] [hours]` | Lists top nodes ranked by packet count and airtime bytes. |
| `db neighbors [hours]` | Shows direct 0-hop RF neighbors with min/avg/max SNR & RSSI. |
| `db storm [limit] [hours]` | Detects duplicate packet floods, echo multipliers, and reverberation duration. |
| `db asymmetry [hours]` | Compares bidirectional SNR with direct neighbors to spot noise/power imbalance. |
| `db spof [limit] [hours]` | Identifies critical single-point-of-failure relay nodes. |
| `db drift [hours]` | Compares radio timestamps against NTP ground truth to detect clock drift. |
| `db hops [hours]` | Displays histogram of packet counts by hop distance (0h, 1h, 2h, 3+). |
| `db apps [hours]` | Shows traffic composition breakdown by application portnum. |
| `db node <node> [hours]` | Displays complete node timeline, RF link statistics, and battery history. |
| `db fading <node> [hours]` | Displays hourly SNR/RSSI trends for a target node to analyze link fading. |
| `db health [hours]` | Shows average channel utilization %, TX airtime %, and duplicate ratios. |
| `db query <SQL>` | Executes a custom read-only SQL query against the database. |
| `db help` / `db -help` | Displays usage summary. |

*Note: The optional `[hours]` parameter defaults to `24` (last 24 hours). Specifying `0` queries all historical data.*

---

## 5. HomeChat Natural Query Handlers (On-Mesh RF Text)

Authorized mesh nodes can query the database directly over LoRa text messages:

| User Text Message | `meshmon` Response |
| :--- | :--- |
| `@meshmon traffic?` or `traffic` | `traffic(24h): 1420 pkts, 68400 bytes, bcast=72%, direct=45%` |
| `@meshmon toptalkers` or `top?` | `toptalkers(24h): !2bf941d4: 312p, Roof: 184p, Base: 92p` |
| `@meshmon neighbors` or `direct?` | `neighbors(24h): 5 nodes. best: Roof (+10.5dB)` |
| `@meshmon storm?` or `storm` | `storm(24h): max echo=4x pkts on pkt !1a2b3c4d from !2bf941d4 (2s duration)` |
| `@meshmon asymmetry?` | `asymmetry: 4 links. weakest: !3c4d5e6f (+1.2dB)` |
| `@meshmon spof?` or `relays?` | `critical relays: Roof (214 relays), Hilltop (118 relays)` |
| `@meshmon drift?` | `clock drift: max skew !4d5e6f7a +18s (samples=42)` |
| `@meshmon hops?` | `hops(24h): 0h:45%, 1h:35%, 2h:18%, 3h:2%` |
| `@meshmon health?` or `channel?` | `health(24h): ch_util=14.2% air_tx=2.8% pkts=1420 dupes=84` |

---

## 6. Configuration

Configure database settings in `~/.meshmon`:

```text
database = {
    enabled = true;
    path = "~/.meshmon.db";
    retention_days = 90; // Automatically prunes records older than 90 days (0 = unlimited)
};
```

Or specify via command-line options:
- `-D / --database <path>`: Specify database file path.
- `--no-database` or `--no-db`: Disable SQLite packet logging.
- `--retention-days <days>`: Override retention threshold.
