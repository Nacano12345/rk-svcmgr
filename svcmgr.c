/*
 * svcmgr - a tiny multi-instance service / process manager with an HTTP API.
 *
 * Manage arbitrary programs that take a config file (-c <config>), start/stop/
 * restart them, edit their configs, read logs, and run several instances of the
 * same binary with different configs. Optional per-instance supervision
 * (autostart at boot + auto-restart on crash).
 *
 * Single file, no third-party dependencies. Static-friendly (musl/glibc).
 *
 * Build:
 *   cc -O2 -o svcmgr svcmgr.c
 *   CROSS=arm-linux- STATIC=1 make      # cross compile (see Makefile)
 *
 * Run:
 *   svcmgr --dir /etc/svcmgr --port 8083
 *
 * Instance DB: <dir>/services.conf   (see --help / README)
 * Per instance: <dir>/<name>.log, <pid-dir>/<name>.pid
 * API token:    <dir>/token (auto-generated on first run)
 *
 * HTTP API (token via ?token= or X-Token header):
 *   GET  /                       tiny web UI
 *   GET  /api/ping               "pong" (also validates the token)
 *   GET  /api/list               JSON [{name,bin,config,running,pid,auto}]
 *   GET  /api/status?name=       running/stopped + pid
 *   GET  /api/start?name=
 *   GET  /api/stop?name=
 *   GET  /api/restart?name=
 *   GET  /api/log?name=          log tail (text)
 *   GET  /api/config?name=       config file content (text)
 *   POST /api/config?name=       body = new config content
 *   POST /api/add?name=&bin=&config=   (name prefixed with '*' = supervised)
 *   GET  /api/del?name=
 *   POST /api/conf               body = whole services.conf
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define VERSION "1.0.0"

#define DEF_DIR   "/userdata/services"
#define DEF_PID   "/run/svcmgr"
#define DEF_PORT  8083
#define DEF_BIND  "0.0.0.0"
#define MAX_INST  64

static char g_dir[512]     = DEF_DIR;
static char g_conf[600];
static char g_tokenf[600];
static char g_piddir[512]  = DEF_PID;
static char g_bind[64]     = DEF_BIND;
static int  g_port         = DEF_PORT;
static char g_token[64];

struct inst { char name[64], bin[256], cfg[256], args[256]; int autosup; long lasttry; };
static struct inst g_inst[MAX_INST];
static int g_n;
static int g_conf_set, g_token_set;

static void usage(const char *p)
{
	printf(
"svcmgr " VERSION " - lightweight multi-instance service manager\n"
"\n"
"Usage: %s [options]\n"
"\n"
"Options:\n"
"  -d, --dir PATH        data dir (conf/token/logs)      [%s]\n"
"  -c, --conf PATH       instance DB file                [<dir>/services.conf]\n"
"      --token-file PATH API token file (created if absent) [<dir>/token]\n"
"  -p, --port N          HTTP listen port                [%d]\n"
"  -b, --bind ADDR       HTTP listen address             [%s]\n"
"      --pid-dir PATH    pidfile directory               [%s]\n"
"  -h, --help            show this help\n"
"  -v, --version         show version\n"
"\n"
"Environment: SVCMGR_DIR SVCMGR_CONF SVCMGR_TOKEN_FILE SVCMGR_PORT\n"
"             SVCMGR_BIND SVCMGR_PID_DIR\n"
"\n"
"Instance DB line:  <name> <binary> <config> [extra-args...]\n"
"  A leading '*' on <name> means autostart at boot + auto-restart on crash.\n",
		p, g_dir, g_port, g_bind, g_piddir);
}

static void defaults(void)
{
	const char *e;
	if ((e = getenv("SVCMGR_DIR")) && *e)        snprintf(g_dir, sizeof g_dir, "%s", e);
	if ((e = getenv("SVCMGR_PID_DIR")) && *e)    snprintf(g_piddir, sizeof g_piddir, "%s", e);
	if ((e = getenv("SVCMGR_BIND")) && *e)       snprintf(g_bind, sizeof g_bind, "%s", e);
	if ((e = getenv("SVCMGR_PORT")) && *e)       g_port = atoi(e);
	if ((e = getenv("SVCMGR_CONF")) && *e)       { snprintf(g_conf, sizeof g_conf, "%s", e); g_conf_set = 1; }
	if ((e = getenv("SVCMGR_TOKEN_FILE")) && *e) { snprintf(g_tokenf, sizeof g_tokenf, "%s", e); g_token_set = 1; }
}

/* derive conf/token paths from --dir unless explicitly overridden */
static void apply_paths(void)
{
	if (!g_conf_set)  snprintf(g_conf, sizeof g_conf, "%s/services.conf", g_dir);
	if (!g_token_set) snprintf(g_tokenf, sizeof g_tokenf, "%s/token", g_dir);
}

