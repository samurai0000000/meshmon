# Home Assistant Integration Guide for MeshMon

`meshmon` seamlessly integrates with Home Assistant using **MQTT Auto-Discovery**. Both individual node telemetry (environmental sensors, power, battery), gateway-level mesh network health metrics, and **bidirectional HomeMesh automation controls** (`meshpump`, `meshroof`, `meshroom`) are automatically populated into Home Assistant without requiring manual YAML entity definitions or device configuration in Home Assistant.

---

## 1. Architecture Overview

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
│  │   MQTT Control Proxy   │  │ SQLite Analytics │  │ In-Memory Robot    │  │
│  │ (Command Translation)  │  │ (Audit Log DB)   │  │ Node State Machine │  │
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
│   │  - Fish / Up Pumps  │  │  - RF PA Amplifier│  │  - AC IR Control   │   │
│   │  - Auto-cutoff timer│  │  - WiFi / Net IP  │  │  - TV IR Control   │   │
│   │  - MAX7219 LED Matrix│ │  - CPU Temp Sensor│  │  - Board Temp / Env│   │
│   │  - Env / Reservoir  │  │  - Buzzer / Morse │  │  - Buzzer / Morse  │   │
│   └─────────────────────┘  └───────────────────┘  └────────────────────┘   │
└────────────────────────────────────────────────────────────────────────────┘
```

When `meshmon` runs, it automatically handles all discovery and state synchronization:
1. **MQTT Discovery Configs (`homeassistant/<domain>/.../config`)**: Broadcast with `retain=true` to automatically register devices, sensors, and control entities in Home Assistant with zero YAML configuration.
2. **State Updates (`meshmon/<node>/.../state`)**: Real-time measurement values, switch states, and climate attributes.
3. **Command Proxying (`meshmon/cmd/<node>/...`)**: Listens for Home Assistant UI interactions or automations and translates them into LoRa Meshtastic messages.
4. **Dynamic Role Migration & Pruning**: If a radio module is moved to another hardware board (e.g. `meshroom` $\rightarrow$ `meshroof`), `meshmon` automatically revokes the obsolete discovery topics by publishing empty retained payloads (`""`) so that stale entities are cleanly removed from Home Assistant without orphaned controls.

---

## 2. Step-by-Step Setup Guide

### Step 1: Install & Configure Mosquitto Broker in Home Assistant
1. In Home Assistant, navigate to **Settings $\rightarrow$ Add-ons $\rightarrow$ Add-on Store**.
2. Search for **Mosquitto broker** and click **Install**.
3. Once installed, start the add-on and toggle **Start on boot** and **Watchdog**.
4. (Optional) Create a dedicated MQTT user in Home Assistant under **Settings $\rightarrow$ People $\rightarrow$ Users** (e.g. username `meshmon`, password `<secure-password>`).

### Step 2: Configure MQTT Integration in Home Assistant
1. Go to **Settings $\rightarrow$ Devices & Services**.
2. If discovered automatically, click **Configure** on the MQTT integration. Otherwise, click **Add Integration**, search for **MQTT**, and enter your broker details (`core-mosquitto` or Home Assistant IP address, port `1883`, username, and password).
3. Ensure **Enable discovery** is checked (enabled by default).

### Step 3: Configure `meshmon` to Connect to MQTT
Edit your `~/.meshmon` configuration file to enable the `mqtt` and `database` blocks:

```text
mqtt = {
    server = "192.168.1.100";  // Home Assistant or Mosquitto broker IP
    port = 1883;
    user = "meshmon";
    password = "your-mqtt-password";
    topic = "meshmon";
    tls = false;
};

