[English](README.en.md) | 中文

# rk-svcmgr

轻量级多实例服务/进程管理器。单文件实现，无第三方依赖，内置 HTTP API 与网页控制台。

适用于以「程序 -c 配置文件」形式运行的服务（如 frpc、easytier）：

- 记录程序路径与配置文件，支持启动、停止与重启。
- 同一程序可挂载多份配置，以多实例方式运行。
- 可在网页或 API 中直接编辑配置文件、查看日志。
- 可选守护：开机自启，并在进程崩溃后自动重启。

本项目由 RK-KVM 的维护后台抽出，作为通用工具使用。

## 构建

```sh
# 本机
make

# 静态
make STATIC=1

# 交叉编译（示例：armv7 musl 工具链）
make CROSS=arm-linux- STATIC=1

# 安装
sudo make install PREFIX=/usr/local
```

## 运行

```sh
svcmgr --dir /etc/svcmgr --port 8083
```

打开 `http://<ip>:8083/?token=<token>`，令牌位于 `<dir>/token`（首次运行时自动生成）。

### 命令行选项

| 选项 | 说明 | 默认值 |
|---|---|---|
| `-d, --dir PATH` | 数据目录（配置、令牌、日志） | `/userdata/services` |
| `-c, --conf PATH` | 实例库文件 | `<dir>/services.conf` |
| `--token-file PATH` | API 令牌文件（不存在时创建） | `<dir>/token` |
| `-p, --port N` | HTTP 监听端口 | `8083` |
| `-b, --bind ADDR` | HTTP 监听地址 | `0.0.0.0` |
| `--pid-dir PATH` | pid 文件目录 | `/run/svcmgr` |
| `-h, --help` / `-v, --version` | | |

亦支持环境变量：`SVCMGR_DIR`、`SVCMGR_CONF`、`SVCMGR_TOKEN_FILE`、`SVCMGR_PORT`、`SVCMGR_BIND`、
`SVCMGR_PID_DIR`。

## 实例库 `<dir>/services.conf`

```
# <name> <binary> <config> [extra-args...]
frpc-main   /usr/local/bin/frpc           /etc/svcmgr/frpc/main.toml
frpc-alt    /usr/local/bin/frpc           /etc/svcmgr/frpc/alt.toml
*et-main    /usr/local/bin/easytier-core  /etc/svcmgr/easytier/main.toml
```

- 启动命令固定为 `<binary> -c <config> [extra-args...]`。
- 名称前加 `*` 表示开机自启并在崩溃后自动重启（约每 3 秒检查一次）。
- 实例日志：`<dir>/<name>.log`；pid：`<pid-dir>/<name>.pid`。
- 若程序不接受 `-c`，可编写一个简单的包装脚本作为 `<binary>`，在脚本内自行解析参数。

## HTTP API

鉴权方式：查询参数 `?token=<token>`，或请求头 `X-Token: <token>`。

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/` | 内置网页控制台 |
| GET | `/api/ping` | 返回 `pong`（同时校验令牌） |
| GET | `/api/list` | JSON：`[{name,bin,config,running,pid,auto}]` |
| GET | `/api/status?name=` | `running pid=N` / `stopped pid=0` |
| GET | `/api/start?name=` | 启动 |
| GET | `/api/stop?name=` | 停止 |
| GET | `/api/restart?name=` | 重启 |
| GET | `/api/log?name=` | 日志尾部（文本） |
| GET | `/api/config?name=` | 读取配置 |
| POST | `/api/config?name=` | 写入配置（body 为内容） |
| POST | `/api/add?name=&bin=&config=` | 新增实例（name 加 `*` 前缀表示守护） |
| GET | `/api/del?name=` | 删除实例 |
| POST | `/api/conf` | 整体替换实例库（body） |

跨域：所有响应均带 `Access-Control-Allow-Origin: *`，可供其它网页（如 RK-KVM 后台）直接调用。

## 开机自启

- systemd：见 `contrib/svcmgr.service`。
- sysvinit/BusyBox：见 `contrib/S60svcmgr.sh`。
- OpenWrt：可配合上述脚本与 `/etc/rc.local` 或 procd 使用。

## 嵌入网页（HTTPS 反向代理）

将 svcmgr 嵌入 HTTPS 页面时，浏览器会以混合内容为由拦截对明文 `http://host:8083` 的请求。
解决方法是在同一 TLS 站点内，将 `/svcmgr/*` 同源反代至 svcmgr（Caddy 示例）：

```
https://example.com {
    handle_path /svcmgr/* {
        reverse_proxy 127.0.0.1:8083
    }
}
```

前端随后可使用 `https://example.com/svcmgr/api/list?token=...` 访问（同源，无跨域）。

## 相关项目

- [RK-KVM](https://github.com/Nacano12345/RK-KVM)：本组件即由其维护后台抽出；其“服务管理”标签可将
  svcmgr 实例作为栏目管理（本地或远程加 API Token）。

## 安全说明

- 令牌即口令，保存在 `<dir>/token`（权限 `0600`）。若泄露，删除该文件并重启即可重新生成。
- 默认绑定 `0.0.0.0`。若无对外需要，建议使用 `--bind 127.0.0.1`，并通过反向代理（带 TLS）暴露。
  令牌会出现在 URL 查询串中，请勿在不可信网络上明文传输。
- 程序以当前用户权限运行；若以 root 运行，受管程序亦以 root 运行。

## 许可

[MIT](LICENSE) © 2026 Nacano12345
