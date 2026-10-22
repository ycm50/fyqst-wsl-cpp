// fyweb - common.cpp : md5 / 工具函数 / 数据库 / 会话
#include "common.h"

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <random>

// ============================================================ 全局配置
Cfg          g_cfg;
SessionStore g_sessions;

// ============================================================ MD5 (RFC 1321)
namespace {

struct MD5Ctx {
    uint32_t      h[4];
    uint64_t      len;
    unsigned char buf[64];
    size_t        n;
};

inline uint32_t rotl32(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

void md5_transform(MD5Ctx &c, const unsigned char *p) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
        0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
        0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
        0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
        0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
        0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

    uint32_t M[16];
    for (int i = 0; i < 16; i++) {
        M[i] = (uint32_t)p[i * 4] | ((uint32_t)p[i * 4 + 1] << 8) |
               ((uint32_t)p[i * 4 + 2] << 16) | ((uint32_t)p[i * 4 + 3] << 24);
    }

    uint32_t A = c.h[0], B = c.h[1], C = c.h[2], D = c.h[3];
    for (int i = 0; i < 64; i++) {
        uint32_t F;
        int      g;
        if (i < 16) {
            F = (B & C) | (~B & D);
            g = i;
        } else if (i < 32) {
            F = (D & B) | (~D & C);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            F = B ^ C ^ D;
            g = (3 * i + 5) % 16;
        } else {
            F = C ^ (B | ~D);
            g = (7 * i) % 16;
        }
        uint32_t tmp = D;
        D            = C;
        C            = B;
        B            = B + rotl32(A + F + K[i] + M[g], S[i]);
        A            = tmp;
    }
    c.h[0] += A;
    c.h[1] += B;
    c.h[2] += C;
    c.h[3] += D;
}

void md5_init(MD5Ctx &c) {
    c.h[0] = 0x67452301;
    c.h[1] = 0xefcdab89;
    c.h[2] = 0x98badcfe;
    c.h[3] = 0x10325476;
    c.len  = 0;
    c.n    = 0;
}

void md5_update(MD5Ctx &c, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    c.len += len;
    while (len > 0) {
        size_t take = 64 - c.n;
        if (take > len) take = len;
        memcpy(c.buf + c.n, p, take);
        c.n += take;
        p += take;
        len -= take;
        if (c.n == 64) {
            md5_transform(c, c.buf);
            c.n = 0;
        }
    }
}

void md5_final(MD5Ctx &c, unsigned char out[16]) {
    uint64_t bits = c.len * 8;
    unsigned char pad[72];
    size_t        plen = (c.n < 56) ? (56 - c.n) : (120 - c.n);
    pad[0]             = 0x80;
    memset(pad + 1, 0, plen - 1);
    for (int i = 0; i < 8; i++) pad[plen + i] = (unsigned char)((bits >> (8 * i)) & 0xff);
    md5_update(c, pad, plen + 8);
    for (int i = 0; i < 4; i++) {
        out[i * 4]     = (unsigned char)(c.h[i] & 0xff);
        out[i * 4 + 1] = (unsigned char)((c.h[i] >> 8) & 0xff);
        out[i * 4 + 2] = (unsigned char)((c.h[i] >> 16) & 0xff);
        out[i * 4 + 3] = (unsigned char)((c.h[i] >> 24) & 0xff);
    }
}

} // namespace

std::string md5(const std::string &s) {
    MD5Ctx        c;
    unsigned char d[16];
    md5_init(c);
    md5_update(c, s.data(), s.size());
    md5_final(c, d);
    static const char *hex = "0123456789abcdef";
    std::string        out;
    out.reserve(32);
    for (int i = 0; i < 16; i++) {
        out.push_back(hex[d[i] >> 4]);
        out.push_back(hex[d[i] & 0x0f]);
    }
    return out;
}

// ============================================================ 工具函数
std::string html_escape(const std::string &s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size(); i++) {
        switch (s[i]) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        case '\'': o += "&#39;"; break;
        default: o.push_back(s[i]);
        }
    }
    return o;
}

