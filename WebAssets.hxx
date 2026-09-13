/*
 * WebAssets.hxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#ifndef WEBASSETS_HXX
#define WEBASSETS_HXX

namespace assets {

inline constexpr const char* INDEX_HTML = R"raw_asset(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>meshmon | Meshtastic Telemetry & RF Gateway</title>
    <link rel="stylesheet" href="style.css">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&family=JetBrains+Mono:wght@400;500;600&display=swap" rel="stylesheet">
</head>
<body>
    <div class="app-container">
        <!-- Top Navigation Bar -->
        <header class="navbar">
            <div class="brand">
                <div class="logo-radar">
                    <div class="radar-sweep"></div>
                    <div class="radar-dot"></div>
                </div>
                <div class="brand-text">
                    <h1>meshmon</h1>
                    <span class="badge-sub">Meshtastic RF & Automation Gateway</span>
                </div>
            </div>

            <div class="header-center">
                <div class="status-chip" id="chip-radio" title="Radio Status">
                    <span class="status-dot dot-online"></span>
                    <span class="chip-label">Radio:</span>
                    <span class="chip-val" id="val-radio-name">Connecting...</span>
                </div>
                <div class="status-chip" id="chip-db" title="SQLite Database">
                    <span class="status-dot dot-online"></span>
                    <span class="chip-label">DB:</span>
                    <span class="chip-val" id="val-db-packets">-- pkts</span>
                </div>
                <div class="status-chip" id="chip-stream" title="Live Server-Sent Events">
                    <span class="status-dot dot-online" id="dot-sse"></span>
                    <span class="chip-val" id="val-sse-status">Live Stream</span>
                </div>
            </div>

            <div class="header-actions">
                <!-- Security Lock/Unlock Badge -->
                <button id="btn-auth-toggle" class="btn-auth locked" title="Click to unlock administrative controls">
                    <svg class="icon-lock" viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2">
                        <rect x="3" y="11" width="18" height="11" rx="2" ry="2"></rect>
                        <path d="M7 11V7a5 5 0 0 1 10 0v4"></path>
                    </svg>
                    <span id="auth-btn-label">View Only</span>
                </button>

                <span id="last-updated" class="last-updated">Updated: Just now</span>
                <button id="refresh-btn" class="btn-action" title="Refresh Dashboard">
                    <svg viewBox="0 0 24 24" width="15" height="15" stroke="currentColor" stroke-width="2" fill="none">
                        <path d="M23 4v6h-6"></path>
                        <path d="M1 20v-6h6"></path>
                        <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"></path>
                    </svg>
                    Refresh
                </button>
            </div>
        </header>

        <!-- Top Metrics Ribbon -->
        <section class="metrics-ribbon">
            <div class="metric-card glass-card">
                <div class="metric-icon icon-cyan">📻</div>
                <div class="metric-info">
                    <span class="metric-label">Gateway Node</span>
                    <span class="metric-value font-mono" id="ribbon-gateway-id">--</span>
                    <span class="metric-sub" id="ribbon-gateway-fw">Meshtastic FW --</span>
                </div>
            </div>

            <div class="metric-card glass-card">
                <div class="metric-icon icon-green">📡</div>
                <div class="metric-info">
                    <span class="metric-label">Active Fleet</span>
                    <span class="metric-value" id="ribbon-nodes-count">0 Nodes</span>
                    <span class="metric-sub" id="ribbon-nodes-online">0 Online &bull; 0 Stale</span>
                </div>
            </div>

            <div class="metric-card glass-card">
                <div class="metric-icon icon-amber">⚡</div>
                <div class="metric-info">
                    <span class="metric-label">Airtime & Channel</span>
                    <span class="metric-value font-mono" id="ribbon-channel-util">0.0%</span>
                    <span class="metric-sub" id="ribbon-air-tx">Gateway Tx: 0.0%</span>
                </div>
            </div>

            <div class="metric-card glass-card">
                <div class="metric-icon icon-purple">🥧</div>
                <div class="metric-info">
                    <span class="metric-label">Host Hardware</span>
                    <span class="metric-value font-mono" id="ribbon-cpu-temp">--°C</span>
                    <span class="metric-sub" id="ribbon-host-uptime">Uptime: --</span>
                </div>
            </div>

            <div class="metric-card glass-card">
                <div class="metric-icon icon-magenta">🗄️</div>
                <div class="metric-info">
                    <span class="metric-label">Packet Storage</span>
                    <span class="metric-value font-mono" id="ribbon-db-size">0 KB</span>
                    <span class="metric-sub" id="ribbon-db-records">0 Packets Logged</span>
                </div>
            </div>
        </section>

        <!-- Navigation Tabs (Two Rows - Zero Horizontal Scrollbar) -->
        <nav class="tabs-nav glass-card">
            <div class="tabs-row tabs-row-primary">
                <button class="tab-btn active" data-tab="analytics">
                    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="20" x2="18" y2="10"></line><line x1="12" y1="20" x2="12" y2="4"></line><line x1="6" y1="20" x2="6" y2="14"></line></svg>
                    Mesh Network Insights
                </button>
                <button class="tab-btn" data-tab="sniffer">
                    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"></polyline></svg>
                    Live Packet Sniffer
                    <span class="tab-badge" id="sniffer-counter">0</span>
                </button>
                <button class="tab-btn" data-tab="spatial">
                    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><polygon points="1 6 1 22 8 18 16 22 23 18 23 2 16 6 8 2 1 6"></polygon><line x1="8" y1="2" x2="8" y2="18"></line><line x1="16" y1="6" x2="16" y2="22"></line></svg>
                    Spatial Radar
                </button>
                <button class="tab-btn" data-tab="fleet">
                    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"></path><circle cx="9" cy="7" r="4"></circle><path d="M23 21v-2a4 4 0 0 0-3-3.87"></path><path d="M16 3.13a4 4 0 0 1 0 7.75"></path></svg>
                    Fleet Nodes
                </button>
            </div>
            <div class="tabs-row tabs-row-secondary">
                <button class="tab-btn" data-tab="automation">
                    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"></polygon></svg>
                    HomeMesh Automation
                </button>
                <button class="tab-btn" data-tab="messaging">
                    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"></path></svg>
                    Mesh Chat & AI
                </button>
                <button class="tab-btn" data-tab="console">
                    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><polyline points="4 17 10 11 4 5"></polyline><line x1="12" y1="19" x2="20" y2="19"></line></svg>
                    SQL Console
                </button>
            </div>
        </nav>

        <!-- Main Tab Content Area -->
        <main class="tab-content-container">
            <!-- TAB 1: FLEET EXPLORER -->
            <section id="tab-fleet" class="tab-pane">
                <div class="pane-toolbar glass-card">
                    <div class="search-box">
                        <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"></circle><line x1="21" y1="21" x2="16.65" y2="16.65"></line></svg>
                        <input type="text" id="node-search-input" placeholder="Search node name, hex ID (!2bf941d4), or role...">
                    </div>
                    <div class="filter-group">
                        <select id="role-filter-select" class="dropdown-filter">
                            <option value="all">All Roles</option>
                            <option value="CLIENT">Client</option>
                            <option value="ROUTER">Router</option>
                            <option value="REPEATER">Repeater</option>
                            <option value="ROUTER_CLIENT">Router Client</option>
                        </select>
                        <select id="status-filter-select" class="dropdown-filter">
                            <option value="all">All Status</option>
                            <option value="online">Online (&le;15m)</option>
                            <option value="stale">Stale (&le;1h)</option>
                            <option value="offline">Offline (&gt;1h)</option>
                        </select>
                    </div>
                </div>

                <div id="nodes-grid" class="nodes-grid">
                    <div class="empty-state">Scanning mesh for active nodes...</div>
                </div>
            </section>

            <!-- TAB 2: LIVE PACKET SNIFFER -->
            <section id="tab-sniffer" class="tab-pane">
                <div class="pane-toolbar glass-card">
                    <div class="sniffer-controls">
                        <button id="btn-toggle-sniffer" class="btn-action active">
                            <span class="dot-status dot-online"></span>
                            Pause Stream
                        </button>
                        <button id="btn-clear-sniffer" class="btn-action">Clear View</button>
                        <span class="sniffer-rate-text" id="sniffer-rate-label">0 packets/sec</span>
                    </div>
                    <div class="search-box">
                        <input type="text" id="packet-filter-input" placeholder="Filter packets by node, app, or text payload...">
                    </div>
                    <select id="portnum-filter-select" class="dropdown-filter">
                        <option value="-1">All Apps / PortNums</option>
                        <option value="1">TEXT_MESSAGE_APP</option>
                        <option value="67">TELEMETRY_APP</option>
                        <option value="3">POSITION_APP</option>
                        <option value="4">NODEINFO_APP</option>
                        <option value="5">ROUTING_APP</option>
                        <option value="6">ADMIN_APP</option>
                        <option value="8">TRACEROUTE_APP</option>
                    </select>
                </div>

                <div class="table-responsive glass-card">
                    <table class="data-table" id="table-packets">
                        <thead>
                            <tr>
                                <th style="width: 85px;">Time</th>
                                <th style="width: 140px;">App / Port</th>
                                <th style="width: 160px;">From</th>
                                <th style="width: 160px;">To</th>
                                <th style="width: 70px;">Ch</th>
                                <th style="width: 80px;">SNR</th>
                                <th style="width: 70px;">Hops</th>
                                <th>Decoded Payload / Content</th>
                            </tr>
                        </thead>
                        <tbody id="tbody-packets">
                            <tr><td colspan="8" class="empty-cell">Waiting for live packets...</td></tr>
                        </tbody>
                    </table>
                </div>
            </section>

            <!-- TAB 3: RF & MESH ANALYTICS (DEFAULT TAB) -->
            <section id="tab-analytics" class="tab-pane active">
                <div class="pane-toolbar glass-card">
                    <div class="toolbar-title-group">
                        <h2>Mesh Network Observability & RF Intelligence</h2>
                        <span class="toolbar-sub">Deep telemetry, hop propagation depth, spectrum duty cycle, and backbone infrastructure</span>
                    </div>
                    <div class="time-window-selector">
                        <span class="time-label">Horizon:</span>
                        <button class="btn-time-window" data-hours="1">1h</button>
                        <button class="btn-time-window" data-hours="6">6h</button>
                        <button class="btn-time-window active" data-hours="24">24h</button>
                        <button class="btn-time-window" data-hours="168">7d</button>
                    </div>
                </div>

                <!-- High-Level Insights Hero Banner -->
                <div class="insights-summary-banner glass-card">
                    <div class="insight-stat-box">
                        <span class="insight-label">Direct vs Relayed Traffic</span>
                        <div class="ratio-bar-container">
                            <div class="ratio-bar-fill direct-fill" id="bar-direct-pct" style="width: 50%;"></div>
                            <div class="ratio-bar-fill relay-fill" id="bar-relay-pct" style="width: 50%;"></div>
                        </div>
                        <div class="ratio-legend">
                            <span class="ratio-legend-item"><span class="dot-legend dot-direct"></span> Direct (0-Hop): <strong id="lbl-direct-pct">--%</strong></span>
                            <span class="ratio-legend-item"><span class="dot-legend dot-relay"></span> Relayed: <strong id="lbl-relay-pct">--%</strong></span>
                        </div>
                    </div>
                    <div class="insight-stat-box">
                        <span class="insight-label">Avg Propagation Depth</span>
                        <div class="insight-metric-val font-mono" id="lbl-avg-hops">--</div>
                        <span class="insight-sub">Average Network Hop Count</span>
                    </div>
                    <div class="insight-stat-box">
                        <span class="insight-label">Backbone Repeaters</span>
                        <div class="insight-metric-val font-mono" id="lbl-backbone-count">--</div>
                        <span class="insight-sub" id="lbl-top-relay">Top Relay: --</span>
                    </div>
                    <div class="insight-stat-box">
                        <span class="insight-label">Mesh Loop / Storm Watch</span>
                        <div class="insight-metric-val" id="lbl-storm-status"><span class="badge badge-green">Clear</span></div>
                        <span class="insight-sub" id="lbl-storm-detail">0 Echo Storms</span>
                    </div>
                </div>

                <div class="analytics-grid">
                    <!-- Hop Count Distribution -->
                    <div class="card glass-card">
                        <div class="card-header">
                            <h3>RF Hop Count Distribution</h3>
                            <span class="badge badge-cyan">RF Proximity</span>
                        </div>
                        <div class="chart-container">
                            <svg id="chart-hops" class="svg-chart" viewBox="0 0 420 180"></svg>
                        </div>
                        <div class="chart-legend" id="hops-legend"></div>
                    </div>

                    <!-- Channel Health Card -->
                    <div class="card glass-card">
                        <div class="card-header">
                            <h3>Channel Health & Duty Cycle</h3>
                            <span class="badge badge-amber">Spectrum</span>
                        </div>
                        <div class="gauge-duo">
                            <div class="circular-gauge" id="gauge-chan-util">
                                <svg viewBox="0 0 120 120" class="gauge-svg">
                                    <circle class="gauge-bg" cx="60" cy="60" r="50"></circle>
                                    <circle class="gauge-val gauge-amber" id="circle-chan-util" cx="60" cy="60" r="50"></circle>
                                </svg>
                                <div class="gauge-text">
                                    <span class="gauge-num font-mono" id="num-chan-util">0%</span>
                                    <span class="gauge-sub">Channel Util</span>
                                </div>
                            </div>
                            <div class="circular-gauge" id="gauge-air-tx">
                                <svg viewBox="0 0 120 120" class="gauge-svg">
                                    <circle class="gauge-bg" cx="60" cy="60" r="50"></circle>
                                    <circle class="gauge-val gauge-cyan" id="circle-air-tx" cx="60" cy="60" r="50"></circle>
                                </svg>
                                <div class="gauge-text">
                                    <span class="gauge-num font-mono" id="num-air-tx">0%</span>
                                    <span class="gauge-sub">Air Util Tx</span>
                                </div>
                            </div>
                        </div>
                        <div class="metric-note" id="note-channel-stats">Calculating spectrum airtime metrics...</div>
                    </div>

                    <!-- Protocol & App Traffic Breakdown -->
                    <div class="card glass-card">
                        <div class="card-header">
                            <h3>Meshtastic Protocol Breakdown</h3>
                            <span class="badge badge-purple">App Ports</span>
                        </div>
                        <div class="app-dist-list" id="app-dist-container">
                            <div class="empty-state">Aggregating protocol distribution...</div>
                        </div>
                    </div>

                    <!-- Direct Line-of-Sight RF Neighbors (0-Hop Links) -->
                    <div class="card glass-card">
                        <div class="card-header">
                            <h3>Direct RF Neighbors (0-Hop Line-of-Sight)</h3>
                            <span class="badge badge-green">Direct Links</span>
                        </div>
                        <div class="table-responsive" style="max-height: 240px; overflow-y: auto;">
                            <table class="data-table" id="table-neighbors">
                                <thead>
                                    <tr>
                                        <th>Node</th>
                                        <th>Name</th>
                                        <th>Packets</th>
                                        <th>Avg SNR</th>
                                        <th>Avg RSSI</th>
                                    </tr>
                                </thead>
                                <tbody id="tbody-neighbors">
                                    <tr><td colspan="5" class="empty-cell">Finding 0-hop neighbors...</td></tr>
                                </tbody>
                            </table>
                        </div>
                    </div>

                    <!-- Critical Repeaters Card -->
                    <div class="card glass-card">
                        <div class="card-header">
                            <h3>Critical Mesh Repeaters (Backbone Infrastructure)</h3>
                            <span class="badge badge-cyan">Relay Load</span>
                        </div>
                        <div class="table-responsive">
                            <table class="data-table" id="table-repeaters">
                                <thead>
                                    <tr>
                                        <th>Repeater Hex</th>
                                        <th>Short Name</th>
                                        <th>Relayed Packets</th>
                                        <th>Avg SNR</th>
                                    </tr>
                                </thead>
                                <tbody id="tbody-repeaters">
                                    <tr><td colspan="4" class="empty-cell">Identifying backbone repeaters...</td></tr>
                                </tbody>
                            </table>
                        </div>
                    </div>

                    <!-- Spatial Reach & Coverage Summary -->
                    <div class="card glass-card spatial-summary-card">
                        <div class="card-header">
                            <h3>Geographic Reach & Spatial Topology</h3>
                            <button class="btn-ctrl" id="btn-jump-spatial">
                                <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2"><polygon points="1 6 1 22 8 18 16 22 23 18 23 2 16 6 8 2 1 6"></polygon></svg>
                                Open Radar Map
                            </button>
                        </div>
                        <div class="reach-stats-grid">
                            <div class="reach-stat-item">
                                <div class="reach-stat-val" id="spatial-val-farthest">-- km</div>
                                <div class="reach-stat-lbl">Max Reach</div>
                            </div>
                            <div class="reach-stat-item">
                                <div class="reach-stat-val" id="spatial-val-premise">--</div>
                                <div class="reach-stat-lbl">On-Premise Nodes</div>
                            </div>
                            <div class="reach-stat-item">
                                <div class="reach-stat-val" id="spatial-val-gps">--</div>
                                <div class="reach-stat-lbl">GPS Positioned Nodes</div>
                            </div>
                        </div>
                        <div class="metric-note" id="spatial-val-farthest-node">Farthest detected radio link: Calculating...</div>
                    </div>

                    <!-- Top Talkers Leaderboard -->
                    <div class="card glass-card span-2">
                        <div class="card-header">
                            <h3>Top Mesh Talkers (Airtime Contributors)</h3>
                            <span class="badge badge-purple">Traffic Rank</span>
                        </div>
                        <div class="table-responsive">
                            <table class="data-table" id="table-top-talkers">
                                <thead>
                                    <tr>
                                        <th>Node Hex</th>
                                        <th>Short Name</th>
                                        <th>Packets Sent</th>
                                        <th>Payload Bytes</th>
                                        <th>Avg SNR</th>
                                        <th>Avg Hops</th>
                                        <th>Last Seen</th>
                                    </tr>
                                </thead>
                                <tbody id="tbody-top-talkers">
                                    <tr><td colspan="7" class="empty-cell">Calculating top talkers...</td></tr>
                                </tbody>
                            </table>
                        </div>
                    </div>
                </div>
            </section>

            <!-- TAB 4: HOMEMESH AUTOMATION -->
            <section id="tab-automation" class="tab-pane">
                <div class="automation-grid">
                    <!-- MeshPump Card -->
                    <div class="card glass-card auto-card" id="card-meshpump">
                        <div class="card-header">
                            <div class="auto-title">
                                <span class="auto-icon">🌊</span>
                                <div>
                                    <h3>MeshPump</h3>
                                    <span class="auto-node-id font-mono" id="pump-node-hex">!--------</span>
                                </div>
                            </div>
                            <span class="status-pill offline" id="pump-status-pill">Offline</span>
                        </div>
                        <div class="auto-body">
                            <div class="auto-stat-row">
                                <span class="stat-label">Main Fish Pump:</span>
                                <span class="stat-value" id="pump-val-fish">--</span>
                            </div>
                            <div class="auto-stat-row">
                                <span class="stat-label">Upward Pump:</span>
                                <span class="stat-value" id="pump-val-up">--</span>
                            </div>
                            <div class="auto-stat-row">
                                <span class="stat-label">Cutoff Timer:</span>
                                <span class="stat-value font-mono" id="pump-val-cutoff">-- sec</span>
                            </div>
                            <div class="auto-stat-row">
                                <span class="stat-label">LED Display:</span>
                                <span class="stat-value font-mono" id="pump-val-led">--</span>
                            </div>
                        </div>
                        <div class="auto-actions">
                            <button class="btn-ctrl btn-auth-guarded" data-cmd="PUMP_START" data-target="meshpump">
                                <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2"><polygon points="5 3 19 12 5 21 5 3"></polygon></svg>
                                Start Up Pump
                            </button>
                            <button class="btn-ctrl btn-danger btn-auth-guarded" data-cmd="PUMP_STOP" data-target="meshpump">
                                <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect></svg>
                                Stop
                            </button>
                        </div>
                    </div>

                    <!-- MeshRoof Card -->
                    <div class="card glass-card auto-card" id="card-meshroof">
                        <div class="card-header">
                            <div class="auto-title">
                                <span class="auto-icon">🏡</span>
                                <div>
                                    <h3>MeshRoof</h3>
                                    <span class="auto-node-id font-mono" id="roof-node-hex">!--------</span>
                                </div>
                            </div>
                            <span class="status-pill offline" id="roof-status-pill">Offline</span>
                        </div>
                        <div class="auto-body">
                            <div class="auto-stat-row">
                                <span class="stat-label">RF Amplifier:</span>
                                <span class="stat-value" id="roof-val-amplify">--</span>
                            </div>
                            <div class="auto-stat-row">
                                <span class="stat-label">WiFi Status:</span>
                                <span class="stat-value" id="roof-val-wifi">--</span>
                            </div>
                            <div class="auto-stat-row">
                                <span class="stat-label">IP Address:</span>
                                <span class="stat-value font-mono" id="roof-val-ip">--</span>
                            </div>
                            <div class="auto-stat-row">
                                <span class="stat-label">Board Temp:</span>
                                <span class="stat-value font-mono" id="roof-val-temp">--°C</span>
                            </div>
                        </div>
                        <div class="auto-actions">
                            <button class="btn-ctrl btn-auth-guarded" data-cmd="ROOF_OPEN" data-target="meshroof">
                                Open Roof
                            </button>
                            <button class="btn-ctrl btn-auth-guarded" data-cmd="ROOF_CLOSE" data-target="meshroof">
                                Close Roof
                            </button>
                            <button class="btn-ctrl btn-danger btn-auth-guarded" data-cmd="ROOF_STOP" data-target="meshroof">
                                Stop
                            </button>
                        </div>
                    </div>

                    <!-- MeshRoom Card -->
                    <div class="card glass-card auto-card" id="card-meshroom">
                        <div class="card-header">
                            <div class="auto-title">
                                <span class="auto-icon">🛋️</span>
                                <div>
                                    <h3>MeshRoom</h3>
                                    <span class="auto-node-id font-mono" id="room-node-hex">!--------</span>
                                </div>
                            </div>
                            <span class="status-pill offline" id="room-status-pill">Offline</span>
                        </div>
                        <div class="auto-body">
                            <div class="auto-stat-row">
                                <span class="stat-label">Room Temp:</span>
                                <span class="stat-value font-mono highlight-temp" id="room-val-temp">--°C</span>
                            </div>
                            <div class="auto-stat-row">
                                <span class="stat-label">Air Conditioner:</span>
                                <span class="stat-value" id="room-val-ac-power">--</span>
                            </div>
                            <div class="auto-stat-row">
                                <span class="stat-label">AC Target / Mode:</span>
                                <span class="stat-value" id="room-val-ac-target">--</span>
                            </div>
                            <div class="auto-stat-row">
                                <span class="stat-label">TV State:</span>
                                <span class="stat-value" id="room-val-tv">--</span>
                            </div>
                        </div>
                        <div class="auto-actions">
                            <button class="btn-ctrl btn-auth-guarded" data-cmd="AC_TOGGLE" data-target="meshroom">
                                Toggle AC
                            </button>
                            <button class="btn-ctrl btn-auth-guarded" data-cmd="TV_TOGGLE" data-target="meshroom">
                                Toggle TV
                            </button>
                        </div>
                    </div>
                </div>

                <!-- Recent Automation Event Log -->
                <div class="card glass-card automation-log-card">
                    <div class="card-header">
                        <h3>Recent Automation Events & RTT Latency</h3>
                        <span class="badge badge-magenta">LoRa Roundtrip</span>
                    </div>
                    <div class="table-responsive">
                        <table class="data-table" id="table-auto-history">
                            <thead>
                                <tr>
                                    <th>Time</th>
                                    <th>Device</th>
                                    <th>Direction</th>
                                    <th>Subsystem</th>
                                    <th>Command</th>
                                    <th>Status</th>
                                    <th>Initiator</th>
                                    <th>RTT Latency</th>
                                </tr>
                            </thead>
                            <tbody id="tbody-auto-history">
                                <tr><td colspan="8" class="empty-cell">No recent automation events</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>
            </section>

            <!-- TAB 5: MESH CHAT & AI -->
            <section id="tab-messaging" class="tab-pane">
                <div class="messaging-container glass-card">
                    <div class="chat-header">
                        <div class="chat-dest-selector">
                            <label>Destination:</label>
                            <select id="chat-dest-select" class="dropdown-filter">
                                <option value="^all">Broadcast (^all)</option>
                            </select>
                            <label>Channel:</label>
                            <select id="chat-channel-select" class="dropdown-filter">
                                <option value="0">Primary (ch 0)</option>
                                <option value="1">Secondary (ch 1)</option>
                            </select>
                        </div>
                        <div class="chat-status-text" id="chat-lock-status">🔒 Unlock with password to transmit</div>
                    </div>

                    <div class="chat-messages" id="chat-messages-feed">
                        <div class="empty-chat-state">Loading mesh message history...</div>
                    </div>

                    <form class="chat-input-bar" id="form-send-chat">
                        <input type="text" id="input-chat-text" placeholder="Type a LoRa mesh message..." autocomplete="off">
                        <button type="submit" id="btn-send-message" class="btn-send btn-auth-guarded" title="Transmit message over LoRa radio">
                            <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><line x1="22" y1="2" x2="11" y2="13"></line><polygon points="22 2 15 22 11 13 2 9 22 2"></polygon></svg>
                            Send
                        </button>
                    </form>
                </div>
            </section>

            <!-- TAB 6: SPATIAL RADAR -->
            <section id="tab-spatial" class="tab-pane">
                <div class="spatial-grid">
                    <div class="card glass-card radar-view-card">
                        <div class="card-header">
                            <h3>Geographic Radar & Link Positions</h3>
                            <span class="badge badge-cyan" id="spatial-ref-label">Origin: Gateway</span>
                        </div>
                        <div class="radar-container">
                            <svg id="spatial-radar-svg" class="radar-svg" viewBox="-250 -250 500 500">
                                <!-- Radar rings & axes -->
                                <circle cx="0" cy="0" r="60" class="radar-ring"></circle>
                                <circle cx="0" cy="0" r="130" class="radar-ring"></circle>
                                <circle cx="0" cy="0" r="200" class="radar-ring"></circle>
                                <line x1="-240" y1="0" x2="240" y2="0" class="radar-axis"></line>
                                <line x1="0" y1="-240" x2="0" y2="240" class="radar-axis"></line>
                                <text x="10" y="-190" class="radar-lbl font-mono">20 km</text>
                                <text x="10" y="-120" class="radar-lbl font-mono">10 km</text>
                                <text x="10" y="-50" class="radar-lbl font-mono">2 km</text>
                                <circle cx="0" cy="0" r="5" class="radar-center-dot"></circle>
                                <g id="radar-nodes-layer"></g>
                            </svg>
                        </div>
                    </div>

                    <div class="card glass-card spatial-stats-card">
                        <div class="card-header">
                            <h3>Distance & Premise Classification</h3>
                        </div>
                        <div class="spatial-stats-summary" id="spatial-summary-box">
                            <div class="stat-tile">
                                <span class="stat-num font-mono" id="spatial-total-gps">0</span>
                                <span class="stat-lbl">Nodes with GPS</span>
                            </div>
                            <div class="stat-tile">
                                <span class="stat-num font-mono" id="spatial-on-premise">0</span>
                                <span class="stat-lbl">On Premise (&le;50m)</span>
                            </div>
                            <div class="stat-tile">
                                <span class="stat-num font-mono" id="spatial-farthest-km">0.0 km</span>
                                <span class="stat-lbl">Farthest Link</span>
                            </div>
                        </div>

                        <div class="table-responsive">
                            <table class="data-table" id="table-spatial-nodes">
                                <thead>
                                    <tr>
                                        <th>Node</th>
                                        <th>Distance</th>
                                        <th>Coordinates</th>
                                        <th>Altitude</th>
                                        <th>Last Seen</th>
                                    </tr>
                                </thead>
                                <tbody id="tbody-spatial-nodes">
                                    <tr><td colspan="5" class="empty-cell">Scanning spatial coordinates...</td></tr>
                                </tbody>
                            </table>
                        </div>
                    </div>
                </div>
            </section>

            <!-- TAB 7: SQL CONSOLE -->
            <section id="tab-console" class="tab-pane">
                <div class="card glass-card sql-console-card">
                    <div class="card-header">
                        <h3>SQLite Database Query Inspector</h3>
                        <div class="sql-presets">
                            <span class="preset-label">Presets:</span>
                            <button class="btn-preset" data-sql="SELECT * FROM packets ORDER BY id DESC LIMIT 25;">Recent Packets</button>
                            <button class="btn-preset" data-sql="SELECT * FROM telemetry ORDER BY id DESC LIMIT 25;">Telemetry Logs</button>
                            <button class="btn-preset" data-sql="SELECT * FROM nodes ORDER BY last_seen DESC LIMIT 25;">Known Nodes</button>
                            <button class="btn-preset" data-sql="SELECT * FROM text_messages ORDER BY id DESC LIMIT 25;">Text Messages</button>
                            <button class="btn-preset" data-sql="SELECT * FROM automation_events ORDER BY id DESC LIMIT 25;">Automation Events</button>
                        </div>
                    </div>

                    <div class="sql-editor-container">
                        <textarea id="sql-query-input" class="font-mono" rows="3" placeholder="SELECT * FROM packets ORDER BY id DESC LIMIT 20;"></textarea>
                        <div class="sql-actions-bar">
                            <button id="btn-run-sql" class="btn-action btn-cyan btn-auth-guarded">
                                <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2"><polygon points="5 3 19 12 5 21 5 3"></polygon></svg>
                                Execute Query
                            </button>
                            <span id="sql-execution-status" class="sql-status-text">Read-only console</span>
                        </div>
                    </div>

                    <div class="table-responsive sql-results-wrapper">
                        <table class="data-table" id="table-sql-results">
                            <thead id="thead-sql"></thead>
                            <tbody id="tbody-sql">
                                <tr><td class="empty-cell">Enter a SELECT query and click Execute</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>
            </section>
        </main>

        <!-- Node Detail Slide-Out Modal -->
        <div id="modal-node-detail" class="modal-backdrop hidden">
            <div class="modal-card glass-card">
                <div class="modal-header">
                    <div class="modal-node-title">
                        <h2 id="modal-node-name">Node Detail</h2>
                        <span class="font-mono modal-hex" id="modal-node-hex">!00000000</span>
                    </div>
                    <button class="btn-close-modal" id="btn-close-node-modal">&times;</button>
                </div>
                <div class="modal-body">
                    <div class="node-detail-stats-grid" id="modal-stats-grid"></div>
                    <div class="fading-chart-section">
                        <h4>24-Hour SNR & RSSI Link Fading</h4>
                        <div class="fading-chart-container">
                            <svg id="modal-fading-chart" class="svg-chart" viewBox="0 0 500 160"></svg>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <!-- Authentication Password Modal -->
        <div id="modal-auth" class="modal-backdrop hidden">
            <div class="modal-card modal-sm glass-card">
                <div class="modal-header">
                    <div class="modal-node-title">
                        <h2>Admin Authentication</h2>
                        <p class="modal-desc">Enter web password to unlock radio transmission and automation controls.</p>
                    </div>
                    <button class="btn-close-modal" id="btn-close-auth-modal">&times;</button>
                </div>
                <form id="form-auth-login" class="modal-form">
                    <div class="form-group">
                        <label for="auth-password-input">Password:</label>
                        <input type="password" id="auth-password-input" class="input-styled" placeholder="Enter configured password..." autocomplete="current-password" required>
                    </div>
                    <div id="auth-error-msg" class="auth-error hidden">Invalid password</div>
                    <div class="modal-footer-actions">
                        <button type="button" id="btn-auth-cancel" class="btn-action">Cancel</button>
                        <button type="submit" class="btn-action btn-cyan">Unlock Controls</button>
                    </div>
                </form>
            </div>
        </div>

        <footer class="app-footer">
            <p>meshmon &bull; Meshtastic LoRa Gateway &bull; Host: fox (aarch64) &bull; Embedded C++17 Dashboard</p>
        </footer>
    </div>

    <script src="app.js"></script>
</body>
</html>
)raw_asset";

inline constexpr const char* STYLE_CSS = R"raw_asset(/*
 * meshmon web dashboard style
 *
 * Copyright (C) 2026, Charles Chiou
 */

