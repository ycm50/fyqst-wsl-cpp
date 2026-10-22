// fyweb - C++ replacement for the PHP web tier of 绯雨骑士团 H5
// common.h : shared helpers (config, md5, db, session, templating)
#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>

#include <mysql.h>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------- config
struct Cfg {
    std::string db_host = "127.0.0.1";
    int         db_port = 3306;
    std::string db_user = "root";
    std::string db_pass = "pwd123456";

    std::string db_account = "account";
    std::string db_main    = "ttf_ly_main";
    std::string db_login   = "ttf_ly_login";
    std::string db_game1   = "ttf_ly_game1";
    std::string db_game2   = "ttf_ly_game2";
    std::string db_gm      = "gmdbyzf";

    std::string gmcode     = "123456";      // GM 码  (gm/user/config.php)
    std::string gm_title   = "绯雨骑士团系统邮件";
    std::string gm_msg     = "谢谢您的支持，这是系统给您的邮件，请注意查收。";
    std::string app_key    = "QQ436043762"; // game.php sign key

    std::string cdk_admin      = "admin";
    std::string cdk_admin_pass = "123456";
    std::string cdk_title      = "GM邮件";
    std::string cdk_content    = "GM邮件已发送";
    int         cdk_max_item   = 9999;

    std::string www_root   = "/www/wwwroot/www";
    std::string tpl_dir    = "/opt/fyweb/tpl";
    std::string listen_addr = "127.0.0.1";
    int         listen_port = 9500;
    std::string timezone   = "Asia/Shanghai";
};

extern Cfg g_cfg;

// ---------------------------------------------------------------- md5
std::string md5(const std::string &s);

// ---------------------------------------------------------------- misc helpers
std::string html_escape(const std::string &s);
std::string url_encode(const std::string &s);
std::string url_decode(const std::string &s);
bool        utf8_has_cjk(const std::string &s);
std::string read_file(const std::string &path, bool *ok = nullptr);
bool        write_file(const std::string &path, const std::string &data);
std::string replace_all(std::string s, const std::string &from, const std::string &to);
std::string trim(const std::string &s);
std::string itos(long long v);
long long   now_ts();
long long   today_start_ts();          // PHP strtotime(date('Y-m-d'))
std::string now_str();                 // Y-m-d H:i:s (local tz)
std::string date_str();                // Y-m-d
std::string safe_query(const std::string &s);   // strip control chars
std::string render_tpl(const std::string &file, const std::map<std::string, std::string> &vars);

// ---------------------------------------------------------------- database
struct Result {
    std::vector<std::string> names;
    std::vector<std::vector<std::string>> rows;

    bool        empty() const { return rows.empty(); }
    size_t      count() const { return rows.size(); }
    std::string get(size_t r, const std::string &col) const;
};

class Db {
public:
    Db() = default;
    ~Db();
    Db(const Db &) = delete;
    Db &operator=(const Db &) = delete;

    bool        open(const std::string &dbname);
    bool        open_no_db();
    void        close();
    bool        ok() const { return m_ != nullptr; }
    const std::string &last_error() const { return m_err; }

    std::string esc(const std::string &s);

    bool        exec(const std::string &sql);                                  // no result expected
    bool        select(const std::string &sql, Result &out);                   // rows
    bool        insert(const std::string &sql);                                // exec + ok
    std::string scalar(const std::string &sql);                                // first col of first row
    long long   count_of(const std::string &sql);
    unsigned long long last_insert_id() const;

private:
    MYSQL      *m_ = nullptr;
    std::string m_err;
};

// ---------------------------------------------------------------- sessions
struct Session {
    std::string sid;
    long long   created = 0;
    long long   touched = 0;

    std::string user;        // game.php logged-in account
    std::string gamename;    // pay.php rolename
    long long   lasttime = 0; // gm/user/query.php rate limit

    // cdk admin session
    std::string cdk_user, cdk_pass, cdk_name, cdk_status, cdk_check;
    long long   cdk_expire = 0;
    std::string cdk_copyright;
};

class SessionStore {
public:
    // returns the session bound to the request cookie, creating one if needed.
    // `set_cookie` is non-empty when a new sid must be sent to the client.
    Session *acquire(const std::string &cookie_header, const std::string &path,
                     std::string &set_cookie);
    void     drop(const std::string &sid);
    void     gc();

private:
    std::mutex                       mux_;
    std::map<std::string, Session>   map_;
    std::string new_sid();
};

extern SessionStore g_sessions;

// small helpers on top of raw cookie header
std::string cookie_value(const std::string &cookie_header, const std::string &name);