database = {
    enabled = true;
    path = "~/.meshmon.db";
    retention_days = 90;
};
```

Restart `meshmon`. Within seconds, Home Assistant will automatically discover the gateway, network diagnostics, telemetry sensors, and all HomeMesh automation controls!

---

## 3. Exported Entities & Topics Catalog

### A. MeshMon Gateway Health & Physics (`meshmon_gateway`)

| Shell Command | Sensor Entity Name | Unique ID | Registered Entity ID | State Topic | Unit | Device Class |
| :--- | :--- | :--- | :--- | :--- | :---: | :---: |
| **db summary** | **Gateway Total Packets** | `meshmon_gateway_packets` | `sensor.meshmon_gateway_gateway_total_packets` | `meshmon/gateway/total_packets` | pkts | — |
| **db summary** | **Gateway Total Nodes** | `meshmon_gateway_total_nodes` | `sensor.meshmon_gateway_gateway_total_nodes` | `meshmon/gateway/total_nodes` | nodes | — |
| **db summary** | **Gateway Total Airtime Data** | `meshmon_gateway_total_bytes` | `sensor.meshmon_gateway_gateway_total_airtime_data` | `meshmon/gateway/total_bytes_mb` | MB | `data_size` |
| **db summary** | **Gateway Direct Ratio** | `meshmon_gateway_direct_ratio` | `sensor.meshmon_gateway_gateway_direct_ratio` | `meshmon/gateway/direct_ratio_pct` | % | — |
| **db summary** | **Gateway Broadcast Ratio** | `meshmon_gateway_broadcast_ratio` | `sensor.meshmon_gateway_gateway_broadcast_ratio` | `meshmon/gateway/broadcast_ratio_pct` | % | — |
| **db summary** | **Gateway Average Hops** | `meshmon_gateway_avg_hops` | `sensor.meshmon_gateway_gateway_average_hops` | `meshmon/gateway/avg_hops` | hops | — |
| **db summary** | **Gateway Total Messages** | `meshmon_gateway_total_messages` | `sensor.meshmon_gateway_gateway_total_messages` | `meshmon/gateway/total_messages` | msgs | — |
| **db summary** | **Gateway Active Nodes (24h)** | `meshmon_gateway_active_nodes` | `sensor.meshmon_gateway_gateway_active_nodes_24h` | `meshmon/gateway/active_nodes_24h` | nodes | — |
| **db summary** | **Gateway DB Size** | `meshmon_gateway_db_size` | `sensor.meshmon_gateway_gateway_db_size` | `meshmon/gateway/db_size_mb` | MB | `data_size` |
| **db neighbors** | **Gateway Average SNR (1h)** | `meshmon_gateway_avg_snr` | `sensor.meshmon_gateway_gateway_average_snr_1h` | `meshmon/gateway/avg_snr_1h` | dB | `signal_strength` |
| **db neighbors** | **Gateway Direct Neighbors** | `meshmon_gateway_direct_neighbors` | `sensor.meshmon_gateway_gateway_direct_neighbors` | `meshmon/gateway/direct_neighbors` | nodes | — |
| **db neighbors** | **Gateway Best Neighbor SNR** | `meshmon_gateway_best_neighbor_snr` | `sensor.meshmon_gateway_gateway_best_neighbor_snr` | `meshmon/gateway/best_neighbor_snr` | dB | `signal_strength` |
| **db toptalkers**| **Gateway Top Talker** | `meshmon_gateway_top_talker` | `sensor.meshmon_gateway_gateway_top_talker` | `meshmon/gateway/top_talker` | — | — |
| **db toptalkers**| **Gateway Top Talker Packets**| `meshmon_gateway_top_talker_packets`| `sensor.meshmon_gateway_gateway_top_talker_packets` | `meshmon/gateway/top_talker_packets`| pkts | — |
| **db storm** | **Gateway Duplicate Packets (24h)**| `meshmon_gateway_duplicate_packets` | `sensor.meshmon_gateway_gateway_duplicate_packets` | `meshmon/gateway/duplicate_packets` | pkts | — |
| **db storm** | **Gateway Max Echo Multiplier**| `meshmon_gateway_max_echo_mult` | `sensor.meshmon_gateway_gateway_max_echo_multiplier` | `meshmon/gateway/max_echo_mult` | x | — |
| **db spof** | **Gateway Critical Relay** | `meshmon_gateway_critical_relay` | `sensor.meshmon_gateway_gateway_critical_relay` | `meshmon/gateway/critical_relay` | — | — |
| **db drift** | **Gateway Max Clock Drift** | `meshmon_gateway_max_clock_drift` | `sensor.meshmon_gateway_gateway_max_clock_drift` | `meshmon/gateway/max_clock_drift_sec` | s | `duration` |
| **db health** | **Gateway Avg Channel Util** | `meshmon_gateway_avg_chan_util` | `sensor.meshmon_gateway_gateway_avg_channel_util` | `meshmon/gateway/avg_channel_util` | % | — |
| **db health** | **Gateway Duplicate Ratio** | `meshmon_gateway_duplicate_ratio` | `sensor.meshmon_gateway_gateway_duplicate_ratio` | `meshmon/gateway/duplicate_ratio_pct` | % | — |
| **robot** | **Automation Total Nodes** | `meshmon_gateway_auto_nodes_total` | `sensor.meshmon_gateway_automation_total_nodes` | `meshmon/gateway/auto_nodes_total` | nodes | — |
| **robot** | **Automation Online Nodes** | `meshmon_gateway_auto_nodes_online` | `sensor.meshmon_gateway_automation_online_nodes` | `meshmon/gateway/auto_nodes_online` | nodes | — |
| **robot** | **Automation Fleet Avg RTT** | `meshmon_gateway_auto_avg_rtt` | `sensor.meshmon_gateway_automation_fleet_avg_rtt` | `meshmon/gateway/auto_avg_rtt` | ms | `duration` |
| **db auto** | **Automation Events (24h)** | `meshmon_gateway_auto_events_24h` | `sensor.meshmon_gateway_automation_events_24h` | `meshmon/gateway/auto_events_24h` | events | — |

---

### B. Per-Node Environmental & Device Sensors (`meshmon_<node_id>_*`)

| Sensor Type | State Topic | Unit | Device Class |
| :--- | :--- | :---: | :---: |
| **Temperature** | `meshmon/<node_id>/temperature` | °C | `temperature` |
| **Relative Humidity** | `meshmon/<node_id>/humidity` | % | `humidity` |
| **Barometric Pressure** | `meshmon/<node_id>/pressure` | hPa | `pressure` |
| **Battery Level** | `meshmon/<node_id>/battery` | % | `battery` |
| **Battery Voltage** | `meshmon/<node_id>/voltage` | V | `voltage` |
| **Channel Utilization** | `meshmon/<node_id>/channel_utilization` | % | — |
| **Air Util (Tx)** | `meshmon/<node_id>/air_util_tx` | % | — |
| **Channel 1 Voltage / Current** | `meshmon/<node_id>/ch1_voltage`, `_current` | V / mA | `voltage` / `current` |
| **Channel 2 Voltage / Current** | `meshmon/<node_id>/ch2_voltage`, `_current` | V / mA | `voltage` / `current` |
| **Channel 3 Voltage / Current** | `meshmon/<node_id>/ch3_voltage`, `_current` | V / mA | `voltage` / `current` |
| **Node Uptime** | `meshmon/<node_id>/uptime` | s | `duration` |
| **Response Latency (RTT)** | `meshmon/<node_id>/rtt` | ms | `duration` |

---

### C. HomeMesh Bidirectional Automation Controls

#### 1. `meshpump` (Fish Tank & Upper Water Pump Relay Controller)
| Domain | Entity Name | State Topic | Command Topic | Payload / Values |
| :--- | :--- | :--- | :--- | :--- |
| **Switch** | `Fish Tank Water Pump` | `meshmon/<node>/pump/fish/state` | `meshmon/cmd/<node>/pump_fish` | `ON` / `OFF` |
| **Switch** | `Upper Water Pump Relay` | `meshmon/<node>/pump/up/state` | `meshmon/cmd/<node>/pump_up` | `ON` / `OFF` |
| **Number** | `Upper Pump Cutoff Duration`| `meshmon/<node>/pump/up/cutoff` | `meshmon/cmd/<node>/pump_up_cutoff` | `1` .. `3600` (s) |
| **Sensor** | `Plant Soil Moisture` | `meshmon/<node>/soil_moisture` | — | `%` |
| **Binary Sensor** | `Reservoir Empty Alarm` | `meshmon/<node>/reservoir/empty` | — | `ON` / `OFF` (problem) |
| **Text** | `LED Display Banner` | — | `meshmon/cmd/<node>/led` | String text |
| **Sensor** | `Node Uptime` | `meshmon/<node>/uptime` | — | `s` (device_class: `duration`) |
| **Sensor** | `Response Latency` | `meshmon/<node>/rtt` | — | `ms` (device_class: `duration`) |

#### 2. `meshroof` (ESP32 High-Power Station)
| Domain | Entity Name | State Topic | Command Topic | Payload / Values |
| :--- | :--- | :--- | :--- | :--- |
| **Switch** | `RF Power Amplifier` | `meshmon/<node>/amplify/state` | `meshmon/cmd/<node>/amplify` | `ON` / `OFF` |
| **Button** | `Sound Buzzer` | — | `meshmon/cmd/<node>/buzz` | `PRESS` |
| **Text** | `Send Morse Code` | — | `meshmon/cmd/<node>/morse` | String text |
| **Sensor** | `WiFi RSSI` | `meshmon/<node>/wifi_rssi` | — | `dBm` |
| **Sensor** | `WiFi IP Address` | `meshmon/<node>/ip_addr` | — | IP string |
| **Sensor** | `ESP32 CPU Temp` | `meshmon/<node>/cpu_temp` | — | `°C` |
| **Sensor** | `Reset Count` | `meshmon/<node>/reset_count`| — | count |
| **Sensor** | `Node Uptime` | `meshmon/<node>/uptime` | — | `s` (device_class: `duration`) |
| **Sensor** | `Response Latency` | `meshmon/<node>/rtt` | — | `ms` (device_class: `duration`) |

#### 3. `meshroom` (RP2040 Room IR Climate & TV Hub)
| Domain | Entity Name | State Topic | Command Topic | Payload / Values |
| :--- | :--- | :--- | :--- | :--- |
| **Climate** | `Room AC Climate` | `meshmon/<node>/ac/climate_state` | `meshmon/cmd/<node>/ac_climate` | JSON `{mode, temp, fan}` |
| **Switch** | `AC Power` | `meshmon/<node>/ac_power/state` | `meshmon/cmd/<node>/ac_power` | `ON` / `OFF` |
| **Number** | `AC Target Temp` | `meshmon/<node>/ac_temp/state` | `meshmon/cmd/<node>/ac_temp` | `16` .. `30` (°C) |
| **Select** | `AC Mode` | `meshmon/<node>/ac_mode/state` | `meshmon/cmd/<node>/ac_mode` | `cool`, `heat`, `dry`, `fan`, `auto` |
| **Select** | `AC Fan Speed` | `meshmon/<node>/ac_fan/state` | `meshmon/cmd/<node>/ac_fan` | `auto`, `quiet`, `low`, `med`, `high`, `max` |
| **Button** | `AC Force IR Blast` | — | `meshmon/cmd/<node>/ac_blast` | `PRESS` |
| **Media Player** / **Switch** | `TV Power` | `meshmon/<node>/tv_power/state` | `meshmon/cmd/<node>/tv_power` | `ON` / `OFF` |
| **Number** | `TV Volume` | `meshmon/<node>/tv_vol/state` | `meshmon/cmd/<node>/tv_vol` | `0` .. `100` |
| **Number** | `TV Channel` | `meshmon/<node>/tv_chan/state` | `meshmon/cmd/<node>/tv_chan` | `1` .. `999` |
| **Switch** | `TV Mute` | `meshmon/<node>/tv_mute/state` | `meshmon/cmd/<node>/tv_mute` | `ON` / `OFF` |
| **Button** | `TV Input Source` | — | `meshmon/cmd/<node>/tv_input` | `PRESS` |
| **Sensor** | `RP2040 Board Temp` | `meshmon/<node>/board_temp` | — | `°C` |
| **Sensor** | `Node Uptime` | `meshmon/<node>/uptime` | — | `s` (device_class: `duration`) |
| **Sensor** | `Response Latency` | `meshmon/<node>/rtt` | — | `ms` (device_class: `duration`) |

For deep-dive protocol specifications and message formats, see [HomeMeshAutomation.md](HomeMeshAutomation.md).

---

## 4. Live Lovelace Dashboard Preview

<p align="center">
  <img src="homeassistant-dashboard.png" alt="Home Assistant MeshMon Dashboard" width="90%"/>
</p>

---

## 5. Sample Lovelace Dashboard Cards (YAML)

Add these custom cards to your Home Assistant dashboard for real-time mesh monitoring, environmental telemetry, and endpoint automation.

### Card 1: Gateway Overview & Mesh Health Gauges
```yaml
type: vertical-stack
cards:
  - type: heading
    heading: Meshtastic Network Health
  - type: grid
    columns: 3
    square: false
    cards:
      - type: gauge
        entity: sensor.meshmon_gateway_gateway_average_snr_1h
        min: -15
        max: 15
        name: Avg SNR (1h)
        needle: true
        severity:
          green: 3
          yellow: -5
          red: -15
      - type: gauge
        entity: sensor.meshmon_gateway_gateway_active_nodes_24h
        min: 0
        max: 50
        name: Active Nodes
      - type: gauge
        entity: sensor.meshmon_gateway_gateway_direct_neighbors
        min: 0
        max: 20
        name: 0-Hop Neighbors
  - type: entities
    title: Gateway Lifetime Stats
    entities:
      - entity: sensor.meshmon_gateway_gateway_total_packets
        name: Lifetime Packets Logged
      - entity: sensor.meshmon_gateway_gateway_total_nodes
        name: Total Unique Nodes Seen
      - entity: sensor.meshmon_gateway_gateway_total_airtime_data
        name: Total Airtime Data
      - entity: sensor.meshmon_gateway_gateway_direct_ratio
        name: Direct RF Traffic Ratio (0-Hop)
      - entity: sensor.meshmon_gateway_gateway_duplicate_packets
        name: Duplicate Packet Storms (24h)
      - entity: sensor.meshmon_gateway_gateway_total_messages
        name: Total Text Messages
      - entity: sensor.meshmon_gateway_gateway_db_size
        name: SQLite Database Size
