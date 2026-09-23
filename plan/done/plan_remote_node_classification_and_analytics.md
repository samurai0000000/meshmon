# Plan: Remote Node Classification, Spatial Behavior & Topology Analytics

## Overview

Introduce a formal classification concept in `meshmon` distinguishing **On-Premise (Local)** nodes from **Off-Premise (Remote)** nodes, and deliver a dedicated C++ engine (`SpatialAnalytics`) alongside two feature-rich web dashboards:
1. **Remote Node Behavior (`tab-remote`)**: Deep spatial profiling, polar bearing radar, mobility tracking (Stationary vs. Mobile), RF path-loss scatter analysis, application spectrum, and comprehensive node filtering (including unpositioned RF-only nodes).
2. **Mesh Topology & Routes (`tab-topology`)**: Multi-hop route visualizer (traceroutes), 2-hop shadow node discovery, link asymmetry matrix (forward vs. reverse SNR discrepancies), repeater dependency mapping, and centroid position estimation for GPS-less nodes.

---

## Classification Rules & Ground Truth

The classification model defines a node's presence based on physical proximity and operational trust:

1. **Origin Reference Point**:
   - Reference center is the GPS position of the radio attached directly to `meshmon` (`_mon->whoami()`).
   - If the attached radio has not acquired a GPS lock, fallback to configured or calibrated gateway coordinates.

2. **On-Premise (Local) Criteria**:
   - **Proximity Rule**: Any node with a reported GPS position located within **50 meters** of the gateway reference is classified as **On-Premise**.
   - **Administrative & Mate Trust Rule**: Any node listed in the gateway's `admins()` or `mates()` authority lists is classified as **On-Premise**, regardless of whether it reports GPS coordinates or its physical distance.
   - **Gateway Self**: The gateway node itself (`whoami()`) is always **On-Premise**.

3. **Off-Premise (Remote) Criteria**:
   - Any node located strictly **> 50 meters** away from the gateway reference.
   - **Unpositioned External Nodes (RF-Only)**: Any external node heard on the mesh that is not an admin/mate and lacks GPS coordinates is included in the Remote analysis tagged as **"RF-Only (No GPS)"**, allowing operators to audit its packet volume, SNR, RSSI, and hops.

---

## User Review Required

