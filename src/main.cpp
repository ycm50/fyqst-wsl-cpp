// fyweb - main.cpp
// 用 C++ 重写绯雨骑士团 H5 的 PHP 前端层：
//   game.php            登录 / 注册网关
//   pay.php + cdkmail.php   CDK 兑换（玩家侧）
//   gm/*.php            GM 后台（管理员 + 玩家后台）
//   cdk/*.php           CDK 生成管理平台
#include "common.h"

#include <cstdlib>
#include <thread>
#include <chrono>
#include <random>
#include <sys/stat.h>

#define CPPHTTPLIB_THREAD_POOL_COUNT 24
#define CPPHTTPLIB_KEEPALIVE_TIMEOUT_SECOND 10
#define CPPHTTPLIB_READ_TIMEOUT_SECOND 15
#define CPPHTTPLIB_WRITE_TIMEOUT_SECOND 15
#include "httplib.h"

// ---------------------------------------------------------------- 基础工具
static std::string P(const httplib::Request &req, const char *name) {
    return req.get_param_value(name);
}

static std::string js_str(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '\'' || c == '\\') {
            o.push_back('\\');
            o.push_back(c);
        } else if (c == '\r') {
        } else if (c == '\n') {
            o += "\\n";
        } else {
            o.push_back(c);
        }
    }
    return o;
}

static void out_html(httplib::Response &res, const std::string &body, int status = 200) {
    res.status = status;
    res.set_content(body, "text/html; charset=utf-8");
}

// 对应 PHP 的 <script>alert('..');history.go(-1)</script>
static void alert_back(httplib::Response &res, const std::string &msg) {
    out_html(res, "<meta charset=\"utf-8\"><script>alert('" + js_str(msg) +
                      "');history.go(-1)</script>");
}

static void alert_go(httplib::Response &res, const std::string &msg, const std::string &url) {
    out_html(res, "<meta charset=\"utf-8\"><script>alert('" + js_str(msg) +
                      "');window.location.href='" + url + "';</script>");
}

static void plain(httplib::Response &res, const std::string &body) {
    res.set_content(body, "text/html; charset=utf-8");
}

static std::string tpl_path(const std::string &name) { return g_cfg.tpl_dir + "/" + name; }

// 随机整数 [lo, hi]
static int rand_int(int lo, int hi) {
    static thread_local std::mt19937 rng((unsigned)time(nullptr) ^ (unsigned)(size_t)&rng);
    std::uniform_int_distribution<int> d(lo, hi);
    return d(rng);
}

static std::string rand_hex_upper(size_t n) {
    static const char *hex = "0123456789ABCDEF";
    std::string s;
    for (size_t i = 0; i < n; i++) s.push_back(hex[rand_int(0, 15)]);
    return s;
}

// ---------------------------------------------------------------- 区服表
struct Region {
    int         id;
    std::string db;
    std::string prefix;
    std::string name;
    bool        hidden;
};

static const std::vector<Region> &regions() {
    static const std::vector<Region> r = {
        {1, g_cfg.db_game1, "s1", "绯雨骑士团一区", false},
        {2, g_cfg.db_game2, "s2", "绯雨骑士团二区", true},
    };
    return r;
}

static const Region *region_by_id(const std::string &qid) {
    std::string q = trim(qid);
    for (auto &r : regions())
        if (itos(r.id) == q) return &r;
    return nullptr;
}

// ---------------------------------------------------------------- 物品列表
struct Item {
    std::string id;
    std::string name;
};

static std::vector<Item> load_items(const std::string &path) {
    std::vector<Item> items;
    bool              ok = false;
    std::string       raw = read_file(path, &ok);
    if (!ok) return items;
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF)
        raw.erase(0, 3);
    size_t pos = 0;
    while (pos <= raw.size()) {
        size_t eol = raw.find('\n', pos);
        std::string line =
            (eol == std::string::npos) ? raw.substr(pos) : raw.substr(pos, eol - pos);
        pos = (eol == std::string::npos) ? raw.size() + 1 : eol + 1;
        line = trim(line);
        if (line.empty()) continue;
        size_t semi = line.find(';');
        if (semi == std::string::npos) continue;
        Item it;
        it.id   = trim(line.substr(0, semi));
        it.name = trim(line.substr(semi + 1));
        if (it.id.empty()) continue;
        items.push_back(it);
    }
    return items;
}

static std::string item_options_html(const std::string &path) {
    std::string out;
    for (auto &it : load_items(path)) {
        out += "<option value=\"" + html_escape(it.id) + "\">" + html_escape(it.name) + "</option>\n";
    }
    return out;
}

static std::string select_options_html(const std::string &path, const std::string &label) {
    std::string out;
    for (auto &it : load_items(path)) {
        out += "<option value=\"" + html_escape(it.id) + "\">" + html_escape(it.name) + "</option>\n";
    }
    (void)label;
    return out;
}

// ---------------------------------------------------------------- 会话辅助
struct Ctx {
    Db      *db = nullptr;
    Session *sess = nullptr;
};

// 取会话并登记 cookie
static Session *bind_session(const httplib::Request &req, httplib::Response &res) {
    std::string set_cookie;
    Session    *s = g_sessions.acquire(req.get_header_value("Cookie"), req.path, set_cookie);
    if (!set_cookie.empty()) res.set_header("Set-Cookie", set_cookie);
    return s;
}

// ================================================================ 1. game.php
static void handle_game(const httplib::Request &req, httplib::Response &res) {
    if (req.method != "POST") {
        res.set_redirect("/");
        return;
    }
    Session *sess = bind_session(req, res);
    std::string name = trim(P(req, "username"));
    std::string pass = P(req, "passwd");

    if (name.empty() && pass.empty()) {
        res.set_redirect("/");
        return;
    }
    if (utf8_has_cjk(name)) {
        alert_back(res, "用户名不能包含中文！");
        return;
    }

    Db db;
    if (!db.open(g_cfg.db_account)) {
        alert_back(res, "数据库连接失败,请联系管理员！");
        return;
    }
    std::string qn = db.esc(name);
    Result      r;
    db.select("select * from account where user='" + qn + "' limit 1", r);
    if (!r.empty()) {
        if (r.get(0, "pass") != md5(pass)) {
            alert_back(res, "登录失败,用户名或密码错误！");
            return;
        }
        sess->user = name;
    } else {
        if (!db.exec("insert into account (user,pass) values ('" + qn + "','" + md5(pass) + "')")) {
            alert_back(res, "网络繁忙,请稍后重试！");
            return;
        }
        sess->user = name;
    }
    if (sess->user.empty()) {
        res.set_redirect("/");
        return;
    }

    long long t    = today_start_ts();
    std::string sign = md5("account=" + name + "&time=" + itos(t) + "&key=" + g_cfg.app_key);
    res.set_redirect("game.html?account=" + url_encode(name) + "&time=" + itos(t) + "&sign=" + sign);
}

