import os
import sys
import time
import uuid
import platform
import threading
import builtins
from datetime import datetime
from collections import deque
import psutil
from flask import Flask, render_template, request, jsonify
from flask_cors import CORS
from config import get_config

def safe_print(*args, **kwargs):
    encoding = getattr(sys.stdout, "encoding", None) or "utf-8"
    safe_args = []
    for arg in args:
        if isinstance(arg, str):
            try:
                arg.encode(encoding)
                safe_args.append(arg)
            except UnicodeEncodeError:
                safe_args.append(arg.encode(encoding, errors="replace").decode(encoding))
        else:
            safe_args.append(arg)
    try:
        builtins.print(*safe_args, **kwargs)
    except Exception:
        pass

print = safe_print

# Initialize Flask app
app = Flask(__name__)
config = get_config()
app.config.from_object(config)
CORS(app, origins=config.CORS_ORIGINS)

# ---------------------------------------------------------------------------
# Performance Logger (Thread-safe, in-memory ring buffer)
# ---------------------------------------------------------------------------
perf_lock = threading.Lock()
perf_log = deque(maxlen=config.PERFORMANCE_LOG_SIZE)


def log_performance(entry: dict):
    """Append a performance entry with timestamp."""
    entry["timestamp"] = datetime.utcnow().isoformat() + "Z"
    with perf_lock:
        perf_log.append(entry)


def get_performance_logs():
    """Return a snapshot of the performance log."""
    with perf_lock:
        return list(perf_log)


# ---------------------------------------------------------------------------
# Global In-Memory State
# ---------------------------------------------------------------------------
# Thread lock for state safety
state_lock = threading.Lock()

# Users schema:
# {
#     "session_token": {
#         "name": "Username",
#         "last_seen": 1715694291.12,  # float timestamp
#         "partner_id": "other_session_token" or None,
#         "is_typing": True/False,
#         "last_typing_update": 1715694291.12
#     }
# }
users = {}

# Messages schema:
# {
#     "chat_id": [
#         {"sender": "Username", "text": "Message content", "time": "HH:MM:SS"}
#     ]
# }
messages = {}

APP_START_TIME = time.time()


# ---------------------------------------------------------------------------
# Helper Functions
# ---------------------------------------------------------------------------
def get_chat_id(t1, t2):
    """Generate a unique deterministic chat ID for a pair of user tokens."""
    return "_".join(sorted([t1, t2]))


def clean_expired_sessions():
    """Remove users who haven't polled in the last 180 seconds. (Thread-safe)"""
    now = time.time()
    with state_lock:
        expired_tokens = []
        for token, u in users.items():
            if now - u["last_seen"] > 180.0:
                expired_tokens.append(token)

        for token in expired_tokens:
            u = users[token]
            name = u["name"]
            partner_id = u["partner_id"]

            print(f"[Session Timeout] Cleaning up expired session for user: {name}")

            # Notify active partner if they have one
            if partner_id and partner_id in users:
                p = users[partner_id]
                p["partner_id"] = None
                p["is_typing"] = False
                
                chat_id = get_chat_id(token, partner_id)
                if chat_id in messages:
                    messages[chat_id].append({
                        "sender": "System",
                        "text": f"'{name}' disconnected (timeout).",
                        "time": datetime.now().strftime("%H:%M:%S")
                    })
            
            # Clean up the user from active users
            users.pop(token, None)


def get_system_info():
    """Gather server system metrics."""
    vm = psutil.virtual_memory()
    try:
        cpu_freq = psutil.cpu_freq()
        cpu_freq_mhz = round(cpu_freq.current, 1) if cpu_freq else "N/A"
    except Exception:
        cpu_freq_mhz = "N/A"
        
    return {
        "environment": config.ENV_NAME,
        "env_label": config.ENV_LABEL,
        "hostname": platform.node(),
        "platform": platform.platform(),
        "architecture": platform.machine(),
        "python_version": platform.python_version(),
        "uptime_s": round(time.time() - APP_START_TIME, 1),
        "cpu_count": psutil.cpu_count(logical=True),
        "cpu_freq_mhz": cpu_freq_mhz,
        "cpu_usage_pct": psutil.cpu_percent(interval=None),
        "ram_used_mb": round(vm.used / (1024 ** 2)),
        "ram_total_mb": round(vm.total / (1024 ** 2)),
        "ram_usage_pct": vm.percent,
        "active_users": len(users)
    }

# ---------------------------------------------------------------------------
# Page Routes
# ---------------------------------------------------------------------------
@app.route("/")
def index():
    """Serve the main client interface."""
    return render_template("index.html", config=config)


@app.route("/mdeco")
def mdeco():
    """Serve the MDECO Dashboard."""
    return render_template("mdeco.html", config=config)


