# HomeMesh Automation: Robot Channel Integration, Analytics & Home Assistant Control

`meshmon` serves as the central automation bridge, analytics engine, and Home Assistant proxy for Meshtastic-powered home and field automation endpoints. This document specifies the message protocol, node authentication/mate verification, boot-up & hourly heartbeat ingestion, dynamic role conflict/migration handling, targeted discovery state machine, command lifecycle, interactive `robot` shell management, SQLite database audit architecture, analytical query interfaces, and bidirectional Home Assistant control proxies for **`meshpump`**, **`meshroof`**, and **`meshroom`**.

---

## 1. System Architecture & Message Flow

```
┌────────────────────────────────────────────────────────────────────────────┐
│                             Home Assistant                                 │
│      (Lovelace Dashboard, Automations, Switches, Climate, Media, Numbers)  │
└──────────────────────▲──────────────────────────────┬──────────────────────┘
                       │ MQTT Telemetry & States      │ MQTT Control Commands
                       │ (meshmon/<node>/...)         │ (meshmon/cmd/<node>/...)
┌──────────────────────┴──────────────────────────────▼──────────────────────┐
│                                 MeshMon                                    │
│  ┌────────────────────────┐  ┌──────────────────┐  ┌────────────────────┐  │
│  │   MQTT Control Proxy   │  │ SQLite Analytics │  │ Interactive 'robot'│  │
│  │ (Command Translation)  │  │ (Audit Log DB)   │  │ Shell & In-Memory  │  │
│  │                        │  │                  │  │ Node State Machine │  │
│  └───────────┬────────────┘  └────────▲─────────┘  └─────────┬──────────┘  │
└──────────────┼────────────────────────┼──────────────────────┼─────────────┘
               │ Transmit LoRa Action   │ Ingest State / ACK   │
               ▼                        ▼                      ▼
┌────────────────────────────────────────────────────────────────────────────┐
│             LoRa Mesh Network (Robot Channel with Mate Authentication)      │
│                                                                            │
│   ┌─────────────────────┐  ┌───────────────────┐  ┌────────────────────┐   │
│   │      meshpump       │  │     meshroof      │  │      meshroom      │   │
│   │ (Raspberry Pi/Linux)│  │ (ESP32/ESP32-S3)  │  │  (RP2040/RP2350)   │   │
│   │  - Boot Announcement│  │  - Boot Announcement│ │  - Boot Announcement│ │
│   │  - Hourly Uptime Ann│  │  - Hourly Uptime Ann│ │  - Hourly Uptime Ann│ │
│   │  - Fish / Up Pumps  │  │  - RF PA Amplifier│  │  - AC IR Control   │   │
│   │  - Auto-cutoff timer│  │  - WiFi / Net IP  │  │  - TV IR Control   │   │
│   │  - MAX7219 LED Matrix│ │  - CPU Temp Sensor│  │  - Board Temp / Env│   │
│   │  - Uptime / Latency │  │  - Buzzer / Morse │  │  - Buzzer / Morse  │   │
│   └─────────────────────┘  └───────────────────┘  └────────────────────┘   │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Security, Authentication & Mate Verification

To prevent rogue nodes or RF spoofing from injecting bogus states or hijacking controls:

1. **Mate Verification Rule**:
   * Every Meshtastic message received on the robot channel must originate from a node present in `meshmon`'s configured **`_mates`** (or **`_admins`**) list.
   * If a Meshtastic message arrives from a node that is NOT an authorized mate, `meshmon` **immediately drops and ignores** the message for all automation, state update, and control proxying operations.
2. **Channel Authorization**:
   * Operations on designated authorized channels verify channel PSK identity before executing privileged actions.
3. **Mate Authorization vs. Automation Robot Gating**:
   * Presence in the `_mates` list authorizes a node to send packets, forward environmental telemetry, or participate in mesh chat. It **does not** automatically register the node as an automation robot.
   * A node is only recognized as a member of the HomeMesh Automation Fleet if it explicitly identifies as a supported robot application (`meshpump`, `meshroof`, `meshroom`). Non-robot mates (such as relays, anchors, or personal communicators) never generate automation controls or appear in the robot fleet.

---

## 3. Dynamic Role Changes & Hardware Board Swaps

In modular LoRa setups, radio modules may be reprogrammed or swapped between physical hardware (e.g., a node previously running `meshroom` firmware is flashed with `meshroof`, or a transceiver board is moved from a room controller to an irrigation pump).

### How Role Conflicts and Migrations are Handled:

```
               [Incoming Message from Node !1a2b3c4d]
               (Identifies app=meshroof; Previous=meshroom)
                                  │
                                  ▼
                ┌───────────────────────────────────┐
                │ Role Change Detected:             │
                │ meshroom ───────► meshroof        │
                └─────────────────┬─────────────────┘
                                  │
         ┌────────────────────────┴────────────────────────┐
         │                                                 │
         ▼                                                 ▼
┌─────────────────────────────────┐       ┌─────────────────────────────────┐
│ 1. MQTT Entity Revocation:       │       │ 2. State & DB Migration:        │
│ Publish empty retained payload  │       │ - Re-initialize AutomationNode  │
│ ("") to obsolete discovery      │       │   with meshroof capabilities.   │
│ topics (e.g., AC, TV entities)  │       │ - Publish new meshroof discovery│
│ -> Pruned from Home Assistant!  │       │   topics to Home Assistant.     │
└─────────────────────────────────┘       │ - Log migration audit event in  │
                                          │   `automation_events` table.    │
                                          └─────────────────────────────────┘
