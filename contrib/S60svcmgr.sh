#!/bin/sh
# sysvinit script for svcmgr (BusyBox / OpenWrt / Rockchip / generic)
DAEMON=${SVCMGR_BIN:-/usr/sbin/svcmgr}
PIDFILE=${SVCMGR_PIDFILE:-/run/svcmgr.pid}
ARGS=${SVCMGR_ARGS:---dir /etc/svcmgr --port 8083}

case "$1" in
  start)
    [ -x "$DAEMON" ] || exit 0
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
      echo "svcmgr already running"; exit 0
    fi
    printf 'Starting svcmgr: '
    "$DAEMON" $ARGS >/var/log/svcmgr.log 2>&1 &
    echo $! > "$PIDFILE"
    echo "OK"
    ;;
  stop)
    [ -f "$PIDFILE" ] && kill "$(cat "$PIDFILE")" 2>/dev/null && rm -f "$PIDFILE"
    echo "Stopped svcmgr"
    ;;
  restart|reload)
    "$0" stop; sleep 1; "$0" start
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}"; exit 1
    ;;
esac
exit 0