:root {
    --bg-main: #0a0e17;
    --bg-card: rgba(17, 24, 39, 0.72);
    --bg-card-hover: rgba(30, 41, 59, 0.85);
    --border-color: rgba(255, 255, 255, 0.08);
    --border-highlight: rgba(0, 242, 254, 0.3);

    --text-primary: #f3f4f6;
    --text-secondary: #9ca3af;
    --text-muted: #6b7280;

    --cyan-glow: #00f2fe;
    --cyan-deep: #0284c7;
    --green-glow: #10b981;
    --amber-glow: #f59e0b;
    --purple-glow: #8b5cf6;
    --magenta-glow: #ec4899;
    --danger-red: #ef4444;

    --font-sans: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    --font-mono: 'JetBrains Mono', monospace;
}

* {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
}

body {
    background-color: var(--bg-main);
    background-image: 
        radial-gradient(circle at 10% 15%, rgba(0, 242, 254, 0.06) 0%, transparent 45%),
        radial-gradient(circle at 90% 25%, rgba(139, 92, 246, 0.06) 0%, transparent 45%),
        radial-gradient(circle at 50% 85%, rgba(16, 185, 129, 0.04) 0%, transparent 50%);
    color: var(--text-primary);
    font-family: var(--font-sans);
    min-height: 100vh;
    display: flex;
    justify-content: center;
    -webkit-font-smoothing: antialiased;
}