```

1. **Detection**: When an incoming message (`identify`, `rollcall:`, `boot-up`, or subsystem telemetry) announces an application type differing from the node's current in-memory record, `meshmon` triggers an immediate **Device Role Migration**.
2. **Home Assistant Entity Revocation (Pruning)**:
   - Home Assistant retains MQTT Auto-Discovery topics until explicitly cleared.
   - `meshmon` iterates over all discovery topics registered for the node's **old** role (e.g., `climate.<node>_ac_climate`, `switch.<node>_tv_power`) and publishes an **empty payload (`""`) with `retain=true`**.
   - Home Assistant immediately removes the stale entities from its registry and Lovelace cards.
3. **New Entity Registration**:
   - `meshmon` publishes the new role's discovery topics (e.g., `switch.<node>_amplify`, `sensor.<node>_cpu_temp`).
4. **Audit Trail**:
   - A `DEVICE_TYPE_MIGRATION` event is logged in SQLite `automation_events` recording the timestamp and transition (`action_param="meshroom -> meshroof"`).
   - Historical time-series telemetry rows maintain their original device type stamped at the time of occurrence.

---

## 4. Ingestion of Boot-up Announcements & Hourly Uptime Heartbeats

All HomeMesh robot nodes automatically broadcast standard lifecycle messages on the robot channel:

### A. Boot-up Broadcast (`boot-up: <ShortName>`)
* **Format**: `boot-up: <ShortName>` (e.g. `boot-up: PUMP`)
* **MeshMon Ingestion Logic**:
  1. **Instant Node Awakening**: Node is immediately marked `ONLINE` in memory and available in Home Assistant.
  2. **Automated Registration Interrogation**: `meshmon` automatically broadcasts `!<node_id> identify` on the robot channel (visible to observers). Only the addressed node reports its application type, version, and capabilities. HomeChat `rollcall` is not used for fleet probing.
  3. **Reboot Counter Tracking**: Increments the node's reboot counter in memory and logs the boot event to SQLite `automation_events` (`subsystem="system"`, `command_name="BOOT_UP"`).

### B. Hourly Uptime Broadcast (`uptime: ...`)
* **Format**:
  * Under 24h: `uptime: HH:MM:SS` (e.g. `uptime: 04:00:00`)
  * Over 24h: `uptime: <N>d HH:MM:SS` (e.g. `uptime: 3d 14:00:00`)
* **MeshMon Ingestion Logic**:
  1. **Passive Liveness Renewal**: Automatically refreshes the node's liveness watchdog timer without active polling.
  2. **Uptime Parsing & Home Assistant Export**: Parses uptime days/hours/minutes into total seconds and updates the Home Assistant `sensor.<node_id>_uptime` entity.
  3. **Unexpected Reboot Detection**: If the parsed uptime is less than the previously recorded uptime for that node, `meshmon` detects an unannounced reboot, updates reboot metrics, and logs the event in SQLite.
  4. **Audit Logging**: Logs the hourly heartbeat in `automation_events` (`subsystem="system"`, `command_name="UPTIME"`).

---

## 5. Targeted Node Discovery (`identify`)

`meshmon` transmits every outbound automation command as a **robot-channel broadcast** (`to = 0xffffffff`), not a Meshtastic direct message. Targeting is in the text so observers on `#home` can see interrogations and controls, while only the addressed node answers or acts.

- **Fleet probe**: `!<nodeid> identify` (HomeChat channel addressing). Old HomeChat nodes ignore `identify` and stay silent. Only updated robot firmware replies.
- **Control verbs**: `!<nodeid> <cmd>` (e.g. `!db2f0dcc fish on`).
- **HomeChat `rollcall`**: social / human command only. MeshMon does **not** use it for automation discovery (legacy firmware replies `"… is at your service"` and must not be fed to Gemini).

### A. Targeted `identify` Command Syntax

| Command Syntax | Target Scope | Behavior |
| :--- | :--- | :--- |
| `!<node_id> identify` | Specific Hex Node ID (e.g. `!2bf941d4`) | Channel broadcast; only the matching node responds. Other nodes remain silent. |
| `<short_name> identify` | Specific Short Name (e.g. `PUMP`, `ROOF`, `ROOM`) | Only the node whose short name matches responds. |
| `<long_name> identify` | Specific Long Name | Only the node whose long name matches responds. |
| `all identify` | All Nodes on Channel | Broadcast discovery; updated nodes reply with small jitter delay. |

### B. Standard Machine-Readable Response Format

```text
identify: app=<app_name> ver=<x.y.z> hw=<platform> [caps=<cap1,cap2...>]
```

MeshMon also accepts the legacy prefix `rollcall: app=…` with the same tokens.

#### Per-Device Structured Responses:
* **`meshpump`**:
  ```text
  identify: app=meshpump ver=2.1.2 hw=linux caps=pump_fish,pump_up,led,env
  ```
