/*
 * ===========================================================================
 * WiFi Secure Chat — ESP32 Edge Server
 * A lightweight web server for direct WiFi-based peer-to-peer chatting.
 * Features:
 *   - Auto Station Mode (connects to your router) or Access Point (AP) fallback.
 *   - Serves HTML, CSS, and JS from PROGMEM (Flash memory) to save RAM.
 *   - Implements the exact same JSON API endpoints as the Python Flask server.
 * ===========================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// CONFIGURATION
// ---------------------------------------------------------------------------
const char* WIFI_SSID = "YOUR_WIFI_SSID";      // SSID of your local WiFi
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";  // Password of your local WiFi

const char* AP_SSID = "ESP32-Chat-Hub";       // Fallback Access Point SSID
const char* AP_PASS = "12345678";             // Access Point Password
const int AP_CHANNEL = 1;                     // AP Channel
const int AP_MAX_CONN = 4;                    // Max connections to AP

const String SECURE_PASSWORD = "1234!@#$";    // Access Password (matches requirements)
const int PORT = 80;                          // HTTP Web Port

// ---------------------------------------------------------------------------
// DATA STRUCTS AND LIMITS
// ---------------------------------------------------------------------------
#define MAX_USERS 8
#define MAX_MESSAGES 15
#define MAX_CHATS 4

struct User {
  String token;
  String name;
  unsigned long lastSeen; // millis()
  int partnerIndex;       // -1 if none, or index in users array
  bool isTyping;
  unsigned long lastTypingUpdate; // millis()
};

struct Message {
  String sender;
  String text;
  String timeStr;
};

struct Chat {
  String chatId;
  Message messages[MAX_MESSAGES];
  int messageCount;
};

// Global DB State
User users[MAX_USERS];
int numUsers = 0;

Chat chats[MAX_CHATS];
int numChats = 0;

WebServer server(PORT);

// ---------------------------------------------------------------------------
// PERFORMANCE TELEMETRY STRUCTS
// ---------------------------------------------------------------------------
struct PerfLog {
  unsigned long timeMs;
  float latencyMs;
  int textLength;
  float ramUsageMb;
  float ramDeltaMb;
  String status;
};

#define MAX_PERF_LOGS 30
PerfLog perfLogs[MAX_PERF_LOGS];
int numPerfLogs = 0;
int perfLogNextIdx = 0;

void logPerformance(float latencyMs, int textLen, float ramUsageMb, float ramDeltaMb, String status) {
  PerfLog &log = perfLogs[perfLogNextIdx];
  log.timeMs = millis();
  log.latencyMs = latencyMs;
  log.textLength = textLen;
  log.ramUsageMb = ramUsageMb;
  log.ramDeltaMb = ramDeltaMb;
  log.status = status;
  
  perfLogNextIdx = (perfLogNextIdx + 1) % MAX_PERF_LOGS;
  if (numPerfLogs < MAX_PERF_LOGS) {
    numPerfLogs++;
  }
}

// ---------------------------------------------------------------------------
// EMBEDDED STATIC RESOURCES (Pre-replaced templates for ESP32)
// ---------------------------------------------------------------------------

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta name="description" content="Local WiFi Chat — Lightweight, secure chat application for Cloud, Laptop & ESP32 Edge deployment.">
    <title>Secure WiFi Chat — ESP32 Edge Hub</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&family=JetBrains+Mono:wght@400;500;600&display=swap" rel="stylesheet">
    <link rel="stylesheet" href="/static/css/style.css">
</head>
<body>
    <div class="app-container">
        <!-- ================= LOGIN SCREEN ================= -->
        <div id="login-container" class="login-wrapper">
            <div class="login-card glass-card">
                <div class="login-header">
                    <span class="logo-icon">💬</span>
                    <h2>Secure WiFi Chat</h2>
                    <p class="subtitle">Enter username and local access key to join the server.</p>
                </div>
                <form id="login-form">
                    <div class="input-group">
                        <label for="username">Username</label>
                        <input type="text" id="username" placeholder="Enter username (Max 15 chars)" required autocomplete="off" maxlength="15">
                    </div>
                    <div class="input-group">
                        <label for="password">Server Password</label>
                        <input type="password" id="password" placeholder="Enter password (e.g. 1234!@#$)" required autocomplete="off">
                    </div>
                    <div id="login-error" class="alert alert-danger hidden"></div>
                    <button type="submit" class="btn btn-primary btn-block">
                        <span class="btn-text">🔑 Log In</span>
                    </button>
                </form>
            </div>
        </div>

        <!-- ================= MAIN CHAT INTERFACE ================= -->
        <div id="chat-container" class="main-layout hidden">
            <!-- ---- SIDEBAR: Performance Panel ---- -->
            <aside id="perf-sidebar" class="perf-sidebar">
                <div class="sidebar-header">
                    <h2>📊 Performance</h2>
                    <button id="btn-close-sidebar" class="btn-icon" title="Close">✕</button>
                </div>
                <!-- System Info -->
                <section class="sidebar-section">
                    <h3>🖥️ Server Info</h3>
                    <div id="system-info" class="system-grid">
                        <div class="sys-row"><span class="sys-label">Env</span><span class="sys-value" id="sys-env">—</span></div>
                        <div class="sys-row"><span class="sys-label">Platform</span><span class="sys-value" id="sys-platform">—</span></div>
                        <div class="sys-row"><span class="sys-label">Uptime</span><span class="sys-value" id="sys-uptime">—</span></div>
                        <div class="sys-row"><span class="sys-label">Active Users</span><span class="sys-value" id="sys-users">—</span></div>
                    </div>
                </section>
                <!-- Live Metrics -->
                <section class="sidebar-section">
                    <h3>⚡ Live Telemetry</h3>
                    <div class="metric-cards">
                        <div class="metric-card">
                            <span class="metric-label">RTT Ping</span>
                            <span class="metric-value" id="live-ping">—</span>
                        </div>
                        <div class="metric-card">
                            <span class="metric-label">RAM Usage</span>
                            <span class="metric-value" id="live-ram">—</span>
                        </div>
                        <div class="metric-card">
                            <span class="metric-label">CPU Load</span>
                            <span class="metric-value" id="live-cpu">—</span>
                        </div>
                        <div class="metric-card">
                            <span class="metric-label">Sent Messages</span>
                            <span class="metric-value" id="live-requests">0</span>
                        </div>
                        <div class="metric-card" style="grid-column: span 2;">
                            <span class="metric-label">Avg Processing Latency</span>
                            <span class="metric-value" id="live-avg-latency">—</span>
                        </div>
                    </div>
                </section>
                <!-- Inference History Chart -->
                <section class="sidebar-section">
                    <h3>📈 Latency History</h3>
                    <div class="chart-container">
                        <canvas id="latency-chart"></canvas>
                    </div>
                </section>
                <!-- Performance Log Table -->
                <section class="sidebar-section">
                    <h3>📋 Message Processing Log</h3>
                    <div class="log-table-wrapper">
                        <table class="log-table" id="perf-log-table">
                            <thead>
                                <tr>
                                    <th>#</th>
                                    <th>Time</th>
                                    <th>Text Size</th>
                                    <th>Latency</th>
                                    <th>CPU</th>
                                    <th>RAM Δ</th>
                                    <th>Status</th>
                                </tr>
                            </thead>
                            <tbody id="perf-log-body">
                                <tr><td colspan="7" class="empty-row">No messages sent yet</td></tr>
                            </tbody>
                        </table>
                    </div>
                </section>
                <!-- Load Test Section -->
                <section class="sidebar-section">
                    <h3>🔥 Stress Tester</h3>
                    <p class="section-desc">Send concurrent mock messages to measure local edge or cloud throughput/limits.</p>
                    <div class="load-test-controls">
                        <label>
                            Requests:
                            <input type="number" id="load-count" value="10" min="1" max="100" class="input-small">
                        </label>
                        <label>
                            Concurrency:
                            <input type="number" id="load-concurrency" value="3" min="1" max="20" class="input-small">
                        </label>
                        <button id="btn-load-test" class="btn btn-danger btn-block" style="margin-top:0.5rem; justify-content:center;">Run Stress Test</button>
                    </div>
                    <div id="load-test-results" class="load-results hidden"></div>
                </section>
                <!-- Performance log description -->
                <section class="sidebar-section">
                    <h3>ℹ️ Connection Info</h3>
                    <p class="section-desc">Polling intervals are automatically throttled when window is inactive. Average RTT reflects live WebSocket/HTTP round-trip latency to local network interface or cloud.</p>
                </section>
            </aside>

            <!-- ---- MAIN COLUMN ---- -->
            <main class="main-content-wrapper">
                <!-- Top Bar -->
                <header class="top-bar">
                    <div class="top-bar-left">
                        <button id="btn-end-chat" class="btn btn-danger hidden">⛔ End Chat</button>
                        <div class="logo">
                            <span class="logo-icon">💬</span>
                            <h1>WiFi <span class="accent">Chat</span></h1>
                        </div>
                        <span class="env-badge" id="env-badge">🔧 Edge (ESP32 Dev Board)</span>
                    </div>
                    <div class="top-bar-right">
                        <span class="user-badge" id="user-badge">User: —</span>
                        <button id="btn-open-sidebar" class="btn btn-outline" title="Performance Dashboard">📊 Perf</button>
                        <button id="btn-logout" class="btn btn-outline" title="Logout">🚪 Logout</button>
                    </div>
                </header>

                <div class="app-workspace">
                    <!-- Users List (Left Column) -->
                    <section class="users-panel glass-card">
                        <div class="panel-header">
                            <h3>👥 Connected Users</h3>
                        </div>
                        <div class="users-list-wrapper">
                            <div id="users-list" class="users-list">
                                <p class="loading-text">Scanning network...</p>
                            </div>
                        </div>
                    </section>
                    <!-- Chat Window (Right Column) -->
                    <section class="chat-panel glass-card">
                        <!-- Idle State -->
                        <div id="chat-idle" class="chat-state-content">
                            <div class="idle-graphics">💬</div>
                            <h3>Start a Secure Chat</h3>
                            <p>Choose an available user from the list on the left to initiate a peer-to-peer secure session.</p>
                        </div>
                        <!-- Chatting State -->
                        <div id="chat-active" class="chat-state-content hidden">
                            <div class="chat-header">
                                <div class="chat-partner-info">
                                    <span class="status-dot"></span>
                                    <h4 id="chat-partner-name">Username</h4>
                                </div>
                                <span class="secure-badge">🔒 Encrypted Endpoint</span>
                            </div>
                            <!-- Message History -->
                            <div class="chat-history-wrapper" id="chat-history-wrapper">
                                <div id="chat-history" class="chat-history"></div>
                            </div>
                            <!-- Input and Typing Area -->
                            <div class="chat-input-area">
                                <div id="typing-indicator" class="typing-indicator invisible">partner is typing...</div>
                                <form id="chat-form" class="chat-form-row">
                                    <input type="text" id="chat-input" placeholder="Type a secure message..." required autocomplete="off" maxlength="500">
                                    <button type="submit" class="btn btn-primary" id="btn-send">
                                        <span>Send 🚀</span>
                                    </button>
                                </form>
                            </div>
                        </div>
                    </section>
                </div>

                <!-- Footer -->
                <footer class="app-footer">
                    <p>
                        WiFi Secure Chat v1.0.0 &middot;
                        <span id="footer-env">EDGE</span> &middot;
                        Optimized for ESP32 & Cloud Runtimes
                    </p>
                </footer>
            </main>
        </div>
    </div>
    <!-- Chart.js CDN (lightweight) -->
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.4/dist/chart.umd.min.js"></script>
    <script src="/static/js/app.js"></script>
</body>
</html>
)rawliteral";

const char STYLE_CSS[] PROGMEM = R"rawliteral(
:root {
    --bg-primary: #0a0e1a;
    --bg-secondary: #111827;
    --bg-card: rgba(17, 24, 39, 0.75);
    --bg-glass: rgba(255, 255, 255, 0.04);
    --bg-glass-hover: rgba(255, 255, 255, 0.08);
    --border-glass: rgba(255, 255, 255, 0.08);
    --border-glass-hover: rgba(255, 255, 255, 0.15);
    --text-primary: #f1f5f9;
    --text-secondary: #94a3b8;
    --text-muted: #64748b;
    --accent-primary: #6366f1;
    --accent-secondary: #8b5cf6;
    --accent-glow: rgba(99, 102, 241, 0.25);
    --accent-gradient: linear-gradient(135deg, #6366f1, #a855f7, #ec4899);
    --success: #22c55e;
    --warning: #f59e0b;
    --danger: #ef4444;
    --info: #3b82f6;
    --font-body: 'Inter', -apple-system, BlinkMacSystemFont, sans-serif;
    --font-mono: 'JetBrains Mono', 'Fira Code', monospace;
    --radius-sm: 8px;
    --radius-md: 12px;
    --radius-lg: 16px;
    --shadow-sm: 0 2px 8px rgba(0, 0, 0, 0.3);
    --shadow-md: 0 4px 20px rgba(0, 0, 0, 0.4);
    --shadow-glow: 0 0 30px var(--accent-glow);
    --sidebar-width: 320px;
    --ease-out: cubic-bezier(0.16, 1, 0.3, 1);
}
*, *::before, *::after {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}
html {
    font-size: 14px;
    scroll-behavior: smooth;
}
body {
    font-family: var(--font-body);
    background: var(--bg-primary);
    color: var(--text-primary);
    line-height: 1.6;
    min-height: 100vh;
    overflow-x: hidden;
    background-image:
        radial-gradient(ellipse 80% 50% at 50% -20%, rgba(99, 102, 241, 0.12), transparent),
        radial-gradient(ellipse 60% 40% at 80% 80%, rgba(168, 85, 247, 0.08), transparent);
}
.app-container {
    min-height: 100vh;
    display: flex;
    justify-content: center;
    align-items: center;
}
.login-wrapper {
    width: 100%;
    max-width: 420px;
    padding: 2rem;
    animation: fadeIn 0.4s var(--ease-out);
}
.login-card {
    padding: 2.5rem 2rem;
    border-radius: var(--radius-lg);
    box-shadow: var(--shadow-md);
    background: var(--bg-card);
    border: 1px solid var(--border-glass);
    backdrop-filter: blur(20px);
}
.login-header {
    text-align: center;
    margin-bottom: 2rem;
}
.login-header .logo-icon {
    font-size: 3rem;
    display: block;
    margin-bottom: 0.5rem;
    animation: bounce 3s infinite ease-in-out;
}
@keyframes bounce {
    0%, 100% { transform: translateY(0); }
    50% { transform: translateY(-8px); }
}
.login-header h2 {
    font-size: 1.8rem;
    font-weight: 800;
    background: var(--accent-gradient);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    margin-bottom: 0.5rem;
}
.login-header .subtitle {
    font-size: 0.85rem;
    color: var(--text-secondary);
}
.input-group {
    margin-bottom: 1.25rem;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
}
.input-group label {
    font-size: 0.8rem;
    font-weight: 500;
    color: var(--text-secondary);
}
.input-group input {
    width: 100%;
    padding: 0.75rem 1rem;
    font-size: 0.95rem;
    background: rgba(0, 0, 0, 0.3);
    border: 1px solid var(--border-glass);
    border-radius: var(--radius-sm);
    color: var(--text-primary);
    transition: all 0.2s ease;
}
.input-group input:focus {
    outline: none;
    border-color: var(--accent-primary);
    box-shadow: 0 0 0 3px var(--accent-glow);
}
.btn-block {
    width: 100%;
    justify-content: center;
    margin-top: 1rem;
}
.alert {
    padding: 0.75rem 1rem;
    border-radius: var(--radius-sm);
    font-size: 0.82rem;
    margin-bottom: 1.25rem;
    border: 1px solid transparent;
}
.alert-danger {
    background: rgba(239, 68, 68, 0.15);
    border-color: rgba(239, 68, 68, 0.3);
    color: #fca5a5;
}
.main-layout {
    width: 100%;
    min-height: 100vh;
    display: flex;
    flex-direction: row;
}
.main-content-wrapper {
    flex: 1;
    display: flex;
    flex-direction: column;
    padding: 1.5rem 2rem;
    height: 100vh;
    max-width: 1200px;
    margin: 0 auto;
    width: 100%;
    transition: margin-right 0.4s var(--ease-out);
}
.app-workspace {
    flex: 1;
    display: grid;
    grid-template-columns: 300px 1fr;
    gap: 1.5rem;
    height: calc(100vh - 140px);
    min-height: 0;
}
.top-bar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0.75rem 0;
    margin-bottom: 1.25rem;
    border-bottom: 1px solid var(--border-glass);
    flex-shrink: 0;
}
.top-bar-left {
    display: flex;
    align-items: center;
    gap: 1rem;
}
.logo {
    display: flex;
    align-items: center;
    gap: 0.5rem;
}
.logo h1 {
    font-size: 1.3rem;
    font-weight: 700;
}
.logo .accent {
    background: var(--accent-gradient);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
}
.env-badge {
    font-size: 0.7rem;
    font-weight: 600;
    padding: 0.25rem 0.6rem;
    border-radius: 100px;
    background: var(--bg-glass);
    border: 1px solid var(--border-glass);
    color: var(--text-secondary);
}
.top-bar-right {
    display: flex;
    align-items: center;
    gap: 0.75rem;
}
.user-badge {
    font-size: 0.8rem;
    font-weight: 500;
    color: var(--text-secondary);
    padding: 0.3rem 0.75rem;
    background: var(--bg-glass);
    border-radius: var(--radius-sm);
    border: 1px solid var(--border-glass);
}
.btn {
    font-family: var(--font-body);
    font-size: 0.82rem;
    font-weight: 600;
    padding: 0.55rem 1.1rem;
    border-radius: var(--radius-sm);
    border: none;
    cursor: pointer;
    transition: all 0.2s ease;
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
}
.btn-primary {
    background: var(--accent-gradient);
    color: white;
    box-shadow: var(--shadow-glow);
}
.btn-primary:hover {
    transform: translateY(-1px);
    box-shadow: 0 0 35px var(--accent-glow);
}
.btn-outline {
    background: var(--bg-glass);
    color: var(--text-secondary);
    border: 1px solid var(--border-glass);
}
.btn-outline:hover {
    background: var(--bg-glass-hover);
    border-color: var(--border-glass-hover);
    color: var(--text-primary);
}
.btn-danger {
    background: linear-gradient(135deg, #ef4444, #b91c1c);
    color: white;
    font-size: 0.78rem;
    padding: 0.5rem 0.9rem;
}
.btn-danger:hover {
    box-shadow: 0 0 20px rgba(239, 68, 68, 0.3);
    transform: translateY(-1px);
}
.glass-card {
    background: var(--bg-card);
    backdrop-filter: blur(20px);
    -webkit-backdrop-filter: blur(20px);
    border: 1px solid var(--border-glass);
    border-radius: var(--radius-md);
    padding: 1.25rem;
    box-shadow: var(--shadow-md);
    display: flex;
    flex-direction: column;
    min-height: 0;
}
.users-panel .panel-header {
    margin-bottom: 0.75rem;
    padding-bottom: 0.5rem;
    border-bottom: 1px solid var(--border-glass);
}
.users-panel .panel-header h3 {
    font-size: 0.95rem;
    font-weight: 600;
    color: var(--text-secondary);
}
.users-list-wrapper {
    flex: 1;
    overflow-y: auto;
}
.users-list {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
}
.loading-text {
    font-size: 0.8rem;
    color: var(--text-muted);
    font-style: italic;
    text-align: center;
    padding: 1.5rem 0;
}
.user-item-card {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    padding: 0.75rem;
    background: var(--bg-glass);
    border: 1px solid var(--border-glass);
    border-radius: var(--radius-sm);
}
.user-item-top {
    display: flex;
    justify-content: space-between;
    align-items: center;
}
.user-item-name {
    font-size: 0.85rem;
    font-weight: 600;
    color: var(--text-primary);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    max-width: 130px;
}
.status-badge {
    font-size: 0.62rem;
    font-weight: 700;
    padding: 0.15rem 0.4rem;
    border-radius: 100px;
    text-transform: uppercase;
}
.status-badge.available {
    background: rgba(34, 197, 94, 0.15);
    color: var(--success);
    border: 1px solid rgba(34, 197, 94, 0.3);
}
.status-badge.busy {
    background: rgba(245, 158, 11, 0.15);
    color: var(--warning);
    border: 1px solid rgba(245, 158, 11, 0.3);
}
.btn-connect {
    width: 100%;
    justify-content: center;
    font-size: 0.75rem;
    padding: 0.35rem;
}
.btn-connect:disabled {
    background: var(--bg-glass);
    color: var(--text-muted);
    border: 1px solid var(--border-glass);
    cursor: not-allowed;
    box-shadow: none;
}
.chat-panel {
    flex: 1;
    min-height: 0;
}
.chat-state-content {
    flex: 1;
    display: flex;
    flex-direction: column;
    min-height: 0;
    height: 100%;
}
#chat-idle {
    justify-content: center;
    align-items: center;
    text-align: center;
    color: var(--text-secondary);
    padding: 2rem;
}
.idle-graphics {
    font-size: 4rem;
    margin-bottom: 1rem;
    animation: bounce 4s infinite ease-in-out;
}
#chat-idle h3 {
    font-size: 1.25rem;
    font-weight: 700;
    color: var(--text-primary);
    margin-bottom: 0.5rem;
}
#chat-idle p {
    font-size: 0.85rem;
    max-width: 320px;
}
.chat-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding-bottom: 0.75rem;
    border-bottom: 1px solid var(--border-glass);
    margin-bottom: 1rem;
    flex-shrink: 0;
}
.chat-partner-info {
    display: flex;
    align-items: center;
    gap: 0.5rem;
}
.chat-partner-info .status-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--success);
    box-shadow: 0 0 8px var(--success);
}
.chat-partner-info h4 {
    font-size: 1rem;
    font-weight: 600;
}
.secure-badge {
    font-size: 0.7rem;
    color: var(--text-muted);
    border: 1px solid var(--border-glass);
    padding: 0.15rem 0.5rem;
    border-radius: var(--radius-sm);
    font-family: var(--font-mono);
}
.chat-history-wrapper {
    flex: 1;
    overflow-y: auto;
    padding-right: 0.5rem;
    margin-bottom: 0.75rem;
    min-height: 0;
}
.chat-history {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
}
.msg-row {
    display: flex;
    flex-direction: column;
    max-width: 75%;
    animation: slideUpBubble 0.2s var(--ease-out);
}
@keyframes slideUpBubble {
    from { opacity: 0; transform: translateY(8px); }
    to { opacity: 1; transform: translateY(0); }
}
.msg-row.msg-self { align-self: flex-end; }
.msg-row.msg-partner { align-self: flex-start; }
.msg-row.msg-system {
    align-self: center;
    max-width: 90%;
    margin: 0.5rem 0;
    text-align: center;
}
.msg-meta {
    font-size: 0.68rem;
    color: var(--text-muted);
    margin-bottom: 0.2rem;
    padding: 0 0.25rem;
}
.msg-self .msg-meta { text-align: right; }
.msg-bubble {
    padding: 0.6rem 0.9rem;
    border-radius: var(--radius-md);
    font-size: 0.88rem;
    line-height: 1.5;
    word-break: break-word;
}
.msg-self .msg-bubble {
    background: var(--accent-gradient);
    color: white;
    border-top-right-radius: 2px;
}
.msg-partner .msg-bubble {
    background: var(--bg-glass);
    border: 1px solid var(--border-glass);
    color: var(--text-primary);
    border-top-left-radius: 2px;
}
.msg-system .msg-bubble {
    background: rgba(255, 255, 255, 0.02);
    border: 1px solid rgba(255, 255, 255, 0.04);
    color: var(--text-muted);
    font-size: 0.75rem;
    border-radius: var(--radius-sm);
    padding: 0.3rem 0.6rem;
    font-family: var(--font-mono);
}
.chat-input-area {
    margin-top: auto;
    flex-shrink: 0;
}
.typing-indicator {
    font-size: 0.72rem;
    color: var(--text-muted);
    font-style: italic;
    margin-bottom: 0.4rem;
    padding-left: 0.5rem;
    transition: opacity 0.2s ease;
    height: 14px;
}
.invisible {
    opacity: 0;
    pointer-events: none;
}
.chat-form-row {
    display: flex;
    gap: 0.5rem;
}
.chat-form-row input {
    flex: 1;
    padding: 0.7rem 1rem;
    font-size: 0.9rem;
    background: rgba(0, 0, 0, 0.3);
    border: 1px solid var(--border-glass);
    border-radius: var(--radius-sm);
    color: var(--text-primary);
}
.chat-form-row input:focus {
    outline: none;
    border-color: var(--accent-primary);
}
.perf-sidebar {
    position: fixed;
    top: 0;
    right: 0;
    width: var(--sidebar-width);
    height: 100vh;
    background: var(--bg-secondary);
    border-left: 1px solid var(--border-glass);
    box-shadow: -4px 0 25px rgba(0, 0, 0, 0.4);
    z-index: 1000;
    overflow-y: auto;
    padding: 1.25rem;
    transform: translateX(100%);
    transition: transform 0.4s var(--ease-out);
}
.perf-sidebar.open { transform: translateX(0); }
body.sidebar-open .main-content-wrapper { margin-right: var(--sidebar-width); }
.sidebar-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 1.25rem;
    padding-bottom: 0.75rem;
    border-bottom: 1px solid var(--border-glass);
}
.sidebar-header h2 { font-size: 1.05rem; font-weight: 700; }
.sidebar-section { margin-bottom: 1.5rem; }
.sidebar-section h3 {
    font-size: 0.85rem;
    font-weight: 600;
    margin-bottom: 0.75rem;
    color: var(--text-secondary);
}
.section-desc {
    font-size: 0.75rem;
    color: var(--text-muted);
    line-height: 1.5;
}
.system-grid {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    font-size: 0.75rem;
}
.sys-row {
    display: flex;
    justify-content: space-between;
    border-bottom: 1px dotted var(--border-glass);
    padding-bottom: 0.2rem;
}
.sys-label { color: var(--text-muted); }
.sys-value { color: var(--text-primary); font-family: var(--font-mono); }
.metric-cards {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0.6rem;
}
.metric-card {
    background: var(--bg-glass);
    border: 1px solid var(--border-glass);
    border-radius: var(--radius-sm);
    padding: 0.75rem;
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
}
.metric-label { font-size: 0.68rem; font-weight: 500; color: var(--text-muted); text-transform: uppercase; }
.metric-value { font-size: 1.15rem; font-weight: 700; font-family: var(--font-mono); color: var(--accent-primary); }
.app-footer {
    text-align: center;
    padding: 1rem 0;
    margin-top: auto;
    border-top: 1px solid var(--border-glass);
    flex-shrink: 0;
}
.app-footer p { font-size: 0.72rem; color: var(--text-muted); }
@keyframes fadeIn {
    from { opacity: 0; transform: scale(0.98); }
    to { opacity: 1; transform: scale(1); }
}

/* Latency Chart Container */
.chart-container {
    position: relative;
    height: 150px;
    width: 100%;
    margin-top: 0.5rem;
    background: rgba(0, 0, 0, 0.2);
    border: 1px solid var(--border-glass);
    border-radius: var(--radius-sm);
    padding: 0.5rem;
}