.font-mono {
    font-family: var(--font-mono);
}

.app-container {
    width: 100%;
    max-width: 1560px;
    padding: 18px 24px 36px;
    display: flex;
    flex-direction: column;
    gap: 18px;
}

/* Glass Card Utility */
.glass-card {
    background: var(--bg-card);
    backdrop-filter: blur(16px);
    -webkit-backdrop-filter: blur(16px);
    border: 1px solid var(--border-color);
    border-radius: 14px;
    box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.37);
    transition: border-color 0.2s ease, transform 0.15s ease;
}

/* Header */
.navbar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 14px 22px;
    border-radius: 16px;
}

.brand {
    display: flex;
    align-items: center;
    gap: 14px;
}

.logo-radar {
    width: 32px;
    height: 32px;
    border-radius: 50%;
    border: 2px solid rgba(0, 242, 254, 0.4);
    position: relative;
    overflow: hidden;
    background: rgba(0, 242, 254, 0.08);
}

.radar-sweep {
    position: absolute;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    background: conic-gradient(from 0deg, rgba(0, 242, 254, 0.4) 0deg, transparent 90deg);
    border-radius: 50%;
    animation: radar-rotate 2.5s linear infinite;
}

@keyframes radar-rotate {
    0% { transform: rotate(0deg); }
    100% { transform: rotate(360deg); }
}

.radar-dot {
    position: absolute;
    top: 50%;
    left: 50%;
    width: 6px;
    height: 6px;
    margin: -3px 0 0 -3px;
    border-radius: 50%;
    background: var(--cyan-glow);
    box-shadow: 0 0 8px var(--cyan-glow);
}

.brand-text h1 {
    font-size: 1.45rem;
    font-weight: 700;
    letter-spacing: -0.5px;
    background: linear-gradient(135deg, #ffffff 40%, var(--cyan-glow));
    -webkit-background-clip: text;
    background-clip: text;
    -webkit-text-fill-color: transparent;
}

.badge-sub {
    font-size: 0.72rem;
    color: var(--text-secondary);
    display: block;
    margin-top: -2px;
}

.header-center {
    display: flex;
    gap: 12px;
    align-items: center;
}

.status-chip {
    display: flex;
    align-items: center;
    gap: 6px;
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid var(--border-color);
    padding: 5px 12px;
    border-radius: 20px;
    font-size: 0.8rem;
}

.chip-label {
    color: var(--text-muted);
}

.chip-val {
    font-family: var(--font-mono);
    color: var(--text-primary);
    font-weight: 500;
}

.header-actions {
    display: flex;
    align-items: center;
    gap: 12px;
}

.last-updated {
    font-size: 0.78rem;
    color: var(--text-muted);
}

/* Buttons */
.btn-action, .btn-ctrl {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    background: rgba(255, 255, 255, 0.07);
    color: var(--text-primary);
    border: 1px solid var(--border-color);
    padding: 7px 14px;
    border-radius: 8px;
    font-size: 0.82rem;
    font-weight: 500;
    cursor: pointer;
    transition: all 0.2s ease;
}

.btn-action:hover, .btn-ctrl:hover {
    background: rgba(255, 255, 255, 0.12);
    border-color: rgba(255, 255, 255, 0.2);
}

.btn-action.active {
    background: rgba(0, 242, 254, 0.15);
    border-color: var(--cyan-glow);
    color: var(--cyan-glow);
}

.btn-cyan {
    background: rgba(0, 242, 254, 0.15);
    border-color: var(--cyan-glow);
    color: var(--cyan-glow);
}
.btn-cyan:hover {
    background: rgba(0, 242, 254, 0.28);
}

.btn-danger {
    background: rgba(239, 68, 68, 0.15);
    border-color: var(--danger-red);
    color: #fca5a5;
}
.btn-danger:hover {
    background: rgba(239, 68, 68, 0.28);
}

/* Auth Toggle Button */
.btn-auth {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 6px 13px;
    border-radius: 20px;
    font-size: 0.8rem;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s ease;
}

.btn-auth.locked {
    background: rgba(245, 158, 11, 0.12);
    border: 1px solid rgba(245, 158, 11, 0.35);
    color: #fcd34d;
}
.btn-auth.locked:hover {
    background: rgba(245, 158, 11, 0.22);
}

.btn-auth.unlocked {
    background: rgba(16, 185, 129, 0.15);
    border: 1px solid rgba(16, 185, 129, 0.4);
    color: #6ee7b7;
}

/* Top Metrics Ribbon */
.metrics-ribbon {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
    gap: 14px;
}

.metric-card {
    display: flex;
    align-items: center;
    gap: 14px;
    padding: 14px 18px;
}

.metric-icon {
    width: 44px;
    height: 44px;
    border-radius: 10px;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 1.3rem;
}

.icon-cyan { background: rgba(0, 242, 254, 0.1); border: 1px solid rgba(0, 242, 254, 0.2); }
.icon-green { background: rgba(16, 185, 129, 0.1); border: 1px solid rgba(16, 185, 129, 0.2); }
.icon-amber { background: rgba(245, 158, 11, 0.1); border: 1px solid rgba(245, 158, 11, 0.2); }
.icon-purple { background: rgba(139, 92, 246, 0.1); border: 1px solid rgba(139, 92, 246, 0.2); }
.icon-magenta { background: rgba(236, 72, 153, 0.1); border: 1px solid rgba(236, 72, 153, 0.2); }

.metric-info {
    display: flex;
    flex-direction: column;
}

.metric-label {
    font-size: 0.75rem;
    color: var(--text-muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
}

.metric-value {
    font-size: 1.25rem;
    font-weight: 600;
    color: var(--text-primary);
    margin: 2px 0;
}

.metric-sub {
    font-size: 0.72rem;
    color: var(--text-secondary);
}

/* Tabs Navigation - Structured Two Rows (No Horizontal Scrollbar) */
.tabs-nav {
    display: flex;
    flex-direction: column;
    gap: 8px;
    padding: 10px 14px;
    border-radius: 14px;
    overflow-x: hidden;
    overflow-y: hidden;
}

.tabs-row {
    display: flex;
    gap: 8px;
    width: 100%;
}

.tabs-row .tab-btn {
    flex: 1 1 0;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 8px;
    padding: 9px 14px;
    border-radius: 9px;
    background: rgba(255, 255, 255, 0.025);
    border: 1px solid rgba(255, 255, 255, 0.05);
    color: var(--text-secondary);
    font-size: 0.86rem;
    font-weight: 500;
    cursor: pointer;
    transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
    white-space: nowrap;
    text-align: center;
}

.tabs-row .tab-btn:hover {
    color: var(--text-primary);
    background: rgba(255, 255, 255, 0.07);
    border-color: rgba(255, 255, 255, 0.12);
    transform: translateY(-1px);
}

.tabs-row .tab-btn.active {
    color: #ffffff;
    background: linear-gradient(135deg, rgba(0, 242, 254, 0.2), rgba(79, 70, 229, 0.2));
    border-color: rgba(0, 242, 254, 0.45);
    box-shadow: 0 2px 12px rgba(0, 242, 254, 0.15);
}

.tabs-row .tab-btn.active svg {
    color: var(--cyan-glow);
    filter: drop-shadow(0 0 6px var(--cyan-glow));
}

.tab-badge {
    background: rgba(0, 242, 254, 0.2);
    color: var(--cyan-glow);
    font-size: 0.7rem;
    font-family: var(--font-mono);
    padding: 1px 6px;
    border-radius: 10px;
}

/* Tab Panes */
.tab-pane {
    display: none;
}
.tab-pane.active {
    display: flex;
    flex-direction: column;
    gap: 16px;
}

/* Pane Toolbar */
.pane-toolbar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 12px 16px;
    gap: 14px;
    flex-wrap: wrap;
}

.search-box {
    display: flex;
    align-items: center;
    gap: 8px;
    background: rgba(0, 0, 0, 0.25);
    border: 1px solid var(--border-color);
    padding: 7px 12px;
    border-radius: 8px;
    flex: 1;
    min-width: 260px;
}

.search-box input {
    background: transparent;
    border: none;
    color: var(--text-primary);
    font-size: 0.85rem;
    width: 100%;
    outline: none;
}

.search-box input::placeholder {
    color: var(--text-muted);
}

.dropdown-filter {
    background: rgba(0, 0, 0, 0.25);
    border: 1px solid var(--border-color);
    color: var(--text-primary);
    padding: 7px 12px;
    border-radius: 8px;
    font-size: 0.82rem;
    outline: none;
    cursor: pointer;
}

.filter-group {
    display: flex;
    gap: 10px;
}

/* Fleet Explorer: Nodes Grid */
.nodes-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(310px, 1fr));
    gap: 16px;
}

