# ============================================================
# Dockerfile — Wifi Chat App (Cloud Deployment: Render / AWS)
# ============================================================
FROM python:3.11-slim

# Environment variables
ENV CHAT_ENV=CLOUD
ENV DOCKER_CONTAINER=1
ENV PORT=8080
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1

# Working directory
WORKDIR /app

# Copy and install dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy application files
COPY . .

# Expose container port
EXPOSE 8080

# Production launch using Gunicorn (workers 1 is required for in-memory session state)
CMD ["gunicorn", "app:app", "--bind", "0.0.0.0:8080", "--workers", "1", "--threads", "4", "--timeout", "120", "--access-logfile", "-", "--error-logfile", "-"]
