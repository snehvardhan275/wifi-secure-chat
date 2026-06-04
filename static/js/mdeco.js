// ============================================================
// MDECO Scheduler — Multi-Criteria Edge-Cloud Offloading
// Controls load dispatching, dynamic allocation & telemetry.
// ============================================================

// --- Scheduler State ---
let endpoints = {
    device: { url: "http://localhost:5002", token: null, online: false, ping: 0, cpu: 0, ram: 0, power: 0, baseThroughput: 100.0, energyPerReq: 0.00015 },
    edge: { url: "", token: null, online: false, ping: 0, cpu: 0, ram: 0, power: 0, baseThroughput: 70.0, energyPerReq: 0.00045 },
    cloud: { url: "https://wifi-secure-chat.onrender.com", token: null, online: false, ping: 0, cpu: 0, ram: 0, power: 0, baseThroughput: 8.0, energyPerReq: 0.00225 }
};

let activeOffloadRun = false;
let cancelOffloadRun = false;
let allocationChart = null;
let currentAllocations = { device: 0, edge: 0, cloud: 0 };
let runHistory = JSON.parse(localStorage.getItem("mdeco_run_history")) || [];

// --- Live Run Analytics ---
let completedCounts = { device: 0, edge: 0, cloud: 0 };
let activeAllocations = { device: 0, edge: 0, cloud: 0 };
let latencySums = { device: 0, edge: 0, cloud: 0 };
let energySpent = { device: 0, edge: 0, cloud: 0 };

// --- DOM Elements ---
const urlDeviceInput = document.getElementById("url-device");
const urlEdgeInput = document.getElementById("url-edge");
const urlCloudInput = document.getElementById("url-cloud");

const statusDevice = document.getElementById("status-device");
const statusEdge = document.getElementById("status-edge");
const statusCloud = document.getElementById("status-cloud");

const testsCountInput = document.getElementById("tests-count");
const testsDeadlineInput = document.getElementById("tests-deadline");
const optModes = document.getElementsByName("opt-mode");

const btnRunOffload = document.getElementById("btn-run-offload");
const btnCancelOffload = document.getElementById("btn-cancel-offload");

const decEstTime = document.getElementById("dec-est-time");
const decStatus = document.getElementById("dec-status");
const decProgress = document.getElementById("dec-progress");
const progressBarFill = document.getElementById("progress-bar-fill");

const allocDevice = document.getElementById("alloc-device");
const allocEdge = document.getElementById("alloc-edge");
const allocCloud = document.getElementById("alloc-cloud");

const progressDevice = document.getElementById("progress-device");
const progressEdge = document.getElementById("progress-edge");
const progressCloud = document.getElementById("progress-cloud");

const liveRunCompletedDevice = document.getElementById("live-run-completed-device");
const liveRunCompletedEdge = document.getElementById("live-run-completed-edge");
const liveRunCompletedCloud = document.getElementById("live-run-completed-cloud");

const liveRunRttDevice = document.getElementById("live-run-rtt-device");
const liveRunRttEdge = document.getElementById("live-run-rtt-edge");
const liveRunRttCloud = document.getElementById("live-run-rtt-cloud");

const liveRunEnergyDevice = document.getElementById("live-run-energy-device");
const liveRunEnergyEdge = document.getElementById("live-run-energy-edge");
const liveRunEnergyCloud = document.getElementById("live-run-energy-cloud");

const historyLogBody = document.getElementById("history-log-body");
const tuningInsights = document.getElementById("tuning-insights");
const schedulerReasoningLog = document.getElementById("scheduler-reasoning-log");

// --- Telemetry Indicators ---
const indicators = {
    device: { ping: document.getElementById("stat-dev-ping"), cpu: document.getElementById("stat-dev-cpu"), ram: document.getElementById("stat-dev-ram"), power: document.getElementById("stat-dev-power") },
    edge: { ping: document.getElementById("stat-edge-ping"), cpu: document.getElementById("stat-edge-cpu"), ram: document.getElementById("stat-edge-ram"), power: document.getElementById("stat-edge-power") },
    cloud: { ping: document.getElementById("stat-cloud-ping"), cpu: document.getElementById("stat-cloud-cpu"), ram: document.getElementById("stat-cloud-ram"), power: document.getElementById("stat-cloud-power") }
};