// ================================================================ 2. GM 后台
static std::string qu_options_html() {
    std::string out;
    for (auto &r : regions()) {
        if (r.hidden) continue;
        out += "<option value=\"" + itos(r.id) + "\">" + html_escape(r.name) + "</option>\n";
    }
    return out;
}

static void handle_gm_admin_page(const httplib::Request &req, httplib::Response &res) {
    bind_session(req, res);
    std::map<std::string, std::string> v;
    v["t"]            = itos(now_ts());
    v["qu_options"]   = qu_options_html();
    v["item_options"] = item_options_html(g_cfg.www_root + "/gm/user/item.txt");
    v["gmcode"]       = html_escape(g_cfg.gmcode);
    out_html(res, render_tpl(tpl_path("gm.html"), v));
}

static void handle_gm_player_page(const httplib::Request &req, httplib::Response &res) {
    bind_session(req, res);
    std::map<std::string, std::string> v;
    v["t"]            = itos(now_ts());
    v["qu_options"]   = qu_options_html();
    v["item_options"] = item_options_html(g_cfg.www_root + "/gm/user/item.txt");
    out_html(res, render_tpl(tpl_path("gm_player.html"), v));
}

// 生成 {"<itemId>":<num>}
static std::string mail_items_json(const std::string &item, const std::string &num) {
    std::string id = trim(item);
    std::string n  = trim(num);
    for (char c : id)
        if (!isdigit((unsigned char)c)) return std::string();
    for (char c : n)
        if (!isdigit((unsigned char)c)) return std::string();
    if (id.empty() || n.empty()) return std::string();
    return "{\"" + id + "\":" + n + "}";
}

static std::string new_order_no() {
    return "ID" + itos(rand_int(900, 999)) + "T" + itos(now_ts()) + itos(rand_int(100, 999));
}

// GM 充值：写 uw_recharge_request（status=1 由游戏服发放）
static bool gm_charge(Db &db, const Region &reg, const std::string &accountId,
                      const std::string &userId, const std::string &goodsId) {
    std::string sql =
        "insert into " + g_cfg.db_main +
        ".uw_recharge_request (`accountId`,`userId`,`serverId`,`rechargeId`,`status`,`addTime`,"
        "`goodsId`,`orderNo`,`gmStatus`,`channelId`,`sdkChannelId`) values ('" +
        accountId + "','" + userId + "','" + itos(reg.id) + "','" + goodsId + "','1',NOW(),' ','" +
        new_order_no() + "','0','10006','10776')";
    return db.exec(sql);
}

// 发道具邮件
static bool gm_mail(Db &db, const Region &reg, const std::string &userId, const std::string &items,
                    const std::string &title, const std::string &content) {
    std::string sql = "INSERT INTO " + reg.db +
                      ".uw_mail (userId,type,fromName,title,content,addTime,delHours,expireTime,"
                      "replaceArgs,items) VALUES ('" +
                      userId + "','0','','" + db.esc(title) + "','" + db.esc(content) +
                      "',NOW(),'12',date_add(now(),interval 7 day),'[]','" + db.esc(items) + "')";
    return db.exec(sql);
}

// ---- VIP 授权（原 PHP 用 1.json / 2.json，这里改用 gmdbyzf.gm_vip 表）
static bool ensure_gm_vip_table(Db &db) {
    return db.exec("create table if not exists " + g_cfg.db_gm +
                   ".gm_vip ( uid varchar(100) not null primary key, pwd varchar(100) not null, "
                   "level int not null default 0, quid int not null default 1 ) "
                   "ENGINE=InnoDB DEFAULT CHARSET=utf8");
}