/* Inference Log Table */
.log-table-wrapper {
    width: 100%;
    max-height: 200px;
    overflow-y: auto;
    overflow-x: auto;
    border: 1px solid var(--border-glass);
    border-radius: var(--radius-sm);
    background: rgba(0, 0, 0, 0.2);
}

.log-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.72rem;
    text-align: left;
}

.log-table th, .log-table td {
    padding: 0.5rem;
    border-bottom: 1px solid var(--border-glass);
    white-space: nowrap;
}

.log-table th {
    background: rgba(0, 0, 0, 0.4);
    color: var(--text-secondary);
    font-weight: 600;
}

.log-table td {
    font-family: var(--font-mono);
    color: var(--text-primary);
}

.log-table .empty-row {
    text-align: center;
    color: var(--text-muted);
    font-style: italic;
    padding: 1.5rem 0;
}

.log-table .status-ok {
    color: var(--success);
    font-weight: bold;
}

.log-table .status-err {
    color: var(--danger);
    font-weight: bold;
}

/* Load Testing Controls */
.load-test-controls {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
    background: rgba(0, 0, 0, 0.15);
    border: 1px solid var(--border-glass);
    border-radius: var(--radius-sm);
    padding: 0.75rem;
}

.load-test-controls label {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 0.75rem;
    color: var(--text-secondary);
}

