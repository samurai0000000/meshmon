# Implementation Plan: MeshMon Documentation Update & Standardization

Standardize and update documentation for the `meshmon` repository (v2.1.16) to align with all recent features, architectural enhancements, and project guidelines.

## User Review Required

> [!IMPORTANT]
> - All documentation strictly follows personal project guidelines (Copyright Charles Chiou, `selfso.com`).
> - Zero mentions of commercial employer or third-party corporate entities.
> - Zero hardcoded home paths (`/home/samurai/...`), private hostnames (`builder`, `fox`, `rhino`), local IP addresses (`192.168.8.*`), or GNU `screen` commands.
> - Build instructions strictly standardize on top-level `Makefile` invocations (`make -j$(nproc)` / `make`), never invoking `cmake` directly.
> - No git commits will be executed until explicitly commanded by the user.

## Proposed Changes

### Documentation Updates for `meshmon`

#### [MODIFY] [README.md](file:///home/samurai/work/meshmon/README.md)
- Update project description and version references to v2.1.16.
- Replace direct `cmake` build instructions with top-level `Makefile` workflow:
  - `make -j$(nproc)` (compilation)
  - `make clean`
  - `make distclean`
- Document newly integrated features:
  - **Embedded Web Server & Command Center**: Dark glassmorphic UI on port 16880 with responsive two-row navigation.
  - **Real-Time SSE Packet Stream**: Streaming LoRa packets over `/events` directly from radio ingest.
  - **Remote Behavior Analytics**: Polar radar canvas, SNR vs. distance scatter plot, mobility detection (stationary vs mobile), and RF link asymmetry monitoring.
  - **Mesh Topology Analytics**: Centroid trilateration for GPS-less nodes, hearing neighbor graph, and multi-hop traceroute sequence log.
  - **Interactive SQL Console**: Read-only SQLite query engine with security guards and diagnostic presets.
  - **Dynamic Aimon MCP Gateway Client**: Auto-registration of `meshmon_*` tools to the central AI gateway.
- Sanitize example MQTT broker configuration (`<mqtt-broker-ip>` / `mqtt.local`).
- Update documentation index table to include root [Design.md](file:///home/samurai/work/meshmon/Design.md).
- Ensure Copyright block strictly adheres to `Copyright (C) 2026, Charles Chiou. All rights reserved.`.

#### [NEW] [Design.md](file:///home/samurai/work/meshmon/Design.md)
- Create comprehensive root architectural specification:
  - **System Overview & Hardware Topology**: Meshtastic LoRa radios via USB serial (`/dev/ttyACM*`, `/dev/ttyUSB*`), TCP, BLE.
  - **System Architecture Diagram**: LoRa radio ingestion pipeline, `MeshMonDb` SQLite WAL worker, `SpatialAnalytics` engine, embedded `WebServer`, `AimonGatewayClient`, and `MeshMonShell`.
  - **Dual-Time Invariant Capture Model**: Contrast between host NTP ground truth (`meshmon_time`) and radio clock (`rx_time`).
  - **Spatial & Remote Behavior Engine**: Haversine distance, polar bearing, centroid trilateration for non-GPS nodes, and mobility tracking.
  - **Web Server Architecture**: REST API endpoints, SSE packet streaming (`/events`), embedded web assets (`WebAssets.hxx`), and query authorization.
  - **Aimon MCP Gateway Protocol**: Dynamic JSON-RPC 2.0 over TCP socket on port 3885.
  - **Configuration & Storage**: Standardized XDG paths (`~/.config/meshmon/meshmon.cfg`, `~/.local/state/meshmon/`).

#### [MODIFY] [doc/Design.md](file:///home/samurai/work/meshmon/doc/Design.md)
- Ensure synchronization with root [Design.md](file:///home/samurai/work/meshmon/Design.md) or update to cross-reference root design.
- Verify zero privacy leaks (no private hostnames, IPs, or absolute home paths).

#### [REVIEW] [doc/PacketLoggingDB.md](file:///home/samurai/work/meshmon/doc/PacketLoggingDB.md), [doc/HomeAssistantIntegration.md](file:///home/samurai/work/meshmon/doc/HomeAssistantIntegration.md), [doc/HomeMeshAutomation.md](file:///home/samurai/work/meshmon/doc/HomeMeshAutomation.md), [doc/HomeChat-meshmon.md](file:///home/samurai/work/meshmon/doc/HomeChat-meshmon.md)
- Scan and sanitize any stale path or IP references.

---

## Verification Plan

### Automated & Manual Checks
- Verify [README.md](file:///home/samurai/work/meshmon/README.md) and [Design.md](file:///home/samurai/work/meshmon/Design.md) render cleanly.
- Run privacy and policy compliance scan across all updated files:
  - No `/home/samurai` absolute paths
  - No private hostnames (`builder`, `fox`, `rhino`)
  - No local internal IP addresses (`192.168.8.*`)
  - No third-party corporate employer mentions
  - Valid copyright headers (`Copyright (C) 2026, Charles Chiou`)
  - Standardized `make` invocations (no raw `cmake` commands)
- Check `git diff` and `git status` to ensure all changes are cleanly staged/unstaged with no unprompted commits.
