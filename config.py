"""
Environment Configuration for Wifi Chat Application.
Detects deployment environment and adjusts settings accordingly.
"""

import os
import platform
import psutil


def detect_environment():
    """Auto-detect the deployment environment."""
    env = os.environ.get("CHAT_ENV", "").upper()
    if env in ("LOCAL", "EDGE", "CLOUD"):
        return env

    if os.path.exists("/.dockerenv") or os.environ.get("DOCKER_CONTAINER"):
        return "CLOUD"
    else:
        return "LOCAL"


class BaseConfig:
    """Base configuration shared across all environments."""
    SECRET_KEY = os.environ.get("SECRET_KEY", "wifi-chat-secure-key-2026")
    VERSION = "1.0.0"
    PASSWORD_HASH = "1234!@#$"  # The static access password
    PERFORMANCE_LOG_SIZE = 50



class LocalConfig(BaseConfig):
    """Laptop / Desktop localhost configuration."""
    ENV_NAME = "LOCAL"
    ENV_LABEL = "💻 Laptop (Localhost)"
    DEBUG = True
    HOST = "0.0.0.0"
    PORT = 5002
    CORS_ORIGINS = "*"


class EdgeConfig(BaseConfig):
    """LAN Laptop Edge Server configuration."""
    ENV_NAME = "EDGE"
    ENV_LABEL = "⚡ Edge (LAN Laptop)"
    DEBUG = False
    HOST = "0.0.0.0"
    PORT = int(os.environ.get("PORT", 5002))
    CORS_ORIGINS = "*"


class CloudConfig(BaseConfig):
    """AWS / GCP / Render Docker container configuration."""
    ENV_NAME = "CLOUD"
    ENV_LABEL = "☁️ Cloud (Render)"
    DEBUG = False
    HOST = "0.0.0.0"
    PORT = int(os.environ.get("PORT", 8080))
    CORS_ORIGINS = "*"


CONFIG_MAP = {
    "LOCAL": LocalConfig,
    "EDGE": EdgeConfig,
    "CLOUD": CloudConfig,
}


def get_config():
    """Return the appropriate config based on detected environment."""
    env = detect_environment()
    return CONFIG_MAP.get(env, LocalConfig)()
