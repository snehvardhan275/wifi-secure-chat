Created At: 2026-05-23T12:43:09Z
Completed At: 2026-05-23T12:43:09Z
File Path: `file:///d:/EdgeAi_resistor_Dl_model/Edge_AI-Computing/wifi_chat_app/app.py`
Total Lines: 403
Total Bytes: 12784
Showing lines 1 to 403
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: import os
2: import time
3: import uuid
4: import platform
5: import threading
6: from datetime import datetime
7: import psutil
8: from flask import Flask, render_template, request, jsonify
9: from flask_cors import CORS
10: from config import get_config
11: 
12: # Initialize Flask app
13: app = Flask(__name__)
14: config = get_config()
15: app.config.from_object(config)
16: CORS(app, origins=config.CORS_ORIGINS)
17: 
18: # ---------------------------------------------------------------------------
19: # Global In-Memory State
20: # ---------------------------------------------------------------------------
21: # Thread lock for state safety
22: state_lock = threading.Lock()
23: 
24: # Users schema:
25: # {
26: #     "session_token": {
27: #         "name": "Username",
28: #         "last_seen": 1715694291.12,  # float timestamp
29: #         "partner_id": "other_session_token" or None,
30: #         "is_typing": True/False,
31: #         "last_typing_update": 1715694291.12
32: #     }
33: # }
34: users = {}
35: 
36: # Messages schema:
37: # {
38: #     "chat_id": [
39: #         {"sender": "Username", "text": "Message content", "time": "HH:MM:SS"}
40: #     ]
41: # }
42: messages = {}
43: 
44: APP_START_TIME = time.time()
45: 
46: 
47: # ---------------------------------------------------------------------------
48: # Helper Functions
49: # ---------------------------------------------------------------------------
50: def get_chat_id(t1, t2):
51:     """Generate a unique deterministic chat ID for a pair of user tokens."""
52:     return "_".join(sorted([t1, t
<truncated 11135 bytes>
ended the chat.",
349:                         "time": datetime.now().strftime("%H:%M:%S")
350:                     })
351:             
352:             user["partner_id"] = None
353:             user["is_typing"] = False
354: 
355:     print(f"[Chat Ended] Chat closed by user token: {token[:8]}...")
356:     return jsonify({"status": "success"})
357: 
358: 
359: @app.route("/api/logout", methods=["POST"])
360: def api_logout():
361:     """Log out from the application."""
362:     data = request.get_json(force=True) or {}
363:     token = data.get("session_token", "")
364: 
365:     with state_lock:
366:         if token in users:
367:             user = users[token]
368:             name = user["name"]
369:             partner_id = user["partner_id"]
370: 
371:             if partner_id and partner_id in users:
372:                 p = users[partner_id]
373:                 p["partner_id"] = None
374:                 p["is_typing"] = False
375:                 
376:                 chat_id = get_chat_id(token, partner_id)
377:                 if chat_id in messages:
378:                     messages[chat_id].append({
379:                         "sender": "System",
380:                         "text": f"'{name}' logged out.",
381:                         "time": datetime.now().strftime("%H:%M:%S")
382:                     })
383: 
384:             users.pop(token, None)
385:             print(f"[Logout] User logged out: {name}")
386: 
387:     return jsonify({"status": "success"})
388: 
389: 
390: if __name__ == "__main__":
391:     print(f"\n{'='*60}")
392:     print(f"  Wifi Chat App — {config.ENV_LABEL}")
393:     print(f"  http://{config.HOST}:{config.PORT}")
394:     print(f"  Environment : {config.ENV_NAME}")
395:     print(f"  Debug       : {config.DEBUG}")
396:     print(f"{'='*60}\n")
397: 
398:     app.run(
399:         host=config.HOST,
400:         port=config.PORT,
401:         debug=config.DEBUG,
402:     )
403: 
The above content shows the entire, complete file contents of the requested file.