* **`meshroof`**:
  ```text
  identify: app=meshroof ver=2.1.2 hw=esp32s3 caps=amplify,wifi,net,cpu_temp,buzzer
  ```
* **`meshroom`**:
  ```text
  identify: app=meshroom ver=2.1.2 hw=rp2040 caps=ac_ir,tv_ir,board_temp,buzzer
  ```

`caps=` is a string literal compiled into each firmware. It advertises
what the application was built to support, and does not vary with how
an individual unit is provisioned or wired. MeshMon records it for
display only and never gates Home Assistant entities on it; see
section 12.B for the provisioning signal that is actually per unit.

### C. Outdated / Unparseable Firmware Policy & Strict Robot Gating
* **Legacy String Rejection**: If an authorized mate responds to a human `rollcall` with a legacy unparseable string (such as `"<Node>, <Target> is at your service"`), `meshmon` consumes the message and does not forward it to Gemini. The node is **strictly excluded** from `_autoNodes` and will not be registered as a robot.
* **Probing without Premature Insertion**: When an unknown node broadcasts `boot-up:` or `uptime:`, `meshmon` dispatches a targeted `!<node_id> identify` probe. However, `meshmon` **refrains** from creating an in-memory `AutomationNode` entry or generating Home Assistant discovery configs until the node responds with a recognized robot application (`app=meshpump`, `app=meshroof`, `app=meshroom`).
* **Non-Robot Exclusion**: Nodes running standard Meshtastic firmware, legacy scripts, or non-robot roles (e.g. `anch`) are never added to the fleet, never appear in the `robot` CLI command, and do not increment gateway automation fleet metrics (`auto_nodes_total`, `auto_nodes_online`).

---

## 6. In-Memory Node State Machine & Command Lifecycle

`meshmon` maintains a real-time in-memory state object for each registered robot node (`AutomationNode`). The state machine tracks node connectivity, subsystem values, and command lifecycles:

### A. Node Connectivity State Transitions

```
    [Boot-up Message / Rollcall RX / Hourly Uptime]
                           │
                           ▼
                     ┌───────────┐
   ┌────────────────►│  ONLINE   │◄────────────────┐
   │                 └─────┬─────┘                 │
   │ Heartbeat /           │ No message for        │ Heartbeat /
   │ Meshtastic Message RX │ 15 minutes            │ Meshtastic Message RX
   │                       ▼                       │
   │                 ┌───────────┐                 │
   └─────────────────┤  OFFLINE  ├─────────────────┘
                     └───────────┘
```

1. **`ONLINE`**: Node is actively heard and responding. Home Assistant entities report `available`.
2. **`OFFLINE`**: No telemetry, command ACK, boot announcement, or hourly uptime received for $> 15\text{ minutes}$. Home Assistant entities transition to `unavailable`.

---

### B. Outbound Command State Transitions (`TX_CMD`)

```
                  ┌───────────────────────────────┐
                  │ Home Assistant / Shell Action │
                  │ Dispatches TX_CMD to Node     │
                  └──────────────┬────────────────┘
                                 │
                                 ▼
                        ┌─────────────────┐
                        │   PENDING_ACK   │  (5.0s Timeout Timer Started)
                        └────────┬────────┘
                                 │
         ┌───────────────────────┼───────────────────────┐
         │ Node replies state    │ 5.0s timer expires    │ Node returns error /
         │ matching command      │ without reply         │ Radio TX fails
         ▼                       ▼                       ▼
   ┌───────────┐           ┌───────────┐           ┌───────────┐
   │   ACKED   │           │  TIMEOUT  │           │  FAILED   │
   └─────┬─────┘           └─────┬─────┘           └─────┬─────┘
         │                       │                       │
         └───────────────────────┴───────────────────────┘
                                 │
                                 ▼
                     Logged to SQLite DB Table
                       `automation_events`
```

---

## 7. Interactive Shell `robot` Command

The **`robot`** command in `MeshMonShell` provides live status inspection for in-memory automation nodes. It strictly filters out any non-robot entries, displaying only verified automation nodes with a valid, non-empty `deviceType`:

```text
  robot                   - Show live status table of all discovered robot nodes
  robot <node>            - Show detailed live operational state, uptime, and telemetry for a node
  robot help              - Show robot command help
```

### Example `robot` Command Outputs:

```text
meshmon> robot
=== Discovered Robot Fleet (In-Memory Live State) ===
Node       App       Ver    HW       Status   Uptime        Primary State          Secondary State        Last Seen
!1a2b3c4d  meshpump  2.1.2  linux    ONLINE   14d 06:12:00  Fish=ON, Up=OFF (30s)  LED: Delay 50ms        12s ago
!2b3c4d5e  meshroof  2.1.2  esp32s3  ONLINE   18d 02:45:10  PA Amplification=ON    WiFi: -62dBm (192.168) 45s ago
!3c4d5e6f  meshroom  2.1.2  rp2040   ONLINE   2d 11:20:00   AC: COOL 24°C Fan AUTO TV: ON (Vol 15)        8s ago

meshmon> robot !1a2b3c4d
=== Robot Node !1a2b3c4d (Garden Pump) ===
Application:     meshpump v2.1.2 (Hardware: linux)
Connectivity:    ONLINE (Last seen: 12 seconds ago)
Uptime:          14 days, 6 hours, 12 minutes (Reboots: 1)
Capabilities:    pump_fish, pump_up, led, env
Subsystems:
  Fish Pump:     ON
  Upper Pump:    OFF (Auto-Cutoff: 30 seconds)
  LED Matrix:    Delay: 50ms, Row0: "Garden Active", SF: 1
  Environment:   Temp: 24.1 °C, Hum: 52%, Press: 1013 hPa
```