# ---------------------------------------------------------------------------
# API Endpoints
# ---------------------------------------------------------------------------
@app.route("/api/login", methods=["POST"])
def api_login():
    """Log in a user with a name and the access password."""
    clean_expired_sessions()
    
    data = request.get_json(force=True) or {}
    name = data.get("name", "").strip()
    password = data.get("password", "")

    if not name or len(name) > 15:
        return jsonify({"status": "error", "message": "Name must be 1 to 15 characters."}), 400

    if password != config.PASSWORD_HASH:
        return jsonify({"status": "error", "message": "Incorrect password."}), 401

    # Check if username is already taken by an active user
    with state_lock:
        for u in users.values():
            if u["name"].lower() == name.lower():
                return jsonify({"status": "error", "message": "Username is already online."}), 400

        # Create session
        token = uuid.uuid4().hex
        users[token] = {
            "name": name,
            "last_seen": time.time(),
            "partner_id": None,
            "is_typing": False,
            "last_typing_update": 0
        }

    print(f"[Login] User logged in: {name} (Token: {token[:8]}...)")
    return jsonify({
        "status": "success",
        "session_token": token,
        "name": name
    })


@app.route("/api/poll", methods=["POST"])
def api_poll():
    """Heartbeat endpoint that updates user's last_seen and retrieves state."""
    clean_expired_sessions()

    data = request.get_json(force=True) or {}
    token = data.get("session_token", "")

    with state_lock:
        if token not in users:
            return jsonify({"status": "error", "message": "Invalid or expired session."}), 401

        user = users[token]
        user["last_seen"] = time.time()

        # Check if typing state expired (reset to false after 2.5s of no update)
        if user["is_typing"] and (time.time() - user["last_typing_update"] > 2.5):
            user["is_typing"] = False

        # Build list of other online users
        online_users = []
        for other_token, other_user in users.items():
            if other_token == token:
                continue
            
            # Status is busy if in a chat
            status = "busy" if other_user["partner_id"] is not None else "available"
            online_users.append({
                "id": other_token,
                "name": other_user["name"],
                "status": status
            })

        # Chat-specific details
        partner_id = user["partner_id"]
        partner_name = ""
        partner_typing = False
        chat_messages = []
        user_state = "idle"

        if partner_id and partner_id in users:
            partner = users[partner_id]
            partner_name = partner["name"]
            partner_typing = partner["is_typing"]
            user_state = "chatting"

            # Retrieve messages
            chat_id = get_chat_id(token, partner_id)
            chat_messages = messages.get(chat_id, [])

        system_info = get_system_info()

    return jsonify({
        "status": "success",
        "user_state": user_state,
        "partner_name": partner_name,
        "partner_typing": partner_typing,
        "messages": chat_messages,
        "users": online_users,
        "system": system_info
    })


@app.route("/api/connect", methods=["POST"])
def api_connect():
    """Request connection to another user to start a chat."""
    clean_expired_sessions()

    data = request.get_json(force=True) or {}
    token = data.get("session_token", "")
    target_id = data.get("target_id", "")

    with state_lock:
        if token not in users:
            return jsonify({"status": "error", "message": "Invalid or expired session."}), 401
        
        if target_id not in users:
            return jsonify({"status": "error", "message": "Target user is offline."}), 400

        user = users[token]
        target = users[target_id]

        if user["partner_id"] is not None:
            return jsonify({"status": "error", "message": "You are already in a chat."}), 400

        if target["partner_id"] is not None:
            return jsonify({"status": "error", "message": "Target user is busy."}), 400

        # Connect them
        user["partner_id"] = target_id
        target["partner_id"] = token
        user["is_typing"] = False
        target["is_typing"] = False

        # Clear/initialize chat messages
        chat_id = get_chat_id(token, target_id)
        messages[chat_id] = [
            {
                "sender": "System",
                "text": f"Chat started between '{user['name']}' and '{target['name']}'.",
                "time": datetime.now().strftime("%H:%M:%S")
            }
        ]

    print(f"[Chat Started] {user['name']} <-> {target['name']}")
    return jsonify({"status": "success"})


