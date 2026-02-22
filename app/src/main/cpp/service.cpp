// 最小化头文件包含以减少内存占用
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <cstring>
#include <thread>
#include <sys/file.h>
#include <chrono>
#include <linux/input.h>
#include <android/log.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <cstdarg>
#include <sys/resource.h>
#include <sched.h>
#include <sys/mman.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <android/log.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <cstdarg>
#include <sys/resource.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>



#define LOG_TAG "KCTRL"
#define LOG_FILE "/storage/emulated/0/Android/data/com.kcrlfront/files/klog.log"

static void ignore_sigpipe(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // ⭐ 很关键

    sigaction(SIGPIPE, &sa, NULL);
}
// 日志文件输出函数 - 内存优化版本
void write_log_to_file(const char* level, const char* format, ...) {
    // 使用静态缓冲区减少内存分配
    static char buffer[512];
    static char time_buffer[32];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // 简化时间格式以减少开销
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm* tm_info = localtime(&time_t);
    snprintf(time_buffer, sizeof(time_buffer), "%02d:%02d:%02d",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

    // 使用C风格文件操作减少开销
    FILE* logfile = fopen(LOG_FILE, "a");
    if (logfile) {
        fprintf(logfile, "[%s][%s] %s\n", time_buffer, level, buffer);
        fclose(logfile);
    }
}

#define LOGI(...) do { \
    if (g_enable_log) { \
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); \
        write_log_to_file("INFO", __VA_ARGS__); \
    } \
} while(0)

#define LOGW(...) do { \
    if (g_enable_log) { \
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__); \
        write_log_to_file("WARN", __VA_ARGS__); \
    } \
} while(0)

#define LOGE(...) do { \
    if (g_enable_log) { \
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); \
        write_log_to_file("ERROR", __VA_ARGS__); \
    } \
} while(0)




// 按键状态结构体 - 极致内存优化版本
struct KeyState {
    uint64_t press_time_ns;      // 纳秒时间戳，8字节
    uint64_t last_click_time_ns; // 纳秒时间戳，8字节
    uint8_t flags;               // 位域：bit0=is_pressed, bit1=timer_active, bit2-7=click_count
    std::thread timer_thread;

    KeyState() : press_time_ns(0), last_click_time_ns(0), flags(0) {}

    inline bool is_pressed() const { return flags & 1; }
    inline void set_pressed(bool pressed) {
        flags = pressed ? (flags | 1) : (flags & 0xFE);
    }
    inline bool timer_active() const { return flags & 2; }
    inline void set_timer_active(bool active) {
        flags = active ? (flags | 2) : (flags & 0xFD);
    }
    inline uint8_t click_count() const { return (flags >> 2) & 0x3F; }
    inline void set_click_count(uint8_t count) {
        flags = (flags & 3) | ((count & 0x3F) << 2);
    }
};

// 全局变量 - 极致内存优化版本
static std::atomic<bool> g_running{true};
static int g_wakelock_fd = -1;

// 使用预分配的小容量容器减少内存碎片
static std::unordered_map<std::string, std::string> g_config;
static std::unordered_map<int, KeyState> g_key_states;

// 合并互斥锁减少同步开销
static std::mutex g_global_mutex;
static std::condition_variable g_shutdown_cv;

// 时间配置参数（毫秒）
static int g_click_threshold = 200;
static int g_short_press_threshold = 500;
static int g_long_press_threshold = 1000;
static int g_double_click_interval = 300;

// 日志开关配置
static bool g_enable_log = false;
// 新增：全局配置文件路径，便于线程内重新加载配置确认独占
static std::string g_config_path;

// 按键扫描相关全局变量
static std::atomic<bool> g_key_scanning{false};
static std::atomic<int> g_scan_state{0};  // 0=正在扫描, 1=已获取按键
static std::string g_last_key;
static std::string g_current_device;
static std::mutex g_key_mutex;
static int g_scan_fd = -1;


void LOGU(const char* format, ...) {
    static char msg[256];

    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    // 输出到 logcat 与文件
    if (g_enable_log) {
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "USER: %s", msg);
        write_log_to_file("USER", "%s", msg);
    }

    // 调用 Java 层悬浮窗服务
    std::string cmd = "am startservice -n com.idlike.kctrl.mgr/.capi.msgx --es text '";
    std::string safeMsg = msg;
    for (char& c : safeMsg) {
        if (c == '\'') c = ' '; // 防止命令注入
    }
    cmd += safeMsg;
    cmd += "' >/dev/null 2>&1";

    std::system(cmd.c_str());
}

// 信号处理函数
void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        {
            std::lock_guard<std::mutex> lock(g_global_mutex);
            g_running = false;
        }
        g_shutdown_cv.notify_all();
        LOGI("Received signal %d, shutting down...", sig);
    }
}


