# Home Assistant Integration Guide for MeshMon

`meshmon` seamlessly integrates with Home Assistant using **MQTT Auto-Discovery**. Both individual node telemetry (environmental sensors, power, battery) and gateway-level mesh network health metrics (active node counts, average SNR, direct neighbors, duplicate packet storm counts, and database storage) are automatically populated into Home Assistant without requiring manual YAML entity definitions.

---

## 1. Architecture Overview

```
                      ┌──────────────────────┐
                      │  Meshtastic Radios   │
                      └──────────┬───────────┘
                                 │ LoRa RF Packets
                                 ▼
                      ┌──────────────────────┐
                      │       MeshMon        │
                      │  (SQLite DB Engine)  │
                      └──────────┬───────────┘
                                 │ MQTT Auto-Discovery + Telemetry State
                                 ▼
                      ┌──────────────────────┐
                      │   Mosquitto Broker   │
                      └──────────┬───────────┘
                                 │
                                 ▼
                      ┌──────────────────────┐
                      │    Home Assistant    │
                      │  (Lovelace Dashboard)│
                      └──────────────────────┘
```

When `meshmon` receives telemetry from authorized nodes or computes aggregate mesh health from its SQLite database, it publishes:
1. **MQTT Discovery Configs (`homeassistant/sensor/.../config`)**: Broadcast with `retain=true` to automatically register devices and sensor entities.
2. **State Updates (`meshmon/...`)**: Real-time measurement values and attributes.

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

### Step 3: Configure `meshmon` to Publish to MQTT
Edit your `~/.meshmon` configuration file to enable the `mqtt` block:

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

Restart `meshmon`. Within seconds, Home Assistant will discover the gateway and all authorized mesh nodes!

---

## 3. Exported Entities & Topics Catalog

### A. MeshMon Gateway Health & Physics (`meshmon_gateway`)

| Sensor Entity Name | Unique ID | Registered Entity ID | State Topic | Unit | Device Class |
| :--- | :--- | :--- | :--- | :---: | :---: |
| **Gateway Total Packets** | `meshmon_gateway_packets` | `sensor.meshmon_gateway_gateway_total_packets` | `meshmon/gateway/total_packets` | pkts | — |
| **Gateway Active Nodes (24h)** | `meshmon_gateway_active_nodes` | `sensor.meshmon_gateway_gateway_active_nodes_24h` | `meshmon/gateway/active_nodes_24h` | nodes | — |
| **Gateway Average SNR (1h)** | `meshmon_gateway_avg_snr` | `sensor.meshmon_gateway_gateway_average_snr_1h` | `meshmon/gateway/avg_snr_1h` | dB | signal_strength |
| **Gateway Direct Neighbors** | `meshmon_gateway_direct_neighbors` | `sensor.meshmon_gateway_gateway_direct_neighbors` | `meshmon/gateway/direct_neighbors` | nodes | — |
| **Gateway Duplicate Packets** | `meshmon_gateway_duplicate_packets` | `sensor.meshmon_gateway_gateway_duplicate_packets` | `meshmon/gateway/duplicate_packets` | pkts | — |
| **Gateway DB Size** | `meshmon_gateway_db_size` | `sensor.meshmon_gateway_gateway_db_size` | `meshmon/gateway/db_size_mb` | MB | data_size |

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

---

## 4. Sample Lovelace Dashboard Cards (YAML)

Add these custom cards to your Home Assistant dashboard for real-time mesh monitoring.

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
      - entity: sensor.meshmon_gateway_gateway_duplicate_packets
        name: Duplicate Packet Storms (24h)
      - entity: sensor.meshmon_gateway_gateway_db_size
        name: SQLite Database Size
```

---

### Card 2: Environmental & Power Telemetry Grid
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

### Card 3: RF Link Quality & Signal History
```yaml
type: history-graph
title: RF Propagation History
hours_to_show: 48
entities:
  - entity: sensor.meshmon_gateway_gateway_average_snr_1h
    name: Gateway Average SNR (dB)
```
