// ============================================================
// WiFi Secure Chat — Front-End Application Logic
// Handles polling, real-time events, typing detection, and UI.
// ============================================================

// --- Application State ---
let sessionToken = localStorage.getItem("chat_session_token") || null;
let username = localStorage.getItem("chat_username") || null;
let userState = "idle"; // "idle" or "chatting"
let pollIntervalId = null;
let lastMessageCount = 0;

// Typing indicator states
let isLocalTyping = false;
let typingTimeout = null;

// Telemetry state
let lastPollTimestamp = 0;
let requestCount = 0;
let totalLatency = 0;
let latencyChart = null;

// --- DOM Elements ---
const loginContainer = document.getElementById("login-container");
const chatContainer = document.getElementById("chat-container");
const loginForm = document.getElementById("login-form");
const usernameInput = document.getElementById("username");
const passwordInput = document.getElementById("password");
const loginError = document.getElementById("login-error");

const userBadge = document.getElementById("user-badge");
const btnLogout = document.getElementById("btn-logout");
const btnEndChat = document.getElementById("btn-end-chat");
const btnOpenSidebar = document.getElementById("btn-open-sidebar");
const btnCloseSidebar = document.getElementById("btn-close-sidebar");
const perfSidebar = document.getElementById("perf-sidebar");

// User List panel
const usersList = document.getElementById("users-list");

// Chat area panel
const chatIdle = document.getElementById("chat-idle");
const chatActive = document.getElementById("chat-active");
const chatPartnerName = document.getElementById("chat-partner-name");
const chatHistoryWrapper = document.getElementById("chat-history-wrapper");
const chatHistory = document.getElementById("chat-history");
const chatForm = document.getElementById("chat-form");
const chatInput = document.getElementById("chat-input");
const typingIndicator = document.getElementById("typing-indicator");

// Performance sidebar
const sysEnv = document.getElementById("sys-env");
const sysPlatform = document.getElementById("sys-platform");
const sysUptime = document.getElementById("sys-uptime");
const sysUsers = document.getElementById("sys-users");
const livePing = document.getElementById("live-ping");
const liveRam = document.getElementById("live-ram");
const liveCpu = document.getElementById("live-cpu");
const liveRequests = document.getElementById("live-requests");
const liveAvgLatency = document.getElementById("live-avg-latency");
const perfLogBody = document.getElementById("perf-log-body");

// Stress testing DOM references
const btnLoadTest = document.getElementById("btn-load-test");
const loadCountInput = document.getElementById("load-count");
const loadConcurrency = document.getElementById("load-concurrency");
const loadTestResults = document.getElementById("load-test-results");



// ============================================================
// Initialization & Authentication
// ============================================================
document.addEventListener("DOMContentLoaded", () => {
    // Check if user is already logged in
    if (sessionToken && username) {
        showMainApp();
    } else {
        showLogin();
    }

    // Event Listeners
    loginForm.addEventListener("submit", handleLogin);
    btnLogout.addEventListener("click", handleLogout);
    btnEndChat.addEventListener("click", handleEndChat);
    chatForm.addEventListener("submit", handleSendMessage);
    chatInput.addEventListener("input", handleTypingState);
    if (btnLoadTest) {
        btnLoadTest.addEventListener("click", runStressTest);
    }

    // Sidebar Toggles
    btnOpenSidebar.addEventListener("click", () => {
        perfSidebar.classList.add("open");
        document.body.classList.add("sidebar-open");
        refreshPerformanceData();
    });
    btnCloseSidebar.addEventListener("click", () => {
        perfSidebar.classList.remove("open");
        document.body.classList.remove("sidebar-open");
    });

    // Initialize Chart.js
    initChart();
});


function showLogin() {
    loginContainer.classList.remove("hidden");
    chatContainer.classList.add("hidden");
    stopPolling();
}

function showMainApp() {
    loginContainer.classList.add("hidden");
    chatContainer.classList.remove("hidden");
    userBadge.textContent = `User: ${username}`;
    
    // Start heartbeat polling
    startPolling();
}