---

## 8. Supported Device Types & Extended Command Protocols

Messages transmitted on the dedicated **Robot Channel** utilize structured `HomeChat` key-value syntax with deterministic acknowledgments. MeshMon prefixes each control verb with the target node hex (`!2bf941d4 pump fish on`) so the packet is a channel broadcast that only the addressed node executes. The tables below list the verb after that prefix.

### A. `meshpump` (Fish Tank & Upper Water Pump Relay Controller)

#### Command Set (MeshMon $\rightarrow$ `meshpump`):
| Command | Arguments | Description | Example |
| :--- | :--- | :--- | :--- |
| `pump fish` | `<on\|off>` | Turn Fish Tank pump relay on or off | `pump fish on` |
| `pump up` | `<on\|off> [cutoff_sec]` | Turn Upper pump on with auto-cutoff timer (or off) | `pump up on 30` |
| `led` | `<row> <text>` | Display/scroll text on LED matrix row | `led 0 Hello World` |
| `led delay` | `<ms>` | Set LED matrix scroll refresh delay in ms | `led delay 50` |
| `led sf` | `<row> <factor>` | Set slowdown factor for specific row | `led sf 0 2` |
| `led blank` | — | Clear/blank the LED matrix | `led blank` |
| `led welcome` | — | Reset LED matrix to default welcome text | `led welcome` |
| `led` | — | Query current LED matrix timing and configuration | `led` |
| `env` | — | Query ambient sensors and CPU temperature | `env` |
| `status` | — | Query both pump relays and the auto-cutoff timer | `status` |

`fish` and `up` are sub-verbs of `pump` and are **not** dispatched on
their own. `pump fish on` works; a bare `fish on` draws no reply at all.
There is no standalone cutoff setter: the auto-cutoff value rides along
with `pump up on <cutoff_sec>`, so MeshMon remembers a cutoff set from
Home Assistant and applies it on the next start of the upper pump.

#### State & Telemetry Reports (`meshpump` $\rightarrow$ MeshMon):
* **Pump Query**: `pump: fish=<on|off> up=<on|off> cutoff=<sec>s`
* **Fish Pump Set**: `pump: fish=<on|off>`
* **Up Pump Set**: `pump: up=<on|off> [cutoff=<sec>s]`
* **Status Report**: `status: fish=<on|off> up=<on|off> up_cutoff=<sec>s`
* **LED Status**: `delay: <ms>ms` followed by one `row <n> ...` line per row
* **Env Telemetry**: `env: temp=<c> rh=<pct> bp=<hpa> temp_cpu=<c>`

`meshpump` has no soil moisture probe and no reservoir level switch, so
it never reports either. The corresponding Home Assistant entities were
removed in 2.1.11.

---

### B. `meshroof` (ESP32/ESP32-S3 High-Power Rooftop Station)

#### Command Set (MeshMon $\rightarrow$ `meshroof`):
| Command | Arguments | Description | Example |
| :--- | :--- | :--- | :--- |
| `amplify` | `[on\|off]` | Enable or bypass external RF power amplifier (PA/LNA) | `amplify on` |
| `amplify` | — | Query current RF power amplifier state | `amplify` |
| `wifi` | — | Query WiFi link status, SSID, BSSID, channel, and RSSI | `wifi` |
| `net` | — | Query IP mode (dhcp/static), IP, netmask, gateway, and DNS | `net` |
| `reset` | `[apply]` | Query boot reset count & uptime, or trigger software reset | `reset` |
| `buzz` | — | Sound onboard acoustic buzzer | `buzz` |
| `morse` | `<text>` | Transmit text as audible Morse code on buzzer | `morse CQ CQ` |
| `env` | — | Query ambient sensors and internal CPU temperature | `env` |
| `status` | — | Multi-line status report (amplify, reset count, CPU temp) | `status` |

#### State & Telemetry Reports (`meshroof` $\rightarrow$ MeshMon):
* **Amplify State**: `amplify: state=<on|off> gain=<level> pa=<n>dBm`
* **WiFi Status**: `wifi: status=<connected|disconnected> [ssid=<ssid> rssi=<n> ip=<addr>]`
* **Network Status**: `net: ip=<ip> gw=<gw> dns=<dns>`
* **Network Ping**: `net: ping=<host> rtt=<n>ms` or `net: ping=<host> failed`
* **Reset Status**: `reset: count=<n> reason=<r> secs_ago=<n>`
* **Status Report**: `status: amplify=<on|off> gain=<level> pa=<n>dBm reset_count=<n> last_reset=<s>s temp_chip=<c>`
* **Buzzer Reply**: `buzz: freq=<hz> dur=<ms>`
* **Morse Reply**: `morse: text='<text>'`
* **Env Telemetry**: `env: temp=<c> rh=<pct> bp=<hpa> temp_chip=<c>`

---