@app.route("/api/send", methods=["POST"])
def api_send():
    """Send a message to the active chat partner."""
    clean_expired_sessions()

    data = request.get_json(force=True) or {}
    token = data.get("session_token", "")
    text = data.get("text", "").strip()

    if not text:
        return jsonify({"status": "error", "message": "Message content cannot be empty."}), 400
    if len(text) > 500:
        return jsonify({"status": "error", "message": "Message exceeds limit of 500 characters."}), 400

    # System metrics BEFORE processing
    try:
        vm_before = psutil.virtual_memory()
        cpu_before = psutil.cpu_percent(interval=None)
    except Exception:
        vm_before = None
        cpu_before = 0

    latency_s = 0.0
    status = "success"
    error_msg = None

    start_time = time.perf_counter()
    try:
        with state_lock:
            if token not in users:
                return jsonify({"status": "error", "message": "Invalid or expired session."}), 401

            user = users[token]
            partner_id = user["partner_id"]

            if not partner_id or partner_id not in users:
                return jsonify({"status": "error", "message": "No active chat partner."}), 400

            chat_id = get_chat_id(token, partner_id)
            
            # Append message
            if chat_id not in messages:
                messages[chat_id] = []
                
            messages[chat_id].append({
                "sender": user["name"],
                "text": text,
                "time": datetime.now().strftime("%H:%M:%S")
            })

            # Reset typing state since they just sent a message
            user["is_typing"] = False
            
        latency_s = time.perf_counter() - start_time
    except Exception as e:
        status = "error"
        error_msg = str(e)
        latency_s = time.perf_counter() - start_time

    # System metrics AFTER processing
    try:
        vm_after = psutil.virtual_memory()
        cpu_after = psutil.cpu_percent(interval=None)
        ram_delta_mb = round((vm_after.used - vm_before.used) / (1024 ** 2)) if vm_before else 0
        ram_after_mb = round(vm_after.used / (1024 ** 2))
    except Exception:
        cpu_after = 0
        ram_delta_mb = 0
        ram_after_mb = 0

    # Calculate CPU clock frequency and CPU cycles spent in processing window
    try:
        cpu_freq = psutil.cpu_freq()
        freq_mhz = cpu_freq.current if cpu_freq else 2500.0
    except Exception:
        freq_mhz = 2500.0
    cpu_cycles = int(freq_mhz * 1000000 * latency_s)

    # Dynamic Energy calculations (in Joules)
    # Laptop: baseline 5W, peak 35W. Cloud: baseline 2W, peak 20W.
    is_cloud = (config.ENV_NAME == "CLOUD")
    p_idle = 2.0 if is_cloud else 5.0
    p_peak = 20.0 if is_cloud else 35.0
    power_w = p_idle + (p_peak - p_idle) * (cpu_after / 100.0)
    energy_j = power_w * latency_s

    perf = {
        "type": "processing",
        "latency_s": round(latency_s, 6),
        "text_length": len(text),
        "cpu_after_pct": cpu_after,
        "ram_after_mb": ram_after_mb,
        "ram_delta_mb": ram_delta_mb,
        "cpu_cycles": cpu_cycles,
        "energy_j": round(energy_j, 8),
        "status": status,
    }
    if error_msg:
        perf["error"] = error_msg

    log_performance(perf)

    return jsonify({"status": "success", "perf": perf})



@app.route("/api/typing", methods=["POST"])
def api_typing():
    """Update user's typing status."""
    data = request.get_json(force=True) or {}
    token = data.get("session_token", "")
    is_typing = bool(data.get("typing", False))

    with state_lock:
        if token in users:
            user = users[token]
            user["is_typing"] = is_typing
            user["last_typing_update"] = time.time()

    return jsonify({"status": "success"})


@app.route("/api/end_chat", methods=["POST"])
def api_end_chat():
    """End the current chat session."""
    clean_expired_sessions()

    data = request.get_json(force=True) or {}
    token = data.get("session_token", "")

    with state_lock:
        if token not in users:
            return jsonify({"status": "error", "message": "Invalid or expired session."}), 401

        user = users[token]
        partner_id = user["partner_id"]

        if partner_id:
            # Notify partner
            if partner_id in users:
                p = users[partner_id]
                p["partner_id"] = None
                p["is_typing"] = False
                
                chat_id = get_chat_id(token, partner_id)
                if chat_id in messages:
                    messages[chat_id].append({
                        "sender": "System",
                        "text": f"'{user['name']}' has ended the chat.",
                        "time": datetime.now().strftime("%H:%M:%S")
                    })
            
            user["partner_id"] = None
            user["is_typing"] = False

    print(f"[Chat Ended] Chat closed by user token: {token[:8]}...")
    return jsonify({"status": "success"})


@app.route("/api/logout", methods=["POST"])
def api_logout():
    """Log out from the application."""
    data = request.get_json(force=True) or {}
    token = data.get("session_token", "")

    with state_lock:
        if token in users:
            user = users[token]
            name = user["name"]
            partner_id = user["partner_id"]

            if partner_id and partner_id in users:
                p = users[partner_id]
                p["partner_id"] = None
                p["is_typing"] = False
                
                chat_id = get_chat_id(token, partner_id)
                if chat_id in messages:
                    messages[chat_id].append({
                        "sender": "System",
                        "text": f"'{name}' logged out.",
                        "time": datetime.now().strftime("%H:%M:%S")
                    })

            users.pop(token, None)
            print(f"[Logout] User logged out: {name}")

    return jsonify({"status": "success"})


@app.route("/api/performance", methods=["GET"])
def api_performance():
    """Return the full performance log."""
    return jsonify({
        "logs": get_performance_logs(),
        "system": get_system_info()
    })


@app.route("/api/system", methods=["GET"])
def api_system():
    """Return live system metrics."""
    return jsonify(get_system_info())



if __name__ == "__main__":
    print(f"\n{'='*60}")
    print(f"  Wifi Chat App - {config.ENV_NAME}")
    print(f"  http://{config.HOST}:{config.PORT}")
    print(f"  Environment : {config.ENV_NAME}")
    print(f"  Debug       : {config.DEBUG}")
    print(f"{'='*60}\n")

    app.run(
        host=config.HOST,
        port=config.PORT,
        debug=config.DEBUG,
    )