static int parse_args(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); exit(0); }
		else if (!strcmp(a, "-v") || !strcmp(a, "--version")) { printf("%s\n", VERSION); exit(0); }
		else if (!strcmp(a, "-d") || !strcmp(a, "--dir")) { if (!v) return -1; snprintf(g_dir, sizeof g_dir, "%s", v); i++; }
		else if (!strcmp(a, "-c") || !strcmp(a, "--conf")) { if (!v) return -1; snprintf(g_conf, sizeof g_conf, "%s", v); g_conf_set = 1; i++; }
		else if (!strcmp(a, "--token-file")) { if (!v) return -1; snprintf(g_tokenf, sizeof g_tokenf, "%s", v); g_token_set = 1; i++; }
		else if (!strcmp(a, "-p") || !strcmp(a, "--port")) { if (!v) return -1; g_port = atoi(v); i++; }
		else if (!strcmp(a, "-b") || !strcmp(a, "--bind")) { if (!v) return -1; snprintf(g_bind, sizeof g_bind, "%s", v); i++; }
		else if (!strcmp(a, "--pid-dir")) { if (!v) return -1; snprintf(g_piddir, sizeof g_piddir, "%s", v); i++; }
		else { fprintf(stderr, "svcmgr: unknown option '%s'\n", a); return -1; }
	}
	return 0;
}

static void load_conf(void)
{
	g_n = 0;
	FILE *f = fopen(g_conf, "r");
	if (!f)
		return;
	char line[1024];
	while (fgets(line, sizeof line, f) && g_n < MAX_INST) {
		char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '#' || *p == '\n' || !*p)
			continue;
		struct inst *it = &g_inst[g_n];
		memset(it, 0, sizeof *it);
		char *nl = strchr(line, '\n');
		if (nl) *nl = 0;
		if (sscanf(line, "%63s %255s %255s %255[^\n]", it->name, it->bin,
			   it->cfg, it->args) >= 2) {
			if (it->name[0] == '*') {
				it->autosup = 1;
				memmove(it->name, it->name + 1, strlen(it->name));
			}
			g_n++;
		}
	}
	fclose(f);
}

static void save_conf(void)
{
	FILE *f = fopen(g_conf, "w");
	if (!f)
		return;
	fprintf(f, "# name binary config [args]\n");
	for (int i = 0; i < g_n; i++)
		fprintf(f, "%s%s %s %s %s\n", g_inst[i].autosup ? "*" : "",
			g_inst[i].name, g_inst[i].bin, g_inst[i].cfg,
			g_inst[i].args);
	fclose(f);
}

static int find(const char *name)
{
	for (int i = 0; i < g_n; i++)
		if (!strcmp(g_inst[i].name, name))
			return i;
	return -1;
}

static void pidpath(const char *name, char *p, size_t n)
{ snprintf(p, n, "%s/%s.pid", g_piddir, name); }