### C. `meshroom` (RP2040/RP2350 Room IR Climate & TV Controller)

#### Command Set (MeshMon $\rightarrow$ `meshroom`):
| Command | Arguments | Description | Example |
| :--- | :--- | :--- | :--- |
| `ac` | `<on\|off>` | Turn Air Conditioner on or off | `ac on` |
| `ac temp` | `<16-30\|up\|down>` | Set or step target AC temperature (°C) | `ac temp 24` |
| `ac mode` | `<cool\|heat\|dry\|fan\|auto>` | Set AC operating mode | `ac mode cool` |
| `ac fan` | `<auto\|quiet\|low\|med\|high\|max\|1-5>` | Set AC blower fan speed | `ac fan auto` |
| `ac vane` | `<1-5\|auto\|swing>` | Set AC airflow vane direction / swing | `ac vane auto` |
| `ac tx` / `ac blast`| — | Force retransmit current AC state via IR blast | `ac tx` |
| `ac` | — | Query current AC settings state | `ac` |
| `tv` | `<on\|off\|toggle>` | Power on, off, or toggle TV power | `tv on` |
| `tv vol` | `<0-100\|up\|down>` | Set or step TV volume level | `tv vol up` |
| `tv chan` | `<1-999\|up\|down>`| Set or step TV channel | `tv chan 5` |
| `tv mute` | `[on\|off]` | Mute, unmute, or toggle TV audio | `tv mute on` |
| `tv input` / `tv source` | — | Cycle TV video input source | `tv input` |
| `tv key` / `tv <0-9>`| `<digit>` | Send numeric digit key (0–9) to TV | `tv 8` |
| `tv` | — | Query current TV state and IR protocol | `tv` |
| `buzz` | — | Sound onboard buzzer | `buzz` |
| `morse` | `<text>` | Sound Morse code transmission on buzzer | `morse SOS` |
| `env` | — | Query ambient sensors and RP2040 onboard temperature | `env` |

#### State & Telemetry Reports (`meshroom` $\rightarrow$ MeshMon):
* **AC State**: `ac: pwr=<on|off> mode=<cool|heat|dry|fan|auto> temp=<16-30> fan=<auto|1-5> vane=<auto|1-5> turbo=<on|off> quiet=<on|off> ir=<protocol|none>`
* **TV State**: `tv: pwr=<on|off> vol=<vol> chan=<chan> mute=<on|off> ir=<protocol|none>`
* **Env Telemetry**: `env: temp=<c> rh=<pct> bp=<hpa> temp_board=<c>`

Single-field setters reply with only the field they changed, such as
`ac: temp=24` or `tv: mute=on`. Only the bare `ac` and `tv` queries
carry the full state including `ir=`. The `env` reply drops the `env:`
prefix entirely when the gateway holds no cached environment metrics
for the node, leaving a bare `temp_board=<c>`, so MeshMon reads the
board temperature from the tokens rather than from the verb.

The AC mode vocabulary differs on the two sides: meshroom says `fan`
where Home Assistant says `fan_only`, and MeshMon translates in both
directions at the MQTT boundary.

---

## 9. SQLite Database Architecture & Analytics (`MeshMonDb`)

```sql
-- Audit Log for all Decoded Robot Channel Commands & Telemetry Events
CREATE TABLE IF NOT EXISTS automation_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meshmon_time INTEGER NOT NULL,          -- Host arrival epoch timestamp (seconds)
    node_id INTEGER NOT NULL,               -- Meshtastic node integer ID (e.g. 0x2bf941d4)
    node_hex TEXT NOT NULL,                 -- Node hex string (e.g. "!2bf941d4")
    device_type TEXT NOT NULL,              -- 'meshpump', 'meshroof', 'meshroom'
    direction TEXT NOT NULL,                -- 'TX_CMD' (MeshMon->Node) or 'RX_STATE' (Node->MeshMon)
    subsystem TEXT NOT NULL,                -- 'pump', 'led', 'amplify', 'wifi', 'ac', 'tv', 'env', 'system'
    command_name TEXT NOT NULL,             -- e.g. 'PUMP_FISH_ON', 'AC_TEMP', 'BOOT_UP', 'UPTIME', 'DEVICE_TYPE_MIGRATION'
    action_param TEXT,                      -- e.g. '24C', 'cutoff=30s', 'uptime=14d', 'meshroom -> meshroof'
    status TEXT NOT NULL,                   -- 'EXECUTED', 'ACKED', 'TIMEOUT', 'FAILED'
    initiator TEXT NOT NULL,                -- 'HOMEASSISTANT', 'SHELL', 'SCHEDULE', 'RF'
    rtt_ms INTEGER DEFAULT 0                -- Measured round-trip response latency in milliseconds
);

-- Durable registry of identified robots, so Home Assistant discovery
-- is correct on the first publish after a gateway restart instead of
-- waiting for a probe round-trip.
CREATE TABLE IF NOT EXISTS automation_nodes (
    node_id INTEGER PRIMARY KEY,            -- Meshtastic node integer ID
    node_hex TEXT NOT NULL,                 -- Node hex string without the '!'
    device_type TEXT NOT NULL,              -- 'meshpump', 'meshroof', 'meshroom'
    version TEXT,                           -- Firmware version from identify ver=
    hardware TEXT,                          -- Hardware string from identify hw=
    capabilities TEXT,                      -- Compile-time caps= list, informational only
    ac_ir_protocol TEXT,                    -- meshroom ac reply ir=, 'none' when cleared
    tv_ir_protocol TEXT,                    -- meshroom tv reply ir=, 'none' when cleared
    first_seen INTEGER NOT NULL,
    last_seen INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_auto_events_node ON automation_events(node_id, meshmon_time);
CREATE INDEX IF NOT EXISTS idx_auto_events_dev ON automation_events(device_type, meshmon_time);
CREATE INDEX IF NOT EXISTS idx_auto_events_sub ON automation_events(subsystem, meshmon_time);
```