async function handleLogin(e) {
    e.preventDefault();
    loginError.classList.add("hidden");

    const name = usernameInput.value.trim();
    const password = passwordInput.value;

    if (!name) return;

    try {
        const response = await fetch("/api/login", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ name, password })
        });

        const data = await response.json();

        if (response.ok && data.status === "success") {
            sessionToken = data.session_token;
            username = data.name;
            localStorage.setItem("chat_session_token", sessionToken);
            localStorage.setItem("chat_username", username);
            
            usernameInput.value = "";
            passwordInput.value = "";
            showMainApp();
        } else {
            loginError.textContent = data.message || "Login failed.";
            loginError.classList.remove("hidden");
        }
    } catch (err) {
        loginError.textContent = "Server unreachable. Make sure backend is running.";
        loginError.classList.remove("hidden");
        console.error("Login Error:", err);
    }
}


// ============================================================
// API Heartbeat & Polling Loop
// ============================================================
function startPolling() {
    // Run immediate poll, then repeat every 900ms (fast response, low ESP32 load)
    pollServer();
    pollIntervalId = setInterval(pollServer, 900);
}

function stopPolling() {
    if (pollIntervalId) {
        clearInterval(pollIntervalId);
        pollIntervalId = null;
    }
}

async function pollServer() {
    if (!sessionToken) return;

    const start = performance.now();

    try {
        const response = await fetch("/api/poll", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ session_token: sessionToken })
        });

        // Compute ping (Round Trip Time)
        const latency = Math.round(performance.now() - start);
        livePing.textContent = `${latency} ms`;

        if (!response.ok) {
            // If session expired (401), force log out
            if (response.status === 401) {
                forceLogoutCleanup("Session expired or timed out.");
            }
            return;
        }

        const data = await response.json();
        if (data.status === "success") {
            updateDashboard(data);
        }
    } catch (err) {
        console.warn("Polling network gap...", err);
        livePing.textContent = "Timeout ⚠️";
    }
}


// ============================================================
// Dashboard Update Rendering
// ============================================================
function updateDashboard(data) {
    // 1. Update Telemetry Panels
    const sys = data.system || {};
    sysEnv.textContent = sys.env_label || sys.environment || "LOCAL";
    sysPlatform.textContent = sys.platform || "ESP32";
    sysUptime.textContent = formatUptime(sys.uptime_s || 0);
    sysUsers.textContent = sys.active_users || "0";
    liveRam.textContent = sys.ram_usage_pct ? `${sys.ram_used_mb}MB (${sys.ram_usage_pct}%)` : "N/A";
    liveCpu.textContent = sys.cpu_usage_pct !== undefined ? `${sys.cpu_usage_pct}%` : "N/A";

    // Update global environment badge
    document.getElementById("env-badge").textContent = sys.env_label || "WiFi Server";


    // 2. Render Online Users List
    renderUsersList(data.users || []);

    // 3. Update Chat Panel States
    userState = data.user_state;
    if (userState === "chatting") {
        chatIdle.classList.add("hidden");
        chatActive.classList.remove("hidden");
        btnEndChat.classList.remove("hidden"); // Show End Chat button top left
        
        chatPartnerName.textContent = data.partner_name;
        
        // Show partner typing indicator
        if (data.partner_typing) {
            typingIndicator.classList.remove("invisible");
        } else {
            typingIndicator.classList.add("invisible");
        }

        // Render messages
        renderMessages(data.messages || []);
    } else {
        chatIdle.classList.remove("hidden");
        chatActive.classList.add("hidden");
        btnEndChat.classList.add("hidden"); // Hide End Chat button top left
        lastMessageCount = 0;
    }
}


// ============================================================
// Core Features Implementation
// ============================================================

