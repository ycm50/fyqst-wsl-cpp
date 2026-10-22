// fylog - C++ 版 logApp（7000）：客户端日志/异常收集端点
// 替代原 Node 版 logApp.js（省下一个 Node 进程 ~36MB）
#include <string>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>

#define CPPHTTPLIB_THREAD_POOL_COUNT 8
#include "httplib.h"

static std::string g_logDir = "/Server/startlog";
static int         g_port   = 7000;
static std::string g_listen = "0.0.0.0";

static std::string readfile(const std::string &p) {
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return std::string();
    std::string out;
    char        buf[4096];
    size_t      n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

static void appendfile(const std::string &p, const std::string &data) {
    FILE *f = fopen(p.c_str(), "ab");
    if (!f) return;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

static std::string nowstr() {
    time_t    t = time(nullptr);
    struct tm tm_;
    localtime_r(&t, &tm_);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_);
    return buf;
}

static void loadConf(const std::string &path) {
    std::string raw = readfile(path);
    size_t      pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find('\n', pos);
        std::string line = eol == std::string::npos ? raw.substr(pos) : raw.substr(pos, eol - pos);
        pos = eol == std::string::npos ? raw.size() : eol + 1;
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while (!k.empty() && (k.back() == ' ' || k.back() == '\r')) k.pop_back();
        while (!v.empty() && (v[0] == ' ')) v.erase(0, 1);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\r')) v.pop_back();
        if (k == "port") g_port = atoi(v.c_str());
        else if (k == "log_dir") g_logDir = v;
        else if (k == "listen") g_listen = v;
    }
}

int main(int argc, char **argv) {
    std::string conf = "/opt/fyweb/fylog.conf";
    if (argc > 1) conf = argv[1];
    loadConf(conf);
    mkdir(g_logDir.c_str(), 0777);

    httplib::Server svr;
    svr.set_payload_max_length(4 * 1024 * 1024);

    // 与原版一致：任何路径都允许跨域，GET /log 记录日志后返回空
    svr.set_pre_routing_handler([](const httplib::Request &req, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "X-Requested-With");
        res.set_header("Access-Control-Allow-Methods", "PUT,POST,GET,DELETE,OPTIONS");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    svr.Get("/log", [](const httplib::Request &req, httplib::Response &res) {
        appendfile(g_logDir + "/client_oss.log",
                   nowstr() + "\t" + req.remote_addr + "\t" + req.target + "\n");
        res.set_header("Content-Type", "application/json;charset=utf-8");
        res.set_content("", "application/json;charset=utf-8");
    });

    svr.Post("/log", [](const httplib::Request &req, httplib::Response &res) {
        appendfile(g_logDir + "/client_oss.log",
                   nowstr() + "\t" + req.remote_addr + "\t" + req.target + "\t" + req.body + "\n");
        res.set_content("", "application/json;charset=utf-8");
    });

    svr.Get("/fylog/health", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("ok", "text/plain");
    });

    svr.set_error_handler([](const httplib::Request &, httplib::Response &res) {
        if (res.body.empty()) res.set_content("", "text/plain");
    });

    fprintf(stderr, "[fylog] listen %s:%d\n", g_listen.c_str(), g_port);
    if (!svr.listen(g_listen.c_str(), g_port)) {
        fprintf(stderr, "[fylog] listen failed\n");
        return 1;
    }
    return 0;
}