### High-Level Shell Database Queries (`db auto ...`):

```text
  db auto pump [hours]         - Fish/Up pump runtimes, duty cycles, and auto-cutoff events
  db auto roof [hours]         - PA amplification active hours, WiFi RSSI trends, CPU thermals
  db auto room [node] [hours]  - AC setpoints, cooling/heating duty cycles, TV activity, board thermals
  db auto latency [node] [hrs] - Response latency (RTT) trend & hourly min/avg/max stats
  db auto history [limit]      - Audit trail of recent automation commands, migrations, and execution status
```

---

## 10. Response Latency (RTT) & Network Performance Tracing

`MeshMon` measures the real-world round-trip time (RTT) between transmitting a command (or targeted `identify`) over the LoRa mesh and receiving the device's corresponding acknowledgment or telemetry reply:

1. **Dispatch Timestamping**: When `sendAutomationCommand()` dispatches a packet, high-resolution monotonic timestamps (`std::chrono::steady_clock`) are stored in `_pendingCommands[nodeId]`.
2. **Reply Ingestion & EMA Filtering**: When a reply packet arrives from `nodeId`, elapsed milliseconds are calculated. `MeshMon` maintains:
   - `lastRttMs`: The most recent round-trip latency.
   - `avgRttMs`: An Exponential Moving Average (EMA, $\alpha=0.25$) smoothing out single-packet RF jitter.
3. **Database & Historical Aggregations**: Every inbound automation state event logs `rtt_ms` in `automation_events`. The `db auto latency [node] [hours]` CLI command queries hourly-bucketed average, minimum, maximum latencies and sample counts.
4. **Home Assistant Real-time Export**: Response latency is exported via MQTT Auto-Discovery as `sensor.meshmon_<node>_rtt` (`device_class: duration`, unit: `ms`), allowing users to monitor LoRa mesh responsiveness and link degradation in Lovelace graphs over time.

---

## 11. Terminal Output & 80-Column Serial Formatting Invariant

To guarantee complete legibility without line wrapping or visual distortion on standard 80-column serial terminals, VT100 displays, and embedded UART consoles:
- All CLI output tables (`robot`, `db auto history`, `db auto latency`, `status`) must adhere strictly to **$\le 74$ columns** (with a hard invariant of $\le 78$ columns).
- Field truncations (e.g. 8-character node short names, 8-character app names, 7-character uptimes) ensure zero column overflowing.

Example 70-column live fleet status table:
```text
=== HomeMesh Smart Automation Fleet (3 Nodes) ===
Node      | Name     | App      | Stat | Uptime  | State / Telemetry    | Seen  
----------+----------+----------+------+---------+----------------------+--------
!2bf941d4 | PumpNode | meshpump | ON   | 14h 22m | Fish:ON Up:OFF       | 12s   
!2c018a12 | RoofNode | meshroof | ON   | 3d 1h   | Amp:ON CPU:38.5C     | 45s   
!2c159e4b | RoomNode | meshroom | ON   | 1d 8h   | AC:ON(24C) TV:OFF    | 2m    
```

Example 70-column automation audit history:
```text
=== Recent Automation Commands & Events (Last 4) ===
Time     | Node      | App      | Dir  | Command          | Param      | Stat 
---------+-----------+----------+------+------------------+------------+------
19:15:02 | !2bf941d4 | meshpump | TX   | pump fish on     |            | OK   
19:15:03 | !2bf941d4 | meshpump | RX   | PUMP_FISH_ON     | ON         | OK   
19:20:11 | !2c159e4b | meshroom | TX   | ac temp 24       | 24C        | OK   
19:20:12 | !2c159e4b | meshroom | RX   | AC_STATE         | ac: temp=24| OK   
```

---

`MeshMon` exports entities to Home Assistant via **MQTT Auto-Discovery** and proxies commands received from Home Assistant to the target mesh devices.

### Device Naming & Identity
- **Parent Device Name**: Uses the human-readable **Long Name** of the node (e.g., `"dev1"`, `"bcnm"`, `"offc"`), falling back to `shortName`, and finally `!<node_id>`.
- **Immutable Hardware Key**: `device.identifiers: ["meshmon_<node_id>"]` links all automation switches, climate entities, numbers, and telemetry sensors to a single physical device in Home Assistant.
- **Dynamic In-Place Updates**: When node info (Long Name) is received via Meshtastic `User` packets, discovery configs are republished in-place, updating the device title without breaking entity IDs.

### A. Exported Controls & State Topics Catalog