// Render Users List
function renderUsersList(users) {
    usersList.innerHTML = "";
    
    if (users.length === 0) {
        const p = document.createElement("p");
        p.className = "loading-text";
        p.textContent = "No other users online.";
        usersList.appendChild(p);
        return;
    }

    users.forEach(u => {
        const card = document.createElement("div");
        card.className = "user-item-card";

        const top = document.createElement("div");
        top.className = "user-item-top";

        const nameSpan = document.createElement("span");
        nameSpan.className = "user-item-name";
        nameSpan.textContent = u.name;

        const badge = document.createElement("span");
        badge.className = `status-badge ${u.status}`;
        badge.textContent = u.status;

        top.appendChild(nameSpan);
        top.appendChild(badge);
        card.appendChild(top);

        const connectBtn = document.createElement("button");
        connectBtn.className = "btn btn-primary btn-connect";
        
        if (u.status === "busy") {
            connectBtn.disabled = true;
            connectBtn.textContent = "In Chat 🔒";
        } else {
            connectBtn.textContent = "Connect 🔌";
            connectBtn.onclick = () => initiateConnection(u.id);
        }

        card.appendChild(connectBtn);
        usersList.appendChild(card);
    });
}

// Request Chat Connection
async function initiateConnection(targetId) {
    if (!sessionToken) return;

    try {
        const response = await fetch("/api/connect", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                session_token: sessionToken,
                target_id: targetId
            })
        });
        const data = await response.json();
        if (response.ok && data.status === "success") {
            chatHistory.innerHTML = "";
            lastMessageCount = 0;
        } else {
            alert(data.message || "Failed to connect.");
        }
    } catch (err) {
        console.error("Connect error:", err);
    }
}

// Render Message Log
function renderMessages(messages) {
    // Only repaint and scroll if message list length changes
    if (messages.length === lastMessageCount) return;

    chatHistory.innerHTML = "";

    messages.forEach(m => {
        const row = document.createElement("div");
        
        // Style row depending on sender
        if (m.sender === "System") {
            row.className = "msg-row msg-system";
        } else if (m.sender === username) {
            row.className = "msg-row msg-self";
        } else {
            row.className = "msg-row msg-partner";
        }

        // Meta tag (Sender Name & Time)
        const meta = document.createElement("div");
        meta.className = "msg-meta";
        meta.textContent = `${m.sender} • ${m.time}`;
        row.appendChild(meta);

        // Message bubble (escaped safe HTML injection via textContent)
        const bubble = document.createElement("div");
        bubble.className = "msg-bubble";
        bubble.textContent = m.text;
        row.appendChild(bubble);

        chatHistory.appendChild(row);
    });

    lastMessageCount = messages.length;
    scrollChatToBottom();
}

// Scroll chat panel to bottom
function scrollChatToBottom() {
    chatHistoryWrapper.scrollTop = chatHistoryWrapper.scrollHeight;
}

// Send Message
async function handleSendMessage(e) {
    e.preventDefault();
    const text = chatInput.value.trim();
    if (!text || !sessionToken) return;

    // Reset typing status on send
    resetTypingIndicator();

    const startReq = performance.now();

    try {
        const response = await fetch("/api/send", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                session_token: sessionToken,
                text: text
            })
        });

        const data = await response.json();
        const elapsedReq = (performance.now() - startReq) / 1000.0;

        if (response.ok && data.status === "success") {
            chatInput.value = "";
            
            // Extract performance telemetry
            const p = data.perf || {
                type: "inference",
                latency_s: 0.001,
                text_length: text.length,
                sentiment: "Neutral",
                cpu_after_pct: 0,
                ram_after_mb: 0,
                ram_delta_mb: 0,
                status: "success"
            };

            requestCount++;
            totalLatency += p.latency_s;

            // Live updates
            liveRequests.textContent = requestCount;
            liveAvgLatency.textContent = (totalLatency / requestCount).toFixed(3) + " s";
            if (p.cpu_after_pct) liveCpu.textContent = `${p.cpu_after_pct}%`;
            if (p.ram_after_mb) liveRam.textContent = `${p.ram_after_mb} MB`;

            // Draw to graph and prepend log row
            addChartPoint(p.latency_s);
            addLogRow(p);

            // Fast poll request to render sent message immediately
            pollServer();
        } else {
            alert(data.message || "Error sending message.");
        }
    } catch (err) {
        console.error("Send error:", err);
    }
}