.node-card {
    padding: 16px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    cursor: pointer;
}

.node-card:hover {
    border-color: var(--border-highlight);
    transform: translateY(-2px);
}

.node-card-header {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
}

.node-title-group {
    display: flex;
    flex-direction: column;
}

.node-name {
    font-size: 1.05rem;
    font-weight: 600;
    color: var(--text-primary);
}

.node-hex {
    font-size: 0.8rem;
    color: var(--cyan-glow);
}

.node-badges-row {
    display: flex;
    gap: 6px;
    flex-wrap: wrap;
}

.badge {
    font-size: 0.7rem;
    font-weight: 600;
    padding: 2px 7px;
    border-radius: 6px;
    text-transform: uppercase;
    letter-spacing: 0.4px;
}

.badge-cyan { background: rgba(0, 242, 254, 0.12); color: var(--cyan-glow); border: 1px solid rgba(0, 242, 254, 0.25); }
.badge-green { background: rgba(16, 185, 129, 0.12); color: #6ee7b7; border: 1px solid rgba(16, 185, 129, 0.25); }
.badge-purple { background: rgba(139, 92, 246, 0.12); color: #c4b5fd; border: 1px solid rgba(139, 92, 246, 0.25); }
.badge-amber { background: rgba(245, 158, 11, 0.12); color: #fcd34d; border: 1px solid rgba(245, 158, 11, 0.25); }
.badge-magenta { background: rgba(236, 72, 153, 0.12); color: #f472b6; border: 1px solid rgba(236, 72, 153, 0.25); }

.status-pill {
    display: inline-flex;
    align-items: center;
    gap: 5px;
    font-size: 0.72rem;
    padding: 3px 8px;
    border-radius: 12px;
    font-weight: 500;
}

.status-pill.online { background: rgba(16, 185, 129, 0.15); color: #6ee7b7; border: 1px solid rgba(16, 185, 129, 0.3); }
.status-pill.stale { background: rgba(245, 158, 11, 0.15); color: #fcd34d; border: 1px solid rgba(245, 158, 11, 0.3); }
.status-pill.offline { background: rgba(107, 114, 128, 0.15); color: #9ca3af; border: 1px solid rgba(107, 114, 128, 0.3); }

.status-dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
}
.dot-online { background: var(--green-glow); box-shadow: 0 0 6px var(--green-glow); }
.dot-stale { background: var(--amber-glow); }
.dot-offline { background: var(--text-muted); }

.node-metrics-box {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 8px;
    background: rgba(0, 0, 0, 0.2);
    padding: 8px 12px;
    border-radius: 8px;
    border: 1px solid rgba(255, 255, 255, 0.04);
}

.node-stat-item {
    display: flex;
    flex-direction: column;
}

.stat-item-label {
    font-size: 0.68rem;
    color: var(--text-muted);
}

.stat-item-val {
    font-size: 0.88rem;
    font-weight: 500;
}

/* Sniffer / Tables */
.table-responsive {
    overflow-x: auto;
    border-radius: 12px;
}

.data-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.83rem;
    text-align: left;
}

.data-table th {
    background: rgba(0, 0, 0, 0.35);
    padding: 10px 14px;
    color: var(--text-muted);
    font-weight: 600;
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    border-bottom: 1px solid var(--border-color);
}

.data-table td {
    padding: 9px 14px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.04);
    color: var(--text-primary);
}

.data-table tr:hover td {
    background: rgba(255, 255, 255, 0.03);
}

.empty-cell {
    text-align: center;
    color: var(--text-muted);
    padding: 24px !important;
}

/* Insights Summary Banner & Toolbar */
.toolbar-title-group {
    display: flex;
    flex-direction: column;
    gap: 2px;
}

.toolbar-title-group h2 {
    font-size: 1.15rem;
    font-weight: 700;
    color: var(--text-primary);
    margin: 0;
}

.toolbar-sub {
    font-size: 0.8rem;
    color: var(--text-secondary);
}

.time-window-selector {
    display: flex;
    align-items: center;
    gap: 6px;
    background: rgba(0, 0, 0, 0.25);
    padding: 4px 6px;
    border-radius: 20px;
    border: 1px solid var(--border-color);
}

.time-label {
    font-size: 0.75rem;
    color: var(--text-muted);
    margin-left: 6px;
    margin-right: 2px;
}

.btn-time-window {
    background: transparent;
    border: none;
    color: var(--text-secondary);
    font-family: var(--font-mono);
    font-size: 0.78rem;
    padding: 4px 10px;
    border-radius: 12px;
    cursor: pointer;
    transition: all 0.15s ease;
}

.btn-time-window:hover {
    color: var(--text-primary);
    background: rgba(255, 255, 255, 0.08);
}

.btn-time-window.active {
    background: var(--cyan-glow);
    color: #0a0e17;
    font-weight: 600;
    box-shadow: 0 0 10px rgba(0, 242, 254, 0.4);
}

.insights-summary-banner {
    display: grid;
    grid-template-columns: 1.5fr 1fr 1fr 1fr;
    gap: 16px;
    padding: 16px 20px;
    border-radius: 14px;
    background: linear-gradient(135deg, rgba(17, 24, 39, 0.85) 0%, rgba(30, 41, 59, 0.7) 100%);
    border: 1px solid rgba(0, 242, 254, 0.15);
    box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
}

.insight-stat-box {
    display: flex;
    flex-direction: column;
    justify-content: center;
    gap: 6px;
    padding-right: 12px;
    border-right: 1px solid rgba(255, 255, 255, 0.06);
}

.insight-stat-box:last-child {
    border-right: none;
    padding-right: 0;
}

.insight-label {
    font-size: 0.78rem;
    font-weight: 500;
    color: var(--text-secondary);
    text-transform: uppercase;
    letter-spacing: 0.5px;
}

.insight-metric-val {
    font-size: 1.65rem;
    font-weight: 700;
    color: var(--text-primary);
    line-height: 1.1;
}

.insight-sub {
    font-size: 0.76rem;
    color: var(--text-muted);
}

/* Direct vs Relayed Ratio Bar */
.ratio-bar-container {
    display: flex;
    width: 100%;
    height: 10px;
    border-radius: 6px;
    overflow: hidden;
    background: rgba(255, 255, 255, 0.08);
    margin: 4px 0 2px 0;
}

.ratio-bar-fill {
    height: 100%;
    transition: width 0.6s ease;
}

.direct-fill {
    background: linear-gradient(90deg, #00f2fe, #4facfe);
    box-shadow: 0 0 8px rgba(0, 242, 254, 0.3);
}

.relay-fill {
    background: linear-gradient(90deg, #ec4899, #f43f5e);
}

.ratio-legend {
    display: flex;
    justify-content: space-between;
    font-size: 0.75rem;
    color: var(--text-secondary);
}

.ratio-legend-item {
    display: flex;
    align-items: center;
    gap: 5px;
}

.dot-legend {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    display: inline-block;
}

.dot-direct { background: #00f2fe; box-shadow: 0 0 6px #00f2fe; }
.dot-relay { background: #ec4899; box-shadow: 0 0 6px #ec4899; }

/* Protocol App Distribution List */
.app-dist-list {
    display: flex;
    flex-direction: column;
    gap: 10px;
    padding: 6px 0;
}

.app-dist-item {
    display: flex;
    flex-direction: column;
    gap: 4px;
}

.app-dist-meta {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 0.82rem;
}

.app-name-tag {
    font-weight: 500;
    color: var(--text-primary);
}

.app-count-tag {
    font-family: var(--font-mono);
    color: var(--text-secondary);
    font-size: 0.78rem;
}

.app-bar-bg {
    width: 100%;
    height: 7px;
    border-radius: 4px;
    background: rgba(255, 255, 255, 0.06);
    overflow: hidden;
}

.app-bar-fill {
    height: 100%;
    border-radius: 4px;
    transition: width 0.6s cubic-bezier(0.16, 1, 0.3, 1);
}

.bar-telemetry { background: linear-gradient(90deg, #10b981, #34d399); }
.bar-position { background: linear-gradient(90deg, #00f2fe, #38bdf8); }
.bar-nodeinfo { background: linear-gradient(90deg, #818cf8, #6366f1); }
.bar-routing { background: linear-gradient(90deg, #f59e0b, #fbbf24); }
.bar-text { background: linear-gradient(90deg, #ec4899, #f472b6); }
.bar-traceroute { background: linear-gradient(90deg, #a855f7, #c084fc); }

/* Spatial Reach Callout */
.reach-stats-grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 12px;
    margin-bottom: 14px;
}

.reach-stat-item {
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid var(--border-color);
    border-radius: 10px;
    padding: 12px;
    text-align: center;
}

.reach-stat-val {
    font-size: 1.3rem;
    font-weight: 700;
    color: var(--cyan-glow);
    font-family: var(--font-mono);
}

.reach-stat-lbl {
    font-size: 0.72rem;
    color: var(--text-secondary);
    margin-top: 2px;
}

/* Analytics Grid */
.analytics-grid {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 16px;
}
.span-2 {
    grid-column: span 2;
}

.card-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 14px;
}

.card-header h3 {
    font-size: 1.05rem;
    font-weight: 600;
}

.chart-container {
    height: 180px;
    width: 100%;
}
.svg-chart {
    width: 100%;
    height: 100%;
}

.gauge-duo {
    display: flex;
    justify-content: space-around;
    align-items: center;
    padding: 14px 0;
}

.circular-gauge {
    position: relative;
    width: 110px;
    height: 110px;
}

.gauge-svg {
    width: 100%;
    height: 100%;
    transform: rotate(-90deg);
}

.gauge-bg {
    fill: none;
    stroke: rgba(255, 255, 255, 0.08);
    stroke-width: 10;
}

.gauge-val {
    fill: none;
    stroke-width: 10;
    stroke-linecap: round;
    stroke-dasharray: 314;
    stroke-dashoffset: 314;
    transition: stroke-dashoffset 0.8s ease;
}

.gauge-amber { stroke: var(--amber-glow); }
.gauge-cyan { stroke: var(--cyan-glow); }

.gauge-text {
    position: absolute;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
}

.gauge-num {
    font-size: 1.25rem;
    font-weight: 700;
}

.gauge-sub {
    font-size: 0.68rem;
    color: var(--text-muted);
}

.metric-note {
    font-size: 0.72rem;
    color: var(--text-muted);
    text-align: center;
}

/* HomeMesh Automation */
.automation-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
    gap: 16px;
}

.auto-card {
    padding: 18px;
    display: flex;
    flex-direction: column;
    gap: 14px;
}

.auto-title {
    display: flex;
    align-items: center;
    gap: 10px;
}

.auto-icon {
    font-size: 1.5rem;
}

.auto-node-id {
    font-size: 0.75rem;
    color: var(--cyan-glow);
}

.auto-body {
    display: flex;
    flex-direction: column;
    gap: 8px;
    background: rgba(0, 0, 0, 0.2);
    padding: 12px 14px;
    border-radius: 8px;
}

.auto-stat-row {
    display: flex;
    justify-content: space-between;
    font-size: 0.84rem;
}

.auto-actions {
    display: flex;
    gap: 8px;
}

.highlight-temp {
    color: var(--cyan-glow);
    font-size: 1.1rem;
    font-weight: 600;
}

/* Messaging */
.messaging-container {
    display: flex;
    flex-direction: column;
    height: 600px;
}

.chat-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 14px 18px;
    border-bottom: 1px solid var(--border-color);
}

.chat-dest-selector {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 0.84rem;
}

.chat-status-text {
    font-size: 0.78rem;
    color: #fcd34d;
}

.chat-messages {
    flex: 1;
    overflow-y: auto;
    padding: 18px;
    display: flex;
    flex-direction: column;
    gap: 10px;
}

.chat-msg-row {
    display: flex;
    flex-direction: column;
    max-width: 75%;
}

.chat-msg-row.outbound {
    align-self: flex-end;
}

.chat-bubble {
    padding: 10px 14px;
    border-radius: 12px;
    font-size: 0.88rem;
    line-height: 1.4;
    word-break: break-word;
}

.chat-msg-row.outbound .chat-bubble {
    background: rgba(0, 242, 254, 0.15);
    border: 1px solid rgba(0, 242, 254, 0.3);
    color: #ffffff;
    border-bottom-right-radius: 2px;
}

.chat-msg-row.inbound .chat-bubble {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid var(--border-color);
    color: var(--text-primary);
    border-bottom-left-radius: 2px;
}

.chat-msg-meta {
    font-size: 0.7rem;
    color: var(--text-muted);
    margin-top: 3px;
    display: flex;
    gap: 6px;
}

.chat-input-bar {
    display: flex;
    gap: 10px;
    padding: 14px 18px;
    border-top: 1px solid var(--border-color);
}

.chat-input-bar input {
    flex: 1;
    background: rgba(0, 0, 0, 0.3);
    border: 1px solid var(--border-color);
    padding: 10px 14px;
    border-radius: 8px;
    color: var(--text-primary);
    font-size: 0.88rem;
    outline: none;
}

.btn-send {
    background: var(--cyan-glow);
    color: #0a0e17;
    border: none;
    padding: 10px 20px;
    border-radius: 8px;
    font-weight: 600;
    font-size: 0.88rem;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 6px;
}

/* Spatial Radar */
.spatial-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 16px;
}

.radar-container {
    height: 380px;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 10px;
}

.radar-svg {
    max-height: 100%;
    width: auto;
}

.radar-ring {
    fill: none;
    stroke: rgba(0, 242, 254, 0.2);
    stroke-width: 1.5;
    stroke-dasharray: 4 3;
}

.radar-axis {
    stroke: rgba(0, 242, 254, 0.15);
    stroke-width: 1;
}

.radar-lbl {
    fill: var(--text-muted);
    font-size: 11px;
}

.radar-center-dot {
    fill: var(--cyan-glow);
}

.radar-node-blip {
    fill: #10b981;
    stroke: #ffffff;
    stroke-width: 1;
    cursor: pointer;
    transition: r 0.2s ease;
}
.radar-node-blip:hover {
    r: 7;
    fill: var(--cyan-glow);
}

.spatial-stats-summary {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 10px;
    margin-bottom: 14px;
}

.stat-tile {
    background: rgba(0, 0, 0, 0.2);
    padding: 12px;
    border-radius: 8px;
    display: flex;
    flex-direction: column;
    align-items: center;
}

.stat-num {
    font-size: 1.35rem;
    font-weight: 700;
    color: var(--cyan-glow);
}

.stat-lbl {
    font-size: 0.72rem;
    color: var(--text-muted);
}

/* SQL Console */
.sql-console-card {
    padding: 18px;
    display: flex;
    flex-direction: column;
    gap: 14px;
}

.sql-presets {
    display: flex;
    gap: 8px;
    align-items: center;
    flex-wrap: wrap;
}

.preset-label {
    font-size: 0.75rem;
    color: var(--text-muted);
}

.btn-preset {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid var(--border-color);
    color: var(--text-secondary);
    padding: 4px 10px;
    border-radius: 6px;
    font-size: 0.75rem;
    cursor: pointer;
}
.btn-preset:hover {
    background: rgba(255, 255, 255, 0.1);
    color: var(--text-primary);
}

.sql-editor-container {
    display: flex;
    flex-direction: column;
    gap: 8px;
}

.sql-editor-container textarea {
    background: rgba(0, 0, 0, 0.35);
    border: 1px solid var(--border-color);
    border-radius: 8px;
    padding: 10px 14px;
    color: var(--text-primary);
    font-size: 0.88rem;
    outline: none;
    resize: vertical;
}

.sql-actions-bar {
    display: flex;
    align-items: center;
    gap: 14px;
}

.sql-status-text {
    font-size: 0.78rem;
    color: var(--text-muted);
}

.sql-results-wrapper {
    max-height: 420px;
    border: 1px solid var(--border-color);
    border-radius: 8px;
}

/* Modals */
.modal-backdrop {
    position: fixed;
    top: 0;
    left: 0;
    width: 100vw;
    height: 100vh;
    background: rgba(0, 0, 0, 0.65);
    backdrop-filter: blur(8px);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 9999;
}

.modal-backdrop.hidden {
    display: none;
}

.modal-card {
    width: 90%;
    max-width: 680px;
    padding: 24px;
    display: flex;
    flex-direction: column;
    gap: 16px;
}

.modal-sm {
    max-width: 420px;
}

.modal-header {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
}

.btn-close-modal {
    background: transparent;
    border: none;
    color: var(--text-muted);
    font-size: 1.5rem;
    cursor: pointer;
}
.btn-close-modal:hover {
    color: var(--text-primary);
}

.modal-desc {
    font-size: 0.82rem;
    color: var(--text-secondary);
    margin-top: 4px;
}

.modal-form {
    display: flex;
    flex-direction: column;
    gap: 14px;
}

.input-styled {
    background: rgba(0, 0, 0, 0.35);
    border: 1px solid var(--border-color);
    padding: 9px 14px;
    border-radius: 8px;
    color: var(--text-primary);
    font-size: 0.9rem;
    width: 100%;
    outline: none;
}

.auth-error {
    color: #fca5a5;
    font-size: 0.8rem;
}
.auth-error.hidden {
    display: none;
}

.modal-footer-actions {
    display: flex;
    justify-content: flex-end;
    gap: 10px;
    margin-top: 10px;
}

/* Footer */
.app-footer {
    text-align: center;
    font-size: 0.74rem;
    color: var(--text-muted);
    padding-top: 12px;
}

/* Guarded disabled controls */
button.disabled-guarded {
    opacity: 0.45;
    cursor: not-allowed !important;
}

@media (max-width: 1024px) {
    .analytics-grid, .spatial-grid {
        grid-template-columns: 1fr;
    }
    .span-2 {
        grid-column: span 1;
    }
}
)raw_asset";

inline constexpr const char* APP_JS = R"raw_asset(/*
 * meshmon web dashboard application logic
 *
 * Copyright (C) 2026, Charles Chiou
 */

const state = {
    currentTab: 'analytics',
    analyticsHours: 24,
    status: null,
    nodes: [],
    packets: [],
    analytics: null,
    automation: null,
    messages: [],
    spatial: null,
    auth: {
        authenticated: false,
        auth_required: false,
        token: localStorage.getItem('meshmon_auth_token') || ''
    },
    snifferPaused: false,
    sseConnected: false,
    filters: {
        nodeSearch: '',
        role: 'all',
        status: 'all',
        packetFilter: '',
        portnum: -1
    }
};

let eventSource = null;
let pollTimer = null;

// ----------------------------------------------------------------------------
// Utility Functions
// ----------------------------------------------------------------------------

function escapeHtml(str) {
    if (str === null || str === undefined) return '';
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#039;');
}

function formatRelativeTime(epoch) {
    if (!epoch || epoch <= 0) return 'Never';
    const now = Math.floor(Date.now() / 1000);
    const diff = Math.max(0, now - epoch);

    if (diff < 10) return 'Just now';
    if (diff < 60) return `${diff}s ago`;
    const mins = Math.floor(diff / 60);
    if (mins < 60) return `${mins}m ago`;
    const hours = Math.floor(mins / 60);
    if (hours < 24) return `${hours}h ago`;
    const days = Math.floor(hours / 24);
    return `${days}d ago`;
}

function formatTimeOnly(epoch) {
    if (!epoch || epoch <= 0) return '--:--:--';
    const d = new Date(epoch * 1000);
    return d.toTimeString().split(' ')[0];
}

function formatElapsed(secs) {
    if (!secs || secs < 0) return '0s';
    const d = Math.floor(secs / 86400);
    const h = Math.floor((secs % 86400) / 3600);
    const m = Math.floor((secs % 3600) / 60);
    const s = Math.floor(secs % 60);
    if (d > 0) return `${d}d ${h}h`;
    if (h > 0) return `${h}h ${m}m`;
    if (m > 0) return `${m}m ${s}s`;
    return `${s}s`;
}

function formatBytes(bytes) {
    if (!bytes || bytes <= 0) return '0 B';
    const units = ['B', 'KB', 'MB', 'GB'];
    let i = 0;
    let b = bytes;
    while (b >= 1024 && i < units.length - 1) {
        b /= 1024;
        i++;
    }
    return `${b.toFixed(i === 0 ? 0 : 1)} ${units[i]}`;
}

async function apiFetch(url, options = {}) {
    options.headers = options.headers || {};
    if (state.auth.token) {
        options.headers['Authorization'] = `Bearer ${state.auth.token}`;
    }

    try {
        const res = await fetch(url, options);
        if (res.status === 401) {
            state.auth.authenticated = false;
            updateAuthBadge();
            openAuthModal('Authentication required for this operation.');
            return null;
        }
        return res;
    } catch (e) {
        console.warn(`Fetch error for ${url}:`, e);
        return null;
    }
}

// ----------------------------------------------------------------------------
// Authentication Management
// ----------------------------------------------------------------------------

async function checkAuthStatus() {
    try {
        const res = await apiFetch('/api/auth/status');
        if (!res) return;
        const data = await res.json();
        state.auth.authenticated = data.authenticated;
        state.auth.auth_required = data.auth_required;
        updateAuthBadge();
    } catch (e) {
        console.warn('Failed to check auth status:', e);
    }
}

function updateAuthBadge() {
    const btn = document.getElementById('btn-auth-toggle');
    const label = document.getElementById('auth-btn-label');
    const chatLock = document.getElementById('chat-lock-status');
    const guardedBtns = document.querySelectorAll('.btn-auth-guarded');

    if (!btn || !label) return;

    if (!state.auth.auth_required) {
        btn.className = 'btn-auth unlocked';
        label.textContent = 'Open Mode';
        btn.title = 'No authentication configured';
        if (chatLock) chatLock.textContent = '🟢 Ready to transmit';
        guardedBtns.forEach(b => b.classList.remove('disabled-guarded'));
    } else if (state.auth.authenticated) {
        btn.className = 'btn-auth unlocked';
        label.textContent = 'Admin Unlocked';
        btn.title = 'Click to lock or logout';
        if (chatLock) chatLock.textContent = '🟢 Admin Unlocked (Ready to transmit)';
        guardedBtns.forEach(b => b.classList.remove('disabled-guarded'));
    } else {
        btn.className = 'btn-auth locked';
        label.textContent = 'View Only';
        btn.title = 'Click to unlock administrative controls';
        if (chatLock) chatLock.textContent = '🔒 Unlock with password to transmit';
        guardedBtns.forEach(b => b.classList.add('disabled-guarded'));
    }
}

function openAuthModal(msg = '') {
    const modal = document.getElementById('modal-auth');
    const errMsg = document.getElementById('auth-error-msg');
    const input = document.getElementById('auth-password-input');
    if (!modal) return;

    if (msg && errMsg) {
        errMsg.textContent = msg;
        errMsg.classList.remove('hidden');
    } else if (errMsg) {
        errMsg.classList.add('hidden');
    }

    if (input) {
        input.value = '';
        setTimeout(() => input.focus(), 100);
    }

    modal.classList.remove('hidden');
}

function closeAuthModal() {
    const modal = document.getElementById('modal-auth');
    if (modal) modal.classList.add('hidden');
}

async function handleLoginSubmit(e) {
    e.preventDefault();
    const input = document.getElementById('auth-password-input');
    const errMsg = document.getElementById('auth-error-msg');
    if (!input) return;

    const password = input.value.trim();
    try {
        const res = await fetch('/api/auth/login', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ password })
        });

        if (res.ok) {
            const data = await res.json();
            state.auth.token = data.token;
            state.auth.authenticated = true;
            localStorage.setItem('meshmon_auth_token', data.token);
            updateAuthBadge();
            closeAuthModal();
        } else {
            const err = await res.json();
            if (errMsg) {
                errMsg.textContent = err.error || 'Invalid password';
                errMsg.classList.remove('hidden');
            }
        }
    } catch (err) {
        if (errMsg) {
            errMsg.textContent = 'Network or server error';
            errMsg.classList.remove('hidden');
        }
    }
}

async function handleLogout() {
    if (!confirm('Lock dashboard and log out of admin mode?')) return;
    try {
        await apiFetch('/api/auth/logout', { method: 'POST' });
    } catch (e) {}
    state.auth.token = '';
    state.auth.authenticated = false;
    localStorage.removeItem('meshmon_auth_token');
    updateAuthBadge();
}

// ----------------------------------------------------------------------------
// API Fetch & Rendering Handlers
// ----------------------------------------------------------------------------

async function fetchStatus() {
    try {
        const res = await apiFetch('/api/status');
        if (!res) return;
        const data = await res.json();
        state.status = data;
        renderStatus(data);
    } catch (e) {
        console.warn('fetchStatus error:', e);
    }
}

function renderStatus(data) {
    // Header status chips
    const radioVal = document.getElementById('val-radio-name');
    if (radioVal) {
        if (data.radio && data.radio.connected) {
            radioVal.textContent = `${data.radio.short_name} (${data.radio.whoami_hex})`;
        } else {
            radioVal.textContent = 'Disconnected';
        }
    }

    const dbVal = document.getElementById('val-db-packets');
    if (dbVal && data.db) {
        dbVal.textContent = `${(data.db.total_packets || 0).toLocaleString()} pkts`;
    }

    // Top ribbon
    const gwIdEl = document.getElementById('ribbon-gateway-id');
    const gwFwEl = document.getElementById('ribbon-gateway-fw');
    if (gwIdEl && data.radio) {
        gwIdEl.textContent = data.radio.whoami_hex || '--';
        gwFwEl.textContent = `FW: ${data.radio.firmware_version || 'Meshtastic 2.5+'}`;
    }

    const cpuEl = document.getElementById('ribbon-cpu-temp');
    const uptimeEl = document.getElementById('ribbon-host-uptime');
    if (cpuEl && data.host) {
        cpuEl.textContent = data.host.cpu_temp_c ? `${data.host.cpu_temp_c.toFixed(1)}°C` : '--°C';
        uptimeEl.textContent = `Uptime: ${formatElapsed(data.host.uptime_seconds)}`;
    }

    const dbSizeEl = document.getElementById('ribbon-db-size');
    const dbRecsEl = document.getElementById('ribbon-db-records');
    if (dbSizeEl && data.db) {
        dbSizeEl.textContent = formatBytes(data.db.size_bytes);
        dbRecsEl.textContent = `${(data.db.total_packets || 0).toLocaleString()} pkts &bull; ${(data.db.total_nodes || 0)} nodes`;
    }

    const updatedEl = document.getElementById('last-updated');
    if (updatedEl) {
        updatedEl.textContent = `Updated: ${formatTimeOnly(Math.floor(Date.now() / 1000))}`;
    }
}

async function fetchNodes() {
    try {
        const res = await apiFetch('/api/nodes');
        if (!res) return;
        const data = await res.json();
        state.nodes = data || [];
        renderNodes(state.nodes);
        updateDestSelector(state.nodes);
    } catch (e) {
        console.warn('fetchNodes error:', e);
    }
}

function renderNodes(nodes) {
    const grid = document.getElementById('nodes-grid');
    if (!grid) return;

    // Filter by search, role, status
    const search = state.filters.nodeSearch.toLowerCase().trim();
    const roleFilter = state.filters.role;
    const statusFilter = state.filters.status;

    let onlineCount = 0;
    let staleCount = 0;

    const filtered = nodes.filter(n => {
        if (n.status === 'online') onlineCount++;
        else if (n.status === 'stale') staleCount++;

        if (roleFilter !== 'all' && n.role_name !== roleFilter) return false;
        if (statusFilter !== 'all' && n.status !== statusFilter) return false;

        if (search) {
            const hexMatch = (n.node_hex || '').toLowerCase().includes(search);
            const shortMatch = (n.short_name || '').toLowerCase().includes(search);
            const longMatch = (n.long_name || '').toLowerCase().includes(search);
            const hwMatch = (n.hw_model_name || '').toLowerCase().includes(search);
            if (!hexMatch && !shortMatch && !longMatch && !hwMatch) return false;
        }
        return true;
    });

    // Update fleet ribbon summary
    const countEl = document.getElementById('ribbon-nodes-count');
    const subEl = document.getElementById('ribbon-nodes-online');
    if (countEl) countEl.textContent = `${nodes.length} Nodes`;
    if (subEl) subEl.textContent = `${onlineCount} Online • ${staleCount} Stale`;

    if (filtered.length === 0) {
        grid.innerHTML = `<div class="empty-state">No mesh nodes match your search criteria.</div>`;
        return;
    }

    grid.innerHTML = filtered.map(n => {
        let statusClass = n.status || 'offline';
        let statusLabel = 'Online';
        if (statusClass === 'stale') statusLabel = 'Stale';
        else if (statusClass === 'offline') statusLabel = 'Offline';

        const batteryStr = n.battery_level !== undefined ? (n.battery_level > 100 ? '⚡ USB' : `🔋 ${n.battery_level}%`) : '🔋 --';
        const voltStr = n.voltage !== undefined ? `${n.voltage.toFixed(2)}V` : '--';
        const snrStr = (n.snr !== undefined && n.snr !== 0) ? `${n.snr.toFixed(1)} dB` : '--';
        const hopsStr = (n.hops !== undefined) ? (n.hops === 0 ? 'Direct RF' : `${n.hops} Hops`) : '--';
        const tempStr = n.temperature !== undefined ? `${n.temperature.toFixed(1)}°C` : '';
        const selfTag = n.is_self ? `<span class="badge badge-purple">LOCAL GATEWAY</span>` : '';
        const autoTag = n.automation_type ? `<span class="badge badge-magenta">${escapeHtml(n.automation_type.toUpperCase())}</span>` : '';

        return `
            <div class="node-card glass-card" data-node-id="${n.node_id}" onclick="openNodeDetailModal(${n.node_id})">
                <div class="node-card-header">
                    <div class="node-title-group">
                        <div class="node-name">${escapeHtml(n.short_name || 'Node')} ${selfTag}</div>
                        <span class="node-hex font-mono">${escapeHtml(n.node_hex)}</span>
                    </div>
                    <span class="status-pill ${statusClass}">
                        <span class="status-dot dot-${statusClass}"></span>
                        ${statusLabel}
                    </span>
                </div>

                <div class="node-badges-row">
                    <span class="badge badge-cyan">${escapeHtml(n.role_name || 'CLIENT')}</span>
                    <span class="badge badge-green">${escapeHtml(n.hw_model_name || 'Radio')}</span>
                    ${autoTag}
                </div>

                <div class="node-metrics-box font-mono">
                    <div class="node-stat-item">
                        <span class="stat-item-label">RF Signal (SNR)</span>
                        <span class="stat-item-val">${snrStr}</span>
                    </div>
                    <div class="node-stat-item">
                        <span class="stat-item-label">Hop Distance</span>
                        <span class="stat-item-val">${hopsStr}</span>
                    </div>
                    <div class="node-stat-item">
                        <span class="stat-item-label">Battery / Power</span>
                        <span class="stat-item-val">${batteryStr} (${voltStr})</span>
                    </div>
                    <div class="node-stat-item">
                        <span class="stat-item-label">Last Heard</span>
                        <span class="stat-item-val">${escapeHtml(n.last_heard_rel || 'Never')}</span>
                    </div>
                </div>
            </div>
        `;
    }).join('');
}

async function openNodeDetailModal(nodeId) {
    const modal = document.getElementById('modal-node-detail');
    if (!modal) return;

    modal.classList.remove('hidden');

    try {
        const res = await apiFetch(`/api/node?id=${nodeId}`);
        if (!res) return;
        const d = await res.json();

        document.getElementById('modal-node-name').textContent = d.short_name || 'Node Details';
        document.getElementById('modal-node-hex').textContent = d.node_hex || '';

        const grid = document.getElementById('modal-stats-grid');
        grid.innerHTML = `
            <div class="node-stat-item"><span class="stat-item-label">Total Packets</span><span class="stat-item-val font-mono">${d.total_packets || 0}</span></div>
            <div class="node-stat-item"><span class="stat-item-label">Average SNR</span><span class="stat-item-val font-mono">${(d.avg_snr || 0).toFixed(1)} dB</span></div>
            <div class="node-stat-item"><span class="stat-item-label">Min / Max SNR</span><span class="stat-item-val font-mono">${(d.min_snr || 0).toFixed(1)} / ${(d.max_snr || 0).toFixed(1)} dB</span></div>
            <div class="node-stat-item"><span class="stat-item-label">Average RSSI</span><span class="stat-item-val font-mono">${(d.avg_rssi || 0).toFixed(1)} dBm</span></div>
            <div class="node-stat-item"><span class="stat-item-label">Last Hops</span><span class="stat-item-val font-mono">${d.last_hops !== undefined ? d.last_hops : '--'}</span></div>
            <div class="node-stat-item"><span class="stat-item-label">Battery Level</span><span class="stat-item-val font-mono">${d.last_battery !== undefined ? d.last_battery + '%' : '--'}</span></div>
        `;

        renderFadingChart(d.fading_history || []);
    } catch (e) {
        console.warn('Failed to load node detail:', e);
    }
}

function renderFadingChart(points) {
    const svg = document.getElementById('modal-fading-chart');
    if (!svg) return;

    if (points.length < 2) {
        svg.innerHTML = `<text x="250" y="80" text-anchor="middle" fill="#6b7280" font-size="12">Insufficient historical points for fading chart</text>`;
        return;
    }

    const W = 500;
    const H = 160;
    const pad = 30;

    let minSnr = Math.min(...points.map(p => p.snr));
    let maxSnr = Math.max(...points.map(p => p.snr));
    if (maxSnr - minSnr < 4) {
        maxSnr += 2;
        minSnr -= 2;
    }

    const minT = points[0].time;
    const maxT = points[points.length - 1].time;
    const rangeT = Math.max(1, maxT - minT);

    const pathData = points.map((p, idx) => {
        const x = pad + ((p.time - minT) / rangeT) * (W - pad * 2);
        const y = H - pad - ((p.snr - minSnr) / (maxSnr - minSnr)) * (H - pad * 2);
        return `${idx === 0 ? 'M' : 'L'} ${x.toFixed(1)} ${y.toFixed(1)}`;
    }).join(' ');

    svg.innerHTML = `
        <line x1="${pad}" y1="${H - pad}" x2="${W - pad}" y2="${H - pad}" stroke="rgba(255,255,255,0.1)" />
        <line x1="${pad}" y1="${pad}" x2="${W - pad}" y2="${pad}" stroke="rgba(255,255,255,0.1)" />
        <text x="${pad - 8}" y="${pad + 4}" fill="#9ca3af" font-size="10" text-anchor="end">${maxSnr.toFixed(0)} dB</text>
        <text x="${pad - 8}" y="${H - pad + 4}" fill="#9ca3af" font-size="10" text-anchor="end">${minSnr.toFixed(0)} dB</text>
        <path d="${pathData}" fill="none" stroke="#00f2fe" stroke-width="2.5" stroke-linecap="round" />
    `;
}

// ----------------------------------------------------------------------------
// Live Packet Sniffer
// ----------------------------------------------------------------------------

async function fetchPackets() {
    try {
        let url = `/api/packets?limit=60`;
        if (state.filters.portnum >= 0) {
            url += `&portnum=${state.filters.portnum}`;
        }
        const res = await apiFetch(url);
        if (!res) return;
        const data = await res.json();
        state.packets = data || [];
        renderPackets(state.packets);
    } catch (e) {
        console.warn('fetchPackets error:', e);
    }
}

function renderPackets(packets) {
    const tbody = document.getElementById('tbody-packets');
    if (!tbody) return;

    const filterText = state.filters.packetFilter.toLowerCase().trim();

    const filtered = packets.filter(p => {
        if (!filterText) return true;
        const fromM = (p.from_hex || '').toLowerCase().includes(filterText) || (p.from_name || '').toLowerCase().includes(filterText);
        const toM = (p.to_hex || '').toLowerCase().includes(filterText) || (p.to_name || '').toLowerCase().includes(filterText);
        const appM = (p.app || '').toLowerCase().includes(filterText);
        const txtM = (p.text || '').toLowerCase().includes(filterText);
        return fromM || toM || appM || txtM;
    });

    if (filtered.length === 0) {
        tbody.innerHTML = `<tr><td colspan="8" class="empty-cell">No packets match the filter.</td></tr>`;
        return;
    }

    tbody.innerHTML = filtered.map(p => {
        const appBadgeClass = p.app.includes('TEXT') ? 'badge-cyan' : (p.app.includes('TELEMETRY') ? 'badge-amber' : (p.app.includes('POSITION') ? 'badge-green' : 'badge-purple'));
        const snrStr = p.snr ? `${p.snr.toFixed(1)} dB` : '--';
        const hopsStr = p.hops !== undefined ? (p.hops === 0 ? '0 (Direct)' : `${p.hops}`) : '--';
        const fromDisp = p.from_name ? `${escapeHtml(p.from_name)} <span style="color:var(--cyan-glow);">${escapeHtml(p.from_hex)}</span>` : escapeHtml(p.from_hex);
        const toDisp = p.to_name ? `${escapeHtml(p.to_name)} (${escapeHtml(p.to_hex)})` : escapeHtml(p.to_hex);
        const textPayload = p.text ? `<strong>"${escapeHtml(p.text)}"</strong>` : `<span style="color:var(--text-muted);">${escapeHtml(p.app)} (${p.payload_size} B)</span>`;

        return `
            <tr class="font-mono">
                <td>${formatTimeOnly(p.time)}</td>
                <td><span class="badge ${appBadgeClass}">${escapeHtml(p.app)}</span></td>
                <td>${fromDisp}</td>
                <td>${toDisp}</td>
                <td>#${p.channel}</td>
                <td>${snrStr}</td>
                <td>${hopsStr}</td>
                <td style="font-family:var(--font-sans);">${textPayload}</td>
            </tr>
        `;
    }).join('');

    const counter = document.getElementById('sniffer-counter');
    if (counter) counter.textContent = packets.length;
}

// ----------------------------------------------------------------------------
// RF & Mesh Analytics
// ----------------------------------------------------------------------------

async function fetchAnalytics() {
    try {
        const hours = state.analyticsHours || 24;
        const res = await apiFetch(`/api/analytics?hours=${hours}`);
        if (!res) return;
        const data = await res.json();
        state.analytics = data;
        renderAnalytics(data);
    } catch (e) {
        console.warn('fetchAnalytics error:', e);
    }
}

function renderAnalytics(data) {
    if (!data) return;

    // 1. High-Level Insights Hero Banner
    if (data.traffic) {
        const tf = data.traffic;
        const directPct = tf.direct_pct || 0;
        const relayPct = tf.broadcast_pct || (100 - directPct);

        const bDirect = document.getElementById('bar-direct-pct');
        const bRelay = document.getElementById('bar-relay-pct');
        const lDirect = document.getElementById('lbl-direct-pct');
        const lRelay = document.getElementById('lbl-relay-pct');
        const lAvgHops = document.getElementById('lbl-avg-hops');

        if (bDirect) bDirect.style.width = `${Math.min(100, Math.max(0, directPct))}%`;
        if (bRelay) bRelay.style.width = `${Math.min(100, Math.max(0, relayPct))}%`;
        if (lDirect) lDirect.textContent = `${directPct.toFixed(1)}%`;
        if (lRelay) lRelay.textContent = `${relayPct.toFixed(1)}%`;
        if (lAvgHops) lAvgHops.textContent = `${(tf.avg_hops || 0).toFixed(2)}`;
    }

    // Critical Repeaters & Backbone Count
    const repeaters = data.critical_repeaters || [];
    const lblBackbone = document.getElementById('lbl-backbone-count');
    const lblTopRelay = document.getElementById('lbl-top-relay');
    if (lblBackbone) lblBackbone.textContent = `${repeaters.length} Active`;
    if (lblTopRelay && repeaters.length > 0) {
        lblTopRelay.textContent = `Top Relay: ${escapeHtml(repeaters[0].short_name || repeaters[0].node_hex)}`;
    } else if (lblTopRelay) {
        lblTopRelay.textContent = `Top Relay: None`;
    }

    // Echo Storm / Rebroadcast Anomalies
    const storms = data.echo_storms || [];
    const lblStormStatus = document.getElementById('lbl-storm-status');
    const lblStormDetail = document.getElementById('lbl-storm-detail');
    if (lblStormStatus) {
        if (storms.length > 0) {
            lblStormStatus.innerHTML = `<span class="badge badge-amber">Warning (${storms.length})</span>`;
        } else {
            lblStormStatus.innerHTML = `<span class="badge badge-green">Clear</span>`;
        }
    }
    if (lblStormDetail) {
        if (storms.length > 0) {
            lblStormDetail.textContent = `${storms.length} Loop Storms Detected`;
        } else {
            lblStormDetail.textContent = `0 Echo Storms Detected`;
        }
    }

    // 2. Channel Health Gauges
    if (data.channel_health) {
        const ch = data.channel_health;
        const chanUtil = Math.min(100, Math.max(0, ch.avg_channel_util || 0));
        const airTx = Math.min(100, Math.max(0, ch.avg_air_util_tx || 0));

        const cCirc = document.getElementById('circle-chan-util');
        const cNum = document.getElementById('num-chan-util');
        if (cCirc && cNum) {
            const offset = 314 - (314 * (chanUtil / 100));
            cCirc.style.strokeDashoffset = offset;
            cNum.textContent = `${chanUtil.toFixed(1)}%`;
        }

        const aCirc = document.getElementById('circle-air-tx');
        const aNum = document.getElementById('num-air-tx');
        if (aCirc && aNum) {
            const offset = 314 - (314 * (airTx / 100));
            aCirc.style.strokeDashoffset = offset;
            aNum.textContent = `${airTx.toFixed(1)}%`;
        }

        const note = document.getElementById('note-channel-stats');
        if (note) {
            note.textContent = `Peak Channel: ${(ch.max_channel_util || 0).toFixed(1)}% | Peak Gateway Tx: ${(ch.max_air_util_tx || 0).toFixed(1)}%`;
        }

        // Also update ribbon
        const rChan = document.getElementById('ribbon-channel-util');
        const rAir = document.getElementById('ribbon-air-tx');
        if (rChan) rChan.textContent = `${chanUtil.toFixed(1)}%`;
        if (rAir) rAir.textContent = `Gateway Tx: ${airTx.toFixed(1)}%`;
    }

    // 3. Hop Distribution SVG Chart
    if (data.hop_distribution) {
        renderHopsChart(data.hop_distribution);
    }

    // 4. Protocol & App Distribution Progress Bars
    const appContainer = document.getElementById('app-dist-container');
    if (appContainer && data.app_distribution) {
        if (data.app_distribution.length === 0) {
            appContainer.innerHTML = `<div class="empty-state">No protocol packets in this window.</div>`;
        } else {
            const appClassMap = {
                'TELEMETRY_APP': 'bar-telemetry',
                'POSITION_APP': 'bar-position',
                'NODEINFO_APP': 'bar-nodeinfo',
                'ROUTING_APP': 'bar-routing',
                'TEXT_MESSAGE_APP': 'bar-text',
                'TRACEROUTE_APP': 'bar-traceroute'
            };

            appContainer.innerHTML = data.app_distribution.map(a => {
                const barClass = appClassMap[a.app] || 'bar-nodeinfo';
                const pct = (a.pct || 0).toFixed(1);
                return `
                    <div class="app-dist-item">
                        <div class="app-dist-meta">
                            <span class="app-name-tag">${escapeHtml(a.app)}</span>
                            <span class="app-count-tag">${(a.packet_count || 0).toLocaleString()} pkts (${pct}%) &bull; ${formatBytes(a.total_bytes || 0)}</span>
                        </div>
                        <div class="app-bar-bg">
                            <div class="app-bar-fill ${barClass}" style="width: ${Math.min(100, Math.max(2, a.pct || 0))}%;"></div>
                        </div>
                    </div>
                `;
            }).join('');
        }
    }

    // 5. Direct Line-of-Sight RF Neighbors (0-Hop Links)
    const tbodyNeigh = document.getElementById('tbody-neighbors');
    if (tbodyNeigh && data.direct_neighbors) {
        if (data.direct_neighbors.length === 0) {
            tbodyNeigh.innerHTML = `<tr><td colspan="5" class="empty-cell">No direct RF neighbors heard in this window.</td></tr>`;
        } else {
            tbodyNeigh.innerHTML = data.direct_neighbors.map(n => `
                <tr class="font-mono">
                    <td><span style="color:var(--cyan-glow);">${escapeHtml(n.node_hex)}</span></td>
                    <td style="font-family:var(--font-sans);">${escapeHtml(n.short_name || n.long_name || '-')}</td>
                    <td>${(n.packet_count || 0).toLocaleString()}</td>
                    <td>${(n.avg_snr || 0).toFixed(1)} dB</td>
                    <td>${(n.avg_rssi || 0).toFixed(0)} dBm</td>
                </tr>
            `).join('');
        }
    }

    // 6. Critical Backbone Repeaters Table
    const tbodyRep = document.getElementById('table-repeaters') ? document.getElementById('tbody-repeaters') : null;
    if (tbodyRep) {
        if (repeaters.length === 0) {
            tbodyRep.innerHTML = `<tr><td colspan="4" class="empty-cell">No repeater relay activity in this window.</td></tr>`;
        } else {
            tbodyRep.innerHTML = repeaters.map(r => `
                <tr class="font-mono">
                    <td><span style="color:var(--cyan-glow);">${escapeHtml(r.node_hex)}</span></td>
                    <td style="font-family:var(--font-sans);">${escapeHtml(r.short_name || '-')}</td>
                    <td>${(r.relayed_packets || 0).toLocaleString()}</td>
                    <td>${(r.avg_snr || 0).toFixed(1)} dB</td>
                </tr>
            `).join('');
        }
    }

    // 7. Spatial Reach & Coverage Highlights
    if (state.spatial && state.spatial.stats) {
        renderSpatialReach(state.spatial.stats);
    } else {
        // Fetch spatial in background to populate reach card
        fetchSpatial().then(() => {
            if (state.spatial && state.spatial.stats) {
                renderSpatialReach(state.spatial.stats);
            }
        });
    }

    // 8. Top Talkers Table
    const tbody = document.getElementById('tbody-top-talkers');
    if (tbody && data.top_talkers) {
        if (data.top_talkers.length === 0) {
            tbody.innerHTML = `<tr><td colspan="7" class="empty-cell">No traffic records in the current window.</td></tr>`;
        } else {
            tbody.innerHTML = data.top_talkers.map(tt => `
                <tr class="font-mono">
                    <td><span style="color:var(--cyan-glow);">${escapeHtml(tt.node_hex)}</span></td>
                    <td style="font-family:var(--font-sans);">${escapeHtml(tt.short_name || '-')}</td>
                    <td>${(tt.packet_count || 0).toLocaleString()}</td>
                    <td>${formatBytes(tt.total_bytes || 0)}</td>
                    <td>${(tt.avg_snr || 0).toFixed(1)} dB</td>
                    <td>${(tt.avg_hops || 0).toFixed(1)}</td>
                    <td>${formatRelativeTime(tt.last_seen)}</td>
                </tr>
            `).join('');
        }
    }
}

function renderSpatialReach(stats) {
    if (!stats) return;
    const sFarthest = document.getElementById('spatial-val-farthest');
    const sPremise = document.getElementById('spatial-val-premise');
    const sGps = document.getElementById('spatial-val-gps');
    const sNode = document.getElementById('spatial-val-farthest-node');

    if (sFarthest) sFarthest.textContent = `${((stats.farthest_distance_meters || 0) / 1000).toFixed(1)} km`;
    if (sPremise) sPremise.textContent = `${stats.on_premise_count || 0}`;
    if (sGps) sGps.textContent = `${stats.total_nodes_with_gps || 0}`;
    if (sNode) {
        sNode.textContent = `Farthest node heard: ${stats.farthest_node || '-'} (${((stats.farthest_distance_meters || 0) / 1000).toFixed(1)} km)`;
    }
}

function renderHopsChart(hopsList) {
    const svg = document.getElementById('chart-hops');
    const legend = document.getElementById('hops-legend');
    if (!svg) return;

    if (hopsList.length === 0) {
        svg.innerHTML = `<text x="210" y="90" text-anchor="middle" fill="#6b7280" font-size="12">No hop data available</text>`;
        return;
    }

    const maxCount = Math.max(...hopsList.map(h => h.packet_count), 1);
    const barWidth = 48;
    const gap = 32;
    const startX = 50;
    const chartHeight = 130;

    let svgHtml = `
        <line x1="30" y1="${chartHeight + 20}" x2="390" y2="${chartHeight + 20}" stroke="rgba(255,255,255,0.1)" />
    `;

    hopsList.forEach((h, idx) => {
        const x = startX + idx * (barWidth + gap);
        const hHeight = ((h.packet_count / maxCount) * chartHeight);
        const y = chartHeight + 20 - hHeight;

        const label = (h.hops === 0) ? '0 (Direct)' : `${h.hops} Hop${h.hops > 1 ? 's' : ''}`;
        const color = (h.hops === 0) ? '#00f2fe' : (h.hops === 1 ? '#10b981' : (h.hops === 2 ? '#f59e0b' : '#ec4899'));

        svgHtml += `
            <rect x="${x}" y="${y}" width="${barWidth}" height="${hHeight}" rx="4" fill="${color}" opacity="0.85" />
            <text x="${x + barWidth/2}" y="${y - 6}" fill="#f3f4f6" font-size="11" font-family="JetBrains Mono" text-anchor="middle">${h.packet_count}</text>
            <text x="${x + barWidth/2}" y="${chartHeight + 36}" fill="#9ca3af" font-size="10" text-anchor="middle">${label}</text>
        `;
    });

    svg.innerHTML = svgHtml;
}

// ----------------------------------------------------------------------------
// HomeMesh Automation
// ----------------------------------------------------------------------------

async function fetchAutomation() {
    try {
        const res = await apiFetch('/api/automation');
        if (!res) return;
        const data = await res.json();
        state.automation = data;
        renderAutomation(data);
    } catch (e) {
        console.warn('fetchAutomation error:', e);
    }
}

function renderAutomation(data) {
    if (!data) return;

    const nodes = data.nodes || [];

    // MeshPump
    const pump = nodes.find(n => n.device_type === 'meshpump');
    const pumpHex = document.getElementById('pump-node-hex');
    const pumpStatus = document.getElementById('pump-status-pill');
    const pumpFish = document.getElementById('pump-val-fish');
    const pumpUp = document.getElementById('pump-val-up');
    const pumpCutoff = document.getElementById('pump-val-cutoff');
    const pumpLed = document.getElementById('pump-val-led');

    if (pump) {
        if (pumpHex) pumpHex.textContent = pump.node_hex;
        if (pumpStatus) {
            pumpStatus.className = `status-pill ${pump.online ? 'online' : 'offline'}`;
            pumpStatus.textContent = pump.online ? 'Online' : 'Offline';
        }
        if (pumpFish) pumpFish.textContent = pump.fish_pump_state ? '🟢 Running' : '⚪ Idle';
        if (pumpUp) pumpUp.textContent = pump.up_pump_state ? '🟢 Running' : '⚪ Idle';
        if (pumpCutoff) pumpCutoff.textContent = `${pump.up_pump_cutoff_sec || 0} sec`;
        if (pumpLed) pumpLed.textContent = `"${pump.led_message || 'OK'}"`;
    }

    // MeshRoof
    const roof = nodes.find(n => n.device_type === 'meshroof');
    const roofHex = document.getElementById('roof-node-hex');
    const roofStatus = document.getElementById('roof-status-pill');
    const roofAmplify = document.getElementById('roof-val-amplify');
    const roofWifi = document.getElementById('roof-val-wifi');
    const roofIp = document.getElementById('roof-val-ip');
    const roofTemp = document.getElementById('roof-val-temp');

    if (roof) {
        if (roofHex) roofHex.textContent = roof.node_hex;
        if (roofStatus) {
            roofStatus.className = `status-pill ${roof.online ? 'online' : 'offline'}`;
            roofStatus.textContent = roof.online ? 'Online' : 'Offline';
        }
        if (roofAmplify) roofAmplify.textContent = roof.amplify_state ? '⚡ Enabled' : 'Off';
        if (roofWifi) roofWifi.textContent = `${roof.wifi_status || 'Connected'} (${roof.wifi_rssi || 0} dBm)`;
        if (roofIp) roofIp.textContent = roof.ip_address || '--';
        if (roofTemp) roofTemp.textContent = `${(roof.cpu_temp_c || 0).toFixed(1)}°C`;
    }

    // MeshRoom
    const room = nodes.find(n => n.device_type === 'meshroom');
    const roomHex = document.getElementById('room-node-hex');
    const roomStatus = document.getElementById('room-status-pill');
    const roomTemp = document.getElementById('room-val-temp');
    const roomAcPower = document.getElementById('room-val-ac-power');
    const roomAcTarget = document.getElementById('room-val-ac-target');
    const roomTv = document.getElementById('room-val-tv');

    if (room) {
        if (roomHex) roomHex.textContent = room.node_hex;
        if (roomStatus) {
            roomStatus.className = `status-pill ${room.online ? 'online' : 'offline'}`;
            roomStatus.textContent = room.online ? 'Online' : 'Offline';
        }
        if (roomTemp) roomTemp.textContent = `${(room.room_temp_c || 0).toFixed(1)}°C`;
        if (roomAcPower) roomAcPower.textContent = room.ac_power ? '🟢 Powered ON' : '⚪ OFF';
        if (roomAcTarget) roomAcTarget.textContent = `${room.ac_target_temp || 24}°C &bull; ${room.ac_mode || 'cool'} &bull; ${room.ac_fan || 'auto'}`;
        if (roomTv) roomTv.textContent = room.tv_power ? `ON (Vol: ${room.tv_volume}, In: ${room.tv_input})` : 'OFF';
    }

    // Automation History Table
    const tbody = document.getElementById('tbody-auto-history');
    if (tbody && data.history) {
        if (data.history.length === 0) {
            tbody.innerHTML = `<tr><td colspan="8" class="empty-cell">No recent automation command events recorded.</td></tr>`;
        } else {
            tbody.innerHTML = data.history.map(ev => `
                <tr class="font-mono">
                    <td>${formatTimeOnly(ev.time)}</td>
                    <td>${escapeHtml(ev.device_type || '-')}</td>
                    <td><span class="badge ${ev.direction === 'OUT' ? 'badge-cyan' : 'badge-green'}">${escapeHtml(ev.direction)}</span></td>
                    <td>${escapeHtml(ev.subsystem || '-')}</td>
                    <td><strong>${escapeHtml(ev.command || '-')}</strong></td>
                    <td><span class="badge ${ev.status === 'OK' ? 'badge-green' : 'badge-amber'}">${escapeHtml(ev.status || 'OK')}</span></td>
                    <td>${escapeHtml(ev.initiator || 'WEB')}</td>
                    <td>${ev.rtt_ms ? ev.rtt_ms + ' ms' : '--'}</td>
                </tr>
            `).join('');
        }
    }
}

async function sendAutomationCommand(deviceType, cmd) {
    if (!state.automation || !state.automation.nodes) return;
    const node = state.automation.nodes.find(n => n.device_type === deviceType);
    if (!node) {
        alert(`Node of type ${deviceType} not discovered yet.`);
        return;
    }

    const res = await apiFetch('/api/automation/command', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            node_id: node.node_id,
            command: cmd
        })
    });

    if (res && res.ok) {
        alert(`Command '${cmd}' successfully dispatched to ${deviceType}.`);
        fetchAutomation();
    }
}

// ----------------------------------------------------------------------------
// Live Messaging & Chat
// ----------------------------------------------------------------------------

function updateDestSelector(nodes) {
    const sel = document.getElementById('chat-dest-select');
    if (!sel) return;

    const currentVal = sel.value;
    sel.innerHTML = `<option value="^all">Broadcast (^all)</option>` +
        nodes.map(n => `<option value="${n.node_hex}">${escapeHtml(n.short_name || 'Node')} (${escapeHtml(n.node_hex)})</option>`).join('');
    sel.value = currentVal || '^all';
}

async function fetchMessages() {
    try {
        const res = await apiFetch('/api/messages?limit=40');
        if (!res) return;
        const data = await res.json();
        state.messages = data || [];
        renderMessages(state.messages);
    } catch (e) {
        console.warn('fetchMessages error:', e);
    }
}

function renderMessages(messages) {
    const feed = document.getElementById('chat-messages-feed');
    if (!feed) return;

    if (messages.length === 0) {
        feed.innerHTML = `<div class="empty-chat-state">No text messages recorded on mesh yet.</div>`;
        return;
    }

    // Reverse so oldest at top, newest at bottom
    const sorted = [...messages].reverse();

    feed.innerHTML = sorted.map(m => {
        const isSelf = state.status && state.status.radio && (m.from_hex === state.status.radio.whoami_hex);
        const rowClass = isSelf ? 'outbound' : 'inbound';
        const sender = m.from_name ? `${escapeHtml(m.from_name)} (${escapeHtml(m.from_hex)})` : escapeHtml(m.from_hex);
        const dest = m.to_name ? `${escapeHtml(m.to_name)} (${escapeHtml(m.to_hex)})` : escapeHtml(m.to_hex);

        return `
            <div class="chat-msg-row ${rowClass}">
                <div class="chat-bubble font-sans">
                    ${escapeHtml(m.message)}
                </div>
                <div class="chat-msg-meta font-mono">
                    <span>${sender} &rarr; ${dest}</span>
                    <span>&bull; ${formatTimeOnly(m.time)} (ch ${m.channel})</span>
                </div>
            </div>
        `;
    }).join('');

    feed.scrollTop = feed.scrollHeight;
}

async function handleSendMessage(e) {
    e.preventDefault();
    const input = document.getElementById('input-chat-text');
    const destSel = document.getElementById('chat-dest-select');
    const chSel = document.getElementById('chat-channel-select');
    if (!input) return;

    const text = input.value.trim();
    if (!text) return;

    const destination = destSel ? destSel.value : '^all';
    const channel = chSel ? parseInt(chSel.value, 10) : 0;

    const res = await apiFetch('/api/messages/send', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ text, destination, channel })
    });

    if (res && res.ok) {
        input.value = '';
        fetchMessages();
    }
}

// ----------------------------------------------------------------------------
// Spatial Radar
// ----------------------------------------------------------------------------

async function fetchSpatial() {
    try {
        const res = await apiFetch('/api/spatial?premise_threshold=50');
        if (!res) return;
        const data = await res.json();
        state.spatial = data;
        renderSpatial(data);
    } catch (e) {
        console.warn('fetchSpatial error:', e);
    }
}

function renderSpatial(data) {
    if (!data) return;

    const refLabel = document.getElementById('spatial-ref-label');
    if (refLabel && data.reference) {
        refLabel.textContent = `Origin: ${data.reference.name}`;
    }

    // Stats
    const totalEl = document.getElementById('spatial-total-gps');
    const premiseEl = document.getElementById('spatial-on-premise');
    const farthestEl = document.getElementById('spatial-farthest-km');

    if (totalEl && data.stats) totalEl.textContent = data.stats.total_nodes_with_gps || 0;
    if (premiseEl && data.stats) premiseEl.textContent = data.stats.on_premise_count || 0;
    if (farthestEl && data.stats) {
        const km = (data.stats.farthest_distance_meters || 0) / 1000;
        farthestEl.textContent = `${km.toFixed(2)} km`;
    }

    // Table
    const tbody = document.getElementById('tbody-spatial-nodes');
    if (tbody && data.nodes) {
        if (data.nodes.length === 0) {
            tbody.innerHTML = `<tr><td colspan="5" class="empty-cell">No nodes reporting GPS coordinates yet.</td></tr>`;
        } else {
            tbody.innerHTML = data.nodes.map(n => `
                <tr class="font-mono">
                    <td><span style="color:var(--cyan-glow);">${escapeHtml(n.node_hex)}</span> (${escapeHtml(n.short_name)})</td>
                    <td>${(n.distance_meters / 1000).toFixed(2)} km (${Math.round(n.distance_meters)} m)</td>
                    <td>(${n.lat.toFixed(5)}, ${n.lon.toFixed(5)})</td>
                    <td>${n.alt || 0} m</td>
                    <td>${formatRelativeTime(n.last_seen)}</td>
                </tr>
            `).join('');
        }
    }

    // Radar SVG Layer
    const layer = document.getElementById('radar-nodes-layer');
    if (layer && data.nodes && data.reference) {
        const refLat = data.reference.lat;
        const refLon = data.reference.lon;
        const maxRadiusKm = 25.0; // Outer circle is ~20-25 km

        layer.innerHTML = data.nodes.map(n => {
            // Rough planar projection for local radar
            const dLatKm = (n.lat - refLat) * 111.0;
            const dLonKm = (n.lon - refLon) * (111.0 * Math.cos(refLat * Math.PI / 180.0));

            const scale = 200.0 / maxRadiusKm;
            let cx = dLonKm * scale;
            let cy = -dLatKm * scale; // Invert Y for screen coordinates

            // Clamp inside radar boundary
            const dist = Math.sqrt(cx*cx + cy*cy);
            if (dist > 210) {
                cx = (cx / dist) * 210;
                cy = (cy / dist) * 210;
            }

            return `
                <circle cx="${cx.toFixed(1)}" cy="${cy.toFixed(1)}" r="4.5" class="radar-node-blip">
                    <title>${escapeHtml(n.short_name)} (${(n.distance_meters/1000).toFixed(2)} km)</title>
                </circle>
                <text x="${(cx + 6).toFixed(1)}" y="${(cy + 3).toFixed(1)}" fill="#f3f4f6" font-size="9" font-family="JetBrains Mono">${escapeHtml(n.short_name)}</text>
            `;
        }).join('');
    }
}

// ----------------------------------------------------------------------------
// SQL Query Console
// ----------------------------------------------------------------------------

async function executeSqlQuery() {
    const input = document.getElementById('sql-query-input');
    const statusEl = document.getElementById('sql-execution-status');
    const thead = document.getElementById('thead-sql');
    const tbody = document.getElementById('tbody-sql');
    if (!input) return;

    const query = input.value.trim();
    if (!query) return;

    if (statusEl) statusEl.textContent = 'Executing query...';

    const res = await apiFetch('/api/db/query', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ query })
    });

    if (!res) return;

    const data = await res.json();
    if (res.ok) {
        if (statusEl) statusEl.textContent = `Returned ${data.row_count} rows`;

        // Build header
        thead.innerHTML = '<tr>' + (data.columns || []).map(c => `<th>${escapeHtml(c)}</th>`).join('') + '</tr>';

        // Build rows
        if (!data.rows || data.rows.length === 0) {
            tbody.innerHTML = `<tr><td colspan="${data.columns.length}" class="empty-cell">Query executed successfully (0 rows returned).</td></tr>`;
        } else {
            tbody.innerHTML = data.rows.map(row => `
                <tr class="font-mono">
                    ${row.map(val => `<td>${escapeHtml(val)}</td>`).join('')}
                </tr>
            `).join('');
        }
    } else {
        if (statusEl) statusEl.textContent = `Error: ${data.error || 'Execution failed'}`;
        tbody.innerHTML = `<tr><td class="empty-cell" style="color:var(--danger-red);">${escapeHtml(data.error || 'Execution failed')}</td></tr>`;
    }
}

// ----------------------------------------------------------------------------
// Server-Sent Events (SSE) Live Feed
// ----------------------------------------------------------------------------

function setupSse() {
    if (eventSource) {
        eventSource.close();
    }

    eventSource = new EventSource('/api/events');

    eventSource.onopen = () => {
        state.sseConnected = true;
        const dot = document.getElementById('dot-sse');
        const status = document.getElementById('val-sse-status');
        if (dot) dot.className = 'status-dot dot-online';
        if (status) status.textContent = 'Live Stream';
    };

    eventSource.addEventListener('packet', (e) => {
        try {
            const pkt = JSON.parse(e.data);
            if (!state.snifferPaused) {
                state.packets.unshift(pkt);
                if (state.packets.length > 200) state.packets.pop();
                if (state.currentTab === 'sniffer') {
                    renderPackets(state.packets);
                }
            }
        } catch (err) {}
    });

    eventSource.addEventListener('message', (e) => {
        try {
            const msg = JSON.parse(e.data);
            state.messages.push(msg);
            if (state.currentTab === 'messaging') {
                renderMessages(state.messages);
            }
        } catch (err) {}
    });

    eventSource.onerror = () => {
        state.sseConnected = false;
        const dot = document.getElementById('dot-sse');
        const status = document.getElementById('val-sse-status');
        if (dot) dot.className = 'status-dot dot-stale';
        if (status) status.textContent = 'Reconnecting...';
    };
}

// ----------------------------------------------------------------------------
// Initialization & Event Binding
// ----------------------------------------------------------------------------

document.addEventListener('DOMContentLoaded', () => {
    // 1. Check auth status
    checkAuthStatus();

    // 2. Initial fetch (prioritizes analytics as default tab)
    fetchStatus();
    fetchAnalytics();
    fetchSpatial();
    fetchNodes();

    // 3. Setup SSE
    setupSse();

    // 4. Tab Navigation
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const tabName = btn.getAttribute('data-tab');
            if (!tabName) return;

            document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
            document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));

            btn.classList.add('active');
            const pane = document.getElementById(`tab-${tabName}`);
            if (pane) pane.classList.add('active');

            state.currentTab = tabName;

            // Load data for newly selected tab
            if (tabName === 'fleet') fetchNodes();
            else if (tabName === 'sniffer') fetchPackets();
            else if (tabName === 'analytics') fetchAnalytics();
            else if (tabName === 'automation') fetchAutomation();
            else if (tabName === 'messaging') fetchMessages();
            else if (tabName === 'spatial') fetchSpatial();
        });
    });

    // 5. Global controls
    document.getElementById('refresh-btn').addEventListener('click', () => {
        fetchStatus();
        if (state.currentTab === 'fleet') fetchNodes();
        else if (state.currentTab === 'sniffer') fetchPackets();
        else if (state.currentTab === 'analytics') fetchAnalytics();
        else if (state.currentTab === 'automation') fetchAutomation();
        else if (state.currentTab === 'messaging') fetchMessages();
        else if (state.currentTab === 'spatial') fetchSpatial();
    });

    // 6. Auth controls
    document.getElementById('btn-auth-toggle').addEventListener('click', () => {
        if (!state.auth.auth_required) return;
        if (state.auth.authenticated) {
            handleLogout();
        } else {
            openAuthModal();
        }
    });

    document.getElementById('form-auth-login').addEventListener('submit', handleLoginSubmit);
    document.getElementById('btn-auth-cancel').addEventListener('click', closeAuthModal);
    document.getElementById('btn-close-auth-modal').addEventListener('click', closeAuthModal);

    // Modal Close
    document.getElementById('btn-close-node-modal').addEventListener('click', () => {
        document.getElementById('modal-node-detail').classList.add('hidden');
    });

    // 7. Fleet Filters
    document.getElementById('node-search-input').addEventListener('input', (e) => {
        state.filters.nodeSearch = e.target.value;
        renderNodes(state.nodes);
    });

    document.getElementById('role-filter-select').addEventListener('change', (e) => {
        state.filters.role = e.target.value;
        renderNodes(state.nodes);
    });

    document.getElementById('status-filter-select').addEventListener('change', (e) => {
        state.filters.status = e.target.value;
        renderNodes(state.nodes);
    });

    // 8. Sniffer Controls
    document.getElementById('btn-toggle-sniffer').addEventListener('click', function() {
        state.snifferPaused = !state.snifferPaused;
        if (state.snifferPaused) {
            this.innerHTML = `<span class="status-dot dot-stale"></span> Resume Stream`;
            this.classList.remove('active');
        } else {
            this.innerHTML = `<span class="status-dot dot-online"></span> Pause Stream`;
            this.classList.add('active');
        }
    });

    document.getElementById('btn-clear-sniffer').addEventListener('click', () => {
        state.packets = [];
        renderPackets([]);
    });

    document.getElementById('packet-filter-input').addEventListener('input', (e) => {
        state.filters.packetFilter = e.target.value;
        renderPackets(state.packets);
    });

    document.getElementById('portnum-filter-select').addEventListener('change', (e) => {
        state.filters.portnum = parseInt(e.target.value, 10);
        fetchPackets();
    });

    // 9. Automation buttons
    document.querySelectorAll('.btn-ctrl[data-cmd]').forEach(btn => {
        btn.addEventListener('click', () => {
            const cmd = btn.getAttribute('data-cmd');
            const target = btn.getAttribute('data-target');
            if (cmd && target) {
                sendAutomationCommand(target, cmd);
            }
        });
    });

    // 10. Chat form
    document.getElementById('form-send-chat').addEventListener('submit', handleSendMessage);

    // 11. SQL Console
    document.getElementById('btn-run-sql').addEventListener('click', executeSqlQuery);

    document.querySelectorAll('.btn-preset').forEach(btn => {
        btn.addEventListener('click', () => {
            const sql = btn.getAttribute('data-sql');
            const input = document.getElementById('sql-query-input');
            if (input && sql) {
                input.value = sql;
                executeSqlQuery();
            }
        });
    });

    // 12. Horizon Selector Controls
    document.querySelectorAll('.btn-time-window').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.btn-time-window').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            const h = parseInt(btn.getAttribute('data-hours') || '24');
            state.analyticsHours = h;
            fetchAnalytics();
        });
    });

    const jumpSpatialBtn = document.getElementById('btn-jump-spatial');
    if (jumpSpatialBtn) {
        jumpSpatialBtn.addEventListener('click', () => {
            const spatialTabBtn = document.querySelector('.tab-btn[data-tab="spatial"]');
            if (spatialTabBtn) spatialTabBtn.click();
        });
    }

    // 13. Periodic Refresh
    pollTimer = setInterval(() => {
        fetchStatus();
        if (state.currentTab === 'analytics') fetchAnalytics();
        else if (state.currentTab === 'fleet') fetchNodes();
        else if (state.currentTab === 'automation') fetchAutomation();
    }, 5000);
});
)raw_asset";

} // namespace assets

#endif // WEBASSETS_HXX

/*
 * Local variables:
 * mode: C++
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
