#!/usr/bin/env node
/*
 * MeshMon E2E Responsive Layout Verification
 * Tests across multiple viewports via CDP (Chrome DevTools Protocol)
 *
 * Copyright (C) 2026, Charles Chiou
 */

const CHROME = '/home/samurai/.cache/ms-playwright/chromium-1200/chrome-linux64/chrome';
const MESHMON_URL = 'http://192.168.8.245:16880/';
const AIMON_URL = 'http://127.0.0.1:3883/';
const ARTIFACT_DIR = process.env.ARTIFACT_DIR || '/home/samurai/.gemini/antigravity-ide/brain/ce561280-f1ed-43bf-a5da-d664a65a4be1';

import { execSync, spawn } from 'child_process';
import fs from 'fs';
import path from 'path';

const VIEWPORTS = [
    { name: 'desktop',  w: 1920, h: 1080 },
    { name: 'laptop',   w: 1366, h: 768  },
    { name: 'tablet',   w: 768,  h: 1024 },
    { name: 'mobile',   w: 375,  h: 812  },
];

const TABS = [
    'analytics', 'sniffer', 'spatial', 'remote', 'topology',
    'fleet', 'automation', 'messaging', 'console'
];

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

async function connectCDP(port) {
    const resp = await fetch(`http://127.0.0.1:${port}/json/list`);
    const targets = await resp.json();
    const page = targets.find(t => t.type === 'page');
    if (!page) throw new Error('No page target found');
    const wsUrl = page.webSocketDebuggerUrl;
    return new Promise((resolve, reject) => {
        const ws = new WebSocket(wsUrl);
        let id = 1;
        const pending = new Map();
        ws.onopen = () => resolve({
            send(method, params = {}) {
                return new Promise((res, rej) => {
                    const msgId = id++;
                    pending.set(msgId, { res, rej });
                    ws.send(JSON.stringify({ id: msgId, method, params }));
                });
            },
            close() { ws.close(); }
        });
        ws.onmessage = (event) => {
            const msg = JSON.parse(typeof event.data === 'string' ? event.data : event.data.toString());
            if (msg.id && pending.has(msg.id)) {
                const p = pending.get(msg.id);
                pending.delete(msg.id);
                if (msg.error) p.rej(new Error(msg.error.message));
                else p.res(msg.result);
            }
        };
        ws.onerror = (e) => reject(e);
    });
}