static void handle_gmquery(const httplib::Request &req, httplib::Response &res) {
    if (req.method != "POST") {
        plain(res, "非法请求!请自重");
        return;
    }
    Db db;
    if (!db.open(g_cfg.db_gm)) {
        plain(res, "数据库连接失败,请联系管理员！");
        return;
    }
    ensure_gm_vip_table(db);

    std::string checknum = trim(P(req, "checknum"));
    std::string quid     = trim(P(req, "qu"));
    std::string uid      = trim(P(req, "uid"));

    if (checknum != g_cfg.gmcode) { plain(res, "GM码不对"); return; }
    const Region *reg = region_by_id(quid);
    if (!reg) { plain(res, "区号错误"); return; }
    if (uid.empty()) { plain(res, "角色ID错误"); return; }

    Result r;
    db.select("SELECT * FROM " + reg->db + ".uw_user WHERE nickName='" +
                  db.esc(reg->prefix + "." + uid) + "' limit 1",
              r);
    if (r.empty()) { plain(res, "角色不存在"); return; }
    std::string userId    = r.get(0, "id");
    std::string accountId = r.get(0, "accountId");

    std::string type = trim(P(req, "type"));
    if (type.empty()) { plain(res, "请求类型不存在！"); return; }

    if (type == "charge") {
        std::string goodsId = trim(P(req, "num"));
        if (goodsId.empty()) { plain(res, "充值项错误"); return; }
        plain(res, gm_charge(db, *reg, accountId, userId, goodsId) ? "充值成功" : "充值失败!");
        return;
    }

    if (type == "addczvip" || type == "addvip") {
        std::string pwd = trim(P(req, "pwd"));
        if (pwd.empty()) { plain(res, "玩家后台的授权密码不能为空"); return; }
        Result vr;
        db.select("select * from " + g_cfg.db_gm + ".gm_vip where uid='" + db.esc(uid) + "' limit 1",
                  vr);
        int wantLevel = (type == "addczvip") ? 0 : 1;
        if (vr.empty()) {
            db.exec("insert into " + g_cfg.db_gm + ".gm_vip (uid,pwd,level,quid) values ('" +
                    db.esc(uid) + "','" + db.esc(pwd) + "','" + itos(wantLevel) + "','" +
                    itos(reg->id) + "')");
            plain(res, (type == "addczvip") ? "加入VIP成功." : "加入或升级VIP成功.");
            return;
        }
        if (type == "addczvip") {
            plain(res, "{\"errcode\":0,\"info\":\"该ID已经是VIP了.\"}");
            return;
        }
        int oldLevel = atoi(vr.get(0, "level").c_str());
        if (oldLevel == 0) {
            db.exec("update " + g_cfg.db_gm + ".gm_vip set pwd='" + db.esc(pwd) + "',level=1,quid=" +
                    itos(reg->id) + " where uid='" + db.esc(uid) + "'");
            plain(res, "加入或升级VIP成功.");
        } else {
            plain(res, "该ID已经是VIP了.");
        }
        return;
    }

    if (type == "quxiaovip") {
        Result vr;
        db.select("select * from " + g_cfg.db_gm + ".gm_vip where uid='" + db.esc(uid) + "' limit 1",
                  vr);
        if (vr.empty()) { plain(res, "该ID并未授权."); return; }
        db.exec("delete from " + g_cfg.db_gm + ".gm_vip where uid='" + db.esc(uid) + "'");
        plain(res, "取消成功.");
        return;
    }

    if (type == "daoju") {
        std::string items = mail_items_json(P(req, "item"), P(req, "num"));
        if (items.empty()) { plain(res, "请选择物品！！"); return; }
        plain(res, gm_mail(db, *reg, userId, items, g_cfg.gm_title, g_cfg.gm_msg)
                       ? "邮件发送成功"
                       : "邮件发送失败!");
        return;
    }

    plain(res, "系统异常，请重试!");
}

// ---- 玩家后台查询（原 gm/user/query.php）
static void handle_gm_query_player(const httplib::Request &req, httplib::Response &res) {
    if (req.method != "POST") {
        plain(res, "非法请求!请自重");
        return;
    }
    Session *sess = bind_session(req, res);
    long long now  = now_ts();
    if (sess->lasttime == 0) {
        sess->lasttime = now;
    } else {
        if (now - sess->lasttime < 5) { plain(res, "刷太快了"); return; }
        sess->lasttime = now;
    }

    Db db;
    if (!db.open(g_cfg.db_gm)) { plain(res, "数据库连接失败,请联系管理员！"); return; }
    ensure_gm_vip_table(db);

    std::string quid = trim(P(req, "qu"));
    std::string uid  = trim(P(req, "uid"));
    std::string pwd  = trim(P(req, "pwd"));

    const Region *reg = region_by_id(quid);
    if (!reg) { plain(res, "区号错误"); return; }
    if (uid.empty()) { plain(res, "角色ID错误"); return; }

    Result r;
    db.select("SELECT * FROM " + reg->db + ".uw_user WHERE nickName='" +
                  db.esc(reg->prefix + "." + uid) + "' limit 1",
              r);
    if (r.empty()) { plain(res, "角色不存在"); return; }
    std::string userId = r.get(0, "id");

    std::string type = trim(P(req, "type"));
    if (type.empty()) { plain(res, "请求类型不存在！"); return; }
    if (pwd.empty()) { plain(res, "授权密码不能为空"); return; }

    Result vr;
    db.select("select * from " + g_cfg.db_gm + ".gm_vip where uid='" + db.esc(uid) + "' limit 1", vr);
    if (vr.empty()) { plain(res, "你没有VIP权限."); return; }
    if (vr.get(0, "pwd") != pwd) { plain(res, "用户密码不匹配."); return; }
    if (vr.get(0, "quid") != itos(reg->id)) { plain(res, "授权用户与当前选择大区不匹配."); return; }
    int viplevel = atoi(vr.get(0, "level").c_str());

    if (type == "charge") {
        std::string goodsId = trim(P(req, "num"));
        if (goodsId.empty()) { plain(res, "充值项错误"); return; }
        Result ur;
        db.select("select accountId from " + reg->db + ".uw_user where id='" + userId + "' limit 1", ur);
        plain(res, gm_charge(db, *reg, ur.get(0, "accountId"), userId, goodsId) ? "邮件发送成功"
                                                                              : "邮件发送失败!");
        return;
    }
    if (type == "daoju") {
        if (viplevel < 1) { plain(res, "VIP权限不足"); return; }
        std::string items = mail_items_json(P(req, "item"), P(req, "num"));
        if (items.empty()) { plain(res, "请选择物品！！"); return; }
        plain(res, gm_mail(db, *reg, userId, items, g_cfg.gm_title, g_cfg.gm_msg)
                       ? "邮件发送成功"
                       : "邮件发送失败!");
        return;
    }
    plain(res, "系统异常，请重试!");
}

// ================================================================ 3. CDK 兑换（玩家侧）
struct Pack {
    std::string items;   // 生成 items json 的模板
    std::string czitem;  // 展示文字
};