// ============================================================
// Initialization
// ============================================================
document.addEventListener("DOMContentLoaded", () => {
    // Read input values
    loadInputsFromStorage();
    
    // Setup Chart.js
    initOffloadChart();

    // Load History log
    renderHistory();
    renderTuningInsights();

    // Event listeners
    btnRunOffload.addEventListener("click", startOffloadExecution);
    btnCancelOffload.addEventListener("click", cancelExecution);

    [urlDeviceInput, urlEdgeInput, urlCloudInput, testsCountInput, testsDeadlineInput].forEach(el => {
        if (el) el.addEventListener("change", () => {
            saveInputsToStorage();
            updateScheduler();
        });
    });

    optModes.forEach(radio => {
        radio.addEventListener("change", () => {
            updateScheduler(true);
        });
    });

    // Run telemetry loop
    pollTelemetry();
    setInterval(pollTelemetry, 3000);
});

function loadInputsFromStorage() {
    if (localStorage.getItem("mdeco_url_device")) urlDeviceInput.value = localStorage.getItem("mdeco_url_device");
    if (localStorage.getItem("mdeco_url_edge")) urlEdgeInput.value = localStorage.getItem("mdeco_url_edge");
    if (localStorage.getItem("mdeco_url_cloud")) urlCloudInput.value = localStorage.getItem("mdeco_url_cloud");
    if (localStorage.getItem("mdeco_tests_count")) testsCountInput.value = localStorage.getItem("mdeco_tests_count");
    if (localStorage.getItem("mdeco_tests_deadline")) testsDeadlineInput.value = localStorage.getItem("mdeco_tests_deadline");
}

function saveInputsToStorage() {
    localStorage.setItem("mdeco_url_device", urlDeviceInput.value);
    localStorage.setItem("mdeco_url_edge", urlEdgeInput.value);
    localStorage.setItem("mdeco_url_cloud", urlCloudInput.value);
    localStorage.setItem("mdeco_tests_count", testsCountInput.value);
    localStorage.setItem("mdeco_tests_deadline", testsDeadlineInput.value);
}

function logReasoning(text) {
    if (!schedulerReasoningLog) return;
    const time = new Date().toLocaleTimeString();
    schedulerReasoningLog.innerHTML = `[${time}] ${text}<br>` + schedulerReasoningLog.innerHTML;
}

// ============================================================
// Telemetry & Node Testing
// ============================================================
async function pollTelemetry() {
    endpoints.device.url = urlDeviceInput.value.trim();
    endpoints.edge.url = urlEdgeInput.value.trim();
    endpoints.cloud.url = urlCloudInput.value.trim();

    const nodes = ["device", "edge", "cloud"];
    
    const polls = nodes.map(async node => {
        const url = endpoints[node].url;
        if (!url) {
            markOffline(node, "configured");
            return;
        }

        const start = performance.now();
        try {
            const controller = new AbortController();
            const id = setTimeout(() => controller.abort(), 2000); // 2s timeout
            
            const res = await fetch(`${url}/api/system`, {
                method: "GET",
                signal: controller.signal
            });
            clearTimeout(id);

            const elapsed = Math.round(performance.now() - start);

            if (res.ok) {
                const data = await res.json();
                endpoints[node].online = true;
                endpoints[node].ping = elapsed;
                endpoints[node].cpu = data.cpu_usage_pct !== undefined ? data.cpu_usage_pct : 10;
                endpoints[node].ram = data.ram_usage_pct !== undefined ? data.ram_usage_pct : 25;
                
                // Update UI indicators
                updateNodeStatusLabel(node, true);
                updateNodeTelemetryUI(node);
                
                // If not logged in yet, trigger auto login
                if (!endpoints[node].token) {
                    autoLoginNode(node);
                }
            } else {
                markOffline(node, "unresponsive");
            }
        } catch (e) {
            markOffline(node, "unreachable");
        }
    });

    await Promise.all(polls);
    
    if (!activeOffloadRun) {
        updateScheduler(false); // updates variables without flooding reasoning logs
    }
}

function markOffline(node, reason) {
    endpoints[node].online = false;
    endpoints[node].ping = 999;
    endpoints[node].cpu = 0;
    endpoints[node].ram = 0;
    updateNodeStatusLabel(node, false, reason);
    updateNodeTelemetryUI(node, true);
}

function updateNodeStatusLabel(node, online, reason = "") {
    const el = document.getElementById(`status-${node}`);
    if (!el) return;
    if (online) {
        el.textContent = "online";
        el.className = "status-indicator connected";
    } else {
        el.textContent = reason ? reason : "offline";
        el.className = "status-indicator disconnected";
    }
}