static int running_pid(const char *name)
{
	char p[700], b[32];
	pidpath(name, p, sizeof p);
	int fd = open(p, O_RDONLY);
	if (fd < 0) return 0;
	int r = read(fd, b, sizeof b - 1);
	close(fd);
	if (r <= 0) return 0;
	b[r] = 0;
	int pid = atoi(b);
	if (pid > 1 && kill(pid, 0) == 0) return pid;
	return 0;
}

static void logpath(const char *name, char *p, size_t n)
{ snprintf(p, n, "%s/%s.log", g_dir, name); }

static int do_start(const char *name)
{
	int i = find(name);
	if (i < 0) return -1;
	if (running_pid(name)) return 0;
	mkdir(g_dir, 0755);
	mkdir(g_piddir, 0755);
	pid_t p = fork();
	if (p == 0) {
		setsid();
		char lp[700];
		logpath(name, lp, sizeof lp);
		int logfd = open(lp, O_WRONLY | O_CREAT | O_APPEND, 0644);
		int nullfd = open("/dev/null", O_RDONLY);
		if (nullfd >= 0) { dup2(nullfd, 0); if (nullfd > 2) close(nullfd); }
		if (logfd >= 0) { dup2(logfd, 1); dup2(logfd, 2); if (logfd > 2) close(logfd); }
		if (g_inst[i].args[0])
			execl("/bin/sh", "sh", "-c",
			      "exec \"$0\" -c \"$1\" $2", g_inst[i].bin,
			      g_inst[i].cfg, g_inst[i].args, (char *)NULL);
		else
			execl(g_inst[i].bin, g_inst[i].bin, "-c", g_inst[i].cfg,
			      (char *)NULL);
		_exit(127);
	}
	if (p < 0) return -1;
	char pf[700];
	pidpath(name, pf, sizeof pf);
	FILE *f = fopen(pf, "w");
	if (f) { fprintf(f, "%d\n", p); fclose(f); }
	return 0;
}

static int do_stop(const char *name)
{
	int pid = running_pid(name);
	if (!pid) { char p[700]; pidpath(name, p, sizeof p); unlink(p); return 0; }
	kill(pid, SIGTERM);
	for (int i = 0; i < 20 && kill(pid, 0) == 0; i++)
		usleep(50000);
	if (kill(pid, 0) == 0)
		kill(pid, SIGKILL);
	char p[700]; pidpath(name, p, sizeof p); unlink(p);
	return 0;
}

/* ---------------- HTTP ---------------- */
static ssize_t wall(int c, const void *b, size_t n)
{ const char *p = b; size_t l = n; while (l) { ssize_t r = write(c, p, l); if (r < 0) { if (errno == EINTR) continue; return -1; } p += r; l -= r; } return n; }

static void reply(int c, const char *ct, const char *body, int len)
{
	char h[192];
	int hl = snprintf(h, sizeof h,
		"HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
		"Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n"
		"Connection: close\r\n\r\n", ct, len);
	wall(c, h, hl);
	wall(c, body, len);
}

static int qval(const char *req, const char *key, char *out, size_t n)
{
	const char *p = req;
	char pat[64];
	snprintf(pat, sizeof pat, "%s=", key);
	while ((p = strstr(p, pat))) {
		if (p == req || p[-1] == '?' || p[-1] == '&') {
			p += strlen(pat);
			size_t i = 0;
			while (*p && *p != '&' && *p != ' ' && *p != '\r' && i < n - 1) {
				if (*p == '%' && i + 2 < n) {
					unsigned v = 0; char h2[3] = { p[1], p[2], 0 };
					if (sscanf(h2, "%x", &v) == 1) { out[i++] = v; p += 3; continue; }
				}
				out[i++] = *p++;
			}
			out[i] = 0;
			return 1;
		}
		p++;
	}
	return 0;
}