static Pack pack_of(const std::string &rechargeId, const std::vector<std::string> &sel) {
    auto s = [&](size_t i) { return (i < sel.size()) ? trim(sel[i]) : std::string(); };
    Pack p;
    if (rechargeId == "1") {
        p.items  = "{\"200\":30000,\"99\":10000000}";
        p.czitem = "钻石：3W，金币：1000W";
    } else if (rechargeId == "2") {
        p.items  = "{\"200\":60000,\"99\":15000000}";
        p.czitem = "钻石：6W，金币：1500W";
    } else if (rechargeId == "3") {
        p.items  = "{\"200\":150000,\"99\":20000000,\"" + s(0) + "\":1,\"5001\":2000}";
        p.czitem = "钻石：15W，金币：2000W ,物品：任选SSR人物*1个,C级人物果实*2000个,";
    } else if (rechargeId == "4") {
        p.items  = "{\"200\":600000,\"99\":30000000,\"" + s(0) + "\":1,\"" + s(1) +
                  "\":1,\"5001\":4000,\"100222\":5999}";
        p.czitem = "钻石：60W，金币：3000W ,物品：任选SSS宠物*1个,任选SSR人物*1个,C级人物果实*4000个,魔魂之尘*5999个";
    } else if (rechargeId == "5") {
        p.items  = "{\"200\":1000000,\"99\":50000000,\"" + s(0) + "\":1,\"" + s(1) + "\":1,\"" +
                  s(2) + "\":1,\"5001\":6000,\"950\":5000}";
        p.czitem = "钻石：100W，金币：5000W ,物品：任选SSS宠物*1个,任选SSR人物*2个,C级人物果实*6000个,深渊魔石*5000个";
    } else if (rechargeId == "6") {
        p.items  = "{\"200\":3000000,\"99\":65000000,\"" + s(0) + "\":1,\"" + s(1) + "\":1,\"" +
                  s(2) + "\":1,\"" + s(3) + "\":1,\"5002\":5000,\"100222\":9999,\"46\":999}";
        p.czitem = "钻石：300W，金币：6500W ,物品：任选SSS宠物*2个,任选SSR人物*2个,B级人物果实*5000个,魔魂之尘*9999个,XX型经验药水*999";
    } else if (rechargeId == "7") {
        p.items  = "{\"200\":6000000,\"99\":200000000,\"" + s(0) + "\":1,\"" + s(1) + "\":1,\"" +
                  s(2) + "\":1,\"" + s(3) + "\":1,\"" + s(4) +
                  "\":1,\"1100250\":1,\"1200250\":1,\"944\":999,\"948\":999,\"45\":999}";
        p.czitem = "钻石：600W，金币：2亿 ,物品：任选SSS宠物3个,任选SSR人物2个,4星100级天使1个和4星100级小恶魔1个,A级经验徽章*999个,A级经验法典*999个,拿铁咖啡*999个";
    } else if (rechargeId == "31") {
        p.items  = "{\"200\":400000,\"99\":20000000,\"" + s(0) + "\":1,\"" + s(1) +
                  "\":1,\"5001\":3000}";
        p.czitem = "钻石：40W，金币：2000W ,物品：任选SSS宠物*1个,任选SSR人物*1个,C级人物果实*3000个";
    } else {
        p.items  = "{\"200\":20000,\"99\":900000}";
        p.czitem = "钻石：2W，金币：90W";
    }
    return p;
}

// pay.php
static void handle_pay(const httplib::Request &req, httplib::Response &res) {
    Session *sess = bind_session(req, res);
    std::string orderId  = P(req, "orderId");
    std::string fee      = P(req, "fee");
    std::string feeid    = P(req, "feeid");
    std::string rolename = P(req, "rolename");

    if (!rolename.empty()) sess->gamename = rolename;

    if (req.method == "POST" && P(req, "submit") == "key") {
        std::string cdkey = trim(P(req, "cdkey"));
        if (cdkey.empty()) { alert_back(res, "激活码不可为空"); return; }
        Db db;
        if (!db.open(g_cfg.db_gm)) { alert_back(res, "系统提示：数据库连接失败"); return; }
        Result r;
        db.select("select * from cdkey where cdkey='" + db.esc(cdkey) + "' limit 1", r);
        if (r.empty() || r.get(0, "cdkey").empty()) {
            alert_back(res, "系统提示：此激活码不可用！");
            return;
        }
        std::string rechargeId = r.get(0, "status");
        if (r.get(0, "isuse") != "未使用") { alert_back(res, "系统提示：此激活码已使用！"); return; }
        res.set_redirect("cdkmail.php?rechargeId=" + url_encode(rechargeId) +
                         "&cdkey=" + url_encode(cdkey));
        return;
    }

    Pack shown = pack_of(feeid, {});

    std::map<std::string, std::string> v;
    v["session_user"] = html_escape(sess->user);
    v["orderId"]      = html_escape(orderId);
    v["fee"]          = html_escape(fee.empty() ? std::string("0") : fee);
    v["czitem"]       = html_escape(shown.czitem);
    out_html(res, render_tpl(tpl_path("pay.html"), v));
}

// 生成自选下拉框
static std::string cdkmail_selects(const std::string &rechargeId) {
    std::string pet  = g_cfg.www_root + "/pay/pet.txt";
    std::string part = g_cfg.www_root + "/pay/partner.txt";
    std::string a = " <div class=\"btn-group\" role=\"group\">";
    std::string b = "</div>";
    std::string out;
    auto one = [&](const std::string &label, const std::string &field, const std::string &file) {
        out += a;
        out += label + "<select class=\"form-control selectpicker\" id=\"" + field + "\" name=\"" +
               field + "\">";
        out += select_options_html(file, label);
        out += "</select>";
        out += b;
    };
    if (rechargeId == "3") {
        one("伙伴", "item1", part);
    } else if (rechargeId == "4" || rechargeId == "31") {
        one("宠物", "item1", pet);
        one("伙伴", "item2", part);
    } else if (rechargeId == "5") {
        one("宠物", "item1", pet);
        one("伙伴", "item2", part);
        one("伙伴", "item3", part);
    } else if (rechargeId == "6") {
        one("宠物", "item1", pet);
        one("宠物", "item2", pet);
        one("伙伴", "item3", part);
        one("伙伴", "item4", part);
    } else if (rechargeId == "7") {
        one("宠物", "item1", pet);
        one("宠物", "item2", pet);
        one("宠物", "item3", pet);
        one("伙伴", "item4", part);
        one("伙伴", "item5", part);
    }
    return out;
}

