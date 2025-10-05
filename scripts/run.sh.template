#!/usr/bin/env bash
# Unified run script for this project. Supports start|stop|restart|status|install-systemd
set -euo pipefail
BASEDIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$BASEDIR"
LOGDIR="$BASEDIR/logs"
mkdir -p "$LOGDIR"
PROXY_MONITOR_BIN="$BASEDIR/build/proxy_traffic_monitor"
APP_BIN="$BASEDIR/build/crypto_trading"
ROTATE_SCRIPT="$BASEDIR/scripts/rotate_logs.sh"
PIDDIR="$LOGDIR"
APP_PIDFILE="$PIDDIR/crypto_trading.pid"
PROXY_PIDFILE="$PIDDIR/proxy_traffic.pid"

# load config and export proxy env if present
export_http_proxies_from_config(){
  cfg="$BASEDIR/config/config.json"
  if [ -f "$cfg" ]; then
    http=$(jq -r '.proxy.http // empty' "$cfg" 2>/dev/null || true)
    https=$(jq -r '.proxy.https // empty' "$cfg" 2>/dev/null || true)
    ws=$(jq -r '.proxy.ws // empty' "$cfg" 2>/dev/null || true)
    if [ -n "$http" ]; then export HTTP_PROXY="$http"; fi
    if [ -n "$https" ]; then export HTTPS_PROXY="$https"; fi
    if [ -n "$ws" ]; then export WS_PROXY="$ws"; fi
  fi
}

start_proxy_monitor(){
  if [ ! -x "$PROXY_MONITOR_BIN" ]; then echo "proxy monitor binary not found: $PROXY_MONITOR_BIN"; return; fi
  if [ -f "$PROXY_PIDFILE" ] && kill -0 "$(cat $PROXY_PIDFILE)" 2>/dev/null; then echo "proxy monitor already running pid=$(cat $PROXY_PIDFILE)"; return; fi
  echo "Starting proxy monitor..."
  nohup "$PROXY_MONITOR_BIN" --listen 8081 --upstream example.com:80 --log "$LOGDIR/proxy_traffic.log" >> "$LOGDIR/proxy_traffic.log" 2>&1 &
  echo $! > "$PROXY_PIDFILE"
  echo "proxy monitor pid=$(cat $PROXY_PIDFILE)"
}

stop_proxy_monitor(){
  if [ -f "$PROXY_PIDFILE" ]; then
    pid=$(cat "$PROXY_PIDFILE")
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" || true
      echo "stopped proxy monitor pid=$pid"
    fi
    rm -f "$PROXY_PIDFILE"
  fi
}

start_app(){
  # respect config flag
  cfg="$BASEDIR/config/config.json"
  if [ -f "$cfg" ]; then
    disabled=$(jq -r '.disable_history_download // false' "$cfg" 2>/dev/null || echo false)
    if [ "$disabled" = "true" ]; then
      echo "History downloads disabled via config; DataPublisher will be short-circuited"
    fi
  fi

  if [ ! -x "$APP_BIN" ]; then echo "app binary not found: $APP_BIN"; return; fi
  if [ -f "$APP_PIDFILE" ] && kill -0 "$(cat $APP_PIDFILE)" 2>/dev/null; then echo "app already running pid=$(cat $APP_PIDFILE)"; return; fi
  echo "Starting app..."
  export_http_proxies_from_config
  # ensure logrotate called before start to keep logs tidy
  if [ -x "$ROTATE_SCRIPT" ]; then "$ROTATE_SCRIPT" 7 || true; fi
  nohup "$APP_BIN" --port 8080 -wwwroot ./spot_demo >> "$LOGDIR/crypto_trading.out" 2>&1 &
  echo $! > "$APP_PIDFILE"
  echo "app pid=$(cat $APP_PIDFILE)"
}

stop_app(){
  if [ -f "$APP_PIDFILE" ]; then
    pid=$(cat "$APP_PIDFILE")
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" || true
      echo "stopped app pid=$pid"
    fi
    rm -f "$APP_PIDFILE"
  fi
}

status(){
  echo "--- proxy monitor ---"
  if [ -f "$PROXY_PIDFILE" ] && kill -0 "$(cat $PROXY_PIDFILE)" 2>/dev/null; then echo "running pid=$(cat $PROXY_PIDFILE)"; else echo "not running"; fi
  echo "--- app ---"
  if [ -f "$APP_PIDFILE" ] && kill -0 "$(cat $APP_PIDFILE)" 2>/dev/null; then echo "running pid=$(cat $APP_PIDFILE)"; else echo "not running"; fi
  echo "--- last app status (/status) ---"
  curl -sS --max-time 3 'http://127.0.0.1:8080/status' || true
}

install_systemd(){
  # install simple user-level systemd unit and timer for logrotate
  mkdir -p ~/.config/systemd/user
  cp -v "$BASEDIR/systemd/crypto_trading_refresh.service" ~/.config/systemd/user/ || true
  cp -v "$BASEDIR/systemd/crypto_trading_refresh.timer" ~/.config/systemd/user/ || true
  systemctl --user daemon-reload
  systemctl --user enable --now crypto_trading_refresh.timer
  echo "installed crypto_trading refresh timer (user)"
}

case "${1:-}" in
  start)
    start_proxy_monitor
    start_app
    ;;
  stop)
    stop_app
    stop_proxy_monitor
    ;;
  restart)
    $0 stop
    sleep 1
    $0 start
    ;;
  status)
    status
    ;;
  install-systemd)
    install_systemd
    ;;
  *)
    echo "Usage: $0 {start|stop|restart|status|install-systemd}"
    exit 2
    ;;
esac