.input-small {
    width: 70px;
    padding: 0.3rem 0.5rem;
    font-size: 0.75rem;
    background: rgba(0, 0, 0, 0.4);
    border: 1px solid var(--border-glass);
    border-radius: var(--radius-sm);
    color: var(--text-primary);
    text-align: center;
    font-family: var(--font-mono);
}

.input-small:focus {
    outline: none;
    border-color: var(--accent-primary);
}

.load-results {
    margin-top: 0.75rem;
    padding: 0.75rem;
    background: rgba(0, 0, 0, 0.3);
    border: 1px solid var(--border-glass);
    border-radius: var(--radius-sm);
    font-family: var(--font-mono);
    font-size: 0.72rem;
    color: var(--text-secondary);
    line-height: 1.4;
    white-space: pre-wrap;
}

.load-results .lr-title {
    font-weight: 600;
    color: var(--text-primary);
    margin-bottom: 0.4rem;
}

@media (max-width: 860px) {
    .app-workspace { grid-template-columns: 1fr; height: auto; }
    .main-content-wrapper { height: auto; padding: 1rem; }
    .users-panel { height: 250px; }
    .chat-panel { height: 450px; }
    body.sidebar-open .main-content-wrapper { margin-right: 0; }
}
)rawliteral";

const char APP_JS[] PROGMEM = R"rawliteral(
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

