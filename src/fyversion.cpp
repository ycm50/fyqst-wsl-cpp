// fyversion - C++ 版 versionApp（9902）：/manifest /bi /log /guidelog
// 替代原 Node 版 versionApp.js（省下一个 Node 进程 ~284MB）
#include "json.h"
#include "logbuf.h"

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define CPPHTTPLIB_THREAD_POOL_COUNT 16
#include "httplib.h"

static std::string g_versionsDir = "/Server/uc/versions";
static int         g_port        = 9902;
static std::string g_listenAddr  = "0.0.0.0";
static std::string g_logDir      = "/Server/startlog";

// 访问日志走缓冲（热路径禁止每请求 fopen）
static LogBuf g_accessLog("/Server/startlog/version_access.log", 200, 1);
static LogBuf g_biLog("/Server/startlog/bi.log", 200, 1);
static LogBuf g_clientLog("/Server/startlog/client_log.log", 200, 1);

// ---------------------------------------------------------------- 工具
static std::string readfile(const std::string &p) {
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return std::string();
    std::string out;
    char        buf[8192];
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

// 版本号比较：manifest2.3.5.json -> [2,3,5]
static std::vector<int> verOf(const std::string &name) {
    std::vector<int> v;
    std::string      s = name;
    if (s.size() > 8 && s.compare(0, 8, "manifest") == 0) s = s.substr(8);
    size_t dot = s.rfind('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    size_t p = 0;
    while (p <= s.size()) {
        size_t e = s.find('.', p);
        std::string part = (e == std::string::npos) ? s.substr(p) : s.substr(p, e - p);
        v.push_back(atoi(part.c_str()));
        if (e == std::string::npos) break;
        p = e + 1;
    }
    return v;
}

static bool verLess(const std::string &a, const std::string &b) {
    auto va = verOf(a), vb = verOf(b);
    size_t n = std::max(va.size(), vb.size());
    for (size_t i = 0; i < n; i++) {
        int x = (i < va.size()) ? va[i] : 0;
        int y = (i < vb.size()) ? vb[i] : 0;
        if (x == y) continue;
        return x < y;
    }
    return false;
}

static std::vector<std::string> listManifests() {
    std::vector<std::string> out;
    DIR                     *d = opendir(g_versionsDir.c_str());
    if (!d) return out;
    struct dirent *e;
    while ((e = readdir(d))) {
        std::string name = e->d_name;
        if (name.compare(0, 8, "manifest") != 0) continue;
        if (name.size() < 5 || name.compare(name.size() - 5, 5, ".json") != 0) continue;
        if (name == "manifest1.8.2.json") continue;  // 原版排除的 QA 版本文件
        out.push_back(name);
    }
    closedir(d);
    std::sort(out.begin(), out.end(), verLess);
    return out;
}

// ---------------------------------------------------------------- manifest 合并
static std::mutex          g_cacheMux;
static std::string         g_cache;
static std::string         g_cacheKey;
static long long           g_cacheTime = 0;

static std::string buildManifest() {
    std::vector<std::string> files = listManifests();
    if (files.empty()) return "{}";
    std::string key = files.back() + "#" + std::to_string(files.size());

    {
        std::lock_guard<std::mutex> lk(g_cacheMux);
        if (!g_cache.empty() && g_cacheKey == key && time(nullptr) - g_cacheTime < 30) return g_cache;
    }

    std::string baseText = readfile(g_versionsDir + "/" + files.back());
    Json        base;
    if (!Json::parse(baseText, base) || !base.isObj()) return "{}";

    Json *project = base.find("project");
    if (project && project->isObj()) {
        Json refresh = Json::object();
        size_t start = files.size() > 5 ? files.size() - 5 : 0;
        for (size_t i = start; i < files.size(); i++) {
            Json f;
            if (!Json::parse(readfile(g_versionsDir + "/" + files[i]), f)) continue;
            Json *p = f.find("project");
            if (!p) continue;
            Json *rr = p->find("resRefreshs");
            if (!rr) continue;
            for (auto &kv : rr->items()) refresh.set(kv.first, kv.second);
        }
        project->set("resRefreshs", refresh);
    }

    std::string out = base.dump();
    {
        std::lock_guard<std::mutex> lk(g_cacheMux);
        g_cache     = out;
        g_cacheKey  = key;
        g_cacheTime = time(nullptr);
    }
    return out;
}

// ---------------------------------------------------------------- main
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
        else if (k == "versions_dir") g_versionsDir = v;
        else if (k == "listen") g_listenAddr = v;
        else if (k == "log_dir") g_logDir = v;
    }
}

