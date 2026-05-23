# WiFi Secure Chat Application — Deployment Guide

This repository contains a lightweight, responsive, and secure peer-to-peer local chat application designed to run on:
1. **Laptop (Localhost):** Using Python virtual environment.
2. **Cloud (Render/AWS):** Containerized via Docker.
3. **Edge (ESP32 Development Board):** Running as a standalone micro-webserver.

---

## Technical Specifications & Features

- **No Heavy Dependencies:** The application utilizes short-polling heartbeats, making it completely independent of heavy WebSocket engines.
- **Micro-Sized Database:** User sessions and message histories are tracked entirely in-memory using thread-safe structures (Python) and static buffer arrays (C++), optimizing them for low-resource microcontrollers.
- **Aesthetic Glassmorphism:** Features a premium dark-mode styling aligned with modern UI trends.
- **Collapsible Performance Telemetry Dashboard:** Includes a live performance logging sidebar:
  - **RTT Ping:** Measures the actual round-trip network response time.
  - **Uptime, Active Users, CPU, and RAM Stats:** Server resource parameters polled dynamically.
  - **Real Message Processing Latency:** Profiles the exact fraction of a millisecond taken to process and append messages to the database (measured via `time.perf_counter()` on Laptop/Cloud, and `micros()` on ESP32 Edge).
  - **Latency History Line Chart:** Dynamically plots message latencies in real time using Chart.js.
  - **Performance Log Table:** Tracks metrics per message (Text Size, Latency, CPU usage, RAM delta, and Status).
  - **Stress Testing Tool:** Allows running concurrency stress tests directly from the dashboard to benchmark edges, laptops, or cloud servers and calculate throughput (req/s), P50, P95, and P99 latencies.
- **Access Control:** Secured via a single master password (`1234!@#$`). Session tokens are automatically generated on login and must accompany all state queries.
- **Orphan-session Sweeper:** Automatically disconnects idle users and signals their partner to close the chat session after 6 seconds of inactivity.

---

## Directory Architecture

```
wifi_chat_app/
├── app.py                # Python Flask Application
├── config.py             # Multi-environment Python Configuration
├── requirements.txt      # Python dependencies
├── Dockerfile            # Cloud container assembly rules
├── .dockerignore         # Docker packaging exclusion filters
├── .gitignore            # Git code storage exclusions
├── static/
│   ├── css/
│   │   └── style.css     # Premium styling stylesheet
│   └── js/
│       └── app.js        # Polling and message rendering logic
├── templates/
│   └── index.html        # Jinja-based client user interface
└── esp32_chat_app/
    └── esp32_chat_app.ino # Arduino IDE Web Server C++ code
```

---

## 1. Laptop Setup & Local Deployment

This strategy sets up a baseline environment on your local machine.

### Step 1: Initialize Virtual Environment
Navigate to the project root and create a virtual environment:
```powershell
cd d:\EdgeAi_resistor_Dl_model\Edge_AI-Computing\wifi_chat_app
python -m venv venv
```

### Step 2: Activate the Virtual Environment
- **On Windows (PowerShell):**
  ```powershell
  .\venv\Scripts\Activate.ps1
  ```
- **On Linux/macOS:**
  ```bash
  source venv/bin/activate
  ```

### Step 3: Install Dependencies
```powershell
pip install -r requirements.txt
```

### Step 4: Launch the Server
```powershell
python app.py
```
> **PowerShell Command Precedence Note:** If you type `app.py` directly in PowerShell, it will throw a `CommandNotFoundException` error because PowerShell does not run scripts in the current directory by default. Always start the server using `python app.py` or `.\app.py`.

### Step 5: Access the Web App
Open your web browser and navigate to `http://127.0.0.1:5002`.
> Note: To connect a mobile phone or another device on the same local network, find your laptop's local IP address (e.g. `192.168.1.X`) and open `http://192.168.1.X:5002` on your mobile browser.

---

## 2. Cloud Deployment (Render via Git/Docker)

Render reads the `Dockerfile` in the root of the project to automatically compile and build the container.