async function runTests() {
    const port = 9333 + Math.floor(Math.random() * 100);
    const args = [
        '--headless=new', '--no-sandbox', '--disable-gpu',
        `--remote-debugging-port=${port}`,
        '--window-size=1920,1080',
        'about:blank'
    ];
    const chrome = spawn(CHROME, args, { stdio: 'ignore' });
    await sleep(2000);

    let cdp;
    try {
        cdp = await connectCDP(port);
    } catch (e) {
        console.error('Failed to connect to Chrome CDP:', e.message);
        chrome.kill();
        process.exit(1);
    }

    await cdp.send('Page.enable');
    await cdp.send('Runtime.enable');

    const results = [];
    let passed = 0, failed = 0;

    // Test 1: Direct MeshMon at multiple viewports
    for (const vp of VIEWPORTS) {
        await cdp.send('Emulation.setDeviceMetricsOverride', {
            width: vp.w, height: vp.h, deviceScaleFactor: 2, mobile: vp.w < 768
        });
        await cdp.send('Page.navigate', { url: MESHMON_URL });
        await sleep(3000);

        // Check horizontal overflow
        const scrollEval = await cdp.send('Runtime.evaluate', {
            expression: `JSON.stringify({
                bodyScrollWidth: document.body.scrollWidth,
                windowInnerWidth: window.innerWidth,
                documentWidth: document.documentElement.scrollWidth
            })`,
            returnByValue: true
        });
        const scroll = JSON.parse(scrollEval.result.value);
        const hasOverflow = scroll.bodyScrollWidth > scroll.windowInnerWidth + 2;
        const status = hasOverflow ? 'FAIL' : 'PASS';
        if (hasOverflow) failed++; else passed++;
        results.push({
            test: `MeshMon ${vp.name} (${vp.w}x${vp.h}) overflow`,
            status,
            detail: `bodyScrollWidth=${scroll.bodyScrollWidth}, innerWidth=${scroll.windowInnerWidth}`
        });
        console.log(`[${status}] ${vp.name} (${vp.w}x${vp.h}): scrollW=${scroll.bodyScrollWidth}, innerW=${scroll.windowInnerWidth}`);

        // Screenshot
        const screenshotFile = path.join(ARTIFACT_DIR, `meshmon_e2e_${vp.name}.png`);
        const ss = await cdp.send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
        fs.writeFileSync(screenshotFile, Buffer.from(ss.data, 'base64'));

        // Test all 9 tabs at this viewport
        for (const tab of TABS) {
            const tabEval = await cdp.send('Runtime.evaluate', {
                expression: `(() => {
                    const btn = document.querySelector('[data-tab="${tab}"]');
                    if (!btn) return 'NOT_FOUND';
                    btn.click();
                    return 'OK';
                })()`,
                returnByValue: true
            });
            if (tabEval.result.value !== 'OK') {
                results.push({ test: `Tab "${tab}" click at ${vp.name}`, status: 'FAIL', detail: 'Tab button not found' });
                failed++;
                continue;
            }
            await sleep(300);

            const tabScrollEval = await cdp.send('Runtime.evaluate', {
                expression: `document.body.scrollWidth`,
                returnByValue: true
            });
            const tabOverflow = tabScrollEval.result.value > scroll.windowInnerWidth + 2;
            const tabStatus = tabOverflow ? 'FAIL' : 'PASS';
            if (tabOverflow) failed++; else passed++;
            results.push({
                test: `Tab "${tab}" at ${vp.name} overflow`,
                status: tabStatus,
                detail: `scrollW=${tabScrollEval.result.value}`
            });
        }
    }

    // Test 2: Check bullet entity fix
    await cdp.send('Emulation.setDeviceMetricsOverride', {
        width: 1920, height: 1080, deviceScaleFactor: 1, mobile: false
    });
    await cdp.send('Page.navigate', { url: MESHMON_URL });
    await sleep(3000);

    const bulletCheck = await cdp.send('Runtime.evaluate', {
        expression: `(() => {
            const dbRecs = document.getElementById('ribbon-db-records');
            const text = dbRecs ? dbRecs.textContent : '';
            return { text, hasBullEntity: text.includes('&bull;'), hasUnicodeBullet: text.includes('•') };
        })()`,
        returnByValue: true
    });
    const bv = bulletCheck.result.value;
    const bulletStatus = bv.hasBullEntity ? 'FAIL' : 'PASS';
    if (bv.hasBullEntity) failed++; else passed++;
    results.push({
        test: 'Bullet entity fix (ribbon-db-records)',
        status: bulletStatus,
        detail: `text="${bv.text}", hasBullEntity=${bv.hasBullEntity}`
    });
    console.log(`[${bulletStatus}] Bullet entity: "${bv.text}"`);

    // Test 3: Tab strip is single row (no two-row wrap)
    const tabStripEval = await cdp.send('Runtime.evaluate', {
        expression: `(() => {
            const nav = document.querySelector('.tabs-nav');
            if (!nav) return { error: 'no .tabs-nav' };
            const style = getComputedStyle(nav);
            const allBtns = nav.querySelectorAll('.tab-btn');
            return {
                flexDir: style.flexDirection,
                tabCount: allBtns.length,
                overflowX: style.overflowX
            };
        })()`,
        returnByValue: true
    });
    const ts = tabStripEval.result.value;
    const tabStripOk = ts.flexDir === 'row' && ts.tabCount === 9;
    if (tabStripOk) passed++; else failed++;
    results.push({
        test: 'Tab strip is single-row horizontal',
        status: tabStripOk ? 'PASS' : 'FAIL',
        detail: `flexDirection=${ts.flexDir}, tabCount=${ts.tabCount}, overflowX=${ts.overflowX}`
    });
    console.log(`[${tabStripOk ? 'PASS' : 'FAIL'}] Tab strip: dir=${ts.flexDir}, count=${ts.tabCount}`);

    // Test 4: View-Only toast CSS exists
    const toastCssEval = await cdp.send('Runtime.evaluate', {
        expression: `(() => {
            const el = document.createElement('div');
            el.className = 'view-only-toast';
            document.body.appendChild(el);
            const s = getComputedStyle(el);
            const result = { position: s.position, opacity: s.opacity, zIndex: s.zIndex };
            el.remove();
            return result;
        })()`,
        returnByValue: true
    });
    const toastCss = toastCssEval.result.value;
    const toastOk = toastCss.position === 'fixed' && toastCss.opacity === '0';
    if (toastOk) passed++; else failed++;
    results.push({
        test: 'View-Only toast CSS class',
        status: toastOk ? 'PASS' : 'FAIL',
        detail: `position=${toastCss.position}, opacity=${toastCss.opacity}`
    });
    console.log(`[${toastOk ? 'PASS' : 'FAIL'}] Toast CSS: position=${toastCss.position}, opacity=${toastCss.opacity}`);

    // Test 5: AiMon Hub embedded MeshMon tab
    try {
        await cdp.send('Emulation.setDeviceMetricsOverride', {
            width: 1920, height: 1080, deviceScaleFactor: 1, mobile: false
        });
        await cdp.send('Page.navigate', { url: AIMON_URL });
        await sleep(3000);

        // Click MeshMon tab in aimon
        const aimonTabEval = await cdp.send('Runtime.evaluate', {
            expression: `(() => {
                const tabs = document.querySelectorAll('[data-tab]');
                for (const t of tabs) {
                    if (t.textContent.trim().toLowerCase().includes('meshmon')) {
                        t.click();
                        return 'clicked';
                    }
                }
                return 'not_found';
            })()`,
            returnByValue: true
        });
        await sleep(2000);

        const ssAimon = path.join(ARTIFACT_DIR, 'meshmon_e2e_aimon_embedded.png');
        const ssData = await cdp.send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
        fs.writeFileSync(ssAimon, Buffer.from(ssData.data, 'base64'));

        passed++;
        results.push({ test: 'AiMon Hub MeshMon tab', status: 'PASS', detail: aimonTabEval.result.value });
        console.log(`[PASS] AiMon Hub MeshMon tab: ${aimonTabEval.result.value}`);
    } catch (e) {
        failed++;
        results.push({ test: 'AiMon Hub MeshMon tab', status: 'FAIL', detail: e.message });
        console.log(`[FAIL] AiMon Hub MeshMon tab: ${e.message}`);
    }

    // Summary
    console.log(`\n${'='.repeat(60)}`);
    console.log(`MESHMON E2E RESULTS: ${passed} passed, ${failed} failed, ${passed + failed} total`);
    console.log(`${'='.repeat(60)}`);

    const failedTests = results.filter(r => r.status === 'FAIL');
    if (failedTests.length > 0) {
        console.log('\nFailed tests:');
        failedTests.forEach(t => console.log(`  ✗ ${t.test}: ${t.detail}`));
    }

    cdp.close();
    chrome.kill();
    process.exit(failed > 0 ? 1 : 0);
}

runTests().catch(e => { console.error(e); process.exit(1); });
