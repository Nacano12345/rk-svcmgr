[中文](README.md) | English

# rk-svcmgr

A **tiny multi-instance service/process manager**: single file, zero third-party dependencies,
with an HTTP API and a built-in web console.

Made for long-running services of the form `program -c config` (e.g. frpc, easytier):
- records the **program path + config file**, one-click start / stop / **restart**;
- runs several instances of the same program with **different configs**;
- edit config files and view logs right from the web/API;
- optional **supervision**: autostart at boot + auto-restart on crash.

Extracted from the [RK-KVM](https://github.com/Nacano12345/RK-KVM) admin console as a general tool.

## Build

```sh
# native
make

# static
make STATIC=1

# cross-compile (example: armv7 musl toolchain)
make CROSS=arm-linux- STATIC=1

# install
sudo make install PREFIX=/usr/local
```

## Run

```sh
svcmgr --dir /etc/svcmgr --port 8083
```

Open `http://<ip>:8083/?token=<token>`; the token is in `<dir>/token` (auto-generated on first run).

### Command-line options

| Option | Description | Default |
|---|---|---|
| `-d, --dir PATH` | data dir (config/token/logs) | `/userdata/services` |
| `-c, --conf PATH` | instance DB file | `<dir>/services.conf` |
| `--token-file PATH` | API token file (created if absent) | `<dir>/token` |
| `-p, --port N` | HTTP listen port | `8083` |
| `-b, --bind ADDR` | HTTP listen address | `0.0.0.0` |
| `--pid-dir PATH` | pidfile directory | `/run/svcmgr` |
| `-h, --help` / `-v, --version` | | |

Environment variables: `SVCMGR_DIR` `SVCMGR_CONF` `SVCMGR_TOKEN_FILE` `SVCMGR_PORT` `SVCMGR_BIND` `SVCMGR_PID_DIR`.

## Instance DB `<dir>/services.conf`

```
# <name> <binary> <config> [extra-args...]
frpc-main   /usr/local/bin/frpc           /etc/svcmgr/frpc/main.toml
frpc-alt    /usr/local/bin/frpc           /etc/svcmgr/frpc/alt.toml
*et-main    /usr/local/bin/easytier-core  /etc/svcmgr/easytier/main.toml
```

- The launch command is always: `<binary> -c <config> [extra-args...]`.
- A leading `*` on `<name>` = **autostart at boot + auto-restart on crash** (checked about every 3 s).
- Per-instance log: `<dir>/<name>.log`; pid: `<pid-dir>/<name>.pid`.
- If a program does **not accept `-c`**, use a tiny wrapper script as `<binary>` (parse `$2` etc. inside it).

## HTTP API

Auth: query parameter `?token=<token>` or header `X-Token: <token>`.

| Method | Path | Description |
|---|---|---|
| GET | `/` | built-in web console |
| GET | `/api/ping` | returns `pong` (also validates the token) |
| GET | `/api/list` | JSON: `[{name,bin,config,running,pid,auto}]` |
| GET | `/api/status?name=` | `running pid=N` / `stopped pid=0` |
| GET | `/api/start?name=` | start |
| GET | `/api/stop?name=` | stop |
| GET | `/api/restart?name=` | restart |
| GET | `/api/log?name=` | log tail (text) |
| GET | `/api/config?name=` | read config |
| POST | `/api/config?name=` | write config (body = content) |
| POST | `/api/add?name=&bin=&config=` | add instance (`*` prefix on name = supervised) |
| GET | `/api/del?name=` | delete instance |
| POST | `/api/conf` | replace the whole instance DB (body) |

CORS: all responses carry `Access-Control-Allow-Origin: *`, so other web pages (e.g. the RK-KVM
admin) can call it directly.

## Autostart

- systemd: see `contrib/svcmgr.service`
- sysvinit/BusyBox: see `contrib/S60svcmgr.sh`
- OpenWrt: use the script above with `/etc/rc.local` or procd (wrap it yourself).

## Embedding in a web page (HTTPS reverse proxy)

When embedding svcmgr into an **HTTPS page**, browsers block requests to plain
`http://host:8083` as "mixed content". Fix it by **same-origin reverse-proxying** `/svcmgr/*`
to svcmgr within the same TLS site (Caddy example):

```
https://example.com {
    handle_path /svcmgr/* {
        reverse_proxy 127.0.0.1:8083
    }
}
```

The frontend can then use `https://example.com/svcmgr/api/list?token=...` (same-origin, no CORS).

## Related projects

- [**RK-KVM**](https://github.com/Nacano12345/RK-KVM) — the project this component was extracted
  from; its "Service manager" tab can manage svcmgr instances as columns (local or remote + API token).

## Security notes

- The token is a password, stored in `<dir>/token` (mode `0600`). If leaked, delete the file and
  restart to regenerate it.
- It binds `0.0.0.0` by default. If not needed externally, prefer `--bind 127.0.0.1` and expose it
  through a reverse proxy (with TLS); **the token appears in the URL query string**, so do not send
  it over untrusted networks in cleartext.
- It runs with the current user's privileges; if run as root, the managed programs run as root too — be careful.

## License

[MIT](LICENSE) © 2026 Nacano12345