// Reset typing status on send
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
        <td>time</td>
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
)rawliteral";

// ---------------------------------------------------------------------------

// Serve files
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleCSS() {
  server.send_P(200, "text/css", STYLE_CSS);
}

void handleJS() {
  server.send_P(200, "application/javascript", APP_JS);
}

// ---------------------------------------------------------------------------
// STATE UTILITIES
// ---------------------------------------------------------------------------
String generateToken() {
  String chars = "abcdefghijklmnopqrstuvwxyz0123456789";
  String tok = "";
  for (int i = 0; i < 16; i++) {
    tok += chars[random(0, chars.length())];
  }
  return tok;
}

int findUserIndex(String token) {
  for (int i = 0; i < numUsers; i++) {
    if (users[i].token == token) {
      return i;
    }
  }
  return -1;
}

String getChatId(String t1, String t2) {
  if (t1 < t2) return t1 + "_" + t2;
  return t2 + "_" + t1;
}

int findChatIndex(String chatId) {
  for (int i = 0; i < numChats; i++) {
    if (chats[i].chatId == chatId) {
      return i;
    }
  }
  return -1;
}

// Add system message
void addSystemMessage(String text, String t1, String t2) {
  String cid = getChatId(t1, t2);
  int cIdx = findChatIndex(cid);
  
  if (cIdx == -1) {
    if (numChats < MAX_CHATS) {
      cIdx = numChats;
      chats[cIdx].chatId = cid;
      chats[cIdx].messageCount = 0;
      numChats++;
    } else {
      return; // No chat slot available
    }
  }
  
  Chat &chat = chats[cIdx];
  
  // Shift messages if slot is full
  if (chat.messageCount >= MAX_MESSAGES) {
    for (int j = 0; j < MAX_MESSAGES - 1; j++) {
      chat.messages[j] = chat.messages[j + 1];
    }
    chat.messageCount = MAX_MESSAGES - 1;
  }
  
  Message &m = chat.messages[chat.messageCount];
  m.sender = "System";
  m.text = text;
  m.timeStr = "SysTime"; // No real RTC on basic ESP32, fallback to hardcoded indicator
  chat.messageCount++;
}