// ============================================================
// Typing Detection and Throttling
// ============================================================
function handleTypingState() {
    if (!sessionToken) return;

    if (!isLocalTyping) {
        isLocalTyping = true;
        sendTypingStatus(true);
    }

    // Debounce resetting typing state
    clearTimeout(typingTimeout);
    typingTimeout = setTimeout(() => {
        isLocalTyping = false;
        sendTypingStatus(false);
    }, 2000);
}

async function sendTypingStatus(typingVal) {
    try {
        await fetch("/api/typing", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                session_token: sessionToken,
                typing: typingVal
            })
        });
    } catch (err) {
        console.error("Typing status error:", err);
    }
}

function resetTypingIndicator() {
    isLocalTyping = false;
    clearTimeout(typingTimeout);
    sendTypingStatus(false);
}


// ============================================================
// Termination & Cleanup Handlers
// ============================================================
async function handleEndChat() {
    if (!sessionToken || !confirm("Are you sure you want to end this chat?")) return;

    try {
        const response = await fetch("/api/end_chat", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ session_token: sessionToken })
        });
        const data = await response.json();
        if (response.ok && data.status === "success") {
            // Immediate poll to sync UI back to idle
            pollServer();
        }
    } catch (err) {
        console.error("End chat error:", err);
    }
}

async function handleLogout() {
    if (!sessionToken) return;

    try {
        await fetch("/api/logout", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ session_token: sessionToken })
        });
    } catch (err) {
        console.error("Logout error:", err);
    }

    forceLogoutCleanup();
}

function forceLogoutCleanup(message = "") {
    stopPolling();
    sessionToken = null;
    username = null;
    localStorage.removeItem("chat_session_token");
    localStorage.removeItem("chat_username");
    
    if (message) alert(message);
    showLogin();
}

// Helpers
function formatUptime(seconds) {
    const s = Math.floor(seconds % 60);
    const m = Math.floor((seconds / 60) % 60);
    const h = Math.floor(seconds / 3600);
    return `${h}h ${m}m ${s}s`;
}

// ============================================================
// Performance Telemetry & Stress Testing Helpers
// ============================================================

