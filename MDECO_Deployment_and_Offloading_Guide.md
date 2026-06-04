# Multi-Criteria Deadline-Constrained Edge–Cloud Offloading (MDECO) — Deployment & Benchmarking Guide

This guide describes how to run, link, deploy, and benchmark the **Multi-Criteria Deadline-Constrained Edge-Cloud Offloading (MDECO)** framework across three heterogeneous tiers: **Local Device Laptop**, **LAN Edge Laptop Server**, and **Cloud Render Server**.

---

## 1. System Overview & Offloading Architectures

The MDECO coordinator runs locally on the user's laptop at `http://localhost:5002/mdeco`. It divides test request workloads dynamically between three configured server nodes:

| Node Tier | Network Mode | Typical RTT | Energy Footprint | Primary Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **Device Laptop** (Localhost) | Local Loopback | < 1 ms | High Local Power (15-35W) | Ultra-fast execution, zero network delay. |
| **Edge Laptop** (LAN Server) | Local WiFi | 5–25 ms | Medium Local Power (15-35W) | Local network processing, eco-friendly offload. |
| **Cloud Server** (Render WAN) | Public Internet | 90–250 ms | Scalable, high WAN RTT | Elastic fallback, high radio transmission energy. |

### Dynamic Optimization Modes
1. **Lowest Latency**: Allocation is divided based on inverse RTT score: $Score_i = 1 / RTT_i$. Cloud WAN gets negligible share if local connections are active.
2. **Lowest Energy**: Allocation is divided based on inverse estimated processing and radio transmission energy: $Score_i = 1 / Energy_i$.
3. **Balanced (Cost-Based)**: Allocates workload to minimize the cost function: $Cost_i = 0.5 \times Latency_i + 0.3 \times CPU_i + 0.2 \times Energy_i$.
4. **Deadline Constrained (MDECO)**: Required throughput is calculated as $T_{req} = Tests / Deadline$. Share of each node is determined by its active capacity: $Cap_i = Throughput_i \times (1.0 - CPU_i)$. If CPU load increases, the scheduler dynamically shifts allocations in real time (every 2 seconds).
5. **Less Energy Consumption**: Overrides Cloud allocation to 0% to prevent high WAN radio power transmission, splitting workload strictly between local loop and LAN Edge nodes.

---

## 2. Local Device Run Guide (Main Laptop)

Run the coordinator server locally on your main laptop:

### Step 1: Initialize and Activate Virtual Environment
Open PowerShell (Windows) or Terminal (macOS/Linux) in the project directory:
```powershell
cd d:\EdgeAi_resistor_Dl_model\Edge_AI-Computing\wifi_chat_app
python -m venv venv
```
Activate it:
- **On Windows (PowerShell)**:
  ```powershell
  .\venv\Scripts\Activate.ps1
  ```
- **On macOS/Linux**:
  ```bash
  source venv/bin/activate
  ```

### Step 2: Install Dependencies
```powershell
pip install -r requirements.txt
```

### Step 3: Run the Server
```powershell
python app.py
```
Open your browser and navigate to: **`http://localhost:5002/mdeco`**

---

## 3. Edge Laptop Build & Run Guide (Second Laptop Server)

Deploy the standalone portable package on your second laptop:

### Step 1: Transfer the Folder
Copy the entire updated `laptop_edge_server/` folder to the second laptop.

### Step 2: Find the WiFi IP Address
On the second laptop, retrieve its IP address on the home WiFi network:
- **On Windows**:
  ```powershell
  ipconfig
  ```
  Look for the IPv4 Address under **Wireless LAN adapter Wi-Fi** (e.g. `192.168.1.15`).
- **On macOS/Linux**:
  ```bash
  ifconfig | grep "inet "
  ```

### Step 3: Build the Docker Container
Open a terminal inside the copied `laptop_edge_server` folder on the second laptop and run:
```bash
docker build -t laptop-edge-chat .
```

### Step 4: Run the Docker Container
Launch the container by forwarding host port `5002` to container port `8080` and setting the environment to `EDGE`:
```bash
docker run -d -p 5002:8080 --name laptop-edge-chat -e CHAT_ENV=EDGE --restart unless-stopped laptop-edge-chat
```
*(Verify your firewall permits inbound traffic on port `5002` on the edge laptop if you encounter connection timeouts)*

> [!WARNING]
> **Do NOT Navigate to `0.0.0.0:8080` in your Browser:**
> While Gunicorn logs will print `Listening at: http://0.0.0.0:8080`, `0.0.0.0` is a special routing code instructing the server to listen on all interfaces. Browsers cannot resolve `0.0.0.0` as a destination address and will throw an `ERR_ADDRESS_INVALID` error page.
> 
> To resolve this, access the container using:
> - **Directly on the Edge Laptop**: Open **`http://localhost:5002`** or **`http://127.0.0.1:5002`**.
> - **From your main Device Laptop (MDECO Dashboard)**: Use the edge laptop's local WiFi IP, e.g. **`http://192.168.1.15:5002`**.

---

## 4. Git Sync & Cloud Re-deployment Guide (Render Cloud Node)

When code is pushed to GitHub, Render automatically detects changes, rebuilds the Docker container, and redeploys the server in the cloud.

### Step 1: Initialize Git and Link to GitHub
Open PowerShell in the project root directory `wifi_chat_app/` (on your local laptop) and configure Git:
```powershell
# Navigate to project root
cd d:\EdgeAi_resistor_Dl_model\Edge_AI-Computing\wifi_chat_app

# Initialize git repository
git init

# Track all modified files
git add .

# Commit changes
git commit -m "Implement MDECO scheduler, dynamic offloading dashboard, and extended timeouts"

# Rename default branch to main
git branch -M main

# Add remote GitHub repository (Replace with your repository URL)
git remote add origin https://github.com/snehvardhan275/wifi-secure-chat.git
```

### Step 2: Push Code to Trigger Render Automatic Rebuild
Push the main branch to GitHub:
```powershell
git push -u origin main -f
```
Once pushed, Render's GitHub connection hook automatically schedules a fresh container assembly.
- Navigate to your [Render Dashboard](https://dashboard.render.com).
- Open the `wifi-secure-chat` service to view the live build logs.
- The cloud server will be accessible at your public DNS URL (e.g. `https://wifi-secure-chat.onrender.com`).

---

## 5. Connecting & Executing offloading Benchmarks

Once all three nodes are running, synchronize them on the MDECO Dashboard:

1. **Configure URLs**:
   - Device: `http://localhost:5002`
   - Edge: `http://<EDGE_WIFI_IP>:5002` (e.g., `http://192.168.1.15:5002`)
   - Cloud: `https://wifi-secure-chat.onrender.com`
2. **Telemetry Verification**:
   - The dashboard will poll nodes and update indicators to **online** once connection is established. It registers/logs in the user `mdeco_tester` in the background to fetch session tokens.
3. **Set Constraints**:
   - Enter **Total Tests** (e.g. `500`) and **Deadline** (e.g. `15` seconds).
   - Select an **Optimization Mode** (e.g. *Deadline Constrained (MDECO)*).
4. **Benchmark Execution**:
   - Click **Run Offload Execution**.
   - Watch the live progress counters per node (`Completed: X / Allocated: Y`) and cumulative energy/latency spend calculations update in real time.
   - The scheduler reasoning log will print decisions, load-shifts, and final summaries.
   - The result is archived inside the **Historical Strategy Archive** for capacity tuning.