static int tail_file(const char *path, char *out, size_t n)
{
	int fd = open(path, O_RDONLY);
	out[0] = 0;
	if (fd < 0) return -1;
	off_t sz = lseek(fd, 0, SEEK_END);
	off_t start = sz > (off_t)(n - 1) ? sz - (off_t)(n - 1) : 0;
	lseek(fd, start, SEEK_SET);
	int r = read(fd, out, n - 1);
	close(fd);
	if (r < 0) r = 0;
	out[r] = 0;
	return 0;
}

static const char *PAGE =
"<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>svcmgr</title>"
"<style>html,body{margin:0;background:#0b0b0d;color:#ddd;font-family:ui-monospace,Consolas,monospace}"
"#bar{padding:8px 12px;background:#16161a;border-bottom:1px solid #26262c}#bar b{color:#4ea1ff;font-family:system-ui}"
"table{width:100%;border-collapse:collapse}td,th{padding:6px 8px;border-bottom:1px solid #26262c;text-align:left;font-size:13px}"
"button{background:#2a6df4;color:#fff;border:0;border-radius:6px;padding:5px 9px;margin:1px;cursor:pointer}button.g{background:#333}"
"pre{background:#050506;border:1px solid #26262c;border-radius:8px;padding:8px;overflow:auto;font-size:12px}"
"textarea{width:100%;height:220px;background:#0e0e11;color:#eee;border:1px solid #333;border-radius:8px;font:12px monospace;box-sizing:border-box}"
"#wrap{padding:10px}.on{color:#39d353}.off{color:#888}</style></head><body>"
"<div id=bar><b>svcmgr</b> <span id=ver style=opacity:.6;font-size:12px></span></div><div id=wrap>"
"<table id=t><tr><th>name</th><th>binary</th><th>config</th><th>status</th><th>actions</th></tr></table>"
"<h4>config editor</h4><div id=cfghdr style=color:#8bd;font-size:13px></div><textarea id=cfg></textarea>"
"<div style='margin:6px 0'><button onclick=saveCfg()>save</button>"
"<button class=g onclick=loadLog()>log</button><button class=g onclick=loadCfg()>reload</button></div>"
"<pre id=out></pre></div><script>\n"
"const T=new URLSearchParams(location.search).get('token')||'';\n"
"const A=p=>'/api/'+p+(p.includes('?')?'&':'?')+'token='+encodeURIComponent(T);\n"
"const j=async p=>(await fetch(A(p))).text();let cur=null;\n"
"async function list(){const d=JSON.parse(await j('list'));const t=document.getElementById('t');"
"t.innerHTML='<tr><th>name</th><th>binary</th><th>config</th><th>status</th><th>actions</th></tr>';"
"for(const s of d){const tr=document.createElement('tr');"
"tr.innerHTML='<td>'+s.name+(s.auto?' <span style=color:#8bd>[sup]</span>':'')+'</td><td>'+s.bin+'</td><td>'+s.config+'</td>'+"
"'<td class=\"'+(s.running?'on':'off')+'\">'+(s.running?'run '+s.pid:'stopped')+'</td><td></td>';"
"const td=tr.lastChild;['start','stop','restart'].forEach(a=>{const b=document.createElement('button');b.textContent=a;b.className='g';"
"b.onclick=async()=>{document.getElementById('out').textContent=await j(a+'?name='+encodeURIComponent(s.name));list();};td.appendChild(b);});"
"const e=document.createElement('button');e.textContent='edit';e.onclick=()=>{cur=s.name;loadCfg();};td.appendChild(e);t.appendChild(tr);}}\n"
"async function loadCfg(){if(!cur)return;document.getElementById('cfghdr').textContent=cur;"
"document.getElementById('cfg').value=await j('config?name='+encodeURIComponent(cur));}\n"
"async function saveCfg(){if(!cur)return;const body=document.getElementById('cfg').value;"
"const r=await fetch(A('config?name='+encodeURIComponent(cur)),{method:'POST',body});"
"document.getElementById('out').textContent='saved: '+(await r.text());}\n"
"async function loadLog(){if(!cur)return;document.getElementById('cfg').value=await j('log?name='+encodeURIComponent(cur));}\n"
"list();setInterval(list,4000);\n"
"</script></body></html>";