// cdkmail.php
static void handle_cdkmail(const httplib::Request &req, httplib::Response &res) {
    Session *sess = bind_session(req, res);
    std::string rechargeId = trim(P(req, "rechargeId"));
    std::string cdkey      = trim(P(req, "cdkey"));

    Db db;
    if (!db.open(g_cfg.db_gm)) { alert_back(res, "系统提示：数据库连接失败"); return; }
    Result r;
    db.select("select * from cdkey where cdkey='" + db.esc(cdkey) + "' limit 1", r);
    if (r.empty() || r.get(0, "cdkey").empty()) { alert_back(res, "系统提示：此激活码不可用！"); return; }
    if (r.get(0, "isuse") != "未使用") { alert_back(res, "系统提示：此激活码已使用！"); return; }
    if (r.get(0, "status") != rechargeId) { alert_back(res, "系统提示：订单出错！"); return; }

    std::vector<std::string> sel = {P(req, "item1"), P(req, "item2"), P(req, "item3"), P(req, "item4"),
                                    P(req, "item5")};
    Pack                     pack = pack_of(rechargeId, sel);

    if (req.method == "POST" && P(req, "submit") == "mail") {
        std::string name = trim(P(req, "name"));
        if (name.size() < 2) { alert_back(res, "角色不存在,请输入正确名称例如：s1.一阵风  !!"); return; }
        std::string sid(1, name[1]);
        std::string dbgame;
        if (sid == "1") dbgame = g_cfg.db_game1;
        else if (sid == "2") dbgame = g_cfg.db_game2;

        Db gdb;
        if (dbgame.empty() || !gdb.open(dbgame)) {
            alert_back(res, "角色不存在,请输入正确名称例如：s1.一阵风  !!");
            return;
        }
        Result ur;
        gdb.select("SELECT * FROM uw_user WHERE nickName='" + gdb.esc(name) + "' limit 1", ur);
        if (ur.empty()) {
            alert_back(res, "角色不存在,请输入正确名称例如：s1.一阵风  !!");
            return;
        }
        std::string id        = ur.get(0, "id");
        std::string accountId = ur.get(0, "accountId");
        std::string serverId  = ur.get(0, "serverId");
        if (serverId.empty()) serverId = "1";

        // 记录充值请求（status=1）
        db.exec("insert into " + g_cfg.db_main +
                ".uw_recharge_request (`accountId`,`userId`,`serverId`,`rechargeId`,`status`,"
                "`addTime`,`goodsId`,`orderNo`,`gmStatus`,`channelId`,`sdkChannelId`) values ('" +
                gdb.esc(accountId) + "','" + id + "','" + serverId + "','" + db.esc(rechargeId) +
                "','1',NOW(),' ','" + new_order_no() + "','0','10006','10776')");

        bool okMail = gdb.exec("INSERT INTO uw_mail (userId,type,fromName,title,content,addTime,"
                               "delHours,expireTime,replaceArgs,items) VALUES ('" +
                               id + "','0','','充值套餐','充值套餐已发送，请查收',NOW(),'12',"
                                     "date_add(now(),interval 7 day),'[]','" +
                               gdb.esc(pack.items) + "')");

        std::string logdir = g_cfg.www_root + "/pay/log";
        mkdir(logdir.c_str(), 0777);
        std::string logfile = logdir + "/cdkey" + date_str() + ".log";
        if (okMail) {
            db.exec("update cdkey set isuse='已使用', qu='" + gdb.esc(serverId) + "', binduser='" +
                    db.esc(name) + "', create_time=now() where cdkey='" + db.esc(cdkey) + "'");
            FILE *f = fopen(logfile.c_str(), "a");
            if (f) {
                fprintf(f, "%s\t角色%s%s套餐领取成功！激活码：%s\t 套餐：%s\t%s\n", now_str().c_str(),
                        name.c_str(), rechargeId.c_str(), cdkey.c_str(), pack.czitem.c_str(),
                        req.remote_addr.c_str());
                fclose(f);
            }
            alert_go(res, "充值成功！", "index.html");
        } else {
            FILE *f = fopen(logfile.c_str(), "a");
            if (f) {
                fprintf(f, "%s\t角色%s%s套餐领取失败！订单号：%s\t 套餐：%s\t%s\n", now_str().c_str(),
                        name.c_str(), rechargeId.c_str(), cdkey.c_str(), pack.czitem.c_str(),
                        req.remote_addr.c_str());
                fclose(f);
            }
            alert_go(res, "充值失败！", "index.html");
        }
        return;
    }

    std::map<std::string, std::string> v;
    v["name"]    = html_escape(sess->gamename);
    v["cdkey"]   = html_escape(cdkey);
    v["czitem"]  = html_escape(pack.czitem);
    v["selects"] = cdkmail_selects(rechargeId);
    out_html(res, render_tpl(tpl_path("cdkmail.html"), v));
}

// ================================================================ 4. CDK 平台
static const char *CDK_TITLE = "CDKEY";

struct CdkAuth {
    bool        ok = false;
    std::string user, name, status;
};

// 校验 cdk 后台登录状态（对应各页面开头那段校验代码）
static CdkAuth cdk_check_auth(Session *sess, Db &db, const std::string &ip) {
    CdkAuth a;
    if (!db.ok() && !db.open(g_cfg.db_gm)) return a;
    if (sess->cdk_user.empty() || sess->cdk_pass.empty()) {
        return a;
    }
    Result r;
    db.select("select * from `user` where `user`='" + db.esc(sess->cdk_user) + "' and `password`='" +
                  db.esc(sess->cdk_pass) + "' limit 1",
              r);
    if (r.empty()) {
        sess->cdk_user.clear();
        sess->cdk_pass.clear();
        return a;
    }
    std::string check = md5(r.get(0, "user") + r.get(0, "password"));
    if (sess->cdk_check != check || sess->cdk_status.empty() || sess->cdk_status != r.get(0, "status") ||
        sess->cdk_status != "admin") {
        return a;
    }
    if (sess->cdk_expire != 0 && sess->cdk_expire < now_ts()) {
        db.exec("INSERT INTO gmlog (user,create_time,name,remark,ip) VALUES ('" +
                db.esc(sess->cdk_user) + "',now(),'" + db.esc(sess->cdk_name) + "','退出登陆','" +
                db.esc(ip) + "')");
        sess->cdk_expire = 0;
        sess->cdk_user.clear();
        sess->cdk_pass.clear();
        sess->cdk_status.clear();
        return a;
    }
    sess->cdk_expire = now_ts() + 3600;
    a.ok     = true;
    a.user   = r.get(0, "user");
    a.name   = r.get(0, "name");
    a.status = r.get(0, "status");
    return a;
}

static void cdk_deny(httplib::Response &res) {
    out_html(res, "<meta charset=\"utf-8\"><script>alert('您无此权限！');window.location.href="
                  "'index.php';</script>");
}