```

---

### Card 2: Mesh Diagnostics & Routing (24h)
```yaml
type: entities
title: Mesh Diagnostics & Routing (24h)
entities:
  - entity: sensor.meshmon_gateway_gateway_top_talker
    name: "#1 Top Talker Node"
  - entity: sensor.meshmon_gateway_gateway_top_talker_packets
    name: Top Talker Packet Count
  - entity: sensor.meshmon_gateway_gateway_best_neighbor_snr
    name: Best Direct Neighbor SNR
  - entity: sensor.meshmon_gateway_gateway_critical_relay
    name: Critical Relay (SPOF)
  - entity: sensor.meshmon_gateway_gateway_average_hops
    name: Average Hop Distance
  - entity: sensor.meshmon_gateway_gateway_max_echo_multiplier
    name: Max Storm Multiplier
  - entity: sensor.meshmon_gateway_gateway_max_clock_drift
    name: Max Clock Drift Skew
  - entity: sensor.meshmon_gateway_gateway_avg_channel_util
    name: Average Channel Utilization
  - entity: sensor.meshmon_gateway_gateway_duplicate_ratio
    name: Duplicate Echo Percentage
```

---

### Card 3: Environmental & Power Telemetry Grid
*(Replace example entity IDs with your discovered station names or node hex IDs)*
```yaml
type: vertical-stack
cards:
  - type: heading
    heading: Remote Station Telemetry
  - type: grid
    columns: 2
    square: false
    cards:
      - type: sensor
        entity: sensor.station_alpha_temperature
        name: Station Temperature
        graph: line
      - type: sensor
        entity: sensor.station_alpha_humidity
        name: Station Humidity
        graph: line
      - type: sensor
        entity: sensor.repeater_roof_battery
        name: Repeater Battery %
        graph: line
      - type: sensor
        entity: sensor.repeater_roof_voltage
        name: Repeater Voltage
        graph: line