function updateNodeTelemetryUI(node, clear = false) {
    const ind = indicators[node];
    if (!ind || !ind.ping) return;
    if (clear) {
        ind.ping.textContent = "—";
        ind.cpu.textContent = "—";
        ind.ram.textContent = "—";
        ind.power.textContent = "—";
        return;
    }
    
    ind.ping.textContent = `${endpoints[node].ping} ms`;
    ind.cpu.textContent = `${endpoints[node].cpu}%`;
    ind.ram.textContent = `${endpoints[node].ram}%`;

    // Dynamic power estimation label based on node type and CPU
    let powerW = 0;
    if (node === "device") {
        powerW = 5.0 + (35.0 - 5.0) * (endpoints[node].cpu / 100.0);
    } else if (node === "edge") {
        powerW = 5.0 + (35.0 - 5.0) * (endpoints[node].cpu / 100.0);
    } else {
        powerW = 2.0 + (20.0 - 2.0) * (endpoints[node].cpu / 100.0);
    }
    endpoints[node].power = powerW;
    ind.power.textContent = `${powerW.toFixed(1)} W`;
}

// Auto registration and login protocol
async function autoLoginNode(node) {
    const url = endpoints[node].url;
    if (!url) return;

    try {
        const res = await fetch(`${url}/api/login`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ name: "mdeco_tester", password: "1234!@#$" })
        });
        if (res.ok) {
            const data = await res.json();
            endpoints[node].token = data.session_token;
            console.log(`[MDECO] Successfully authenticated with ${node} server!`);
            logReasoning(`Successfully logged in to ${node} node.`);
        }
    } catch (e) {
        console.warn(`[MDECO] Failed to authenticate with ${node}`, e);
    }
}

// ============================================================
// MDECO Scheduler Decision Logic
// ============================================================
function getActiveMode() {
    let mode = "LATENCY";
    optModes.forEach(radio => {
        if (radio.checked) mode = radio.value;
    });
    return mode;
}