static std::map<std::string, std::string> cdk_vars(const Session *sess) {
    std::map<std::string, std::string> v;
    v["gmbt"]      = CDK_TITLE;
    v["name"]      = html_escape(sess->cdk_name);
    v["copyright"] = std::string("<br> ");
    return v;
}

static void handle_cdk_index(const httplib::Request &req, httplib::Response &res) {
    bind_session(req, res);
    std::map<std::string, std::string> v;
    v["gmbt"] = CDK_TITLE;
    out_html(res, render_tpl(tpl_path("cdk_index.html"), v));
}

// 每个 IP 的密码错误计数（10 分钟内 5 次锁定）
static std::mutex                     g_cdk_fail_mtx;
static std::map<std::string, std::pair<int, long long>> g_cdk_fail;

static int cdk_fail_count(const std::string &ip) {
    std::lock_guard<std::mutex> lk(g_cdk_fail_mtx);
    auto it = g_cdk_fail.find(ip);
    if (it == g_cdk_fail.end()) return 0;
    if (now_ts() - it->second.second > 600) {
        g_cdk_fail.erase(it);
        return 0;
    }
    return it->second.first;
}

static void cdk_fail_add(const std::string &ip) {
    std::lock_guard<std::mutex> lk(g_cdk_fail_mtx);
    auto &e = g_cdk_fail[ip];
    e.first++;
    e.second = now_ts();
}

static void cdk_fail_clear(const std::string &ip) {
    std::lock_guard<std::mutex> lk(g_cdk_fail_mtx);
    g_cdk_fail.erase(ip);
}

static void handle_cdk_check(const httplib::Request &req, httplib::Response &res) {
    Session *sess = bind_session(req, res);
    std::string ip = req.remote_addr;

    Db db;
    if (!db.open(g_cfg.db_gm)) { alert_back(res, "系统提示：数据库连接失败"); return; }

    std::string user = trim(P(req, "user"));
    std::string pass = P(req, "pass");

    if (cdk_fail_count(ip) >= 5) {
        alert_back(res, "你已经输错5次密码,请等10分钟再试试！");
        return;
    }
    Result r;
    db.select("select * from `user` where `user`='" + db.esc(user) + "' and `password`='" +
                  db.esc(pass) + "' limit 1",
              r);
    if (r.empty()) {
        cdk_fail_add(ip);
        alert_back(res, "系统提示：帐号或者密码错误");
        return;
    }
    cdk_fail_clear(ip);
    sess->cdk_user   = r.get(0, "user");
    sess->cdk_pass   = r.get(0, "password");
    sess->cdk_name   = r.get(0, "name");
    sess->cdk_status = r.get(0, "status");
    sess->cdk_check  = md5(sess->cdk_user + sess->cdk_pass);
    sess->cdk_expire = now_ts() + 3600;

    db.exec("INSERT INTO gmlog (user,create_time,name,remark,ip) VALUES ('" + db.esc(sess->cdk_user) +
            "',now(),'" + db.esc(sess->cdk_name) + "','登陆成功','" + db.esc(ip) + "')");

    std::string status = sess->cdk_status;
    if (status == "admin") {
        res.set_redirect("gm.php");
    } else {
        // 原包里没有 player*.php / proxy.php，这里给出明确提示
        alert_go(res, "当前版本仅实现管理员后台（admin）", "index.php");
    }
}

static void handle_cdk_gm(const httplib::Request &req, httplib::Response &res) {
    Session *sess = bind_session(req, res);
    Db       db;
    CdkAuth  a = cdk_check_auth(sess, db, req.remote_addr);
    if (!a.ok) { cdk_deny(res); return; }
    out_html(res, render_tpl(tpl_path("cdk_gm.html"), cdk_vars(sess)));
}

static void handle_cdk_exit(const httplib::Request &req, httplib::Response &res) {
    Session *sess = bind_session(req, res);
    Db       db;
    if (db.open(g_cfg.db_gm) && !sess->cdk_user.empty()) {
        db.exec("INSERT INTO gmlog (user,create_time,name,remark,ip) VALUES ('" +
                db.esc(sess->cdk_user) + "',now(),'" + db.esc(sess->cdk_name) + "','退出登陆','" +
                db.esc(req.remote_addr) + "')");
    }
    sess->cdk_user.clear();
    sess->cdk_pass.clear();
    sess->cdk_status.clear();
    sess->cdk_expire = 0;
    res.set_redirect("index.php");
}

// gmkey.php
static void handle_cdk_gmkey(const httplib::Request &req, httplib::Response &res) {
    Session *sess = bind_session(req, res);
    Db       db;
    CdkAuth  a = cdk_check_auth(sess, db, req.remote_addr);
    if (!a.ok) { cdk_deny(res); return; }

    std::string keys;
    if (req.method == "POST") {
        std::string submit = P(req, "submit");
        if (submit == "generate") {
            std::string c = trim(P(req, "c"));
            int         n = atoi(c.c_str());
            if (c.empty()) { alert_go(res, "数量不可为空", "gmkey.php"); return; }
            if (n < 0) n = 0;
            if (n > 500) n = 500;
            for (int i = 0; i < n; i++) {
                std::string h = rand_hex_upper(32);
                std::string key = h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" +
                                  h.substr(16, 4) + "-" + h.substr(20, 12);
                if (!keys.empty()) keys += "\r\n";
                keys += key;
            }
        } else if (submit == "addkey") {
            std::string raw = P(req, "cdkey");
            std::string vip = trim(P(req, "vip"));
            if (vip.empty()) { alert_go(res, "VIP不可为空", "gmkey.php"); return; }
            if (trim(raw).empty()) { alert_go(res, "CDKEY不可为空", "gmkey.php"); return; }
            // 按 \r\n 或 \n 拆分
            std::vector<std::string> lines;
            {
                std::string cur;
                for (size_t i = 0; i <= raw.size(); i++) {
                    if (i == raw.size() || raw[i] == '\n') {
                        std::string t = trim(cur);
                        if (!t.empty()) lines.push_back(t);
                        cur.clear();
                    } else if (raw[i] != '\r') {
                        cur.push_back(raw[i]);
                    }
                }
            }
            int added = 0;
            for (auto &k : lines) {
                Result ex;
                db.select("select id from cdkey where cdkey='" + db.esc(k) + "' limit 1", ex);
                if (!ex.empty()) {
                    alert_go(res, "系统提示：该cdkey已经存在数据库了", "gmkey.php");
                    return;
                }
                if (!db.exec("INSERT INTO cdkey (cdkey,status,isuse) VALUES ('" + db.esc(k) + "','" +
                             db.esc(vip) + "','未使用')")) {
                    alert_go(res, "添加cdkey失败", "gmkey.php");
                    return;
                }
                added++;
                db.exec("INSERT INTO gmlog (user,create_time,name,remark,ip) VALUES ('" +
                        db.esc(sess->cdk_user) + "',now(),'" + db.esc(sess->cdk_name) +
                        "','添加 cdkey:" + db.esc(k) + " 成功!!','" + db.esc(req.remote_addr) + "')");
            }
            alert_go(res, "添加cdkey成功(" + itos(added) + "条)", "gmkey.php");
            return;
        }
    }

    std::map<std::string, std::string> v = cdk_vars(sess);
    v["keys"] = html_escape(keys);
    out_html(res, render_tpl(tpl_path("cdk_gmkey.html"), v));
}