bool check_single_instance() {
    const char* cmd = "top -n 1 | grep libservice.so";
    FILE* fp = popen(cmd, "r");
    if (!fp) return false;

    char line[512];
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        // 排除 grep 自身的行
        if (strstr(line, "grep libservice.so")) continue;

        // 如果包含 libservice.so，计数
        if (strstr(line, "libservice.so")) count++;
        LOGU("%s", line);
    }

    pclose(fp);
    printf("%d",count);
    // count > 0 表示已有实例
    return count > 1;
}



// 获取WakeLock
bool acquire_wakelock() {
    system("am broadcast -a com.idlike.kctrl.service.GET_WAKELOCK");
    return true;
}

// 释放WakeLock
void release_wakelock() {
    if (g_wakelock_fd != -1) {
        const char* wakeunlock_path = "/sys/power/wake_unlock";
        int fd = open(wakeunlock_path, O_WRONLY);
        if (fd != -1) {
            const char* lock_name = "kctrl_wakelock";
            write(fd, lock_name, strlen(lock_name));
            close(fd);
        }
        close(g_wakelock_fd);
        g_wakelock_fd = -1;
        LOGI("Wake lock released");
    }
}

// 读取配置文件 - 深度内存优化版本
bool load_config(const char* config_file) {
    // 清空现有配置，确保重新加载时不会累积旧配置
    g_config.clear();

    // 使用C风格文件操作减少内存开销
    FILE* file = fopen(config_file, "r");
    if (!file) {
        LOGE("Failed to open config file: %s", config_file);
        return false;
    }

    // 使用固定大小缓冲区避免动态分配
    static char line_buffer[256];
    static char key_buffer[64];
    static char value_buffer[192];

    while (fgets(line_buffer, sizeof(line_buffer), file)) {
        // 跳过空行和注释
        if (line_buffer[0] == '\n' || line_buffer[0] == '#') continue;

        // 查找等号分隔符
        char* eq_pos = strchr(line_buffer, '=');
        if (!eq_pos) continue;

        // 分离键值对
        size_t key_len = eq_pos - line_buffer;
        if (key_len >= sizeof(key_buffer)) continue;

        strncpy(key_buffer, line_buffer, key_len);
        key_buffer[key_len] = '\0';

        // 移除值末尾的换行符
        char* value_start = eq_pos + 1;
        size_t value_len = strlen(value_start);
        if (value_len > 0 && value_start[value_len - 1] == '\n') {
            value_start[value_len - 1] = '\0';
        }

        strncpy(value_buffer, value_start, sizeof(value_buffer) - 1);
        value_buffer[sizeof(value_buffer) - 1] = '\0';

        // 存储配置
        g_config.emplace(key_buffer, value_buffer);

        // 只对非脚本配置输出日志
        if (strncmp(key_buffer, "script_", 7) != 0) {
            LOGI("Config: %s = %s", key_buffer, value_buffer);
        }

        // 解析时间配置参数
        if (strcmp(key_buffer, "click_threshold") == 0) {
            g_click_threshold = atoi(value_buffer);
        } else if (strcmp(key_buffer, "short_press_threshold") == 0) {
            g_short_press_threshold = atoi(value_buffer);
        } else if (strcmp(key_buffer, "long_press_threshold") == 0) {
            g_long_press_threshold = atoi(value_buffer);
        } else if (strcmp(key_buffer, "double_click_interval") == 0) {
            g_double_click_interval = atoi(value_buffer);
        } else if (strcmp(key_buffer, "enable_log") == 0) {
            g_enable_log = (atoi(value_buffer) != 0);
        }
    }

    fclose(file);
    LOGI("Config loaded - Click: %dms, Short: %dms, Long: %dms, Double: %dms, Log: %s",
         g_click_threshold, g_short_press_threshold, g_long_press_threshold, g_double_click_interval,
         g_enable_log ? "enabled" : "disabled");
    return true;
}