```

---

### Card 4: RF Link Quality & Signal History
```yaml
type: history-graph
title: RF Propagation History
hours_to_show: 48
entities:
  - entity: sensor.meshmon_gateway_gateway_average_snr_1h
    name: Gateway Average SNR (dB)
```

---

### Card 5: HomeMesh Smart Robot Fleet Live Summary & Health Gauges
*This card mirrors the CLI `robot` command with live template rendering, dynamic node discovery across your mesh, fleet health rollup metrics, and RTT history tracking.*
```yaml
type: vertical-stack
cards:
  - type: heading
    heading: 🤖 HomeMesh Smart Robot Fleet
  # Overall Fleet Automation Rollup Gauges & Metrics (2x2 Grid)
  - type: grid
    columns: 2
    square: false
    cards:
      - type: sensor
        entity: sensor.meshmon_gateway_automation_total_nodes
        name: Total Robots
      - type: sensor
        entity: sensor.meshmon_gateway_automation_online_nodes
        name: Online Robots
      - type: gauge
        entity: sensor.meshmon_gateway_automation_fleet_avg_rtt
        name: Fleet Avg RTT
        min: 0
        max: 1000
        needle: true
        severity:
          green: 150
          yellow: 400
          red: 800
      - type: sensor
        entity: sensor.meshmon_gateway_automation_events_24h
        name: 24h Commands & Events

  # HomeChat Response Latency (RTT Performance Graph)
  - type: history-graph
    title: HomeChat Response Latency (RTT Trend)
    hours_to_show: 24
    entities:
      - entity: sensor.meshmon_gateway_automation_fleet_avg_rtt
        name: Fleet Average Latency (ms)

  # Live Fleet Status Table (Mirrors shell 'robot' output dynamically)
  - type: markdown
    title: Live Fleet Operational Table
    content: >
      | Node | Device Name | Subsystem | Status | Uptime | Key State & Telemetry | Latency (RTT) |
      | :--- | :--- | :--- | :---: | :---: | :--- | :---: |
      {% set uptime_sensors = states.sensor | selectattr('entity_id', 'match', '^sensor\.meshmon_[0-9a-fA-F]{8}_uptime$') | list %}
      {% if uptime_sensors | length == 0 %}
      | - | *No active HomeMesh automation nodes discovered yet* | - | ⚪ Searching | - | Waiting for node telemetry | - |
      {% else %}
      {% for s in uptime_sensors %}
        {% set node_id = s.entity_id.split('_')[1] %}
        {% set dev_name = state_attr(s.entity_id, 'friendly_name') | replace(' Uptime', '') or ('!' ~ node_id) %}
        {% set up = s.state | int(0) %}
        {% set rtt = states('sensor.meshmon_' ~ node_id ~ '_rtt') | int(0) %}
        {% set hours = (up // 3600) %}
        {% set mins = ((up % 3600) // 60) %}
        {% set is_online = (s.state != 'unavailable' and s.state != 'unknown' and up > 0) %}
        {% set app_type = 'generic' %}
        {% set details = [] %}
        {% if states('switch.meshmon_' ~ node_id ~ '_pump_fish') != 'unknown' %}
          {% set app_type = 'meshpump' %}
          {% set details = details + ['Fish:' ~ states('switch.meshmon_' ~ node_id ~ '_pump_fish') | upper] %}
          {% set details = details + ['Up:' ~ states('switch.meshmon_' ~ node_id ~ '_pump_up') | upper] %}
          {% set details = details + ['Soil:' ~ states('sensor.meshmon_' ~ node_id ~ '_soil_moisture') ~ '%'] %}
        {% elif states('switch.meshmon_' ~ node_id ~ '_amplify') != 'unknown' %}
          {% set app_type = 'meshroof' %}
          {% set details = details + ['PA:' ~ states('switch.meshmon_' ~ node_id ~ '_amplify') | upper] %}
          {% set details = details + ['CPU:' ~ states('sensor.meshmon_' ~ node_id ~ '_cpu_temp') ~ '°C'] %}
        {% elif states('switch.meshmon_' ~ node_id ~ '_ac_power') != 'unknown' %}
          {% set app_type = 'meshroom' %}
          {% set details = details + ['AC:' ~ states('switch.meshmon_' ~ node_id ~ '_ac_power') | upper] %}
          {% set details = details + ['TV:' ~ states('switch.meshmon_' ~ node_id ~ '_tv_power') | upper] %}
          {% set details = details + ['Board:' ~ states('sensor.meshmon_' ~ node_id ~ '_board_temp') ~ '°C'] %}
        {% endif %}
        | `!{{ node_id }}` | **{{ dev_name }}** | `{{ app_type }}` | {{ '🟢 Online' if is_online else '🔴 Offline' }} | {{ hours }}h {{ mins }}m | {{ details | join(' ') }} | `{{ rtt }} ms` |
      {% endfor %}
      {% endif %}
```

---

### Card 6: HomeMesh Automation Controls & Subsystem Hub
```yaml
type: vertical-stack
cards:
  - type: heading
    heading: ⚡ HomeMesh Subsystem Controls & Safeguards
  # Quick Endpoint Toggles
  - type: grid
    columns: 3
    square: false
    cards:
      - type: tile
        entity: switch.meshmon_2bf941d4_pump_fish
        name: Fish Tank Pump
        icon: mdi:fishbowl
        color: blue
      - type: tile
        entity: switch.meshmon_2c018a12_amplify
        name: Rooftop RF PA
        icon: mdi:signal-cellular-outline
        color: amber
      - type: tile
        entity: switch.meshmon_2c159e4b_tv_power
        name: Living Room TV
        icon: mdi:television
        color: purple
  # Climate Control Card
  - type: thermostat
    entity: climate.meshmon_2c159e4b_ac
    name: Room Air Conditioner
  # Irrigation & Safeguards List
  - type: entities
    title: Garden Pump & Irrigation Safeguards
    entities:
      - entity: switch.meshmon_2bf941d4_pump_up
        name: Upper Water Pump
      - entity: number.meshmon_2bf941d4_pump_up_cutoff
        name: Upper Pump Auto-Cutoff (seconds)
      - entity: binary_sensor.meshmon_2bf941d4_reservoir_empty
        name: Water Reservoir Warning
      - entity: sensor.meshmon_2bf941d4_soil_moisture
        name: Soil Moisture
```
