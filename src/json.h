// json.h - 极简 JSON（保序对象 + 解析 + 序列化），供 fyversion / fynative 使用
#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>

class Json {
public:
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };

    Json() : t_(NUL) {}
    explicit Json(Type t) : t_(t) {}

    static Json object() { return Json(OBJ); }
    static Json array() { return Json(ARR); }
    static Json str(const std::string &s) {
        Json j(STR);
        j.s_ = s;
        return j;
    }
    static Json num(double d) {
        Json j(NUM);
        j.n_ = d;
        j.isInt_ = (d == (double)(long long)d);
        return j;
    }
    static Json numRaw(const std::string &raw) {
        Json j(NUM);
        j.raw_ = raw;
        j.n_ = strtod(raw.c_str(), nullptr);
        j.isInt_ = raw.find_first_of(".eE") == std::string::npos;
        return j;
    }
    static Json boolean(bool b) {
        Json j(BOOL);
        j.b_ = b;
        return j;
    }

    Type type() const { return t_; }
    bool isObj() const { return t_ == OBJ; }
    bool isArr() const { return t_ == ARR; }
    bool isStr() const { return t_ == STR; }
    bool isNum() const { return t_ == NUM; }

    const std::string &strVal() const { return s_; }
    double             numVal() const { return n_; }
    bool               boolVal() const { return b_; }

    // object
    std::vector<std::pair<std::string, Json>> &items() { return o_; }
    const std::vector<std::pair<std::string, Json>> &items() const { return o_; }

    Json *find(const std::string &k) {
        for (auto &kv : o_)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
    const Json *find(const std::string &k) const {
        for (auto &kv : o_)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
    void set(const std::string &k, const Json &v) {
        for (auto &kv : o_)
            if (kv.first == k) {
                kv.second = v;
                return;
            }
        o_.emplace_back(k, v);
    }
    void setRaw(const std::string &k, const std::string &raw) {
        for (auto &kv : o_)
            if (kv.first == k) {
                kv.second = numRaw(raw);
                return;
            }
        o_.emplace_back(k, numRaw(raw));
    }
    // array
    std::vector<Json> &arr() { return a_; }
    const std::vector<Json> &arr() const { return a_; }

    // ---------------- 解析
    static bool parse(const std::string &text, Json &out) {
        Parser p{text, 0};
        if (!p.parseValue(out)) return false;
        p.skipWs();
        return true;
    }

    // ---------------- 序列化
    std::string dump() const {
        std::string out;
        out.reserve(1024);
        dumpTo(out);
        return out;
    }

private:
    struct Parser {
        const std::string &s;
        size_t             i;

        void skipWs() {
            while (i < s.size()) {
                char c = s[i];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                    i++;
                else
                    break;
            }
        }
        bool lit(const char *w) {
            size_t n = strlen(w);
            if (s.compare(i, n, w) == 0) {
                i += n;
                return true;
            }
            return false;
        }
        bool parseValue(Json &out) {
            skipWs();
            if (i >= s.size()) return false;
            char c = s[i];
            if (c == '{') return parseObject(out);
            if (c == '[') return parseArray(out);
            if (c == '"') {
                std::string v;
                if (!parseString(v)) return false;
                out = Json::str(v);
                return true;
            }
            if (lit("true")) {
                out = Json::boolean(true);
                return true;
            }
            if (lit("false")) {
                out = Json::boolean(false);
                return true;
            }
            if (lit("null")) {
                out = Json();
                return true;
            }
            return parseNumber(out);
        }
        bool parseNumber(Json &out) {
            size_t start = i;
            if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
            while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '.' || s[i] == 'e' ||
                                    s[i] == 'E' || s[i] == '-' || s[i] == '+'))
                i++;
            if (i == start) return false;
            out = Json::numRaw(s.substr(start, i - start));
            return true;
        }
        bool parseString(std::string &out) {
            if (s[i] != '"') return false;
            i++;
            out.clear();
            while (i < s.size()) {
                char c = s[i++];
                if (c == '"') return true;
                if (c != '\\') {
                    out.push_back(c);
                    continue;
                }
                if (i >= s.size()) return false;
                char e = s[i++];
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    unsigned cp = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16);
                    i += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' &&
                        s[i + 1] == 'u') {
                        unsigned lo = (unsigned)strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
                        i += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    // UTF-8 编码
                    if (cp < 0x80) {
                        out.push_back((char)cp);
                    } else if (cp < 0x800) {
                        out.push_back((char)(0xC0 | (cp >> 6)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    } else if (cp < 0x10000) {
                        out.push_back((char)(0xE0 | (cp >> 12)));
                        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back((char)(0xF0 | (cp >> 18)));
                        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: out.push_back(e);
                }
            }
            return false;
        }
        bool parseArray(Json &out) {
            out = Json::array();
            i++;  // [
            skipWs();
            if (i < s.size() && s[i] == ']') {
                i++;
                return true;
            }
            while (i < s.size()) {
                Json v;
                if (!parseValue(v)) return false;
                out.arr().push_back(std::move(v));
                skipWs();
                if (i < s.size() && s[i] == ',') {
                    i++;
                    continue;
                }
                if (i < s.size() && s[i] == ']') {
                    i++;
                    return true;
                }
                return false;
            }
            return false;
        }
        bool parseObject(Json &out) {
            out = Json::object();
            i++;  // {
            skipWs();
            if (i < s.size() && s[i] == '}') {
                i++;
                return true;
            }
            while (i < s.size()) {
                skipWs();
                std::string key;
                if (i >= s.size() || s[i] != '"') return false;
                if (!parseString(key)) return false;
                skipWs();
                if (i >= s.size() || s[i] != ':') return false;
                i++;
                Json v;
                if (!parseValue(v)) return false;
                out.items().emplace_back(key, std::move(v));
                skipWs();
                if (i < s.size() && s[i] == ',') {
                    i++;
                    continue;
                }
                if (i < s.size() && s[i] == '}') {
                    i++;
                    return true;
                }
                return false;
            }
            return false;
        }
    };

    static void escapeTo(std::string &out, const std::string &s) {
        out.push_back('"');
        for (size_t i = 0; i < s.size(); i++) {
            unsigned char c = (unsigned char)s[i];
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back((char)c);
                }
            }
        }
        out.push_back('"');
    }

    void dumpTo(std::string &out) const {
        char buf[64];
        switch (t_) {
        case NUL: out += "null"; break;
        case BOOL: out += (b_ ? "true" : "false"); break;
        case NUM:
            if (!raw_.empty()) {
                out += raw_;
            } else if (isInt_) {
                snprintf(buf, sizeof(buf), "%lld", (long long)n_);
                out += buf;
            } else {
                snprintf(buf, sizeof(buf), "%.17g", n_);
                out += buf;
            }
            break;
        case STR: escapeTo(out, s_); break;
        case ARR: {
            out.push_back('[');
            for (size_t i = 0; i < a_.size(); i++) {
                if (i) out.push_back(',');
                a_[i].dumpTo(out);
            }
            out.push_back(']');
            break;
        }
        case OBJ: {
            out.push_back('{');
            for (size_t i = 0; i < o_.size(); i++) {
                if (i) out.push_back(',');
                escapeTo(out, o_[i].first);
                out.push_back(':');
                o_[i].second.dumpTo(out);
            }
            out.push_back('}');
            break;
        }
        }
    }

    Type        t_ = NUL;
    std::string s_;
    double      n_ = 0;
    bool        b_ = false;
    bool        isInt_ = true;
    std::string raw_;
    std::vector<std::pair<std::string, Json>> o_;
    std::vector<Json>                         a_;
};