function updateScheduler(logChange = false) {
    const mode = getActiveMode();
    const tests = parseInt(testsCountInput.value) || 100;
    const deadline = parseFloat(testsDeadlineInput.value) || 20;

    let deviceShare = 0;
    let edgeShare = 0;
    let cloudShare = 0;

    const dev = endpoints.device;
    const edg = endpoints.edge;
    const cld = endpoints.cloud;

    // Apply historical database corrections to base throughput estimates
    applyHistoricalCorrections();

    if (mode === "LATENCY") {
        // Higher share to nodes with lower RTT
        let devWeight = dev.online ? (1.0 / Math.max(dev.ping, 1)) : 0;
        let edgWeight = edg.online ? (1.0 / Math.max(edg.ping, 1)) : 0;
        let cldWeight = cld.online ? (1.0 / Math.max(cld.ping, 1)) : 0;

        let totalWeight = devWeight + edgWeight + cldWeight;
        if (totalWeight > 0) {
            deviceShare = devWeight / totalWeight;
            edgeShare = edgWeight / totalWeight;
            cloudShare = cldWeight / totalWeight;
        } else {
            deviceShare = 1.0;
        }

        if (logChange) {
            logReasoning(`Selected Latency Optimization. Work split based on network RTT: Laptop (${dev.ping}ms) | Edge (${edg.ping}ms) | Cloud (${cld.ping}ms).`);
        }

    } else if (mode === "ENERGY") {
        // Higher share to nodes with lower estimated energy per request
        // E = Power * ProcessingTime + NetworkTransmissionEnergy
        let devEnergy = dev.energyPerReq;
        let edgEnergy = edg.energyPerReq + (edg.ping / 1000.0) * 1.0; // WiFi active transmission
        let cldEnergy = cld.energyPerReq + (cld.ping / 1000.0) * 1.5; // WAN active transmission

        let devWeight = dev.online ? (1.0 / devEnergy) : 0;
        let edgWeight = edg.online ? (1.0 / edgEnergy) : 0;
        let cldWeight = cld.online ? (1.0 / cldEnergy) : 0;

        let totalWeight = devWeight + edgWeight + cldWeight;
        if (totalWeight > 0) {
            deviceShare = devWeight / totalWeight;
            edgeShare = edgWeight / totalWeight;
            cloudShare = cldWeight / totalWeight;
        }

        if (logChange) {
            logReasoning(`Selected Energy Optimization. Energy estimates: Laptop (${formatEnergy(devEnergy)}) | Edge (${formatEnergy(edgEnergy)}) | Cloud (${formatEnergy(cldEnergy)}).`);
        }

    } else if (mode === "BALANCED") {
        // Cost = 0.5 * Latency + 0.3 * CPU + 0.2 * Energy
        const getCost = (nodeKey, n) => {
            if (!n.online) return 99999;
            let latency = n.ping; // ms
            let cpu = n.cpu;
            let energy = n.energyPerReq * 1000000; // micro Joules
            if (nodeKey === "edge") energy += (n.ping / 1000.0) * 1.0 * 1000000;
            if (nodeKey === "cloud") energy += (n.ping / 1000.0) * 1.5 * 1000000;

            return (0.5 * latency) + (0.3 * cpu) + (0.2 * energy * 0.001);
        };

        let devCost = getCost("device", dev);
        let edgCost = getCost("edge", edg);
        let cldCost = getCost("cloud", cld);

        let devWeight = dev.online ? (1.0 / Math.max(devCost, 0.01)) : 0;
        let edgWeight = edg.online ? (1.0 / Math.max(edgCost, 0.01)) : 0;
        let cldWeight = cld.online ? (1.0 / Math.max(cldCost, 0.01)) : 0;

        let totalWeight = devWeight + edgWeight + cldWeight;
        if (totalWeight > 0) {
            deviceShare = devWeight / totalWeight;
            edgeShare = edgWeight / totalWeight;
            cloudShare = cldWeight / totalWeight;
        }

        if (logChange) {
            logReasoning(`Selected Balanced Optimization. Score Cost calculated: Laptop (${devCost.toFixed(0)}) | Edge (${edgCost.toFixed(0)}) | Cloud (${cldCost.toFixed(0)}). Proportional allocation applied.`);
        }

    } else if (mode === "DEADLINE") {
        // Multi-Criteria Deadline-Constrained Edge-Cloud Offloading (MDECO)
        // Cap_i = Throughput_i * (1.0 - CPU_i / 100.0)
        let devCap = dev.online ? dev.baseThroughput * (1.0 - (dev.cpu / 100.0)) : 0;
        let edgCap = edg.online ? edg.baseThroughput * (1.0 - (edg.cpu / 100.0)) : 0;
        let cldCap = cld.online ? cld.baseThroughput * (1.0 - (cld.cpu / 100.0)) : 0;

        let totalCap = devCap + edgCap + cldCap;
        if (totalCap > 0) {
            deviceShare = devCap / totalCap;
            edgeShare = edgCap / totalCap;
            cloudShare = cldCap / totalCap;
        }

        if (logChange) {
            const reqThr = tests / deadline;
            logReasoning(`Selected MDECO Deadline Scheduler. Req. throughput: ${reqThr.toFixed(1)} req/s. Current capacities: Laptop (${devCap.toFixed(1)}) | Edge (${edgCap.toFixed(1)}) | Cloud (${cldCap.toFixed(1)}) req/s.`);
        }

    } else if (mode === "LOW_ENERGY") {
        // Optimize purely for green low-energy usage by offloading to local and edge, bypass cloud WAN
        let devEnergy = dev.energyPerReq;
        let edgEnergy = edg.energyPerReq + (edg.ping / 1000.0) * 1.0;

        let devWeight = dev.online ? (1.0 / devEnergy) : 0;
        let edgWeight = edg.online ? (1.0 / edgEnergy) : 0;

        let totalWeight = devWeight + edgWeight;
        if (totalWeight > 0) {
            deviceShare = devWeight / totalWeight;
            edgeShare = edgWeight / totalWeight;
            cloudShare = 0.0;
        }

        if (logChange) {
            logReasoning(`Selected Less Energy Mode. Forcing Cloud allocation to 0% to avoid WAN radio power overhead.`);
        }
    }

    // Split absolute tests counts
    currentAllocations.device = Math.round(tests * deviceShare);
    currentAllocations.edge = Math.round(tests * edgeShare);
    currentAllocations.cloud = Math.round(tests - currentAllocations.device - currentAllocations.edge);

    // Guard against negative allocations
    if (currentAllocations.cloud < 0) currentAllocations.cloud = 0;

    // Estimate completion time
    // Time = Tests_i / Capacity_i
    let activeDevThru = dev.online ? dev.baseThroughput * (1.0 - (dev.cpu / 100.0)) : 0.0001;
    let activeEdgThru = edg.online ? edg.baseThroughput * (1.0 - (edg.cpu / 100.0)) : 0.0001;
    let activeCldThru = cld.online ? cld.baseThroughput * (1.0 - (cld.cpu / 100.0)) : 0.0001;

    let devTime = currentAllocations.device > 0 ? (currentAllocations.device / activeDevThru) : 0;
    let edgTime = currentAllocations.edge > 0 ? (currentAllocations.edge / activeEdgThru) : 0;
    let cldTime = currentAllocations.cloud > 0 ? (currentAllocations.cloud / activeCldThru) : 0;

    let estTimeSec = Math.max(devTime, edgTime, cldTime);
    if (isNaN(estTimeSec) || estTimeSec === Infinity) estTimeSec = 0;

    // Render results to UI
    decEstTime.textContent = `${estTimeSec.toFixed(1)} s`;
    
    // Verify constraints
    const decSatCard = document.getElementById("dec-sat-card");
    if (estTimeSec <= deadline && (currentAllocations.device > 0 || currentAllocations.edge > 0 || currentAllocations.cloud > 0)) {
        decStatus.textContent = "✓ SATISFIED";
        if (decSatCard) {
            decSatCard.className = "decision-metric-card sat-yes";
        }
    } else {
        decStatus.textContent = "✗ VIOLATED";
        if (decSatCard) {
            decSatCard.className = "decision-metric-card sat-no";
        }
    }

    allocDevice.textContent = `${currentAllocations.device} req (${Math.round(deviceShare * 100)}%)`;
    allocEdge.textContent = `${currentAllocations.edge} req (${Math.round(edgeShare * 100)}%)`;
    allocCloud.textContent = `${currentAllocations.cloud} req (${Math.round(cloudShare * 100)}%)`;

    // Only update progress labels when NOT executing to prevent flickering initial values
    if (!activeOffloadRun) {
        progressDevice.textContent = `Completed: 0 / ${currentAllocations.device}`;
        progressEdge.textContent = `Completed: 0 / ${currentAllocations.edge}`;
        progressCloud.textContent = `Completed: 0 / ${currentAllocations.cloud}`;
        
        liveRunCompletedDevice.textContent = `0 / ${currentAllocations.device}`;
        liveRunCompletedEdge.textContent = `0 / ${currentAllocations.edge}`;
        liveRunCompletedCloud.textContent = `0 / ${currentAllocations.cloud}`;
    }

    updateOffloadChartData();
}