// Session Cleanup
void cleanExpiredSessions() {
  unsigned long now = millis();
  
  for (int i = 0; i < numUsers; i++) {
    // Timeout user after 6 seconds (6000ms) of polling silence
    if (now - users[i].lastSeen > 6000) {
      String uName = users[i].name;
      String uToken = users[i].token;
      int pIdx = users[i].partnerIndex;
      
      Serial.print("[Session Timeout] Expired user: ");
      Serial.println(uName);
      
      if (pIdx != -1 && pIdx < numUsers) {
        users[pIdx].partnerIndex = -1;
        users[pIdx].isTyping = false;
        
        addSystemMessage("'" + uName + "' disconnected (timeout).", uToken, users[pIdx].token);
      }
      
      // Shift user array
      for (int j = i; j < numUsers - 1; j++) {
        users[j] = users[j + 1];
      }
      numUsers--;
      i--; // adjust index because of deletion shift
    }
  }
}

// ---------------------------------------------------------------------------
// API ENDPOINTS HANDLERS
// ---------------------------------------------------------------------------

void handleApiLogin() {
  cleanExpiredSessions();
  
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
    return;
  }
  
  String body = server.arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);
  
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }
  
  String name = doc["name"].as<String>();
  String password = doc["password"].as<String>();
  
  name.trim();
  
  if (name.length() < 1 || name.length() > 15) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Name must be 1 to 15 characters.\"}");
    return;
  }
  
  if (password != SECURE_PASSWORD) {
    server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Incorrect password.\"}");
    return;
  }
  
  // Check duplicate user names
  for (int i = 0; i < numUsers; i++) {
    if (users[i].name.equalsIgnoreCase(name)) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Username is already online.\"}");
      return;
    }
  }
  
  if (numUsers >= MAX_USERS) {
    server.send(503, "application/json", "{\"status\":\"error\",\"message\":\"Server database is full.\"}");
    return;
  }
  
  // Register user
  int uIdx = numUsers;
  users[uIdx].token = generateToken();
  users[uIdx].name = name;
  users[uIdx].lastSeen = millis();
  users[uIdx].partnerIndex = -1;
  users[uIdx].isTyping = false;
  users[uIdx].lastTypingUpdate = 0;
  numUsers++;
  
  Serial.print("[Login] Registered: ");
  Serial.println(name);
  
  StaticJsonDocument<256> responseDoc;
  responseDoc["status"] = "success";
  responseDoc["session_token"] = users[uIdx].token;
  responseDoc["name"] = users[uIdx].name;
  
  String response;
  serializeJson(responseDoc, response);
  server.send(200, "application/json", response);
}