static std::string qval(const httplib::Request &req, const char *name) {
    return req.get_param_value(name);
}

int main(int argc, char **argv) {
    std::string conf = "/opt/fyweb/fyversion.conf";
    if (argc > 1) conf = argv[1];
    loadConf(conf);

    mkdir(g_logDir.c_str(), 0777);

    httplib::Server svr;
    svr.set_payload_max_length(8 * 1024 * 1024);

    // 跨域放行（客户端页面在 :81，manifest 在 :9902，属于跨源请求）
    svr.set_pre_routing_handler([](const httplib::Request &req, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "X-Requested-With, Content-Type");
        res.set_header("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // 请求日志：便于排查客户端加载卡住时到底在要什么
    // 注意：这里是热路径，必须用缓冲写盘（每请求 fopen/fwrite/fclose 实测约 40ms，
    // 会把 /manifest 的吞吐从数万 req/s 压到 ~1.1k req/s）
    svr.set_post_routing_handler([](const httplib::Request &req, httplib::Response &res) {
        g_accessLog.add(req.remote_addr + "\t" + std::to_string(res.status) + "\t" + req.path +
                        (req.target.size() > req.path.size() ? req.target.substr(req.path.size())
                                                             : std::string()) +
                        "\n");
    });

    svr.Get("/manifest", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(buildManifest(), "application/json; charset=utf-8");
    });

    // BI 打点：原版写数据库，这里落盘（数据分析用，不影响玩法）
    svr.Get("/bi", [](const httplib::Request &req, httplib::Response &res) {
        std::string line = nowstr() + "\tbi\t" + req.target + "\t" +
                           (req.has_header("X-Forwarded-For")
                                ? req.get_header_value("X-Forwarded-For")
                                : req.remote_addr) +
                           "\n";
        g_biLog.add(line);
        res.set_content("", "text/plain");
    });

    svr.Get("/log", [](const httplib::Request &req, httplib::Response &res) {
        g_clientLog.add(nowstr() + "\t" + req.target + "\n");
        res.set_content("", "text/plain");
    });

    // 新手引导埋点：格式与原版一致
    svr.Get("/guidelog", [](const httplib::Request &req, httplib::Response &res) {
        std::string line = nowstr() + "," + qval(req, "UID") + "," + qval(req, "token") + "," +
                           qval(req, "step") + "," + qval(req, "kingdom") + "," + qval(req, "num") +
                           "\n";
        appendfile("/Server/uc/ttf_progress.log", line);
        res.set_content("", "text/plain");
    });

    svr.Get("/fyversion/health", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("ok", "text/plain");
    });

    // 兜底：万一客户端把 9902 当作资源根（root_url），也把静态资源一起服出去，
    // 这样无论它从 :81 还是 :9902 拉资源都能拿到。
    const char *wwwPrefixes[] = {"/resource", "/libs", "/js", "/css", "/pay", "/gm", "/cdk"};
    for (const char *p : wwwPrefixes) {
        svr.set_mount_point(p, std::string("/www/wwwroot/www") + p);
    }

    svr.set_error_handler([](const httplib::Request &, httplib::Response &res) {
        if (res.body.empty()) res.set_content("", "text/plain");
    });

    fprintf(stderr, "[fyversion] listen %s:%d, versions=%s\n", g_listenAddr.c_str(), g_port,
            g_versionsDir.c_str());
    if (!svr.listen(g_listenAddr.c_str(), g_port)) {
        fprintf(stderr, "[fyversion] listen failed\n");
        return 1;
    }
    return 0;
}