// 基于通配符的简单匹配（仅支持'*'）
static bool wildcard_match(const std::string& text, const std::string& pattern) {
    size_t t = 0, p = 0, star = std::string::npos, match = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == text[t])) {
            ++t; ++p; // 逐字符匹配
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            match = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++match; // 尝试扩展'*'匹配
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

// 获取输入设备名称
static bool get_input_device_name(const std::string& device_path, std::string& out_name) {
    int fd = open(device_path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    char name[256] = {0};
    bool ok = ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0;
    close(fd);
    if (ok) out_name.assign(name);
    return ok;
}

// 从 /dev/input 直接枚举 event 设备并读取名称
struct InputDevInfo { std::string path; std::string name; };
static std::vector<InputDevInfo> enumerate_event_devices_via_dev() {
    std::vector<InputDevInfo> out;
    DIR* dir = opendir("/dev/input");
    if (!dir) return out;
    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
        if (strncmp(de->d_name, "event", 5) == 0) {
            std::string path = std::string("/dev/input/") + de->d_name;
            std::string name;
            if (get_input_device_name(path, name)) {
                out.push_back({path, name});
            } else {
                out.push_back({path, std::string()});
            }
        }
    }
    closedir(dir);
    return out;
}

// 解析 /proc/bus/input/devices 获取名称与 event 映射（fallback）
static std::vector<InputDevInfo> enumerate_event_devices_via_proc() {
    std::vector<InputDevInfo> out;
    std::ifstream fin("/proc/bus/input/devices");
    if (!fin.is_open()) return out;
    std::string line;
    std::string cur_name;
    while (std::getline(fin, line)) {
        if (line.rfind("N:", 0) == 0 || line.find("Name=") != std::string::npos) {
            auto pos = line.find("Name=");
            if (pos != std::string::npos) {
                std::string val = line.substr(pos + 5);
                // 去掉引号
                if (!val.empty() && (val.front()=='\"' || val.front()=='\'')) {
                    char q = val.front();
                    if (val.back()==q && val.size()>=2) val = val.substr(1, val.size()-2);
                }
                cur_name = val;
            }
        } else if (line.rfind("H:", 0) == 0 || line.find("Handlers=") != std::string::npos) {
            // 提取 eventX
            size_t start = 0;
            while (start < line.size()) {
                while (start < line.size() && line[start] == ' ') ++start;
                size_t end = start;
                while (end < line.size() && line[end] != ' ') ++end;
                if (end > start) {
                    std::string tok = line.substr(start, end - start);
                    if (tok.rfind("event", 0) == 0) {
                        out.push_back({std::string("/dev/input/") + tok, cur_name});
                    }
                }
                start = end + 1;
            }
            cur_name.clear();
        }
    }
    return out;
}

// 综合枚举：优先 /dev/input，其次 /proc/bus/input/devices
static std::vector<InputDevInfo> enumerate_all_event_devices() {
    auto via_dev = enumerate_event_devices_via_dev();
    if (!via_dev.empty()) return via_dev;
    auto via_proc = enumerate_event_devices_via_proc();
    return via_proc;
}

// 解析设备配置：支持绝对路径、event编号以及名称/通配符（带 fallback）
static std::vector<std::string> resolve_device_config(const std::string& devices_config) {
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : devices_config) {
        if (c == '|') { if (!cur.empty()) { tokens.push_back(cur); cur.clear(); } }
        else { cur += c; }
    }
    if (!cur.empty()) tokens.push_back(cur);

    std::vector<std::string> resolved;
    auto add_unique = [&resolved](const std::string& path){ for (auto& p : resolved) if (p == path) return; resolved.push_back(path); };

    auto all_events = enumerate_all_event_devices();
    if (all_events.empty()) {
        LOGW("No /dev/input events found via both /dev and /proc; name matching may fail");
    } else {
        for (const auto& info : all_events) {
            LOGI("Enumerated input: %s -> %s", info.path.c_str(), info.name.empty()?"<unknown>":info.name.c_str());
        }
    }

    for (auto token : tokens) {
        // 去空格
        while (!token.empty() && (token.front()==' '||token.front()=='\t')) token.erase(token.begin());
        while (!token.empty() && (token.back()==' '||token.back()=='\t')) token.pop_back();
        if (token.empty()) continue;

        // 可选前缀
        if (token.rfind("name:", 0) == 0) token = token.substr(5);
        else if (token.rfind("name=", 0) == 0) token = token.substr(5);
        // 去引号
        if (token.size() >= 2 && ((token.front()=='\"' && token.back()=='\"') || (token.front()=='\'' && token.back()=='\''))) {
            token = token.substr(1, token.size()-2);
        }

        // 1) 绝对路径
        if (!token.empty() && token[0] == '/') { add_unique(token); continue; }
        // 2) event编号
        if (token.rfind("event", 0) == 0) { add_unique(std::string("/dev/input/") + token); continue; }
        // 3) 名称或通配符
        for (const auto& info : all_events) {
            const std::string& name = info.name;
            if (!name.empty()) {
                if (token.find('*') != std::string::npos) {
                    if (wildcard_match(name, token)) add_unique(info.path);
                } else {
                    if (name == token) add_unique(info.path);
                }
            }
        }
    }

    // 如果没有找到任何设备，默认返回event0
    if (resolved.empty()) {
        LOGW("未找到任何匹配的设备，默认使用 /dev/input/event0");
        resolved.push_back("/dev/input/event0");
    }
    return resolved;
}

// 执行shell脚本 - 内存优化版本
void execute_script(const std::string& script_name, const std::string& event_type) {
    // 使用静态缓冲区避免动态分配
    static char command_buffer[512];

    // 直接构建命令字符串
    snprintf(command_buffer, sizeof(command_buffer),
             "sh /storage/emulated/0/Android/data/com.kcrlfront/files/scripts/%s %s",
             script_name.c_str(), event_type.c_str());

    LOGI("Executing: %s", command_buffer);

    int result = system(command_buffer);
    if (result == -1) {
        LOGE("Failed to execute script: %s", strerror(errno));
    } else {
        LOGI("Script executed with result: %d", result);
    }
}