static void handle(int c, char *req)
{
	char tok[64] = {0};
	int ok = 0;
	if (qval(req, "token", tok, sizeof tok) && !strcmp(tok, g_token)) ok = 1;
	char *hx = strcasestr(req, "X-Token:");
	if (hx) { while (*hx && *hx != ' ') hx++; while (*hx == ' ') hx++;
		char t[64]; int i = 0; while (*hx && *hx != '\r' && *hx != '\n' && i < 63) t[i++] = *hx++; t[i] = 0;
		if (!strcmp(t, g_token)) ok = 1; }
	if (!ok) { reply(c, "text/plain", "unauthorized", 12); return; }

	char method[8] = {0}, path[256] = {0};
	sscanf(req, "%7s %255s", method, path);

	if (!strcmp(path, "/")) { reply(c, "text/html; charset=utf-8", PAGE, strlen(PAGE)); return; }

	char name[64] = {0};
	qval(req, "name", name, sizeof name);

	char *body = strstr(req, "\r\n\r\n");
	if (body) body += 4;

	if (!strncmp(path, "/api/ping", 9)) {
		reply(c, "text/plain", "pong", 4);
	} else if (!strncmp(path, "/api/list", 9)) {
		char out[8192]; int o = snprintf(out, sizeof out, "[");
		for (int i = 0; i < g_n; i++) {
			int pid = running_pid(g_inst[i].name);
			o += snprintf(out + o, sizeof out - o,
				"%s{\"name\":\"%s\",\"bin\":\"%s\",\"config\":\"%s\",\"running\":%d,\"pid\":%d,\"auto\":%d}",
				i ? "," : "", g_inst[i].name, g_inst[i].bin,
				g_inst[i].cfg, pid ? 1 : 0, pid, g_inst[i].autosup);
		}
		snprintf(out + o, sizeof out - o, "]");
		reply(c, "application/json", out, strlen(out));
	} else if (!strncmp(path, "/api/status", 11)) {
		char out[128];
		int pid = name[0] ? running_pid(name) : 0;
		int o = snprintf(out, sizeof out, "%s pid=%d", pid ? "running" : "stopped", pid);
		reply(c, "text/plain", out, o);
	} else if (!strncmp(path, "/api/start", 10)) {
		int r = do_start(name);
		reply(c, "text/plain", r ? "err" : "ok", r ? 3 : 2);
	} else if (!strncmp(path, "/api/stop", 9)) {
		int r = do_stop(name);
		reply(c, "text/plain", r ? "err" : "ok", r ? 3 : 2);
	} else if (!strncmp(path, "/api/restart", 12)) {
		do_stop(name); usleep(200000); int r = do_start(name);
		reply(c, "text/plain", r ? "err" : "ok", r ? 3 : 2);
	} else if (!strncmp(path, "/api/log", 8)) {
		char lp[700], out[16384];
		logpath(name, lp, sizeof lp);
		tail_file(lp, out, sizeof out);
		reply(c, "text/plain; charset=utf-8", out, strlen(out));
	} else if (!strncmp(path, "/api/config", 11)) {
		int i = find(name);
		if (i < 0) { reply(c, "text/plain", "no such instance", 16); return; }
		if (!strcmp(method, "POST")) {
			int fd = open(g_inst[i].cfg, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd >= 0) { if (body) if (write(fd, body, strlen(body)) < 0) {} close(fd); }
			reply(c, "text/plain", "ok", 2);
		} else {
			char out[16384];
			tail_file(g_inst[i].cfg, out, sizeof out);
			reply(c, "text/plain; charset=utf-8", out, strlen(out));
		}
	} else if (!strncmp(path, "/api/add", 8)) {
		char bin[256] = {0}, cfg[256] = {0};
		qval(req, "bin", bin, sizeof bin);
		qval(req, "config", cfg, sizeof cfg);
		if (g_n < MAX_INST && name[0] && bin[0]) {
			struct inst *it = &g_inst[g_n++];
			memset(it, 0, sizeof *it);
			if (name[0] == '*') { it->autosup = 1; memmove(name, name + 1, strlen(name)); }
			snprintf(it->name, sizeof it->name, "%s", name);
			snprintf(it->bin, sizeof it->bin, "%s", bin);
			snprintf(it->cfg, sizeof it->cfg, "%s", cfg);
			save_conf();
			reply(c, "text/plain", "ok", 2);
		} else reply(c, "text/plain", "err", 3);
	} else if (!strncmp(path, "/api/del", 8)) {
		int i = find(name);
		if (i >= 0) { do_stop(name); for (int j = i; j < g_n - 1; j++) g_inst[j] = g_inst[j + 1]; g_n--; save_conf(); }
		reply(c, "text/plain", "ok", 2);
	} else if (!strncmp(path, "/api/conf", 9)) {
		if (body) {
			int fd = open(g_conf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd >= 0) { if (write(fd, body, strlen(body)) < 0) {} close(fd); }
			load_conf();
		}
		reply(c, "text/plain", "ok", 2);
	} else {
		reply(c, "text/plain", "not found", 9);
	}
}

