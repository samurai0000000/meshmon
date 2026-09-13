/*
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