// 执行shell命令
std::string execute_shell_command(const std::string& command) {
    LOGI("Executing shell command: %s", command.c_str());

    std::string result;
    char buffer[1024];
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        LOGE("Failed to execute shell command: %s", strerror(errno));
        return "ERROR: Failed to execute command";
    }

    try {
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
    } catch (...) {
        pclose(pipe);
        return "ERROR: Exception while reading output";
    }

    int exit_code = pclose(pipe);
    if (exit_code == -1) {
        LOGE("Failed to close pipe: %s", strerror(errno));
        return "ERROR: Failed to close pipe";
    }

    LOGI("Shell command executed with exit code: %d", exit_code);
    return result;
}
inline void cleanup_network_service(int server_fd, int client_fd) {
    if (server_fd >= 0) close(server_fd);
    if (client_fd >= 0) close(client_fd);
    {
        std::lock_guard<std::mutex> lock(g_global_mutex);
        g_running = false;
    }
    g_shutdown_cv.notify_all();
    LOGI("Network service stopped (cleanup)");
}



void stop_key_scanning() {
    g_key_scanning = false;
    if (g_scan_fd >= 0) {
        close(g_scan_fd);
        g_scan_fd = -1;
    }
    std::lock_guard<std::mutex> lock(g_key_mutex);
    g_scan_state = -1;
    g_last_key.clear();
    g_current_device.clear();
    LOGI("Key scanning stopped");
}

void key_scan_thread(const std::string& device_path) {
    int fd = open(device_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        LOGE("Failed to open device for scanning: %s", device_path.c_str());
        std::lock_guard<std::mutex> lock(g_key_mutex);
        g_scan_state = -1;
        g_key_scanning = false;
        return;
    }

    g_scan_fd = fd;
    LOGI("Key scanning started on: %s", device_path.c_str());

    struct input_event ev;
    while (g_key_scanning && g_running) {
        ssize_t bytes = read(fd, &ev, sizeof(ev));
        if (bytes == sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1) {
                std::lock_guard<std::mutex> lock(g_key_mutex);
                g_last_key = std::to_string(ev.code);
                g_scan_state = 1;
                LOGI("Key captured: %d", ev.code);
                break;
            }
        } else if (bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            } else if (errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
    }

    close(fd);
    g_scan_fd = -1;
    if (g_key_scanning) {
        g_key_scanning = false;
    }
}

void start_key_scanning(const std::string& device_param) {
    stop_key_scanning();

    std::string device_path;
    if (device_param.empty()) {
        device_path = "/dev/input/event0";
    } else if (device_param[0] == '/') {
        device_path = device_param;
    } else if (device_param.rfind("event", 0) == 0) {
        device_path = std::string("/dev/input/") + device_param;
    } else {
        auto all_devices = enumerate_all_event_devices();
        for (const auto& info : all_devices) {
            if (info.name == device_param ||
                (device_param.find('*') != std::string::npos && wildcard_match(info.name, device_param))) {
                device_path = info.path;
                break;
            }
        }
        if (device_path.empty() && !all_devices.empty()) {
            device_path = all_devices[0].path;
        }
    }

    if (device_path.empty()) {
        device_path = "/dev/input/event0";
    }

    {
        std::lock_guard<std::mutex> lock(g_key_mutex);
        g_current_device = device_path;
        g_last_key.clear();
        g_scan_state = 0;
    }

    g_key_scanning = true;
    std::thread scan_thread(key_scan_thread, device_path);
    scan_thread.detach();
}