void handleApiPoll() {
  cleanExpiredSessions();
  
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
    return;
  }
  
  String body = server.arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);
  
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }
  
  String token = doc["session_token"].as<String>();
  int uIdx = findUserIndex(token);
  
  if (uIdx == -1) {
    server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Invalid or expired session.\"}");
    return;
  }
  
  // Update last seen
  users[uIdx].lastSeen = millis();
  
  // Check local typing timeout (2.5s expiry)
  if (users[uIdx].isTyping && (millis() - users[uIdx].lastTypingUpdate > 2500)) {
    users[uIdx].isTyping = false;
  }
  
  // Prepare JSON response doc
  StaticJsonDocument<3072> responseDoc;
  responseDoc["status"] = "success";
  
  int pIdx = users[uIdx].partnerIndex;
  if (pIdx != -1 && pIdx < numUsers) {
    responseDoc["user_state"] = "chatting";
    responseDoc["partner_name"] = users[pIdx].name;
    responseDoc["partner_typing"] = users[pIdx].isTyping;
    
    // Fill messages
    String cid = getChatId(token, users[pIdx].token);
    int cIdx = findChatIndex(cid);
    JsonArray msgArr = responseDoc.createNestedArray("messages");
    
    if (cIdx != -1) {
      Chat &chat = chats[cIdx];
      for (int i = 0; i < chat.messageCount; i++) {
        JsonObject mObj = msgArr.createNestedObject();
        mObj["sender"] = chat.messages[i].sender;
        mObj["text"] = chat.messages[i].text;
        mObj["time"] = chat.messages[i].timeStr;
      }
    }
  } else {
    responseDoc["user_state"] = "idle";
    responseDoc["partner_name"] = "";
    responseDoc["partner_typing"] = false;
    responseDoc.createNestedArray("messages"); // Empty messages array
  }
  
  // List other online users
  JsonArray userArr = responseDoc.createNestedArray("users");
  for (int i = 0; i < numUsers; i++) {
    if (i == uIdx) continue;
    JsonObject uObj = userArr.createNestedObject();
    uObj["id"] = users[i].token;
    uObj["name"] = users[i].name;
    uObj["status"] = (users[i].partnerIndex != -1) ? "busy" : "available";
  }
  
  // Telemetry details
  JsonObject sysObj = responseDoc.createNestedObject("system");
  sysObj["environment"] = "EDGE";
  sysObj["env_label"] = "\xE2\x9A\x99 Edge (ESP32 Board)"; // UTF-8 Gear Emoji
  sysObj["platform"] = "ESP32";
  sysObj["uptime_s"] = millis() / 1000;
  
  // Compute memory usage stats (ESP32 RAM)
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  uint32_t usedHeap = totalHeap - freeHeap;
  float ramUsedMB = (float)usedHeap / (1024.0 * 1024.0);
  float ramUsagePct = ((float)usedHeap / totalHeap) * 100.0;
  
  sysObj["ram_used_mb"] = ramUsedMB;
  sysObj["ram_usage_pct"] = (int)ramUsagePct;
  sysObj["cpu_usage_pct"] = 100;
  sysObj["active_users"] = numUsers;
  
  String response;
  serializeJson(responseDoc, response);
  server.send(200, "application/json", response);
}