std::string url_encode(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string        o;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            o.push_back((char)c);
        } else {
            o.push_back('%');
            o.push_back(hex[c >> 4]);
            o.push_back(hex[c & 0x0f]);
        }
    }
    return o;
}

std::string url_decode(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hexv = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hexv(s[i + 1]), lo = hexv(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                o.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (s[i] == '+') {
            o.push_back(' ');
        } else {
            o.push_back(s[i]);
        }
    }
    return o;
}

// 判断 UTF-8 字符串中是否含有中日韩统一表意文字（对应 PHP 的 /[\x{4e00}-\x{9fa5}]/u）
bool utf8_has_cjk(const std::string &s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        uint32_t      cp = 0;
        int           extra;
        if (c < 0x80) {
            i++;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            i++;
            continue;
        }
        if (i + extra >= s.size()) break;
        for (int k = 1; k <= extra; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += extra + 1;
        if (cp >= 0x4E00 && cp <= 0x9FA5) return true;
    }
    return false;
}

std::string read_file(const std::string &path, bool *ok) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        if (ok) *ok = false;
        return std::string();
    }
    std::string out;
    char        buf[8192];
    size_t      n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    if (ok) *ok = true;
    return out;
}

bool write_file(const std::string &path, const std::string &data) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

std::string replace_all(std::string s, const std::string &from, const std::string &to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string itos(long long v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", v);
    return std::string(buf);
}

long long now_ts() { return (long long)time(nullptr); }

long long today_start_ts() {
    time_t    t = time(nullptr);
    struct tm tm_;
    localtime_r(&t, &tm_);
    tm_.tm_hour = 0;
    tm_.tm_min  = 0;
    tm_.tm_sec  = 0;
    return (long long)mktime(&tm_);
}

std::string now_str() {
    time_t    t = time(nullptr);
    struct tm tm_;
    localtime_r(&t, &tm_);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_);
    return std::string(buf);
}

std::string date_str() {
    time_t    t = time(nullptr);
    struct tm tm_;
    localtime_r(&t, &tm_);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_);
    return std::string(buf);
}

std::string safe_query(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\r' || c == '\n' || c == '\0') continue;
        o.push_back((char)c);
    }
    return o;
}

std::string render_tpl(const std::string &file, const std::map<std::string, std::string> &vars) {
    bool        ok = false;
    std::string tpl = read_file(file, &ok);
    if (!ok) return "<h1>template missing: " + html_escape(file) + "</h1>";
    for (auto &kv : vars) tpl = replace_all(tpl, "{{" + kv.first + "}}", kv.second);
    // 未替换的占位符清空，避免把 {{x}} 暴露到页面上
    size_t p = 0;
    while ((p = tpl.find("{{", p)) != std::string::npos) {
        size_t e = tpl.find("}}", p);
        if (e == std::string::npos) break;
        tpl.erase(p, e - p + 2);
    }
    return tpl;
}

// ============================================================ 数据库
std::string Result::get(size_t r, const std::string &col) const {
    if (r >= rows.size()) return std::string();
    for (size_t i = 0; i < names.size(); i++) {
        if (names[i] == col) return (i < rows[r].size()) ? rows[r][i] : std::string();
    }
    return std::string();
}

Db::~Db() { close(); }

void Db::close() {
    if (m_) {
        mysql_close(m_);
        m_ = nullptr;
    }
}

