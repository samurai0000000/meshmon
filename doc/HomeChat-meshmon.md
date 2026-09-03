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