function applyHistoricalCorrections() {
    if (runHistory.length === 0) return;
    
    // Compute exponential moving averages from successful runs to adjust base throughput capacities
    const nodes = ["device", "edge", "cloud"];
    
    nodes.forEach(node => {
        let nodeRuns = runHistory.filter(r => r.status === "success" && r.allocations && r.allocations[node] > 0);
        if (nodeRuns.length > 0) {
            let totalActualThroughput = 0;
            let count = 0;
            
            nodeRuns.forEach(run => {
                let nodeRequests = run.allocations[node];
                let duration = run.actualTime;
                if (duration > 0) {
                    let observedThroughput = nodeRequests / duration;
                    totalActualThroughput += observedThroughput;
                    count++;
                }
            });
            
            if (count > 0) {
                let averageActual = totalActualThroughput / count;
                // Blend historical observed throughput with theoretical base
                let blended = (averageActual * 0.40) + (endpoints[node].baseThroughput * 0.60);
                endpoints[node].baseThroughput = Math.round(blended * 10) / 10;
            }
        }
    });
}

function renderTuningInsights() {
    if (!tuningInsights) return;
    
    if (runHistory.length === 0) {
        tuningInsights.innerHTML = `<p style="font-style: italic; color: var(--text-muted); text-align: center; margin-top: 2rem;">No historical records found for this configuration. Executing runs will populate training profiles.</p>`;
        return;
    }

    let dev = endpoints.device;
    let edg = endpoints.edge;
    let cld = endpoints.cloud;

    tuningInsights.innerHTML = `
        <div class="system-grid">
            <div style="font-weight: 700; margin-bottom: 0.2rem; color: var(--text-primary);">Historical Capacity Models:</div>
            <div class="sys-row"><span class="sys-label">Device Throughput Bias</span><span class="sys-value">${dev.baseThroughput} req/s</span></div>
            <div class="sys-row"><span class="sys-label">Edge Throughput Bias</span><span class="sys-value">${edg.baseThroughput} req/s</span></div>
            <div class="sys-row"><span class="sys-label">Cloud Throughput Bias</span><span class="sys-value">${cld.baseThroughput} req/s</span></div>
            <div class="sys-row" style="margin-top: 0.5rem;"><span class="sys-label">Logged Runs</span><span class="sys-value" style="color: var(--accent-primary); font-weight:700;">${runHistory.length} profiles</span></div>
        </div>
    `;
}