bool can_sent_pass = true;
char socket_passwd[8];
void network_service() {
    int server_fd = -1;
    int client_fd = -1;
    struct sockaddr_in server_addr{};
    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    char buffer[1024];

    // ---- 生成 8 位随机 socket 密码 ----
    std::string socket_passwd(8, '0');
    srand((unsigned int)time(NULL) ^ (unsigned int)std::hash<std::thread::id>{}(std::this_thread::get_id()));
    for (char &c : socket_passwd) {
        c = '0' + (rand() % 10);
    }
    LOGI("Socket password generated");

    bool password_sent = false; // 密码是否已发送

    // ---- 创建 socket ----
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(50501);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) return;
    if (listen(server_fd, 3) < 0) return;

    LOGI("Network service listening on 127.0.0.1:50501");

    while (g_running) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        struct timeval tv{5, 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        std::string command;
        while (g_running) {
            memset(buffer, 0, sizeof(buffer));
            int n = read(client_fd, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                command.append(buffer, n);
                if (command.find('\n') != std::string::npos ||
                    command.find('\r') != std::string::npos) break;
            } else break;
        }

        command.erase(command.find_last_not_of("\r\n") + 1);

        std::string response;

        if (!command.empty()) {
            LOGI("Received command: %s", command.c_str());

            if (command == "socket_passwd") {
                if (!password_sent) {
                    response = socket_passwd + "\n";
                    password_sent = true;
                    LOGI("Password sent to client");
                } else {
                    response = "ERROR: password already sent\n";
                }
            } else if (command == "testng") {
                response = "working\n";
            } else if (command == "shutdown" || command == "exit" || command == "close") {
                response = "OK: shutdown\n";
                send(client_fd, response.c_str(), response.size(), 0);
                g_running = false;
                g_shutdown_cv.notify_all();
                close(client_fd);
                client_fd = -1;
                break;
            } else if (command.length() >= 9) {
                std::string cmd_passwd = command.substr(0, 8);
                std::string cmd_body = command.substr(9);
                if (cmd_body.rfind("exec ", 0) == 0) {
                        std::string shell_cmd = cmd_body.substr(5);
                        std::string output = execute_shell_command(shell_cmd);
                        response = output;
                        if (response.empty()) {
                            response = "OK: exec (no output)\n";
                        }
                } else if (cmd_body == "shutdown" || cmd_body == "exit" || cmd_body == "close") {
                        response = "OK: shutdown\n";
                        send(client_fd, response.c_str(), response.size(), 0);
                        g_running = false;
                        g_shutdown_cv.notify_all();
                        close(client_fd);
                        client_fd = -1;
                        break;
                } else if (cmd_body.rfind("lison ", 0) == 0){
                    std::string device_param = cmd_body.substr(6);
                    start_key_scanning(device_param);
                    response = "{\"state\":0,\"keycode\":\"\",\"device\":\"" + device_param + "\"}\n";
                } else if (cmd_body == "lison_close"){
                    stop_key_scanning();
                    response = "{\"state\":-1,\"keycode\":\"\",\"device\":\"\"}\n";
                } else if (cmd_body == "lison_data"){
                    std::lock_guard<std::mutex> lock(g_key_mutex);
                    response = "{\"state\":" + std::to_string(g_scan_state) + 
                              ",\"keycode\":\"" + g_last_key + 
                              "\",\"device\":\"" + g_current_device + "\"}\n";
                } else {
                        response = "ERROR: unknown command\n";
                }
            } else {
                response = "ERROR: invalid format\n";
            }


        }

        if (!response.empty() && client_fd >= 0) {
            send(client_fd, response.c_str(), response.size(), 0);
        }

        if (client_fd >= 0) {
            close(client_fd);
            client_fd = -1;
        }
    }

    cleanup:
    if (server_fd >= 0) close(server_fd);
    if (client_fd >= 0) close(client_fd);
    g_running = false;
    g_shutdown_cv.notify_all();
    LOGI("Network service stopped");
}
// ---- 辅助函数，不使用 goto ----
inline void goto_cleanup(int server_fd, int client_fd) {
    if (server_fd >= 0) close(server_fd);
    if (client_fd >= 0) close(client_fd);
    {
        std::lock_guard<std::mutex> lock(g_global_mutex);
        g_running = false;
    }
    g_shutdown_cv.notify_all();
    LOGI("Network service stopped (cleanup)");
}

// 处理按键事件类型识别 - 内存优化版本
void handle_key_event_type(int keycode, const std::string& event_type, int duration_ms = 0) {
    // 每次事件触发前重新加载配置文件，实现实时更新
    const char* config_file = "/storage/emulated/0/Android/data/com.kcrlfront/files/kctrl.conf";
    if (!load_config(config_file)) {
        LOGE("Failed to reload config file before event handling");
        return;
    }

    // 使用静态缓冲区避免重复分配
    static char script_key_buffer[64];

    // 直接构建脚本键名，避免字符串拼接
    const char* suffix;
    if (event_type == "click") {
        suffix = "_click";
    } else if (event_type == "double_click") {
        suffix = "_double_click";
    } else if (event_type == "short_press") {
        suffix = "_short_press";
    } else if (event_type == "long_press") {
        suffix = "_long_press";
    } else {
        return; // 不支持的事件类型
    }

    snprintf(script_key_buffer, sizeof(script_key_buffer), "script_%d%s", keycode, suffix);

    auto it = g_config.find(script_key_buffer);
    if (it != g_config.end()) {
        // 移除文件日志记录，直接执行脚本
        std::thread script_thread(execute_script, it->second, event_type);
        script_thread.detach();
    }
}

// 处理双击检测定时器 - 内存优化版本
void double_click_timer(int keycode) {
    std::this_thread::sleep_for(std::chrono::milliseconds(g_double_click_interval));

    // 减少锁持有时间，先读取状态再处理
    uint8_t click_count;
    bool timer_active;

    {
        std::lock_guard<std::mutex> lock(g_global_mutex);
        auto& state = g_key_states[keycode];
        click_count = state.click_count();
        timer_active = state.timer_active();

        if (timer_active) {
            state.set_click_count(0);
            state.set_timer_active(false);
        }
    }

    // 在锁外处理事件，减少锁竞争
    if (timer_active) {
        if (click_count == 1) {
            handle_key_event_type(keycode, "click");
        } else if (click_count >= 2) {
            handle_key_event_type(keycode, "double_click");
        }
    }
}