bool Db::open(const std::string &dbname) {
    close();
    m_ = mysql_init(nullptr);
    if (!m_) {
        m_err = "mysql_init failed";
        return false;
    }
    bool reconnect = true;
    mysql_options(m_, MYSQL_OPT_RECONNECT, &reconnect);
    mysql_options(m_, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    unsigned int timeout = 10;
    mysql_options(m_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    if (!mysql_real_connect(m_, g_cfg.db_host.c_str(), g_cfg.db_user.c_str(),
                            g_cfg.db_pass.c_str(), dbname.c_str(), (unsigned int)g_cfg.db_port,
                            nullptr, 0)) {
        m_err = mysql_error(m_);
        close();
        return false;
    }
    return true;
}

bool Db::open_no_db() { return open(std::string()); }

std::string Db::esc(const std::string &s) {
    if (!m_) return s;
    std::string out(s.size() * 2 + 1, '\0');
    unsigned long n = mysql_real_escape_string(m_, &out[0], s.c_str(), (unsigned long)s.size());
    out.resize(n);
    return out;
}

bool Db::exec(const std::string &sql) {
    if (!m_) return false;
    if (mysql_real_query(m_, sql.c_str(), (unsigned long)sql.size()) != 0) {
        m_err = mysql_error(m_);
        return false;
    }
    // 丢弃可能的结果集
    MYSQL_RES *r = mysql_store_result(m_);
    if (r) mysql_free_result(r);
    return true;
}

bool Db::select(const std::string &sql, Result &out) {
    out = Result();
    if (!m_) return false;
    if (mysql_real_query(m_, sql.c_str(), (unsigned long)sql.size()) != 0) {
        m_err = mysql_error(m_);
        return false;
    }
    MYSQL_RES *res = mysql_store_result(m_);
    if (!res) {
        m_err = mysql_error(m_);
        return false;
    }
    unsigned int nf = mysql_num_fields(res);
    MYSQL_FIELD *fields = mysql_fetch_fields(res);
    out.names.reserve(nf);
    for (unsigned int i = 0; i < nf; i++) out.names.push_back(fields[i].name ? fields[i].name : "");
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        unsigned long *lens = mysql_fetch_lengths(res);
        std::vector<std::string> r;
        r.reserve(nf);
        for (unsigned int i = 0; i < nf; i++) {
            if (row[i])
                r.push_back(std::string(row[i], lens[i]));
            else
                r.push_back(std::string());
        }
        out.rows.push_back(std::move(r));
    }
    mysql_free_result(res);
    return true;
}

bool Db::insert(const std::string &sql) { return exec(sql); }

std::string Db::scalar(const std::string &sql) {
    Result r;
    if (!select(sql, r) || r.empty() || r.rows[0].empty()) return std::string();
    return r.rows[0][0];
}

long long Db::count_of(const std::string &sql) {
    std::string v = scalar(sql);
    if (v.empty()) return 0;
    return strtoll(v.c_str(), nullptr, 10);
}

unsigned long long Db::last_insert_id() const { return m_ ? mysql_insert_id(m_) : 0; }

// ============================================================ 会话
std::string SessionStore::new_sid() {
    static std::mt19937_64 rng(std::random_device{}() ^ (unsigned long long)time(nullptr));
    static const char *hexs = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 32; i++) s.push_back(hexs[rng() & 0xf]);
    return s;
}

std::string cookie_value(const std::string &cookie_header, const std::string &name) {
    size_t pos = 0;
    while (pos < cookie_header.size()) {
        size_t semi = cookie_header.find(';', pos);
        std::string item = cookie_header.substr(pos, semi == std::string::npos ? std::string::npos
                                                                              : semi - pos);
        item = trim(item);
        size_t eq = item.find('=');
        if (eq != std::string::npos) {
            if (trim(item.substr(0, eq)) == name) return trim(item.substr(eq + 1));
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return std::string();
}

Session *SessionStore::acquire(const std::string &cookie_header, const std::string &path,
                               std::string &set_cookie) {
    std::string sid = cookie_value(cookie_header, "FYWSID");
    std::lock_guard<std::mutex> lk(mux_);
    auto it = map_.find(sid);
    if (sid.empty() || it == map_.end()) {
        Session s;
        s.sid     = new_sid();
        s.created = time(nullptr);
        s.touched = s.created;
        map_[s.sid] = s;
        set_cookie  = "FYWSID=" + s.sid + "; Path=/; HttpOnly";
        return &map_[s.sid];
    }
    it->second.touched = time(nullptr);
    return &it->second;
}

void SessionStore::drop(const std::string &sid) {
    std::lock_guard<std::mutex> lk(mux_);
    map_.erase(sid);
}

void SessionStore::gc() {
    long long t = time(nullptr);
    std::lock_guard<std::mutex> lk(mux_);
    for (auto it = map_.begin(); it != map_.end();) {
        if (t - it->second.touched > 3600 * 4)
            it = map_.erase(it);
        else
            ++it;
    }
}