// 列表页（gmkeylist.php = 未使用列表；gmallkeylist.php = 全部列表）
static std::string key_rows_html(Db &db, const std::string &where, int page, int per,
                                 bool all_columns) {
    int total = (int)db.count_of("select count(*) from cdkey " + where);
    if (page < 1) page = 1;
    int totalPage = (total + per - 1) / per;
    int start     = (page - 1) * per;
    Result r;
    db.select("select * from cdkey " + where + " limit " + itos(start) + "," + itos(per), r);
    std::string rows;
    for (size_t i = 0; i < r.count(); i++) {
        rows += "<tr>";
        rows += "<td>" + html_escape(r.get(i, "id")) + "</td>";
        rows += "<td>" + html_escape(r.get(i, "cdkey")) + "</td>";
        rows += "<td>" + html_escape(r.get(i, "status")) + "</td>";
        rows += "<td>" + html_escape(r.get(i, "isuse")) + "</td>";
        if (all_columns) {
            rows += "<td>" + html_escape(r.get(i, "qu")) + "</td>";
            rows += "<td>" + html_escape(r.get(i, "binduser")) + "</td>";
            rows += "<td>" + html_escape(r.get(i, "create_time")) + "</td>";
        }
        rows += "<td><a href='gmcopy.php?getkey=" + url_encode(r.get(i, "cdkey")) + "'>复制</a></td>";
        rows += "</tr>\n";
    }
    return rows;
}

static std::string key_pager_html(const std::string &base, int page, int totalPage,
                                  const std::string &extra) {
    std::string p;
    if (page != 1)
        p += "<tr><a href=\"" + base + "?page=" + itos(page - 1) + extra + "\">上一页</a></tr>\n";
    for (int i = 1; i <= totalPage; i++) {
        p += "<tr><a href=\"" + base + "?page=" + itos(i) + extra + "\"> 第 " + itos(i) + " 页 </a></tr>\n";
    }
    if (page < totalPage)
        p += "<tr><a href=\"" + base + "?page=" + itos(page + 1) + extra + "\">下一页</a></tr>\n";
    return p;
}

static void handle_cdk_keylist(const httplib::Request &req, httplib::Response &res) {
    Session *sess = bind_session(req, res);
    Db       db;
    CdkAuth  a = cdk_check_auth(sess, db, req.remote_addr);
    if (!a.ok) { cdk_deny(res); return; }

    bool all  = req.path.find("gmallkeylist") != std::string::npos;
    int  page = atoi(P(req, "page").c_str());
    if (page < 1) page = 1;
    int per = 10;

    std::string where, extra;
    if (!all) {
        std::string vip = trim(P(req, "vip"));
        where = "where isuse='未使用' and status='" + db.esc(vip) + "'";
        extra = "&vip=" + url_encode(vip);
    }

    std::string base = all ? "gmallkeylist.php" : "gmkeylist.php";
    int total = (int)db.count_of("select count(*) from cdkey " + where);
    int totalPage = (total + per - 1) / per;

    std::map<std::string, std::string> v = cdk_vars(sess);
    v["rows"]   = key_rows_html(db, where, page, per, all);
    v["pager"]  = key_pager_html(base, page, totalPage, extra);
    v["title"]  = all ? "CDKEY所有列表" : "CDKEY未使用列表";
    v["total"]  = itos(total);
    out_html(res, render_tpl(tpl_path("cdk_keylist.html"), v));
}

static void handle_cdk_gmcopy(const httplib::Request &req, httplib::Response &res) {
    Session *sess = bind_session(req, res);
    Db       db;
    CdkAuth  a = cdk_check_auth(sess, db, req.remote_addr);
    if (!a.ok) { cdk_deny(res); return; }
    std::map<std::string, std::string> v = cdk_vars(sess);
    std::string key = P(req, "getkey");
    v["getkey"] = html_escape(key);
    out_html(res, render_tpl(tpl_path("cdk_gmcopy.html"), v));
}

// ================================================================ 启动
static void load_config(const std::string &path) {
    bool        ok  = false;
    std::string raw = read_file(path, &ok);
    if (!ok) return;
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find('\n', pos);
        std::string line = (eol == std::string::npos) ? raw.substr(pos) : raw.substr(pos, eol - pos);
        pos = (eol == std::string::npos) ? raw.size() : eol + 1;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq));
        std::string v = trim(line.substr(eq + 1));
        if (k == "db_host") g_cfg.db_host = v;
        else if (k == "db_port") g_cfg.db_port = atoi(v.c_str());
        else if (k == "db_user") g_cfg.db_user = v;
        else if (k == "db_pass") g_cfg.db_pass = v;
        else if (k == "db_account") g_cfg.db_account = v;
        else if (k == "db_main") g_cfg.db_main = v;
        else if (k == "db_login") g_cfg.db_login = v;
        else if (k == "db_game1") g_cfg.db_game1 = v;
        else if (k == "db_game2") g_cfg.db_game2 = v;
        else if (k == "db_gm") g_cfg.db_gm = v;
        else if (k == "gmcode") g_cfg.gmcode = v;
        else if (k == "app_key") g_cfg.app_key = v;
        else if (k == "cdk_admin") g_cfg.cdk_admin = v;
        else if (k == "cdk_admin_pass") g_cfg.cdk_admin_pass = v;
        else if (k == "www_root") g_cfg.www_root = v;
        else if (k == "tpl_dir") g_cfg.tpl_dir = v;
        else if (k == "listen_addr") g_cfg.listen_addr = v;
        else if (k == "listen_port") g_cfg.listen_port = atoi(v.c_str());
        else if (k == "timezone") g_cfg.timezone = v;
    }
}