> [!IMPORTANT]
> - **Dedicated C++ Class (Option 2 Confirmed)**:
>   - Introduce [`SpatialAnalytics.hxx`](file:///home/samurai/work/meshmon/SpatialAnalytics.hxx) and [`SpatialAnalytics.cxx`](file:///home/samurai/work/meshmon/SpatialAnalytics.cxx) to encapsulate all spatial math, bearing calculations, mobility tracking, link asymmetry, centroid estimation, and remote analytics.
>   - Add `SpatialAnalytics.cxx` to `add_executable(meshmon ...)` in [`CMakeLists.txt`](file:///home/samurai/work/meshmon/CMakeLists.txt).
> - **Two Dedicated Dashboard Tabs**:
>   - **`Remote Node Behavior` (`tab-remote`)**: Focuses on physical space, bearing radar, mobility, distance bands, and RF attenuation.
>   - **`Mesh Topology & Routes` (`tab-topology`)**: Focuses on multi-hop routes, traceroutes, link asymmetry, bridging repeaters, and centroid estimation for RF-only nodes.
> - **Navigation Layout**: Clean two-row navigation bar ensuring zero horizontal scrollbars.
> - **Subsystem Recompilation & Deployment**: As per project guidelines, compiling `meshmon` will be verified locally, compiled natively on `fox` (`aarch64`), and relaunched in its existing GNU screen session (`meshmon`) without killing the session.

---

## Confirmed Design Decisions

- **Architecture**: Dedicated C++ class `SpatialAnalytics` holding references to `shared_ptr<MeshMon>` and `shared_ptr<MeshMonDb>`.
- **Unpositioned External Nodes**: Included in remote analytics tagged as `RF-Only (No GPS)` with dedicated filter toggle `[RF-Only]`.
- **Mobility Tracking**: Historical coordinate variance and bounding displacement across historical fixes in `positions` table are computed to classify remote nodes into:
  - `Stationary`: Stable coordinates within jitter tolerance (fixed mountain repeaters, base stations).
  - `Mobile Tracker`: Coordinate displacement > 100 meters (trackers, vehicles, hikers).
  - `RF-Only`: No GPS fixes reported.
- **Topology & Neighborhood Intelligence**:
  - **Traceroute Discovery Paths**: Render multi-hop transmission chains (`Gateway` $\rightarrow$ `Relay` $\rightarrow$ `Remote Target`) from `traceroutes` table.
  - **Link Asymmetry**: Highlight links with $>6\,\text{dB}$ discrepancy between forward and reverse SNR.
  - **RF-Only Centroid Estimation**: Estimate rough geographic location of GPS-less nodes by computing the centroid of the known positions of their hearing neighbors.

---

## Subsystem Architecture & Implementation Details

### 1. Dedicated `SpatialAnalytics` C++ Class

Files: [`SpatialAnalytics.hxx`](file:///home/samurai/work/meshmon/SpatialAnalytics.hxx) & [`SpatialAnalytics.cxx`](file:///home/samurai/work/meshmon/SpatialAnalytics.cxx)

#### Responsibilities:
- **Reference Coordinates & Geographic Math**:
  - Dynamically extracts gateway reference position (`lat`, `lon`, `alt`) from `_mon->whoami()` and `_mon->positions()`.
  - `haversineMeters(lat1, lon1, lat2, lon2)`
  - `calculateBearing(lat1, lon1, lat2, lon2)` returning angle degrees ($0^\circ-360^\circ$) and compass label (N, NE, E, SE, S, SW, W, NW).
- **Authority-Aware Classification**:
  - `bool isNodeOnPremise(uint32_t nodeId, double lat, double lon, double thresholdM = 50.0) const;`
    - Checks `_mon->whoami()`, `_mon->admins()`, and `_mon->mates()`.
    - If trusted admin/mate -> always `true`.
    - If valid GPS and within `thresholdM` -> `true`.
    - Otherwise -> `false`.
- **Remote Node Profiling & Mobility Detection**:
  - Queries `MeshMonDb` for nodes, coordinates in `positions`, and packets in `packets`.
  - Computes max displacement between historical coordinates to assign `Stationary` vs `Mobile Tracker` vs `RF-Only`.
  - Compiles average/min/max Rx SNR, RSSI, 0-hop vs relayed counts, and application portnum breakdown.
- **Topology, Routing & Centroid Estimation**:
  - Parses multi-hop chains from `traceroutes` table.
  - Computes link asymmetry across 0-hop bidirectional links.
  - Computes centroid coordinates for unpositioned nodes based on neighbor positions.
- **JSON Serialization**:
  - `nlohmann::json getRemoteSummaryJson(double thresholdM = 50.0);`
  - `nlohmann::json getRemoteNodesJson(double thresholdM = 50.0);`
  - `nlohmann::json getTopologyRoutesJson();`
  - `nlohmann::json getTopologyAsymmetryJson();`
  - `nlohmann::json getTopologyCentroidsJson();`

---

### 2. WebServer Integration

File: [`WebServer.cxx`](file:///home/samurai/work/meshmon/WebServer.cxx) & [`WebServer.hxx`](file:///home/samurai/work/meshmon/WebServer.hxx)
- Initialize `_spatial = make_shared<SpatialAnalytics>(_mon, _db);`
- Route handlers dispatching to `_spatial`:
  - `GET /api/spatial`: Enhanced with admin/mate authority checks.
  - `GET /api/remote/summary`: Summary metrics.
  - `GET /api/remote/nodes`: Full array of remote node profiles.
  - `GET /api/topology/routes`: Active multi-hop routes from `traceroutes`.
  - `GET /api/topology/asymmetry`: Link asymmetry data.
  - `GET /api/topology/centroids`: Estimated centroids for RF-only nodes.

---

### 3. Frontend Web Dashboards

#### Dashboard A: "Remote Node Behavior" (`tab-remote`)
- **KPI Ribbon**: Remote Nodes count, Remote Airtime %, Mean Distance, Farthest Link, Mobile Trackers.
- **Spatial Bearing & Radar Polar Plot**: Visual compass radar plotting remote nodes with color-coded RF quality and distance rings (1 km, 5 km, 15 km, 30 km, 50+ km).
- **RF Path Loss Scatter Plot**: SNR & RSSI vs Distance (km) scatter visualization.
- **Remote Traffic & App Spectrum**: Portnum breakdown for remote nodes and transmission timeline.
- **Filterable Remote Node Inspector Table**:
  - Filter pills: `[All Remote]`, `[GPS Positioned]`, `[RF-Only (No GPS)]`, `[Mobile Trackers]`, `[Stationary]`.
  - Sortable by distance, packets, SNR, last seen.

#### Dashboard B: "Mesh Topology & Routes" (`tab-topology`)
- **Topology KPI Ribbon**: Discovered Traceroute Paths, 0-Hop Neighbors, Asymmetric Links (>6 dB gap), Top Ingestion Relays.
- **Visual Multi-Hop Route Canvas / SVG**: Graph showing transmission hops from Gateway through intermediate repeaters to distant nodes with per-hop SNR callouts.
- **Link Asymmetry Diagnostic Matrix**: Table and bar graph comparing Rx vs Tx SNR.
- **RF-Only Centroid Estimation View**: Estimated position circles and confidence radii for GPS-less nodes.
- **Traceroute History Inspector Table**: Discovered routes log with origin, destination, hop sequence, and route SNR.

---

## Proposed Changes

### Component 1: C++ Subsystems & Build
#### [NEW] [SpatialAnalytics.hxx](file:///home/samurai/work/meshmon/SpatialAnalytics.hxx)
- Declare `SpatialAnalytics` class, data structures, and analysis methods.

#### [NEW] [SpatialAnalytics.cxx](file:///home/samurai/work/meshmon/SpatialAnalytics.cxx)
- Implement spatial trigonometry, bearing, mobility classification, traceroute parser, asymmetry detection, and JSON exports.

#### [MODIFY] [CMakeLists.txt](file:///home/samurai/work/meshmon/CMakeLists.txt)
- Add `SpatialAnalytics.cxx` to `meshmon` executable sources.

#### [MODIFY] [WebServer.hxx](file:///home/samurai/work/meshmon/WebServer.hxx)
- Add `_spatial` member of type `shared_ptr<SpatialAnalytics>`.
- Declare handlers for `/api/remote/*` and `/api/topology/*`.

#### [MODIFY] [WebServer.cxx](file:///home/samurai/work/meshmon/WebServer.cxx)
- Instantiate `_spatial` in constructor.
- Register new routes and dispatch requests to `_spatial`.

#### [MODIFY] [AimonGatewayClient.cxx](file:///home/samurai/work/meshmon/AimonGatewayClient.cxx)
- Update `toolGetSpatialAnalytics`: enforce admins/mates as on-premise.

### Component 2: Frontend Dashboard (`web/` & `WebAssets.hxx`)
#### [MODIFY] [web/index.html](file:///home/samurai/work/meshmon/web/index.html)
- Add two navigation tab buttons: `Remote Behavior` and `Mesh Topology`.
- Add `<section id="tab-remote" class="tab-pane">`.
- Add `<section id="tab-topology" class="tab-pane">`.

#### [MODIFY] [web/style.css](file:///home/samurai/work/meshmon/web/style.css)
- Add styling for polar radar, scatter plot, topology graph SVG, asymmetry bars, mobility badges, and confidence circles.

#### [MODIFY] [web/app.js](file:///home/samurai/work/meshmon/web/app.js)
- Implement `fetchRemoteAnalytics()` and `fetchTopologyAnalytics()`.
- Draw remote polar radar with cardinal markers.
- Implement interactive scatter plot and SVG route topology graph.
- Implement filtering for remote nodes (All, GPS, RF-Only, Mobile, Stationary).

#### [MODIFY] [WebAssets.hxx](file:///home/samurai/work/meshmon/WebAssets.hxx)
- Synchronize updated `index.html`, `style.css`, and `app.js` into compiled fallback strings.

---

## Verification Plan

### 1. Automated & Compilation Verification
- Local build check:
  ```bash
  make -j$(nproc)
  ```
- Native target host `fox` (`aarch64`) build:
  ```bash
  ssh -n fox "cd ~/work/meshmon && make -j$(nproc)"
  ```

### 2. Manual & Behavioral Verification
- Test all API endpoints:
  - `/api/spatial`: Verify admins/mates tagged on-premise regardless of distance.
  - `/api/remote/summary` & `/api/remote/nodes`: Verify distance, bearing, mobility, and RF-only tagging.
  - `/api/topology/routes` & `/api/topology/asymmetry`: Verify traceroutes and asymmetry calculations.
- Inspect the web UI:
  - Test switching to "Remote Behavior" and "Mesh Topology" tabs.
  - Verify polar radar, scatter chart, topology graph, and asymmetry tables.
  - Test filtering and sorting across all tables.