#### 1. Gateway-Level Automation Rollup Telemetry (`meshmon_gateway`)
| Home Assistant Domain | Entity Name | State Topic | Unit | Device Class |
| :--- | :--- | :--- | :---: | :---: |
| **Sensor** | `Automation Total Nodes` | `meshmon/gateway/auto_nodes_total` | nodes | — |
| **Sensor** | `Automation Online Nodes` | `meshmon/gateway/auto_nodes_online` | nodes | — |
| **Sensor** | `Automation Fleet Avg RTT`| `meshmon/gateway/auto_avg_rtt` | ms | `duration` |
| **Sensor** | `Automation Events (24h)` | `meshmon/gateway/auto_events_24h` | events| — |

#### 2. Common Robot Lifecycle & Diagnostic Sensors
Published for every identified robot regardless of device type. Every
robot entity also carries `availability_topic: meshmon/<node>/availability`
with `online` / `offline` payloads, so Home Assistant shows entities as
unavailable rather than holding the last reading forever.

| Home Assistant Domain | Entity Name | State Topic | Unit | Device Class / Category |
| :--- | :--- | :--- | :---: | :--- |
| **Sensor** | `Subsystem` | `meshmon/<node>/app` | — | `diagnostic` (icon: `mdi:robot`) |
| **Sensor** | `Uptime` | `meshmon/<node>/uptime` | s | `duration` |
| **Sensor** | `Response Latency` | `meshmon/<node>/rtt` | ms | `duration` |

#### 3. `meshpump` Controls:
| Home Assistant Domain | Entity Name | State Topic | Command Topic | Payload |
| :--- | :--- | :--- | :--- | :--- |
| **Switch** | `Fish Pump` | `meshmon/<node>/pump_fish/state` | `meshmon/cmd/<node>/pump_fish` | `ON` / `OFF` |
| **Switch** | `Upper Pump` | `meshmon/<node>/pump_up/state` | `meshmon/cmd/<node>/pump_up` | `ON` / `OFF` |
| **Number** | `Upper Pump Cutoff` | `meshmon/<node>/pump_up_cutoff/state` | `meshmon/cmd/<node>/pump_up_cutoff` | `5` .. `300` (sec) |
| **Text** | `LED Matrix Message` | `meshmon/<node>/led_message/state` | `meshmon/cmd/<node>/led` | String text |

#### 4. `meshroof` Controls:
| Home Assistant Domain | Entity Name | State Topic | Command Topic | Payload |
| :--- | :--- | :--- | :--- | :--- |
| **Switch** | `RF Power Amplifier` | `meshmon/<node>/amplify/state` | `meshmon/cmd/<node>/amplify` | `ON` / `OFF` |
| **Button** | `Sound Buzzer` | — | `meshmon/cmd/<node>/buzz` | `PRESS` |
| **Text** | `Morse Code Transmitter` | `meshmon/<node>/morse/state` | `meshmon/cmd/<node>/morse` | String text |
| **Sensor** | `ESP32 CPU Temperature` | `meshmon/<node>/cpu_temp` | — | `°C` (device_class: `temperature`) |

#### 5. `meshroom` Controls:
The AC and TV groups are gated on the node's IR provisioning; see
section 12.B.

| Home Assistant Domain | Entity Name | State Topic | Command Topic | Payload |
| :--- | :--- | :--- | :--- | :--- |
| **Climate** | `Room AC` | `meshmon/<node>/ac/mode/state` | `meshmon/cmd/<node>/ac_mode` | `off`, `cool`, `heat`, `dry`, `fan_only`, `auto` |
| **Climate** | (target temp) | `meshmon/<node>/ac/temp/state` | `meshmon/cmd/<node>/ac_temp` | `16` .. `30` (°C) |
| **Climate** | (fan mode) | `meshmon/<node>/ac/fan/state` | `meshmon/cmd/<node>/ac_fan` | `auto`, `1` .. `5` |
| **Climate** | (current temp) | `meshmon/<node>/temperature` | — | °C from Meshtastic env metrics |
| **Switch** | `AC Power` | `meshmon/<node>/ac/power/state` | `meshmon/cmd/<node>/ac_power` | `ON` / `OFF` |
| **Button** | `AC IR Force Blast` | — | `meshmon/cmd/<node>/ac_blast` | `PRESS` |
| **Switch** | `TV Power` | `meshmon/<node>/tv/power/state` | `meshmon/cmd/<node>/tv_power` | `ON` / `OFF` |
| **Switch** | `TV Mute` | `meshmon/<node>/tv/mute/state` | `meshmon/cmd/<node>/tv_mute` | `ON` / `OFF` |
| **Number** | `TV Volume` | `meshmon/<node>/tv/volume/state` | `meshmon/cmd/<node>/tv_vol` | `0` .. `100` |
| **Number** | `TV Channel` | `meshmon/<node>/tv/channel/state` | `meshmon/cmd/<node>/tv_chan` | `1` .. `999` |
| **Button** | `TV Input Next` | — | `meshmon/cmd/<node>/tv_input` | `PRESS` |
| **Sensor** | `RP2040 Board Temperature` | `meshmon/<node>/board_temp` | — | `°C` (device_class: `temperature`) |

### B. IR-Gated Capability Discovery