// 处理按键释放后的事件判断定时器 - 内存优化版本
void key_release_timer(int keycode, long long duration) {
    // 直接处理事件，无需锁定状态
    if (duration >= g_long_press_threshold) {
        // 长按事件
        handle_key_event_type(keycode, "long_press", duration);
    } else if (duration > g_click_threshold) {
        // 短按事件
        handle_key_event_type(keycode, "short_press", duration);
    }
    // 点击事件在双击检测定时器中处理
}

// 监听输入设备事件
void monitor_input_device(const std::string& device_path) {
    // 尝试以非阻塞模式打开设备，避免阻塞线程
    int fd = open(device_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        int open_errno = errno;
        LOGE("Failed to open input device %s: %s (errno=%d)", device_path.c_str(), strerror(open_errno), open_errno);

        // 提供具体的错误诊断
        if (open_errno == EINVAL) {
            LOGE("建议: 检查设备路径是否正确，或尝试使用其他event设备");
        } else if (open_errno == EACCES) {
            LOGE("建议: 确保以root权限运行");
        } else if (open_errno == ENOENT) {
            LOGE("建议: 设备文件不存在，请检查路径");
        }
        return;
    }

    // 独占设备（EVIOCGRAB）逻辑：由配置 udevice/odevice 控制
    bool grabbed = false;
    int udevice = 0;
    int odevice = 0;
    {
        // 读取当前配置
        std::lock_guard<std::mutex> lock(g_global_mutex);
        auto it_u = g_config.find("udevice");
        auto it_o = g_config.find("odevice");
        if (it_u != g_config.end()) udevice = std::atoi(it_u->second.c_str());
        if (it_o != g_config.end()) odevice = std::atoi(it_o->second.c_str());
    }

    if (udevice == 1) {
        // 申请独占
        if (ioctl(fd, EVIOCGRAB, 1) == 0) {
            grabbed = true;
            LOGI("EVIOCGRAB 独占成功: %s", device_path.c_str());
        } else {
            LOGW("EVIOCGRAB 独占失败(%s)，将以非独占方式继续: %s", strerror(errno), device_path.c_str());
        }

        // 如果独占成功且未立即确认（odevice==0），则等待10秒后重新读取配置进行确认
        if (grabbed && odevice == 0) {
            LOGI("已独占设备，等待10秒以确认(在配置中设置 odevice=1 以继续，或保持0以释放并退出)");
            // 可中断的等待，总计约10秒
            for (int i = 0; i < 100 && g_running; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (g_running) {
                // 重新加载配置以读取最新的odevice
                bool reload_ok = false;
                {
                    std::lock_guard<std::mutex> lock(g_global_mutex);
                    reload_ok = load_config(g_config_path.c_str());
                }
                if (!reload_ok) {
                    LOGW("重新加载配置失败，按未确认处理(odevice=0)");
                }
                // 读取最新的odevice
                {
                    std::lock_guard<std::mutex> lock(g_global_mutex);
                    auto it_o2 = g_config.find("odevice");
                    odevice = (it_o2 != g_config.end()) ? std::atoi(it_o2->second.c_str()) : 0;
                }
                if (odevice == 0) {
                    // 未确认，释放独占并请求整体退出
                    ioctl(fd, EVIOCGRAB, 0);
                    grabbed = false;
                    LOGI("未确认独占(odevice=0)，已释放设备独占并准备退出程序");
                    {
                        std::lock_guard<std::mutex> lock(g_global_mutex);
                        g_running = false;
                    }
                    g_shutdown_cv.notify_all();
                    close(fd);
                    return;
                } else {
                    LOGI("已确认独占(odevice=1)，继续运行");
                }
            } else {
                // 程序已要求退出
                ioctl(fd, EVIOCGRAB, 0);
                grabbed = false;
                close(fd);
                return;
            }
        }
    }

    LOGI("Monitoring input device: %s", device_path.c_str());

    struct input_event ev;
    int retry_count = 0;
    const int max_retries = 3;

    while (g_running) {
        ssize_t bytes = read(fd, &ev, sizeof(ev));
        if (bytes == sizeof(ev)) {
            // 成功读取到事件，重置重试计数
            retry_count = 0;

            // 只处理按键事件
            if (ev.type == EV_KEY) {
                if (ev.value == 1) {
                    // 按键按下
                    std::lock_guard<std::mutex> lock(g_global_mutex);
                    auto& state = g_key_states[ev.code];

                    state.set_pressed(true);
                    state.press_time_ns = std::chrono::steady_clock::now().time_since_epoch().count();

                    LOGI("Key pressed: %d", ev.code);

                    // 按下时不触发任何脚本，等待释放时判断事件类型

                } else if (ev.value == 0) {
                    // 按键释放
                    std::lock_guard<std::mutex> lock(g_global_mutex);
                    auto& state = g_key_states[ev.code];

                    if (state.is_pressed()) {
                        state.set_pressed(false);
                        auto release_time_ns = std::chrono::steady_clock::now().time_since_epoch().count();
                        int duration = static_cast<int>((release_time_ns - state.press_time_ns) / 1000000); // 转换为毫秒

                        LOGI("Key released: %d (duration: %dms)", ev.code, duration);

                        // 判断事件类型
                        if (duration <= g_click_threshold) {
                            // 点击事件 - 需要检测单击/双击
                            state.set_click_count(state.click_count() + 1);
                            state.last_click_time_ns = release_time_ns;

                            if (!state.timer_active()) {
                                state.set_timer_active(true);
                                if (state.timer_thread.joinable()) {
                                    state.timer_thread.detach();
                                }
                                state.timer_thread = std::thread(double_click_timer, ev.code);
                            }
                        } else {
                            // 短按或长按事件 - 使用统一的定时器处理
                            if (state.timer_thread.joinable()) {
                                state.timer_thread.detach();
                            }
                            state.timer_thread = std::thread(key_release_timer, ev.code, duration);
                        }
                        // 按键释放时不触发keyup事件
                    }
                } else {
                    continue; // 忽略重复事件
                }
            }
        } else if (bytes == -1) {
            if (errno == EINTR) {
                continue; // 被信号中断，继续读取
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞模式下没有数据可读，短暂休眠后继续
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                retry_count = 0; // 这是正常情况，重置重试计数
                continue;
            } else if (errno == ENODEV) {
                // 设备已断开连接
                LOGE("Device disconnected: %s", device_path.c_str());
                break;
            } else {
                // 其他读取错误，记录详细错误信息
                LOGE("Error reading from input device %s: %s (errno=%d)",
                     device_path.c_str(), strerror(errno), errno);

                retry_count++;
                if (retry_count >= max_retries) {
                    LOGE("Max retries reached for device %s, stopping monitor", device_path.c_str());
                    break;
                }

                // 短暂延迟后重试
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
        } else if (bytes == 0) {
            // 读到文件末尾（设备可能已断开）
            LOGE("End of file reached for device: %s", device_path.c_str());
            break;
        } else {
            // 读取到不完整的数据
            LOGE("Incomplete read from device %s: expected %zu bytes, got %zd bytes",
                 device_path.c_str(), sizeof(ev), bytes);
            retry_count++;
            if (retry_count >= max_retries) {
                LOGE("Max incomplete read retries reached for device %s", device_path.c_str());
                break;
            }
            continue;
        }
    }

    // 退出前释放独占
    if (grabbed) {
        ioctl(fd, EVIOCGRAB, 0);
        LOGI("已释放设备独占: %s", device_path.c_str());
    }

    close(fd);
    LOGI("Stopped monitoring input device: %s", device_path.c_str());
}

// 清理函数
void cleanup() {
    g_running = false;

    // 清理按键状态和线程
    {
        std::lock_guard<std::mutex> lock(g_global_mutex);
        for (auto& pair : g_key_states) {
            auto& state = pair.second;
            state.set_pressed(false);
            state.set_timer_active(false);
            if (state.timer_thread.joinable()) {
                state.timer_thread.detach();
            }
        }
        g_key_states.clear();
    }

    // 恢复系统资源设置
    munlockall(); // 解锁内存，允许使用swap

    // 恢复CPU亲和性到所有核心
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    for (int i = 0; i < sysconf(_SC_NPROCESSORS_ONLN); i++) {
        CPU_SET(i, &cpu_set);
    }
    sched_setaffinity(0, sizeof(cpu_set), &cpu_set);

    release_wakelock();
    unlink("/storage/emulated/0/Android/data/com.kcrlfront/files/mpid.txt");
    LOGI("Cleanup completed with system resources restored");
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    ignore_sigpipe();
    LOGI("KCTRL v2.4 starting...");
    LOGI("Author: IDlike");
    LOGI("Description: 适用于Android15+的按键控制模块");



    // 系统资源优化设置
    // 1. 降低CPU优先级（nice值越高优先级越低，范围-20到19）
    if (setpriority(PRIO_PROCESS, 0, 10) == 0) {
        LOGI("CPU priority lowered to nice=10");
    } else {
        LOGW("Failed to lower CPU priority");
    }

    // 2. CPU亲和性设置将在加载配置文件后进行

    // 3. 设置内存策略优先使用swap
    // 确保内存不被锁定，允许系统将进程内存交换到swap
    munlockall();

    // 建议内核优先回收此进程的内存页面
    if (madvise(nullptr, 0, MADV_DONTNEED) == 0) {
        LOGI("Memory policy set to prefer swap usage");
    } else {
        LOGI("Memory unlocked, will use swap when needed");
    }

    // 4. 极致内存优化设置
    // 预分配容器以最小容量减少内存碎片
    // g_config.reserve(8);  // 最小配置项数量 - 移除限制
    // g_key_states.reserve(4);  // 最多监听4个按键 - 移除限制

    // 设置内存映射建议，优先回收不活跃页面
    if (madvise(nullptr, 0, MADV_SEQUENTIAL) == 0) {
        LOGI("Memory access pattern optimized for sequential access");
    }

    // 检查单实例运行
    if (check_single_instance()) {
        LOGE("Another instance is already running");
        LOGU("kctrl服务已经有一个实例了。");
        return 1;
    }

    // 获取WakeLock
    if (!acquire_wakelock()) {
        LOGE("Failed to acquire wake lock");
        LOGU("kctrl服务启动失败：唤醒锁");
        cleanup();
        return 1;
    }

    // 加载配置文件
    const char* config_file = (argc > 1) ? argv[1] : "/storage/emulated/0/Android/data/com.kcrlfront/files/kctrl.conf";
    // 记录配置文件路径，供线程内二次确认独占时重新读取
    g_config_path = config_file;
    if (!load_config(config_file)) {
        LOGE("Failed to load config file");
        LOGU("kctrl服务启动失败：配置错误 [无法打开]");
        cleanup();
        return 1;
    }

    // 设置CPU亲和性（在加载配置文件后）
    std::string cpu_affinity_config = "0"; // 默认使用CPU0
    auto cpu_it = g_config.find("cpu_affinity");
    if (cpu_it != g_config.end()) {
        cpu_affinity_config = cpu_it->second;
        LOGI("Found CPU affinity config: %s", cpu_affinity_config.c_str());
    } else {
        LOGI("No CPU affinity config found, using default: CPU0");
    }

    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);

    // 解析CPU亲和性配置
    std::string cpu_str = cpu_affinity_config;
    size_t pos = 0;
    bool has_valid_cpu = false;

    while (pos < cpu_str.length()) {
        size_t comma_pos = cpu_str.find(',', pos);
        std::string cpu_num_str = cpu_str.substr(pos, comma_pos == std::string::npos ? std::string::npos : comma_pos - pos);

        // 去除空格
        cpu_num_str.erase(0, cpu_num_str.find_first_not_of(" \t"));
        cpu_num_str.erase(cpu_num_str.find_last_not_of(" \t") + 1);

        if (!cpu_num_str.empty()) {
            int cpu_num = std::atoi(cpu_num_str.c_str());
            if (cpu_num >= 0 && cpu_num < CPU_SETSIZE) {
                CPU_SET(cpu_num, &cpu_set);
                has_valid_cpu = true;
                LOGI("Added CPU %d to affinity set", cpu_num);
            } else {
                LOGW("Invalid CPU number: %d", cpu_num);
            }
        }

        if (comma_pos == std::string::npos) break;
        pos = comma_pos + 1;
    }

    // 如果没有有效的CPU配置，默认使用CPU0
    if (!has_valid_cpu) {
        CPU_SET(0, &cpu_set);
        LOGI("No valid CPU affinity config found, using default CPU 0");
    }

    if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) == 0) {
        LOGI("CPU affinity set successfully");
    } else {
        LOGW("Failed to set CPU affinity");
    }

    // 获取要监听的设备路径
    auto device_it = g_config.find("device");
    if (device_it == g_config.end()) {
        LOGE("No device specified in config file");
        LOGU("kctrl服务启动失败：配置错误 [路径配置为空]");
        cleanup();
        return 1;
    }

    std::string devices_config = device_it->second;
    LOGI("Device config: %s", devices_config.c_str());

    // 新增：支持通过设备名称/通配符解析为实际路径
    std::vector<std::string> device_paths = resolve_device_config(devices_config);

    if (device_paths.empty()) {
        LOGE("No valid device paths found");
        LOGU("kctrl服务启动失败：配置错误 [路径配置无效]");
        cleanup();
        return 1;
    }

    LOGI("Found %zu device(s) to monitor", device_paths.size());
    for (const auto& path : device_paths) {
        LOGI("Target device: %s", path.c_str());
    }

    // 注册退出时的清理


    // 为每个设备启动独立的监听线程
    std::vector<std::thread> monitor_threads;
    monitor_threads.reserve(device_paths.size());

    for (const auto& device_path : device_paths) {
        monitor_threads.emplace_back(monitor_input_device, device_path);
    }

    // 启动网络监听服务线程
    std::thread network_thread(network_service);
    printf("请直接关闭本窗口而不要使用Ctrl+C！\n");
    // 主循环 - 使用条件变量优化响应性和定期内存回收
    {
        std::unique_lock<std::mutex> lock(g_global_mutex);
        while (g_running) {
            // 等待5分钟或直到程序退出
            if (g_shutdown_cv.wait_for(lock, std::chrono::minutes(5), []{ return !g_running; })) {
                break; // 程序退出
            }

            // 定期内存优化（每5分钟执行一次）
            if (g_running) {
                // 建议内核回收不活跃的内存页面
                madvise(nullptr, 0, MADV_DONTNEED);

                // 清理可能的内存碎片
                g_config.rehash(0);
                g_key_states.rehash(0);

                LOGI("Periodic memory optimization completed");
            }
        }
    }

    // 等待所有监听线程结束
    for (auto& thread : monitor_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    // 等待网络线程结束
    if (network_thread.joinable()) {
        network_thread.join();
    }

    cleanup();
    LOGI("KCTRL stopped");
    return 0;
}