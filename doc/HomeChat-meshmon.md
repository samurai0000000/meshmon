# MeshMon HomeChat Protocol Extensions

`meshmon` is a central monitoring and gateway node that extends `HomeChat` with natural language power and telemetry queries, master clock synchronization broadcasts, and integration with AI chatbots (such as Google Gemini).

---

## 1. Natural Language & Sensor Queries

In addition to exact keywords, `meshmon` supports conversational inquiries from authorized nodes:

| Query Type | Example Inputs | Example Reply |
| :--- | :--- | :--- |
| **Power & Battery** | `power?`, `battery`, `how is battery?` | `power: vbat=4.12V ibat=150mA pwr=solar` |
| **Solar / Grid** | `solar?`, `grid?` | `solar: vin=18.4V iin=850mA pin=15.6W` |
| **Environment** | `temperature?`, `humidity?`, `env` | `env: temp=25.2 hum=48.6 press=1012.4` |
| **Node Lookup** | `where is <node>?`, `who is <node>?` | `node: id=!2bf941d4 name=Roof snr=+8.2dB hops=0` |

---

## 2. Master Time Synchronization

`meshmon` acts as a master time reference across the mesh:
- Periodically broadcasts wall-clock epoch time and timezone over authorized channels:
  ```text
  time: <epoch_seconds> <timezone_name>
  ```
  Example: `time: 1740000000 Asia/Taipei`
- Mate nodes and devices receiving this broadcast synchronize their local system clocks automatically.

---

## 3. AI Chatbot Gateway (`GeminiChat`)

When configured with an AI backend (e.g. Gemini), unhandled conversational questions addressed to `meshmon` from authorized users are routed to the AI assistant model:
- Natural responses are truncated and formatted to fit Meshtastic packet MTU limits.
- Supports conversational assistance, summary of network conditions, and general inquiries over LoRa text messages.

---

## 4. Mesh Database & RF Network Analytics Queries

When SQLite packet logging is active, `meshmon` responds to on-air text queries with live RF metrics and network health summaries:

| Query Command / Synonyms | Description | Example Reply |
| :--- | :--- | :--- |
| `traffic?`, `traffic`, `pkts?` | Packet counts & hourly breakdown | `traffic: 1250 pkts (1h: 120, 24h: 980)` |
| `talkers?`, `toptalkers` | Top transmitting nodes (last 24h) | `top: !2bf941d4 (42 pkts), !a1b2c3d4 (18 pkts)` |
| `neighbors?`, `direct` | Direct (0-hop) neighbors with RSSI/SNR | `neighbors(2): !2bf941d4 (rssi=-78 snr=6.2) !4a5b6c7d (rssi=-95 snr=-2.1)` |
| `hops?`, `hopdist` | Distribution of hops traversed | `hops: 0hop=45% 1hop=35% 2hop=15% 3+hop=5%` |
| `health?`, `chanhealth` | Channel airtime & SNR health | `channel: avg_snr=+4.8dB min=-11.2 max=+10.5 total_pkts=1250` |
| `storm?`, `echoes?` | Mesh duplicate echo / storm detection | `storm: 21% dups (182 dups / 850 orig) max_echoes=5/pkt` |
| `asymmetry?`, `noise?` | Link asymmetry & local noise floor elevation | `asymmetry: 3 nodes flagged (elevated local noise / TX power imbalance)` |
| `spof?`, `repeaters?` | Critical relay repeater discovery | `spof: !2bf941d4 relays 64% of 2+ hop mesh traffic` |
| `drift?`, `clocks?` | Remote node clock drift analysis | `drift: !a1b2c3d4 off by +42s, !c4d5e6f7 off by -120s` |