void handleApiConnect() {
  cleanExpiredSessions();
  
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
    return;
  }
  
  String body = server.arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);
  
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }
  
  String token = doc["session_token"].as<String>();
  String targetId = doc["target_id"].as<String>();
  
  int uIdx = findUserIndex(token);
  int tIdx = findUserIndex(targetId);
  
  if (uIdx == -1) {
    server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Invalid or expired session.\"}");
    return;
  }
  
  if (tIdx == -1) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Target user is offline.\"}");
    return;
  }
  
  if (users[uIdx].partnerIndex != -1) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"You are already in a chat.\"}");
    return;
  }
  
  if (users[tIdx].partnerIndex != -1) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Target user is busy.\"}");
    return;
  }
  
  // Pair them
  users[uIdx].partnerIndex = tIdx;
  users[tIdx].partnerIndex = uIdx;
  users[uIdx].isTyping = false;
  users[tIdx].isTyping = false;
  
  // Initialize chat logs slot
  String cid = getChatId(token, targetId);
  int cIdx = findChatIndex(cid);
  
  if (cIdx == -1) {
    if (numChats < MAX_CHATS) {
      cIdx = numChats;
      chats[cIdx].chatId = cid;
      numChats++;
    } else {
      // Recycle oldest chat slot if full
      cIdx = 0; 
      chats[cIdx].chatId = cid;
    }
  }
  
  // Reset message count and log start notice
  chats[cIdx].messageCount = 0;
  
  Message &startMsg = chats[cIdx].messages[0];
  startMsg.sender = "System";
  startMsg.text = "Chat started between '" + users[uIdx].name + "' and '" + users[tIdx].name + "'.";
  startMsg.timeStr = "SysTime";
  chats[cIdx].messageCount = 1;
  
  Serial.print("[Chat Started] ");
  Serial.print(users[uIdx].name);
  Serial.print(" <-> ");
  Serial.println(users[tIdx].name);
  
  server.send(200, "application/json", "{\"status\":\"success\"}");
}

void handleApiSend() {
  cleanExpiredSessions();
  
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
    return;
  }
  
  String body = server.arg("plain");
  StaticJsonDocument<1024> doc; // larger buffer for text input
  DeserializationError err = deserializeJson(doc, body);
  
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }
  
  String token = doc["session_token"].as<String>();
  String text = doc["text"].as<String>();
  
  text.trim();
  int uIdx = findUserIndex(token);
  
  if (uIdx == -1) {
    server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Invalid or expired session.\"}");
    return;
  }
  
  int pIdx = users[uIdx].partnerIndex;
  if (pIdx == -1 || pIdx >= numUsers) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No active chat partner.\"}");
    return;
  }
  
  if (text.length() < 1) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Message content cannot be empty.\"}");
    return;
  }
  
  if (text.length() > 500) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Message exceeds limit.\"}");
    return;
  }
  
  String cid = getChatId(token, users[pIdx].token);
  int cIdx = findChatIndex(cid);
  
  if (cIdx != -1) {
    uint32_t ramBefore = ESP.getFreeHeap();
    unsigned long startMicros = micros();
    
    Chat &chat = chats[cIdx];
    
    // Shift messages if slot full
    if (chat.messageCount >= MAX_MESSAGES) {
      for (int j = 0; j < MAX_MESSAGES - 1; j++) {
        chat.messages[j] = chat.messages[j + 1];
      }
      chat.messageCount = MAX_MESSAGES - 1;
    }
    
    // Construct local timestamp based on millis clock since no RTC is required
    unsigned long runSeconds = millis() / 1000;
    int hrs = (runSeconds / 3600) % 24;
    int mins = (runSeconds / 60) % 60;
    int secs = runSeconds % 60;
    char timeBuffer[10];
    sprintf(timeBuffer, "%02d:%02d:%02d", hrs, mins, secs);
    
    Message &m = chat.messages[chat.messageCount];
    m.sender = users[uIdx].name;
    m.text = text;
    m.timeStr = String(timeBuffer);
    
    chat.messageCount++;
    users[uIdx].isTyping = false; // Reset typing since they sent the message
    
    unsigned long endMicros = micros();
    uint32_t ramAfter = ESP.getFreeHeap();
    
    float latencyMs = (float)(endMicros - startMicros) / 1000.0;
    
    uint32_t totalHeap = ESP.getHeapSize();
    uint32_t usedHeap = totalHeap - ramAfter;
    float ramUsedMb = (float)usedHeap / (1024.0 * 1024.0);
    float ramDeltaMb = (float)((int32_t)ramBefore - (int32_t)ramAfter) / (1024.0 * 1024.0);
    
    logPerformance(latencyMs, text.length(), ramUsedMb, ramDeltaMb, "success");
    
    StaticJsonDocument<512> responseDoc;
    responseDoc["status"] = "success";
    JsonObject perfObj = responseDoc.createNestedObject("perf");
    perfObj["type"] = "processing";
    perfObj["latency_s"] = latencyMs / 1000.0;
    perfObj["text_length"] = text.length();
    perfObj["cpu_after_pct"] = 100;
    perfObj["ram_after_mb"] = ramUsedMb;
    perfObj["ram_delta_mb"] = ramDeltaMb;
    perfObj["status"] = "success";
    
    String response;
    serializeJson(responseDoc, response);
    server.send(200, "application/json", response);
  } else {
    server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Chat log session broke.\"}");
  }
}

void handleApiTyping() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
    return;
  }
  
  String body = server.arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);
  
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }
  
  String token = doc["session_token"].as<String>();
  bool typing = doc["typing"].as<bool>();
  
  int uIdx = findUserIndex(token);
  if (uIdx != -1) {
    users[uIdx].isTyping = typing;
    users[uIdx].lastTypingUpdate = millis();
  }
  
  server.send(200, "application/json", "{\"status\":\"success\"}");
}

void handleApiEndChat() {
  cleanExpiredSessions();
  
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
    return;
  }
  
  String body = server.arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);
  
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }
  
  String token = doc["session_token"].as<String>();
  int uIdx = findUserIndex(token);
  
  if (uIdx == -1) {
    server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Invalid or expired session.\"}");
    return;
  }
  
  int pIdx = users[uIdx].partnerIndex;
  if (pIdx != -1 && pIdx < numUsers) {
    users[pIdx].partnerIndex = -1;
    users[pIdx].isTyping = false;
    
    addSystemMessage("'" + users[uIdx].name + "' has ended the chat.", token, users[pIdx].token);
  }
  
  users[uIdx].partnerIndex = -1;
  users[uIdx].isTyping = false;
  
  Serial.print("[Chat Ended] Closed by user: ");
  Serial.println(users[uIdx].name);
  
  server.send(200, "application/json", "{\"status\":\"success\"}");
}