// Update live progress table metrics
function updateLiveProgressUI() {
    progressDevice.textContent = `Completed: ${completedCounts.device} / ${activeAllocations.device}`;
    progressEdge.textContent = `Completed: ${completedCounts.edge} / ${activeAllocations.edge}`;
    progressCloud.textContent = `Completed: ${completedCounts.cloud} / ${activeAllocations.cloud}`;

    liveRunCompletedDevice.textContent = `${completedCounts.device} / ${activeAllocations.device}`;
    liveRunCompletedEdge.textContent = `${completedCounts.edge} / ${activeAllocations.edge}`;
    liveRunCompletedCloud.textContent = `${completedCounts.cloud} / ${activeAllocations.cloud}`;

    liveRunRttDevice.textContent = (completedCounts.device > 0 ? (latencySums.device / completedCounts.device).toFixed(1) : "0.0") + " ms";
    liveRunRttEdge.textContent = (completedCounts.edge > 0 ? (latencySums.edge / completedCounts.edge).toFixed(1) : "0.0") + " ms";
    liveRunRttCloud.textContent = (completedCounts.cloud > 0 ? (latencySums.cloud / completedCounts.cloud).toFixed(1) : "0.0") + " ms";

    liveRunEnergyDevice.textContent = formatEnergy(energySpent.device);
    liveRunEnergyEdge.textContent = formatEnergy(energySpent.edge);
    liveRunEnergyCloud.textContent = formatEnergy(energySpent.cloud);
}

