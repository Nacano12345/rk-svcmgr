[中文](README.md) | English

# rk-svcmgr

A lightweight multi-instance service/process manager. It is a single-file implementation with no
third-party dependencies, and provides an HTTP API and a built-in web console.

It is intended for services that run as `program -c config` (for example frpc and easytier):

- Records the program path and config file, and supports start, stop, and restart.
- Runs several instances of the same program with different configs.
- Edits config files and views logs directly from the web or API.
- Optional supervision: autostart at boot and automatic restart after a crash.

This project was extracted from the RK-KVM admin console and is provided as a general-purpose tool.

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

Open `http://<ip>:8083/?token=<token>`. The token is located in `<dir>/token` and is generated
automatically on first run.

### Command-line options

| Option | Description | Default |
|---|---|---|
| `-d, --dir PATH` | data directory (config, token, logs) | `/userdata/services` |
| `-c, --conf PATH` | instance DB file | `<dir>/services.conf` |
| `--token-file PATH` | API token file (created if absent) | `<dir>/token` |
| `-p, --port N` | HTTP listen port | `8083` |
| `-b, --bind ADDR` | HTTP listen address | `0.0.0.0` |
| `--pid-dir PATH` | pidfile directory | `/run/svcmgr` |
| `-h, --help` / `-v, --version` | | |

Environment variables are also supported: `SVCMGR_DIR`, `SVCMGR_CONF`, `SVCMGR_TOKEN_FILE`,
`SVCMGR_PORT`, `SVCMGR_BIND`, and `SVCMGR_PID_DIR`.

## Instance DB `<dir>/services.conf`

```
# <name> <binary> <config> [extra-args...]
frpc-main   /usr/local/bin/frpc           /etc/svcmgr/frpc/main.toml
frpc-alt    /usr/local/bin/frpc           /etc/svcmgr/frpc/alt.toml
*et-main    /usr/local/bin/easytier-core  /etc/svcmgr/easytier/main.toml
```

- The launch command is always `<binary> -c <config> [extra-args...]`.
- A leading `*` on the name means autostart at boot and automatic restart after a crash (checked
  approximately every 3 seconds).
- Per-instance log: `<dir>/<name>.log`; pid: `<pid-dir>/<name>.pid`.
- If a program does not accept `-c`, use a simple wrapper script as `<binary>` and parse the arguments
  inside it.

## HTTP API

Authentication: the query parameter `?token=<token>` or the header `X-Token: <token>`.

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
| POST | `/api/config?name=` | write config (body is the content) |
| POST | `/api/add?name=&bin=&config=` | add instance (a `*` prefix on the name means supervised) |
| GET | `/api/del?name=` | delete instance |
| POST | `/api/conf` | replace the entire instance DB (body) |

CORS: all responses include `Access-Control-Allow-Origin: *`, so other web pages (such as the RK-KVM
admin console) can call it directly.

## Autostart

- systemd: see `contrib/svcmgr.service`.
- sysvinit/BusyBox: see `contrib/S60svcmgr.sh`.
- OpenWrt: use the script above with `/etc/rc.local` or procd.

## Embedding in a web page (HTTPS reverse proxy)

When svcmgr is embedded in an HTTPS page, browsers block requests to the plain `http://host:8083`
endpoint as mixed content. The solution is to reverse-proxy `/svcmgr/*` to svcmgr within the same
TLS site (Caddy example):

```
https://example.com {
    handle_path /svcmgr/* {
        reverse_proxy 127.0.0.1:8083
    }
}
```

The frontend can then use `https://example.com/svcmgr/api/list?token=...` (same-origin, no CORS).

## Related projects

- [RK-KVM](https://github.com/Nacano12345/RK-KVM): the project this component was extracted from.
  Its "Service manager" tab can manage svcmgr instances as columns (local or remote, with an API token).

## Security notes

- The token acts as a password and is stored in `<dir>/token` (mode `0600`). If it is disclosed,
  delete the file and restart to regenerate it.
- It binds to `0.0.0.0` by default. If external access is not required, prefer `--bind 127.0.0.1` and
  expose it through a reverse proxy with TLS. The token appears in the URL query string, so do not
  transmit it in cleartext over untrusted networks.
- The program runs with the privileges of the current user; if run as root, the managed programs also
  run as root.

## License

[MIT](LICENSE) © 2026 Nacano12345