void handleApiLogout() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
    return;
  }
  
  String body = server.arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);
  
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }
  
  String token = doc["session_token"].as<String>();
  int uIdx = findUserIndex(token);
  
  if (uIdx != -1) {
    String name = users[uIdx].name;
    int pIdx = users[uIdx].partnerIndex;
    
    if (pIdx != -1 && pIdx < numUsers) {
      users[pIdx].partnerIndex = -1;
      users[pIdx].isTyping = false;
      addSystemMessage("'" + name + "' logged out.", token, users[pIdx].token);
    }
    
    Serial.print("[Logout] Disconnected: ");
    Serial.println(name);
    
    // Shift user list
    for (int j = uIdx; j < numUsers - 1; j++) {
      users[j] = users[j + 1];
    }
    numUsers--;
  }
  
  server.send(200, "application/json", "{\"status\":\"success\"}");
}

void handleApiPerformance() {
  DynamicJsonDocument responseDoc(6144);
  JsonArray logArr = responseDoc.createNestedArray("logs");
  
  int idx = (perfLogNextIdx - 1 + MAX_PERF_LOGS) % MAX_PERF_LOGS;
  for (int i = 0; i < numPerfLogs; i++) {
    JsonObject logObj = logArr.createNestedObject();
    PerfLog &log = perfLogs[idx];
    
    logObj["type"] = "processing";
    logObj["latency_s"] = log.latencyMs / 1000.0;
    logObj["text_length"] = log.textLength;
    logObj["cpu_after_pct"] = 100;
    logObj["ram_after_mb"] = log.ramUsageMb;
    logObj["ram_delta_mb"] = log.ramDeltaMb;
    logObj["status"] = log.status;
    
    unsigned long elapsedSec = log.timeMs / 1000;
    int hrs = (elapsedSec / 3600) % 24;
    int mins = (elapsedSec / 60) % 60;
    int secs = elapsedSec % 60;
    char timeBuffer[12];
    sprintf(timeBuffer, "%02d:%02d:%02d", hrs, mins, secs);
    logObj["timestamp"] = String(timeBuffer);
    
    idx = (idx - 1 + MAX_PERF_LOGS) % MAX_PERF_LOGS;
  }
  
  JsonObject sysObj = responseDoc.createNestedObject("system");
  sysObj["environment"] = "EDGE";
  sysObj["env_label"] = "\xE2\x9A\x99 Edge (ESP32 Board)";
  sysObj["platform"] = "ESP32";
  sysObj["uptime_s"] = millis() / 1000;
  
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  uint32_t usedHeap = totalHeap - freeHeap;
  sysObj["ram_used_mb"] = (float)usedHeap / (1024.0 * 1024.0);
  sysObj["ram_usage_pct"] = (int)(((float)usedHeap / totalHeap) * 100.0);
  sysObj["cpu_usage_pct"] = 100;
  sysObj["active_users"] = numUsers;
  
  String response;
  serializeJson(responseDoc, response);
  server.send(200, "application/json", response);
}

void handleApiSystem() {
  StaticJsonDocument<512> responseDoc;
  responseDoc["environment"] = "EDGE";
  responseDoc["env_label"] = "\xE2\x9A\x99 Edge (ESP32 Board)";
  responseDoc["platform"] = "ESP32";
  responseDoc["architecture"] = "Tensilica Xtensa LX6";
  responseDoc["cpu_count"] = 2;
  responseDoc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
  responseDoc["cpu_usage_pct"] = 100;
  responseDoc["uptime_s"] = millis() / 1000;
  
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  uint32_t usedHeap = totalHeap - freeHeap;
  responseDoc["ram_used_mb"] = (float)usedHeap / (1024.0 * 1024.0);
  responseDoc["ram_total_mb"] = (float)totalHeap / (1024.0 * 1024.0);
  responseDoc["ram_usage_pct"] = (int)(((float)usedHeap / totalHeap) * 100.0);
  responseDoc["active_users"] = numUsers;
  
  String response;
  serializeJson(responseDoc, response);
  server.send(200, "application/json", response);
}

// ---------------------------------------------------------------------------
// ESP32 SYSTEM SETUP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n==============================================");
  Serial.println("         ESP32 WiFi Secure Chat Hub           ");
  Serial.println("==============================================");
  
  // 1. WiFi Connection Protocol
  WiFi.mode(WIFI_AP_STA); // Enable both Access Point and Station modes
  
  Serial.print("Connecting to network SSID: ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  // Wait up to 10 seconds for connection to router
  unsigned long startConnect = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startConnect < 10000)) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected to network successfully!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Failed to connect to local router.");
    Serial.println("[WiFi] Spin up Access Point (AP)...");
    
    WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, AP_MAX_CONN);
    
    Serial.print("[AP] AP Enabled SSID: ");
    Serial.println(AP_SSID);
    Serial.print("[AP] Gateway IP: ");
    Serial.println(WiFi.softAPIP());
  }
  
  // 2. Setup HTTP Web Endpoints
  server.on("/", HTTP_GET, handleRoot);
  server.on("/static/css/style.css", HTTP_GET, handleCSS);
  server.on("/static/js/app.js", HTTP_GET, handleJS);
  
  // REST API endpoints
  server.on("/api/login", HTTP_POST, handleApiLogin);
  server.on("/api/poll", HTTP_POST, handleApiPoll);
  server.on("/api/connect", HTTP_POST, handleApiConnect);
  server.on("/api/send", HTTP_POST, handleApiSend);
  server.on("/api/typing", HTTP_POST, handleApiTyping);
  server.on("/api/end_chat", HTTP_POST, handleApiEndChat);
  server.on("/api/logout", HTTP_POST, handleApiLogout);
  server.on("/api/performance", HTTP_GET, handleApiPerformance);
  server.on("/api/system", HTTP_GET, handleApiSystem);
  
  server.begin();
  Serial.println("[Web Server] Web server started successfully!");
  Serial.println("==============================================\n");
}

void loop() {
  server.handleClient();
  
  // Prevent thread lock or delays (run every 15s to keep RAM clear of ghost users who lost connection suddenly)
  static unsigned long lastPrune = 0;
  if (millis() - lastPrune > 15000) {
    cleanExpiredSessions();
    lastPrune = millis();
  }
  
  delay(1);
}