// ============================================================
// Load Generator Execution Loop
// ============================================================
async function startOffloadExecution() {
    if (activeOffloadRun) return;
    
    // Ensure nodes are authenticated
    const inactiveNodes = Object.keys(endpoints).filter(n => endpoints[n].online && !endpoints[n].token);
    if (inactiveNodes.length > 0) {
        alert("Authenticating nodes... please wait 1 second and click again.");
        inactiveNodes.forEach(n => autoLoginNode(n));
        return;
    }

    activeOffloadRun = true;
    cancelOffloadRun = false;

    // Toggle button views
    btnRunOffload.classList.add("hidden");
    btnCancelOffload.classList.remove("hidden");

    const tests = parseInt(testsCountInput.value) || 100;
    const deadline = parseFloat(testsDeadlineInput.value) || 20;
    const mode = getActiveMode();

    progressBarFill.style.width = "0%";
    decProgress.textContent = "0%";

    // Reset analytics metrics
    completedCounts.device = 0;
    completedCounts.edge = 0;
    completedCounts.cloud = 0;
    
    activeAllocations.device = currentAllocations.device;
    activeAllocations.edge = currentAllocations.edge;
    activeAllocations.cloud = currentAllocations.cloud;

    latencySums.device = 0;
    latencySums.edge = 0;
    latencySums.cloud = 0;

    energySpent.device = 0;
    energySpent.edge = 0;
    energySpent.cloud = 0;

    updateLiveProgressUI();
    logReasoning(`Offload Dispatch Initialized. Running ${tests} requests (Mode: ${mode}). Allocated splits: Device (${activeAllocations.device}) | Edge (${activeAllocations.edge}) | Cloud (${activeAllocations.cloud}).`);

    let totalCompleted = 0;
    let totalEnergyJ = 0;
    let totalFailed = 0;

    const startAll = performance.now();
    const batchSize = 10;
    let batchAllocations = { ...currentAllocations };

    // Dynamic telemetry adjustments during execution loop
    const telemetryInterval = setInterval(() => {
        if (!cancelOffloadRun) {
            // Poll system stats to get live CPU/RAM updates
            pollTelemetry();

            if (mode === "DEADLINE") {
                const remainingTests = tests - totalCompleted;
                if (remainingTests > 0) {
                    let devCap = endpoints.device.online ? endpoints.device.baseThroughput * (1.0 - (endpoints.device.cpu / 100.0)) : 0;
                    let edgCap = endpoints.edge.online ? endpoints.edge.baseThroughput * (1.0 - (endpoints.edge.cpu / 100.0)) : 0;
                    let cldCap = endpoints.cloud.online ? endpoints.cloud.baseThroughput * (1.0 - (endpoints.cloud.cpu / 100.0)) : 0;

                    let totalCap = devCap + edgCap + cldCap;
                    if (totalCap > 0) {
                        // Re-distribute the REMAINING workload
                        let devRem = Math.round(remainingTests * (devCap / totalCap));
                        let edgRem = Math.round(remainingTests * (edgCap / totalCap));
                        let cldRem = Math.max(0, remainingTests - devRem - edgRem);

                        batchAllocations.device = devRem;
                        batchAllocations.edge = edgRem;
                        batchAllocations.cloud = cldRem;

                        // Update the active targets
                        activeAllocations.device = completedCounts.device + devRem;
                        activeAllocations.edge = completedCounts.edge + edgRem;
                        activeAllocations.cloud = completedCounts.cloud + cldRem;

                        logReasoning(`Re-scheduler: Shift triggered. New targets: Device (${activeAllocations.device}) | Edge (${activeAllocations.edge}) | Cloud (${activeAllocations.cloud}).`);
                        
                        updateLiveProgressUI();
                    }
                }
            }
        }
    }, 2000);

    // Launch requests workers
    const dispatchNodeRequest = async (node) => {
        const url = endpoints[node].url;
        const token = endpoints[node].token;
        if (!url || !token) return false;

        const requestStart = performance.now();
        try {
            const res = await fetch(`${url}/api/send`, {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ session_token: token, text: "MDECO Mock payload message dispatch." })
            });

            const elapsed = performance.now() - requestStart;
            latencySums[node] += elapsed;

            if (res.ok) {
                const data = await res.json();
                const elapsedS = elapsed / 1000.0;
                let energyVal = data.perf ? data.perf.energy_j : 0;
                if (node === "edge") energyVal += elapsedS * 1.0;
                if (node === "cloud") energyVal += elapsedS * 1.5;
                
                energySpent[node] += energyVal;
                totalEnergyJ += energyVal;
                completedCounts[node]++;
                updateLiveProgressUI();
                return true;
            }
        } catch (e) {
            console.error(`Request to ${node} failed`, e);
        }
        return false;
    };

    // Run execution batches
    while (totalCompleted < tests && !cancelOffloadRun) {
        const batchPromises = [];
        
        // Spawn batch parallel workers matching allocations
        let devWork = Math.min(batchAllocations.device, batchSize);
        let edgWork = Math.min(batchAllocations.edge, batchSize);
        let cldWork = Math.min(batchAllocations.cloud, batchSize);

        // Discard workers from finished allocations
        batchAllocations.device -= devWork;
        batchAllocations.edge -= edgWork;
        batchAllocations.cloud -= cldWork;

        for (let i = 0; i < devWork; i++) {
            batchPromises.push(dispatchNodeRequest("device"));
        }
        for (let i = 0; i < edgWork; i++) {
            batchPromises.push(dispatchNodeRequest("edge"));
        }
        for (let i = 0; i < cldWork; i++) {
            batchPromises.push(dispatchNodeRequest("cloud"));
        }

        if (batchPromises.length === 0) {
            break;
        }

        const results = await Promise.all(batchPromises);
        results.forEach(success => {
            if (success) {
                totalCompleted++;
            } else {
                totalFailed++;
            }
        });

        // Update progress UI
        let progressPct = Math.round((totalCompleted / tests) * 100);
        progressBarFill.style.width = `${progressPct}%`;
        decProgress.textContent = `${progressPct}%`;

        // Small batch throttling delay (50ms) to prevent freezing client UI
        await new Promise(r => setTimeout(r, 50));
    }

    clearInterval(telemetryInterval);
    const elapsedAllS = (performance.now() - startAll) / 1000.0;

    activeOffloadRun = false;
    btnRunOffload.classList.remove("hidden");
    btnCancelOffload.classList.add("hidden");

    if (cancelOffloadRun) {
        logReasoning("Offload execution aborted by user command.");
        alert("Offloading execution run cancelled.");
        progressBarFill.style.width = "0%";
        decProgress.textContent = "0%";
        return;
    }

    // Save test in localStorage history log database
    const finalStatus = (elapsedAllS <= deadline && totalFailed === 0) ? "success" : "violated";
    const runResult = {
        id: `run_${Date.now()}`,
        mode: mode,
        tests: tests,
        deadline: deadline,
        actualTime: parseFloat(elapsedAllS.toFixed(2)),
        energyJ: parseFloat(totalEnergyJ.toFixed(3)),
        allocations: { ...currentAllocations },
        status: finalStatus
    };

    runHistory.unshift(runResult);
    if (runHistory.length > 30) runHistory.pop(); // keep last 30
    localStorage.setItem("mdeco_run_history", JSON.stringify(runHistory));

    // Refresh display
    renderHistory();
    renderTuningInsights();

    // Final result log
    logReasoning(`Execution Finished. Total Time: ${elapsedAllS.toFixed(2)}s (Limit: ${deadline}s) | Energy spent: ${totalEnergyJ.toFixed(3)} J | Status: ${finalStatus.toUpperCase()}`);

    if (finalStatus === "success") {
        alert(`Success! Load finished in ${elapsedAllS.toFixed(2)} seconds (Goal: ${deadline}s). Constraints fully satisfied.`);
    } else {
        alert(`Warning! Load finished in ${elapsedAllS.toFixed(2)} seconds. Constraints violated (Goal: ${deadline}s).`);
    }
}

