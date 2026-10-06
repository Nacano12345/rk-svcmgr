# rk-svcmgr

一个**极小的多实例服务/进程管理器**，单文件、零第三方依赖，自带 HTTP API 与内嵌网页控制台。

适合管理那些「`程序 -c 配置文件`」形式的常驻服务（例如 frpc、easytier 等）：
- 记录**软件路径 + 配置文件**，一键启动 / 停止 / **重启**；
- 同一程序挂**多份配置**跑多实例；
- 在网页/API 里直接编辑配置文件、看日志；
- 可选**守护**：开机自启 + 进程崩溃后自动拉起。

项目源自 [RK-KVM](https://github.com/Nacano12345/RK-KVM) 的维护后台，抽出为通用工具。

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

打开 `http://<ip>:8083/?token=<token>`，token 在 `<dir>/token`（首次运行自动生成）。

### 命令行选项

| 选项 | 说明 | 默认 |
|---|---|---|
| `-d, --dir PATH` | 数据目录（配置/令牌/日志） | `/userdata/services` |
| `-c, --conf PATH` | 实例库文件 | `<dir>/services.conf` |
| `--token-file PATH` | API 令牌文件（不存在则生成） | `<dir>/token` |
| `-p, --port N` | 监听端口 | `8083` |
| `-b, --bind ADDR` | 监听地址 | `0.0.0.0` |
| `--pid-dir PATH` | pid 文件目录 | `/run/svcmgr` |
| `-h, --help` / `-v, --version` | | |

也支持环境变量：`SVCMGR_DIR` `SVCMGR_CONF` `SVCMGR_TOKEN_FILE` `SVCMGR_PORT` `SVCMGR_BIND` `SVCMGR_PID_DIR`。

## 实例库 `<dir>/services.conf`

```
# <name> <binary> <config> [extra-args...]
frpc-main   /usr/local/bin/frpc           /etc/svcmgr/frpc/main.toml
frpc-alt    /usr/local/bin/frpc           /etc/svcmgr/frpc/alt.toml
*et-main    /usr/local/bin/easytier-core  /etc/svcmgr/easytier/main.toml
```

- 启动命令固定为：`<binary> -c <config> [extra-args...]`。
- 名称前加 `*` = **开机自启 + 崩溃自动重启**（每约 3 秒检查一次）。
- 每实例日志：`<dir>/<name>.log`；pid：`<pid-dir>/<name>.pid`。

## HTTP API

鉴权：查询参数 `?token=<token>` 或请求头 `X-Token: <token>`。

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/` | 内嵌网页控制台 |
| GET | `/api/ping` | 返回 `pong`（同时校验 token） |
| GET | `/api/list` | JSON：`[{name,bin,config,running,pid,auto}]` |
| GET | `/api/status?name=` | `running pid=N` / `stopped pid=0` |
| GET | `/api/start?name=` | 启动 |
| GET | `/api/stop?name=` | 停止 |
| GET | `/api/restart?name=` | 重启 |
| GET | `/api/log?name=` | 日志尾部（文本） |
| GET | `/api/config?name=` | 读取配置 |
| POST | `/api/config?name=` | 写入配置（body 为内容） |
| POST | `/api/add?name=&bin=&config=` | 新增实例（`name` 加 `*` 前缀=守护） |
| GET | `/api/del?name=` | 删除实例 |
| POST | `/api/conf` | 整体替换实例库（body） |

跨域：所有响应带 `Access-Control-Allow-Origin: *`，便于被其它网页（如 RK-KVM 后台）直接调用。

## 开机自启

- systemd：见 `contrib/svcmgr.service`
- sysvinit/BusyBox：见 `contrib/S60svcmgr.sh`
- OpenWrt：可直接用上述脚本配合 `/etc/rc.local` 或 procd（自行封装）。

## 安全提示

- 令牌即密码，保存在 `<dir>/token`（权限 `0600`）。泄露后删除该文件重启即可重生成。
- 默认绑定 `0.0.0.0`。若不需对外，建议 `--bind 127.0.0.1` 并用反向代理（带 TLS）暴露；**令牌会出现在 URL 查询串中**，请勿经不可信网络明文传输。
- 服务以当前用户权限运行；若以 root 运行，受管程序亦即 root，请谨慎。

## License

[MIT](LICENSE) © 2026 Nacano12345