The `caps=` field in an `identify` reply is a compile-time string
literal in each firmware. It lists what the application was built to
support, not what a given unit is provisioned for, so it cannot gate
entities. `meshroom` in particular always claims `ac_ir,tv_ir` even on
a unit where the administrator has cleared one of the IR protocols.

The provisioning is instead reported per feature in the bare `ac` and
`tv` query replies as `ir=<protocol>`, and becomes `ir=none` after an
`ir del` on the node's serial shell. MeshMon treats that as the gate:

| `ir=` value | Meaning | Entities |
| :--- | :--- | :--- |
| (never probed) | No `ac` / `tv` reply seen yet | Published, so a node is never invisible while waiting for its first reply |
| `<protocol>` | Feature is provisioned | Published |
| `none` | Administrator cleared the protocol | Pruned with an empty retained payload |

A node going offline never prunes anything: liveness is carried by the
availability topic, not by discovery. Only an explicit `ir=none`
removes entities, and re-provisioning the protocol restores them on the
next probe.

Both protocols are persisted in the `automation_nodes` table, so gating
is correct on the first publish after a gateway restart instead of
waiting up to 20 minutes for the probe cycle to come around. Inspect
the current values with `robot status <node>`.

### C. One-Time Entity ID Purge (upgrading to 2.1.11)

Releases before 2.1.11 emitted `object_id`, which Home Assistant
deprecated in 2025.9 and ignores from 2026.4 onwards. Once ignored,
entity IDs fall back to being derived from the device and entity names,
producing IDs such as `sensor.casamagnifica_devboard1_uptime` instead
of the intended `sensor.meshmon_ea8ee0b8_uptime`. The Live Fleet Table
card matches `^sensor\.meshmon_[0-9a-fA-F]{8}_uptime$` and splices the
hex ID out of that match to build its other 17 lookups, so a node whose
uptime entity is name-derived is missing from the table entirely.

Nodes registered while `object_id` was still honored already carry the
intended IDs and need nothing done. Only nodes first registered after
Home Assistant began ignoring it are affected, so a fleet is typically
part correct and part broken.

2.1.11 emits `default_entity_id` instead. Because every entity also
carries a `unique_id`, Home Assistant applies `default_entity_id` only
when an entity is created for the first time; for an entity already in
the registry it is read and discarded. Republishing therefore renames
nothing, and the old entities must be deleted once:

```sh
# 1. Stop meshmon so it does not republish while purging
# 2. Clear the retained discovery configs.  An MQTT '+' must occupy a
#    whole topic level, so 'meshmon_+' is not a legal filter: subscribe
#    wide and match the topic names instead.
mosquitto_sub -h <broker> -u <user> -P <pass> -t 'homeassistant/+/+/config' \
    --retained-only -F '%t' -W 5 | grep '/meshmon_' | while read -r topic; do
    mosquitto_pub -h <broker> -u <user> -P <pass> -t "$topic" -r -n
done
# 3. Confirm the devices disappeared from Home Assistant, then restart meshmon
```

To spare the nodes that are already correct, narrow step 2 to just the
affected hex IDs, for example `grep -E '/meshmon_(ae613b60|ea8ee0b8)_'`.

Restarting MeshMon republishes every config from the persisted
`automation_nodes` registry, and Home Assistant creates the entities
fresh with the intended IDs. List the current IDs from Developer Tools
&rarr; Template to confirm:

```jinja
{{ states.sensor | selectattr('entity_id', 'search', 'uptime')
   | map(attribute='entity_id') | list }}
```

---

## 13. Safety & Invariants

1. **Mate Verification**: Unauthorized Meshtastic messages cannot trigger state changes or dispatch commands.
2. **Targeted Discovery**: Directed `!<nodeid> identify` avoids HomeChat `rollcall` storms from older firmware.
3. **Hardware Cutoff Enforcement**: `meshpump` upper pump commands default to automated cutoff timers to protect pumps from running dry.
4. **Automatic Role Migration**: Changing a node's firmware cleanly removes stale entities from Home Assistant without orphaned controls.
5. **Loss-of-Signal Heartbeats & Proactive Probing**: MeshMon marks devices `OFFLINE` if no Meshtastic message, telemetry, or command reply is received within 15 minutes (900s). To prevent false offline transitions and maintain fresh operational telemetry, MeshMon's 1-minute crontab watchdog proactively probes silent nodes every 10 minutes (600s) using alternating subsystem queries (`AutomationDevice`: `MeshRoomDevice` $\rightarrow$ `ac`/`tv`, `MeshPumpDevice` $\rightarrow$ `pump`, `MeshRoofDevice` $\rightarrow$ `amplify`/`wifi`). Every probe verb must be one the firmware dispatches on at the top level; a sub-verb such as `fish` draws no reply and silently starves the node's state. Every probe response updates round-trip latency (RTT) and continuously advances live `uptimeSec`.
6. **Airtime Duty Cycles**: LoRa channel airtime duty cycles are preserved by throttling rapid consecutive command state toggles.
7. **Strict Robot Gating**: An `AutomationNode` is instantiated and published to Home Assistant or displayed in the `robot` CLI command ONLY if the node explicitly identifies with a supported robot application (`meshpump`, `meshroof`, `meshroom`). Casual chat, legacy firmware text, or raw telemetry from non-robot mates never create phantom robot records.