function cancelExecution() {
    cancelOffloadRun = true;
}

// ============================================================
// History Visualizer Renderers
// ============================================================
function renderHistory() {
    if (!historyLogBody) return;
    historyLogBody.innerHTML = "";

    if (runHistory.length === 0) {
        historyLogBody.innerHTML = `<tr><td colspan="8" class="empty-row" style="text-align: center; font-style: italic; color: var(--text-muted); padding: 1rem 0;">No offloading history logged</td></tr>`;
        return;
    }

    runHistory.forEach(r => {
        const row = document.createElement("tr");
        const statusClass = r.status === "success" ? "status-ok" : "status-err";
        
        row.innerHTML = `
            <td>${r.id.substring(4)}</td>
            <td><strong>${r.mode}</strong></td>
            <td>${r.tests} reqs</td>
            <td>${r.deadline} s</td>
            <td>${r.actualTime} s</td>
            <td>${r.energyJ} J</td>
            <td>D:${r.allocations.device} / E:${r.allocations.edge} / C:${r.allocations.cloud}</td>
            <td class="${statusClass}">${r.status === "success" ? "✓ Met" : "✗ Failed"}</td>
        `;
        historyLogBody.appendChild(row);
    });
}

function formatEnergy(val) {
    if (val < 0.001) {
        return (val * 1000000).toFixed(1) + " μJ";
    } else if (val < 1.0) {
        return (val * 1000).toFixed(1) + " mJ";
    } else {
        return val.toFixed(3) + " J";
    }
}

// ============================================================
// Charts Visualization (Chart.js)
// ============================================================
function initOffloadChart() {
    const canvas = document.getElementById("chart-offload-allocation");
    if (!canvas) return;

    if (typeof Chart === "undefined") {
        canvas.parentElement.innerHTML = "<div class='loading-text'>Chart.js offline. Allocation graphics unavailable.</div>";
        return;
    }

    const ctx = canvas.getContext("2d");
    allocationChart = new Chart(ctx, {
        type: "bar",
        data: {
            labels: ["Device (Laptop)", "Edge (LAN Laptop)", "Cloud (Render)"],
            datasets: [{
                label: "Requests Allocated",
                data: [0, 0, 0],
                backgroundColor: [
                    "rgba(99, 102, 241, 0.65)", // Purple/Indigo
                    "rgba(34, 197, 94, 0.65)",  // Green
                    "rgba(245, 158, 11, 0.65)"   // Orange
                ],
                borderColor: [
                    "#6366f1",
                    "#22c55e",
                    "#f59e0b"
                ],
                borderWidth: 1.5,
                borderRadius: 4
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: { display: false },
                tooltip: {
                    backgroundColor: "rgba(17, 24, 39, 0.95)",
                    titleColor: "#f1f5f9",
                    bodyColor: "#94a3b8",
                    borderColor: "rgba(255,255,255,0.08)",
                    borderWidth: 1,
                    cornerRadius: 8
                }
            },
            scales: {
                y: {
                    beginAtZero: true,
                    grid: { color: "rgba(255,255,255,0.04)" },
                    ticks: { color: "#64748b", font: { size: 9 } }
                },
                x: {
                    grid: { display: false },
                    ticks: { color: "#f1f5f9", font: { size: 10, weight: "bold" } }
                }
            }
        }
    });
}

function updateOffloadChartData() {
    if (!allocationChart) return;
    allocationChart.data.datasets[0].data = [
        currentAllocations.device,
        currentAllocations.edge,
        currentAllocations.cloud
    ];
    allocationChart.update();
}