function initChart() {
    const canvas = document.getElementById("latency-chart");
    if (!canvas) return;
    
    // Check if Chart.js is loaded
    if (typeof Chart === "undefined") {
        console.warn("Chart.js not loaded. Displaying text fallback.");
        canvas.parentElement.innerHTML = "<div class='loading-text'>Chart.js offline. Latency graph unavailable.</div>";
        return;
    }

    const ctx = canvas.getContext("2d");
    latencyChart = new Chart(ctx, {
        type: "line",
        data: {
            labels: [],
            datasets: [{
                label: "Inference Latency (ms)",
                data: [],
                borderColor: "#6366f1",
                backgroundColor: "rgba(99, 102, 241, 0.1)",
                fill: true,
                tension: 0.4,
                pointBackgroundColor: "#a855f7",
                pointBorderColor: "#6366f1",
                pointRadius: 3,
                borderWidth: 2,
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: { display: false },
                tooltip: {
                    backgroundColor: "rgba(17, 24, 39, 0.9)",
                    titleColor: "#f1f5f9",
                    bodyColor: "#94a3b8",
                    borderColor: "rgba(255,255,255,0.08)",
                    borderWidth: 1,
                    cornerRadius: 8,
                }
            },
            scales: {
                x: {
                    display: true,
                    grid: { color: "rgba(255,255,255,0.04)" },
                    ticks: { color: "#64748b", font: { size: 9 } },
                },
                y: {
                    display: true,
                    beginAtZero: true,
                    grid: { color: "rgba(255,255,255,0.04)" },
                    ticks: {
                        color: "#64748b",
                        font: { size: 9 },
                        callback: (v) => v.toFixed(1) + " ms"
                    },
                }
            }
        }
    });
}

function addChartPoint(latencyS) {
    if (!latencyChart) return;
    const latencyMs = latencyS * 1000.0;
    const label = `#${requestCount}`;
    latencyChart.data.labels.push(label);
    latencyChart.data.datasets[0].data.push(latencyMs);
    if (latencyChart.data.labels.length > 20) {
        latencyChart.data.labels.shift();
        latencyChart.data.datasets[0].data.shift();
    }
    latencyChart.update("none");
}

function addLogRow(perf) {
    if (!perfLogBody) return;
    const emptyRow = perfLogBody.querySelector(".empty-row");
    if (emptyRow) emptyRow.remove();

    const row = document.createElement("tr");
    const time = new Date().toLocaleTimeString();
    const statusClass = perf.status === "success" ? "status-ok" : "status-err";
    const latencyMs = (perf.latency_s * 1000.0).toFixed(2);

    row.innerHTML = `
        <td>${requestCount}</td>
        <td>${time}</td>
        <td>${perf.text_length || 0} chars</td>
        <td>${latencyMs} ms</td>
        <td>${perf.cpu_after_pct !== undefined ? perf.cpu_after_pct + "%" : "N/A"}</td>
        <td>${perf.ram_delta_mb !== undefined ? (perf.ram_delta_mb >= 0 ? "+" : "") + perf.ram_delta_mb + " MB" : "N/A"}</td>
        <td class="${statusClass}">${perf.status === "success" ? "✓" : "✗"}</td>
    `;
    perfLogBody.prepend(row);
}

async function loadSystemInfo() {
    try {
        const res = await fetch("/api/system");
        if (!res.ok) return;
        const data = await res.json();

        const systemInfo = document.getElementById("system-info");
        if (systemInfo) {
            systemInfo.innerHTML = `
                <div class="sys-row"><span class="sys-label">Environment</span><span class="sys-value">${data.environment || "LOCAL"}</span></div>
                <div class="sys-row"><span class="sys-label">Hostname</span><span class="sys-value">${data.hostname || "N/A"}</span></div>
                <div class="sys-row"><span class="sys-label">Platform</span><span class="sys-value">${truncate(data.platform || "ESP32", 20)}</span></div>
                <div class="sys-row"><span class="sys-label">Arch</span><span class="sys-value">${data.architecture || "N/A"}</span></div>
                <div class="sys-row"><span class="sys-label">CPUs</span><span class="sys-value">${data.cpu_count || "N/A"}</span></div>
                <div class="sys-row"><span class="sys-label">CPU Freq</span><span class="sys-value">${data.cpu_freq_mhz || "N/A"} MHz</span></div>
                <div class="sys-row"><span class="sys-label">Uptime</span><span class="sys-value">${formatUptime(data.uptime_s || 0)}</span></div>
                <div class="sys-row"><span class="sys-label">Active Users</span><span class="sys-value">${data.active_users || "0"}</span></div>
            `;
        }

        if (liveCpu) liveCpu.textContent = data.cpu_usage_pct !== undefined ? `${data.cpu_usage_pct}%` : "N/A";
        if (liveRam) liveRam.textContent = data.ram_usage_pct ? `${data.ram_used_mb}MB (${data.ram_usage_pct}%)` : "N/A";

    } catch (e) {
        console.warn("Failed to retrieve system info:", e);
    }
}

async function refreshPerformanceData() {
    await loadSystemInfo();
    try {
        const res = await fetch("/api/performance");
        if (res.ok) {
            const data = await res.json();
            // Sync any historical logs if desired
        }
    } catch (e) { /* ignore */ }
}

// Auto-refresh when open
setInterval(() => {
    if (perfSidebar && perfSidebar.classList.contains("open")) {
        loadSystemInfo();
    }
}, 5000);

async function runStressTest() {
    const count = parseInt(loadCountInput.value) || 10;
    const concurrency = parseInt(loadConcurrency.value) || 3;

    btnLoadTest.disabled = true;
    btnLoadTest.textContent = "Testing...";
    loadTestResults.classList.remove("hidden");
    loadTestResults.innerHTML = `<div class="lr-title">🔄 Stress Test running: ${count} requests, concurrency ${concurrency}...</div>`;

    const testText = "Stress testing sentiment analyzer parameters with positive awesome happy words.";
    const results = [];
    let completed = 0;
    let errors = 0;

    const startAll = performance.now();

    for (let i = 0; i < count; i += concurrency) {
        const batch = [];
        for (let j = 0; j < concurrency && (i + j) < count; j++) {
            batch.push(
                (async () => {
                    const start = performance.now();
                    try {
                        const res = await fetch("/api/send", {
                            method: "POST",
                            headers: { "Content-Type": "application/json" },
                            body: JSON.stringify({
                                session_token: sessionToken || "mock-token",
                                text: testText
                            })
                        });
                        const data = await res.json();
                        const elapsed = (performance.now() - start) / 1000.0;
                        if (res.ok && data.status === "success") {
                            const p = data.perf || { latency_s: 0.001 };
                            results.push({
                                rttLatency: elapsed,
                                serverLatency: p.latency_s
                            });
                            completed++;
                            requestCount++;
                            totalLatency += p.latency_s;
                            addChartPoint(p.latency_s);
                            addLogRow(p);
                        } else {
                            results.push({
                                rttLatency: elapsed,
                                serverLatency: 0.001
                            });
                            errors++;
                        }
                    } catch (e) {
                        errors++;
                    }
                })()
            );
        }
        await Promise.all(batch);

        const progress = Math.min(i + concurrency, count);
        loadTestResults.innerHTML = `<div class="lr-title">🔄 Progress: ${progress}/${count} requests...</div>`;
    }

    const totalTime = (performance.now() - startAll) / 1000.0;
    
    if (results.length > 0) {
        const rttLatencies = results.map(r => r.rttLatency * 1000.0).sort((a, b) => a - b);
        const serverLatencies = results.map(r => r.serverLatency * 1000.0).sort((a, b) => a - b);
        
        const sum = rttLatencies.reduce((a, b) => a + b, 0);
        const avgRtt = sum / rttLatencies.length;
        const minRtt = rttLatencies[0];
        const maxRtt = rttLatencies[rttLatencies.length - 1];
        const p50 = rttLatencies[Math.floor(rttLatencies.length * 0.5)];
        const p95 = rttLatencies[Math.floor(rttLatencies.length * 0.95)];
        const p99 = rttLatencies[Math.floor(rttLatencies.length * 0.99)];
        
        const serverSum = serverLatencies.reduce((a, b) => a + b, 0);
        const avgServer = serverSum / serverLatencies.length;

        const throughput = completed / totalTime;

        loadTestResults.innerHTML = `
            <div class="lr-title">📊 Stress Test Results</div>
            <div>Total Sent   : ${count} requests</div>
            <div>Successes    : ${completed}</div>
            <div>Failures     : ${errors}</div>
            <div>Total Time   : ${totalTime.toFixed(2)} s</div>
            <div>Throughput   : ${throughput.toFixed(1)} req/s</div>
            <div>──────────────────────</div>
            <div>Avg Processing: ${avgServer.toFixed(2)} ms</div>
            <div>Avg RTT Latency: ${avgRtt.toFixed(1)} ms</div>
            <div>Min RTT Latency: ${minRtt.toFixed(1)} ms</div>
            <div>Max RTT Latency: ${maxRtt.toFixed(1)} ms</div>
            <div>P50 (Median)   : ${p50.toFixed(1)} ms</div>
            <div>P95 Latency    : ${p95.toFixed(1)} ms</div>
            <div>P99 Latency    : ${p99.toFixed(1)} ms</div>
        `;
    } else {
        loadTestResults.innerHTML = `<div class="lr-title">❌ Stress Test failed (all requests errored out).</div>`;
    }

    liveRequests.textContent = requestCount;
    liveAvgLatency.textContent = (totalLatency / requestCount).toFixed(3) + " s";
    btnLoadTest.disabled = false;
    btnLoadTest.textContent = "Run Stress Test";
}

function truncate(str, len) {
    if (!str) return "";
    return str.length > len ? str.substring(0, len) + "..." : str;
}