static void load_token(void)
{
	int fd = open(g_tokenf, O_RDONLY);
	if (fd >= 0) {
		int r = read(fd, g_token, sizeof g_token - 1);
		close(fd);
		if (r > 0) { g_token[r] = 0; char *nl = strchr(g_token, '\n'); if (nl) *nl = 0; }
	}
	if (!g_token[0]) {
		unsigned seed = (unsigned)time(NULL) ^ (unsigned)getpid();
		snprintf(g_token, sizeof g_token, "%08x%08x", seed, seed * 2654435761u);
		mkdir(g_dir, 0755);
		FILE *f = fopen(g_tokenf, "w");
		if (f) { fprintf(f, "%s\n", g_token); fclose(f); chmod(g_tokenf, 0600); }
	}
}

int main(int argc, char **argv)
{
	defaults();
	if (parse_args(argc, argv) < 0) { usage(argv[0]); return 2; }
	apply_paths();
	signal(SIGCHLD, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	mkdir(g_dir, 0755);
	mkdir(g_piddir, 0755);
	load_token();
	load_conf();

	int s = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)g_port);
	if (!strcmp(g_bind, "0.0.0.0")) sa.sin_addr.s_addr = htonl(INADDR_ANY);
	else if (inet_pton(AF_INET, g_bind, &sa.sin_addr) != 1) sa.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(s, (struct sockaddr *)&sa, sizeof sa) < 0) { perror("bind"); return 1; }
	listen(s, 8);
	fprintf(stderr, "svcmgr %s: %s:%d  dir=%s  %d instances\n",
		VERSION, g_bind, g_port, g_dir, g_n);

	for (int i = 0; i < g_n; i++)
		if (g_inst[i].autosup)
			do_start(g_inst[i].name);

	for (;;) {
		fd_set rf;
		FD_ZERO(&rf);
		FD_SET(s, &rf);
		struct timeval tv = { 2, 0 };
		if (select(s + 1, &rf, NULL, NULL, &tv) > 0 && FD_ISSET(s, &rf)) {
			int c = accept(s, NULL, NULL);
			if (c >= 0) {
				setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
				char req[65536];
				int r = read(c, req, sizeof req - 1);
				if (r > 0) { req[r] = 0; handle(c, req); }
				close(c);
			}
		}
		time_t now = time(NULL);
		for (int i = 0; i < g_n; i++) {
			if (!g_inst[i].autosup) continue;
			if (running_pid(g_inst[i].name)) continue;
			if (now - g_inst[i].lasttry < 3) continue;
			g_inst[i].lasttry = now;
			do_start(g_inst[i].name);
		}
	}
	return 0;
}