### Step 1: Create a GitHub Repository
Go to [GitHub](https://github.com) and create a repository named `wifi-secure-chat`.

### Step 2: Push Local Code to GitHub
Run the following commands in your local powershell terminal:
```powershell
# Navigate to the chat directory
cd d:\EdgeAi_resistor_Dl_model\Edge_AI-Computing\wifi_chat_app

# Initialize local git repository
git init

# Track all project files (.gitignore will exclude environment folders automatically)
git add .

# Create the initial commit
git commit -m "Initial commit for WiFi Chat App"

# Rename default branch to main
git branch -M main

# Link to your github repository (Replace with your exact URL)
git remote add origin https://github.com/snehvardhan275/wifi-secure-chat.git

# Push code to GitHub
git push -u origin main
```

### Step 3: Deploy on Render
1. Log in to [Render Dashboard](https://dashboard.render.com).
2. Click **New +** and select **Web Service**.
3. Under *Connect a repository*, link your GitHub account and choose the `wifi-secure-chat` repository.
4. Render will automatically detect the `Dockerfile`. Configure:
   - **Name:** `wifi-secure-chat`
   - **Instance Type:** `Free`
5. Click **Advanced** to add the following **Environment Variables**:
   - `CHAT_ENV` = `CLOUD`
   - `SECRET_KEY` = `some_secure_random_key_string`
6. Click **Create Web Service**. Render will build the Docker container and deploy the app to a public DNS URL (e.g. `https://wifi-secure-chat.onrender.com`).

---

## 3. Edge Deployment (ESP32 Dev Board)

Because the web layout resources (HTML/CSS/JS) are stored in PROGMEM (Flash memory), this deployment has zero reliance on external SD cards or filesystem partitions.

### Step 1: Pre-requisites in Arduino IDE
1. Open **Arduino IDE**.
2. Go to **File > Preferences**.
3. In *Additional Boards Manager URLs*, append the Espressif package URL:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
4. Go to **Tools > Board > Boards Manager...**, search for `esp32` by Espressif, and click **Install**.
5. Go to **Sketch > Include Library > Manage Libraries...**, search for `ArduinoJson` (by Benoit Blanchon) and click **Install** (supports both v6 and v7).

### Step 2: Open and Configure Sketch
1. Open the file `esp32_chat_app/esp32_chat_app.ino` in your Arduino IDE.
2. Scroll to lines 19-20 and update the SSID and password to match your home WiFi router:
   ```cpp
   const char* WIFI_SSID = "YOUR_WIFI_SSID";
   const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
   ```

### Step 3: Compile and Upload
1. Plug your **ESP32 development board** into your laptop via a micro-USB or USB-C cable.
2. Go to **Tools > Board > ESP32 Arduino** and select your board type (e.g., `ESP32 Dev Module`).
3. Go to **Tools > Port** and choose the COM port matching the connected board.
4. Click the **Upload** arrow button in the top left.

### Step 4: Running the Server
1. Once upload completes, click **Tools > Serial Monitor** and set the baud rate to `115200`.
2. Press the **EN/RST** button on the ESP32 board.
3. The board will attempt to connect to your local home network.
   - **Station Mode (Successful Connection):** The Serial Monitor will print a local IP address (e.g., `192.168.1.15`). Any phone or laptop connected to the same home WiFi can open `http://192.168.1.15` in a browser to access the chat web application.
   - **Access Point Mode (Fallback):** If the home WiFi connection fails, the ESP32 will launch its own network hotspot:
     - **Hotspot SSID:** `ESP32-Chat-Hub`
     - **Hotspot Password:** `12345678`
     Open your phone's WiFi settings, connect to `ESP32-Chat-Hub`, and navigate to `http://192.168.4.1` (the default gateway IP) in a browser.

---

## How to Chat (Local WiFi or Cloud)

1. Open the web interface.
2. Enter a **Username** (e.g. `Alice`) and the Server Password: `1234!@#$`. Click **Log In**.
3. Under the **Connected Users** list, you will see other online logged-in users.
4. Click **Connect 🔌** on the card of the user you want to chat with.
5. The other user's interface will update to show they are connected to you. Both users' status badge will show **BUSY**.
6. Type a message in the text input box and click **Send**.
7. While typing, a `partner is typing...` indicator will display in real-time on the other user's screen.
8. To terminate the conversation, click the **⛔ End Chat** button in the top left. Both users will be returned to the idle dashboard and marked **AVAILABLE** again.