static void ensure_cdk_schema() {
    Db db;
    if (!db.open(std::string())) {
        fprintf(stderr, "[fyweb] cannot connect mysql: %s\n", db.last_error().c_str());
        return;
    }
    db.exec("CREATE DATABASE IF NOT EXISTS " + g_cfg.db_gm + " DEFAULT CHARACTER SET utf8");
    db.exec("CREATE TABLE IF NOT EXISTS " + g_cfg.db_gm +
            ".user (id INT NOT NULL AUTO_INCREMENT, user VARCHAR(100) NOT NULL, "
            "password VARCHAR(40) NOT NULL, name VARCHAR(100) NULL, status VARCHAR(100) NOT NULL, "
            "qu INT NOT NULL, roleid VARCHAR(100) NULL, endtime datetime NULL, PRIMARY KEY(id)) "
            "ENGINE=MyISAM DEFAULT CHARSET=utf8");
    db.exec("CREATE TABLE IF NOT EXISTS " + g_cfg.db_gm +
            ".cdkey (id INT NOT NULL AUTO_INCREMENT, cdkey VARCHAR(1000) NOT NULL, "
            "status VARCHAR(100) NOT NULL, isuse VARCHAR(100) NULL, qu INT NULL, "
            "binduser VARCHAR(100) NULL, create_time datetime NULL, PRIMARY KEY(id)) "
            "ENGINE=MyISAM DEFAULT CHARSET=utf8");
    db.exec("CREATE TABLE IF NOT EXISTS " + g_cfg.db_gm +
            ".gmlog (id INT NOT NULL AUTO_INCREMENT, user VARCHAR(100) NOT NULL, "
            "create_time datetime NOT NULL, name VARCHAR(100) NULL, remark VARCHAR(1000) NOT NULL, "
            "ip VARCHAR(100) NOT NULL, PRIMARY KEY(id)) ENGINE=MyISAM DEFAULT CHARSET=utf8");
    ensure_gm_vip_table(db);
    // 首次初始化：写入管理员账号
    if (db.count_of("select count(*) from " + g_cfg.db_gm + ".user") == 0) {
        db.exec("INSERT INTO " + g_cfg.db_gm + ".user (user,password,status,qu,roleid,endtime) VALUES ('" +
                db.esc(g_cfg.cdk_admin) + "','" + db.esc(g_cfg.cdk_admin_pass) +
                "','admin','1','',NOW())");
        fprintf(stderr, "[fyweb] cdk admin account created: %s / %s\n", g_cfg.cdk_admin.c_str(),
                g_cfg.cdk_admin_pass.c_str());
    }
}

int main(int argc, char **argv) {
    std::string conf = "/opt/fyweb/fyweb.conf";
    if (argc > 1) conf = argv[1];
    load_config(conf);

    setenv("TZ", g_cfg.timezone.c_str(), 1);
    tzset();
    mysql_library_init(0, nullptr, nullptr);

    ensure_cdk_schema();

    httplib::Server svr;
    svr.set_payload_max_length(32 * 1024 * 1024);

    // 登录 / 注册网关（原 game.php）
    svr.Get("/game.php", [](const httplib::Request &req, httplib::Response &res) {
        Session *s = bind_session(req, res);
        (void)s;
        res.set_redirect("/");
    });
    svr.Post("/game.php", handle_game);

    // CDK 兑换（原 pay.php / cdkmail.php）
    svr.Get("/pay.php", handle_pay);
    svr.Post("/pay.php", handle_pay);
    svr.Get("/cdkmail.php", handle_cdkmail);
    svr.Post("/cdkmail.php", handle_cdkmail);

    // GM 后台（原 gm/*.php）
    svr.Get("/gm/gm.php", handle_gm_admin_page);
    svr.Get("/gm/index.php", handle_gm_player_page);
    svr.Get("/gm/user/gmquery.php", handle_gmquery);
    svr.Post("/gm/user/gmquery.php", handle_gmquery);
    svr.Get("/gm/user/query.php", handle_gm_query_player);
    svr.Post("/gm/user/query.php", handle_gm_query_player);

    // CDK 管理平台（原 cdk/*.php）
    svr.Get("/cdk/index.php", handle_cdk_index);
    svr.Post("/cdk/check.php", handle_cdk_check);
    svr.Get("/cdk/check.php", handle_cdk_check);
    svr.Get("/cdk/gm.php", handle_cdk_gm);
    svr.Get("/cdk/gmkey.php", handle_cdk_gmkey);
    svr.Post("/cdk/gmkey.php", handle_cdk_gmkey);
    svr.Get("/cdk/gmkeylist.php", handle_cdk_keylist);
    svr.Get("/cdk/gmallkeylist.php", handle_cdk_keylist);
    svr.Get("/cdk/gmcopy.php", handle_cdk_gmcopy);
    svr.Get("/cdk/exit.php", handle_cdk_exit);

    // 健康检查
    svr.Get("/fyweb/health", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("ok", "text/plain");
    });

    // 定时清理过期会话
    std::thread([]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(300));
            g_sessions.gc();
        }
    }).detach();

    fprintf(stderr, "[fyweb] listening on %s:%d, www_root=%s\n", g_cfg.listen_addr.c_str(),
            g_cfg.listen_port, g_cfg.www_root.c_str());
    if (!svr.listen(g_cfg.listen_addr.c_str(), g_cfg.listen_port)) {
        fprintf(stderr, "[fyweb] failed to listen\n");
        return 1;
    }
    return 0;
}
