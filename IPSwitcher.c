// IP 切换器 - 纯 Win32 API 实现
// 编译: cl /O2 /MT /Fe:IPSwitcher.exe IPSwitcher.c resource.res user32.lib comctl32.lib shell32.lib ole32.lib advapi32.lib
//       或 MinGW: gcc -O2 -mwindows -o IPSwitcher.exe IPSwitcher.c resource.res -lcomctl32 -lshell32 -lole32

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 控件 ID ====================
#define IDC_ADAPTER       101
#define IDC_BTN_REFRESH   102
#define IDC_IP            103
#define IDC_MASK          104
#define IDC_GATEWAY       105
#define IDC_DNS1          106
#define IDC_DNS2          107
#define IDC_BTN_APPLY     108
#define IDC_BTN_DHCP      109
#define IDC_PRESET_NAME   110
#define IDC_BTN_SAVE      111
#define IDC_PRESET_LIST   112
#define IDC_BTN_LOAD      113
#define IDC_BTN_DELETE    114
#define IDC_STATUS        115

// ==================== 全局变量 ====================
static HINSTANCE g_hInst;
static HWND g_hAdapter, g_hIP, g_hMask, g_hGateway, g_hDns1, g_hDns2;
static HWND g_hPresetName, g_hPresetList, g_hStatus;
static HWND g_hWnd;
static char g_szConfigPath[MAX_PATH];

// ==================== 简单 JSON 解析/序列化 ====================
#define MAX_PRESETS 50
#define MAX_NAME    64
#define MAX_IPSTR   16

typedef struct {
    char szName[MAX_NAME];
    char szAdapter[256];
    char szIP[MAX_IPSTR];
    char szMask[MAX_IPSTR];
    char szGateway[MAX_IPSTR];
    char szDns1[MAX_IPSTR];
    char szDns2[MAX_IPSTR];
} IPPreset;

static IPPreset g_Presets[MAX_PRESETS];
static int g_nPresetCount = 0;

// 获取配置存储路径
static void GetConfigPath(void) {
    char szAppData[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, szAppData) == S_OK) {
        snprintf(g_szConfigPath, MAX_PATH, "%s\\IPSwitcher\\configs.json", szAppData);
    } else {
        strcpy(g_szConfigPath, "configs.json");
    }
}

// 创建目录（递归）
static void MkDirRecursive(const char* path) {
    char tmp[MAX_PATH];
    char* p = NULL;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            *p = '\0';
            CreateDirectoryA(tmp, NULL);
            *p = '\\';
        }
    }
}

// 极简 JSON 转义
static void JsonEscape(const char* src, char* dst, int dstSize) {
    int j = 0;
    for (int i = 0; src[i] && j < dstSize - 2; i++) {
        switch (src[i]) {
            case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
            case '"':  dst[j++] = '\\'; dst[j++] = '"'; break;
            case '\n': dst[j++] = '\\'; dst[j++] = 'n'; break;
            case '\r': dst[j++] = '\\'; dst[j++] = 'r'; break;
            default:   dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

// 从 JSON 字符串提取值 (key:"value")
static void ExtractJsonValue(const char* json, const char* key, char* value, int valueSize) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* pos = strstr(json, pattern);
    if (!pos) { value[0] = '\0'; return; }
    pos = strchr(pos, ':');
    if (!pos) { value[0] = '\0'; return; }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') pos++;
    if (*pos != '"') { value[0] = '\0'; return; }
    pos++;
    int i = 0;
    while (*pos && *pos != '"' && i < valueSize - 1) {
        if (*pos == '\\' && *(pos + 1)) pos++;
        value[i++] = *pos++;
    }
    value[i] = '\0';
}

// 保存配置到文件
static void SavePresets(void) {
    char szDir[MAX_PATH];
    snprintf(szDir, MAX_PATH, "%s", g_szConfigPath);
    char* pLast = strrchr(szDir, '\\');
    if (pLast) { *pLast = '\0'; MkDirRecursive(szDir); }

    FILE* f = fopen(g_szConfigPath, "w");
    if (!f) return;

    fputs("[\n", f);
    char esc[512];
    for (int i = 0; i < g_nPresetCount; i++) {
        IPPreset* p = &g_Presets[i];
        fprintf(f, "  {\n");

        JsonEscape(p->szName, esc, sizeof(esc));
        fprintf(f, "    \"Name\": \"%s\",\n", esc);

        JsonEscape(p->szAdapter, esc, sizeof(esc));
        fprintf(f, "    \"Adapter\": \"%s\",\n", esc);

        JsonEscape(p->szIP, esc, sizeof(esc));
        fprintf(f, "    \"IP\": \"%s\",\n", esc);

        JsonEscape(p->szMask, esc, sizeof(esc));
        fprintf(f, "    \"Mask\": \"%s\",\n", esc);

        JsonEscape(p->szGateway, esc, sizeof(esc));
        fprintf(f, "    \"Gateway\": \"%s\",\n", esc);

        JsonEscape(p->szDns1, esc, sizeof(esc));
        fprintf(f, "    \"Dns1\": \"%s\",\n", esc);

        JsonEscape(p->szDns2, esc, sizeof(esc));
        fprintf(f, "    \"Dns2\": \"%s\"\n", esc);

        fprintf(f, i < g_nPresetCount - 1 ? "  },\n" : "  }\n");
    }
    fputs("]\n", f);
    fclose(f);
}

// 加载配置
static void LoadPresets(void) {
    g_nPresetCount = 0;
    FILE* f = fopen(g_szConfigPath, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 1024 * 512) { fclose(f); return; }

    char* buf = (char*)malloc(size + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    // 解析对象
    char* p = buf;
    while (*p && g_nPresetCount < MAX_PRESETS) {
        p = strchr(p, '{');
        if (!p) break;
        char* end = strchr(p, '}');
        if (!end) break;
        *end = '\0';

        IPPreset* preset = &g_Presets[g_nPresetCount];
        memset(preset, 0, sizeof(IPPreset));
        ExtractJsonValue(p, "Name", preset->szName, MAX_NAME);
        ExtractJsonValue(p, "Adapter", preset->szAdapter, 256);
        ExtractJsonValue(p, "IP", preset->szIP, MAX_IPSTR);
        ExtractJsonValue(p, "Mask", preset->szMask, MAX_IPSTR);
        ExtractJsonValue(p, "Gateway", preset->szGateway, MAX_IPSTR);
        ExtractJsonValue(p, "Dns1", preset->szDns1, MAX_IPSTR);
        ExtractJsonValue(p, "Dns2", preset->szDns2, MAX_IPSTR);

        if (preset->szName[0]) g_nPresetCount++;
        *end = '}';
        p = end + 1;
    }
    free(buf);
}

// 刷新预设下拉列表
static void RefreshPresetList(void) {
    SendMessage(g_hPresetList, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_nPresetCount; i++) {
        SendMessageA(g_hPresetList, CB_ADDSTRING, 0, (LPARAM)g_Presets[i].szName);
    }
}

// ==================== netsh 操作 ====================
// 运行 netsh 命令，返回输出
static int RunNetsh(const wchar_t* args, char* output, int outputSize) {
    wchar_t cmd[2048];
    swprintf(cmd, 2048, L"netsh %ls", args);

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return -1;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi = { 0 };
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(hWrite);

    if (!ok) { CloseHandle(hRead); return -1; }

    WaitForSingleObject(pi.hProcess, 15000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    DWORD read = 0;
    if (output && outputSize > 0) {
        memset(output, 0, outputSize);
        ReadFile(hRead, output, outputSize - 1, &read, NULL);
    } else {
        char tmp[256];
        ReadFile(hRead, tmp, sizeof(tmp) - 1, &read, NULL);
    }
    CloseHandle(hRead);
    return 0;
}

// 设置状态栏
static void SetStatus(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    vswprintf(buf, 1024, fmt, args);
    va_end(args);
    SetWindowTextW(g_hStatus, buf);
}

// 判断是否有效 IP
static int IsValidIP(const char* ip) {
    if (!ip || !*ip) return 0;
    int a, b, c, d;
    if (sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return 0;
    return (a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
            c >= 0 && c <= 255 && d >= 0 && d <= 255);
}

// 获取选中的网卡名称
static void GetSelectedAdapter(char* buf, int size) {
    int sel = (int)SendMessage(g_hAdapter, CB_GETCURSEL, 0, 0);
    if (sel >= 0) {
        SendMessageA(g_hAdapter, CB_GETLBTEXT, sel, (LPARAM)buf);
    } else {
        buf[0] = '\0';
    }
}

// 获取控件文本
static void GetEditTextA(HWND hwnd, char* buf, int size) {
    GetWindowTextA(hwnd, buf, size);
}

// 设置控件文本
static void SetEditTextA(HWND hwnd, const char* text) {
    SetWindowTextA(hwnd, text);
}

// ==================== 刷新网卡列表 ====================
static void RefreshAdapters(void) {
    SendMessage(g_hAdapter, CB_RESETCONTENT, 0, 0);

    char output[32768];
    if (RunNetsh(L"interface ip show config", output, sizeof(output)) != 0) {
        SetStatus(L"获取网卡列表失败");
        return;
    }

    // 多字节解析 (中文 Windows 输出可能是 GBK/GB2312)
    char* p = output;
    int count = 0;
    while (*p) {
        // 查找 "接口 " 或 "Configuration for interface "
        char* start = NULL;
        char* q1 = strstr(p, "\xB5\xC0\"");
        char* q2 = strstr(p, "interface \"");
        char* q3 = strstr(p, "face \"");

        if (q3) start = q3 + 5; else if (q2) start = q2 + 10; else if (q1) start = q1 + 2;
        else {
            // 直接查找引号行
            p = strchr(p, '\n');
            if (!p) break;
            p++;
            while (*p == '\r' || *p == ' ') p++;
            if (*p == '"') {
                p++;
                char name[256];
                int i = 0;
                while (*p && *p != '"' && i < 255) name[i++] = *p++;
                name[i] = '\0';
                if (i > 0) {
                    SendMessageA(g_hAdapter, CB_ADDSTRING, 0, (LPARAM)name);
                    count++;
                }
            }
            continue;
        }

        char name[256];
        int i = 0;
        while (*start && *start != '"' && i < 255) name[i++] = *start++;
        name[i] = '\0';
        if (i > 0) {
            SendMessageA(g_hAdapter, CB_ADDSTRING, 0, (LPARAM)name);
            count++;
        }
        p = (*start) ? start + 1 : start;
    }

    if (count > 0) {
        SendMessage(g_hAdapter, CB_SETCURSEL, 0, 0);
        wchar_t msg[256];
        swprintf(msg, 256, L"已找到 %d 个网络适配器", count);
        SetWindowTextW(g_hStatus, msg);
    } else {
        SetStatus(L"未找到网络适配器");
    }
}

// ==================== 应用配置 ====================
static void ApplyConfig(void) {
    char adapter[256];
    GetSelectedAdapter(adapter, sizeof(adapter));
    if (!adapter[0]) { SetStatus(L"请先选择网络适配器"); return; }

    char ip[MAX_IPSTR], mask[MAX_IPSTR], gw[MAX_IPSTR], dns1[MAX_IPSTR], dns2[MAX_IPSTR];
    GetEditTextA(g_hIP, ip, MAX_IPSTR);
    GetEditTextA(g_hMask, mask, MAX_IPSTR);
    GetEditTextA(g_hGateway, gw, MAX_IPSTR);
    GetEditTextA(g_hDns1, dns1, MAX_IPSTR);
    GetEditTextA(g_hDns2, dns2, MAX_IPSTR);

    // 去除空格
    for (char* s = ip; *s; s++) if (*s == ' ') *s = '\0';
    for (char* s = mask; *s; s++) if (*s == ' ') *s = '\0';
    for (char* s = gw; *s; s++) if (*s == ' ') *s = '\0';
    for (char* s = dns1; *s; s++) if (*s == ' ') *s = '\0';
    for (char* s = dns2; *s; s++) if (*s == ' ') *s = '\0';

    if (!IsValidIP(ip))      { SetStatus(L"请输入有效的 IP 地址"); return; }
    if (!IsValidIP(mask))    { SetStatus(L"请输入有效的子网掩码"); return; }
    if (gw[0] && !IsValidIP(gw))  { SetStatus(L"请输入有效的默认网关"); return; }
    if (!IsValidIP(dns1))    { SetStatus(L"请输入有效的首选 DNS"); return; }

    SetStatus(L"正在设置 IP 地址...");

    // netsh interface ip set address name="xxx" source=static addr=x.x.x.x mask=x.x.x.x [gateway=x.x.x.x]
    wchar_t wAdapter[256];
    MultiByteToWideChar(CP_UTF8, 0, adapter, -1, wAdapter, 256);

    wchar_t args[1024];
    swprintf(args, 1024, L"interface ip set address name=\"%ls\" source=static addr=%hs mask=%hs",
        wAdapter, ip, mask);
    if (gw[0]) {
        wchar_t tmp[1024];
        swprintf(tmp, 1024, L"%ls gateway=%hs", args, gw);
        wcscpy(args, tmp);
    }
    RunNetsh(args, NULL, 0);

    SetStatus(L"正在设置 DNS...");
    // 先清理旧 DNS
    wchar_t args2[1024];
    swprintf(args2, 1024, L"interface ip set dns name=\"%ls\" source=static addr=%hs register=primary",
        wAdapter, dns1);
    RunNetsh(args2, NULL, 0);

    if (dns2[0] && IsValidIP(dns2)) {
        swprintf(args2, 1024, L"interface ip add dns name=\"%ls\" addr=%hs index=2",
            wAdapter, dns2);
        RunNetsh(args2, NULL, 0);
    }

    wchar_t msg[512];
    swprintf(msg, 512, L"配置已应用 - IP: %hs, DNS: %hs", ip, dns1);
    SetStatus(L"%ls", msg);
}

// ==================== DHCP 切换 ====================
static void SwitchDHCP(void) {
    char adapter[256];
    GetSelectedAdapter(adapter, sizeof(adapter));
    if (!adapter[0]) { SetStatus(L"请先选择网络适配器"); return; }

    wchar_t wAdapter[256];
    MultiByteToWideChar(CP_UTF8, 0, adapter, -1, wAdapter, 256);

    SetStatus(L"正在切换为 DHCP...");

    wchar_t args[1024];
    swprintf(args, 1024, L"interface ip set address name=\"%ls\" source=dhcp", wAdapter);
    RunNetsh(args, NULL, 0);

    swprintf(args, 1024, L"interface ip set dns name=\"%ls\" source=dhcp", wAdapter);
    RunNetsh(args, NULL, 0);

    SetEditTextA(g_hIP, "");
    SetEditTextA(g_hMask, "");
    SetEditTextA(g_hGateway, "");
    SetEditTextA(g_hDns1, "");
    SetEditTextA(g_hDns2, "");

    SetStatus(L"已切换为 DHCP 自动获取");
}

// ==================== 预设操作 ====================
static void SavePreset(void) {
    char name[MAX_NAME];
    GetEditTextA(g_hPresetName, name, MAX_NAME);
    if (!name[0]) { SetStatus(L"请输入配置名"); return; }

    char ip[MAX_IPSTR], mask[MAX_IPSTR], gw[MAX_IPSTR], dns1[MAX_IPSTR], dns2[MAX_IPSTR];
    char adapter[256];
    GetSelectedAdapter(adapter, sizeof(adapter));
    GetEditTextA(g_hIP, ip, MAX_IPSTR);
    GetEditTextA(g_hMask, mask, MAX_IPSTR);
    GetEditTextA(g_hGateway, gw, MAX_IPSTR);
    GetEditTextA(g_hDns1, dns1, MAX_IPSTR);
    GetEditTextA(g_hDns2, dns2, MAX_IPSTR);

    // 查重
    int idx = -1;
    for (int i = 0; i < g_nPresetCount; i++) {
        if (strcmp(g_Presets[i].szName, name) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        if (g_nPresetCount >= MAX_PRESETS) { SetStatus(L"预设数量已达上限"); return; }
        idx = g_nPresetCount++;
    }

    IPPreset* p = &g_Presets[idx];
    strncpy(p->szName, name, MAX_NAME - 1);
    strncpy(p->szAdapter, adapter, 255);
    strncpy(p->szIP, ip, MAX_IPSTR - 1);
    strncpy(p->szMask, mask, MAX_IPSTR - 1);
    strncpy(p->szGateway, gw, MAX_IPSTR - 1);
    strncpy(p->szDns1, dns1, MAX_IPSTR - 1);
    strncpy(p->szDns2, dns2, MAX_IPSTR - 1);

    SavePresets();
    RefreshPresetList();

    // 选中刚保存的
    int newIdx = (int)SendMessageA(g_hPresetList, CB_FINDSTRINGEXACT, -1, (LPARAM)name);
    if (newIdx >= 0) SendMessage(g_hPresetList, CB_SETCURSEL, newIdx, 0);

    wchar_t msg[256];
    swprintf(msg, 256, L"预设 \"%hs\" 已保存", name);
    SetStatus(L"%ls", msg);
}

static void LoadPresetToUI(int idx) {
    if (idx < 0 || idx >= g_nPresetCount) return;
    IPPreset* p = &g_Presets[idx];

    // 设置网卡
    if (p->szAdapter[0]) {
        int aIdx = (int)SendMessageA(g_hAdapter, CB_FINDSTRINGEXACT, -1, (LPARAM)p->szAdapter);
        if (aIdx >= 0) SendMessage(g_hAdapter, CB_SETCURSEL, aIdx, 0);
    }
    SetEditTextA(g_hIP, p->szIP);
    SetEditTextA(g_hMask, p->szMask);
    SetEditTextA(g_hGateway, p->szGateway);
    SetEditTextA(g_hDns1, p->szDns1);
    SetEditTextA(g_hDns2, p->szDns2);
}

static void OnPresetSelChange(void) {
    int idx = (int)SendMessage(g_hPresetList, CB_GETCURSEL, 0, 0);
    if (idx >= 0) LoadPresetToUI(idx);
}

static void LoadPreset(void) {
    int idx = (int)SendMessage(g_hPresetList, CB_GETCURSEL, 0, 0);
    if (idx < 0) { SetStatus(L"请先选择一个预设配置"); return; }
    LoadPresetToUI(idx);
    wchar_t msg[256];
    swprintf(msg, 256, L"已加载预设 \"%hs\"，点击「应用配置」生效", g_Presets[idx].szName);
    SetStatus(L"%ls", msg);
}

static void DeletePreset(void) {
    int idx = (int)SendMessage(g_hPresetList, CB_GETCURSEL, 0, 0);
    if (idx < 0) { SetStatus(L"请先选择一个预设配置"); return; }

    wchar_t msg[256];
    swprintf(msg, 256, L"预设 \"%hs\" 已删除", g_Presets[idx].szName);

    // 移动后续项
    if (idx < g_nPresetCount - 1) {
        memmove(&g_Presets[idx], &g_Presets[idx + 1],
            (g_nPresetCount - idx - 1) * sizeof(IPPreset));
    }
    g_nPresetCount--;
    SavePresets();
    RefreshPresetList();
    SetStatus(L"%ls", msg);
}

// EnumChildWindows 回调：设置字体
static BOOL CALLBACK SetChildFontProc(HWND hwndChild, LPARAM lParam) {
    SendMessageW(hwndChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

// ==================== 窗口消息处理 ====================
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        switch (id) {
        case IDC_BTN_REFRESH:  RefreshAdapters(); break;
        case IDC_BTN_APPLY:    ApplyConfig(); break;
        case IDC_BTN_DHCP:     SwitchDHCP(); break;
        case IDC_BTN_SAVE:     SavePreset(); break;
        case IDC_BTN_LOAD:     LoadPreset(); break;
        case IDC_BTN_DELETE:   DeletePreset(); break;
        case IDC_PRESET_LIST:
            if (code == CBN_SELCHANGE) OnPresetSelChange();
            break;
        }
        break;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hWnd, &rc);
        if (g_hStatus) {
            SendMessage(g_hStatus, WM_SIZE, 0, 0);
        }
        break;
    }
    case WM_CREATE: {
        g_hWnd = hWnd;
        HINSTANCE hInst = g_hInst;
        int lw = 78, fx = 90;
        int y = 10;

        // 使用 CreateWindowEx 创建控件
        // 网卡选择
        CreateWindowW(L"STATIC", L"网络适配器:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            8, y + 3, lw, 20, hWnd, NULL, hInst, NULL);

        g_hAdapter = CreateWindowW(L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            fx, y, 260, 200, hWnd, (HMENU)IDC_ADAPTER, hInst, NULL);

        CreateWindowW(L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            fx + 268, y - 1, 55, 23, hWnd, (HMENU)IDC_BTN_REFRESH, hInst, NULL);

        y += 35;

        // IP 设置 GroupBox
        CreateWindowW(L"BUTTON", L"IP 设置",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            6, y, 496, 190, hWnd, NULL, hInst, NULL);

        // IP地址
        CreateWindowW(L"STATIC", L"IP 地址:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            18, y + 22, lw - 6, 18, hWnd, NULL, hInst, NULL);
        g_hIP = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER,
            fx, y + 20, 155, 22, hWnd, (HMENU)IDC_IP, hInst, NULL);

        // 子网掩码
        CreateWindowW(L"STATIC", L"子网掩码:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            18, y + 48, lw - 6, 18, hWnd, NULL, hInst, NULL);
        g_hMask = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"255.255.255.0",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER,
            fx, y + 46, 155, 22, hWnd, (HMENU)IDC_MASK, hInst, NULL);

        // 默认网关
        CreateWindowW(L"STATIC", L"默认网关:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            18, y + 74, lw - 6, 18, hWnd, NULL, hInst, NULL);
        g_hGateway = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER,
            fx, y + 72, 155, 22, hWnd, (HMENU)IDC_GATEWAY, hInst, NULL);

        // 首选DNS
        CreateWindowW(L"STATIC", L"首选 DNS:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            18, y + 100, lw - 6, 18, hWnd, NULL, hInst, NULL);
        g_hDns1 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER,
            fx, y + 98, 155, 22, hWnd, (HMENU)IDC_DNS1, hInst, NULL);

        // 备用DNS
        CreateWindowW(L"STATIC", L"备用 DNS:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            18, y + 126, lw - 6, 18, hWnd, NULL, hInst, NULL);
        g_hDns2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER,
            fx, y + 124, 155, 22, hWnd, (HMENU)IDC_DNS2, hInst, NULL);

        y += 198;

        // 操作按钮
        CreateWindowW(L"BUTTON", L"应用配置", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            fx, y, 100, 30, hWnd, (HMENU)IDC_BTN_APPLY, hInst, NULL);
        CreateWindowW(L"BUTTON", L"设为 DHCP", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            fx + 108, y, 100, 30, hWnd, (HMENU)IDC_BTN_DHCP, hInst, NULL);

        y += 40;

        // 预设配置 GroupBox
        CreateWindowW(L"BUTTON", L"预设配置",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            6, y, 496, 168, hWnd, NULL, hInst, NULL);

        // 配置名
        CreateWindowW(L"STATIC", L"配置名:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            18, y + 22, lw - 6, 18, hWnd, NULL, hInst, NULL);
        g_hPresetName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            fx, y + 20, 150, 22, hWnd, (HMENU)IDC_PRESET_NAME, hInst, NULL);
        CreateWindowW(L"BUTTON", L"保存当前配置", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            fx + 158, y + 19, 108, 23, hWnd, (HMENU)IDC_BTN_SAVE, hInst, NULL);

        // 预设列表
        CreateWindowW(L"STATIC", L"预设列表:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            18, y + 52, lw - 6, 18, hWnd, NULL, hInst, NULL);
        g_hPresetList = CreateWindowW(L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            fx, y + 50, 200, 200, hWnd, (HMENU)IDC_PRESET_LIST, hInst, NULL);

        CreateWindowW(L"BUTTON", L"加载选中", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            fx, y + 80, 80, 23, hWnd, (HMENU)IDC_BTN_LOAD, hInst, NULL);
        CreateWindowW(L"BUTTON", L"删除选中", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            fx + 88, y + 80, 80, 23, hWnd, (HMENU)IDC_BTN_DELETE, hInst, NULL);

        y += 175;

        // 状态栏
        g_hStatus = CreateWindowW(STATUSCLASSNAMEW, L"就绪",
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hWnd, (HMENU)IDC_STATUS, hInst, NULL);

        // 设置字体
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        EnumChildWindows(hWnd, SetChildFontProc, (LPARAM)hFont);

        // 初始化
        GetConfigPath();
        LoadPresets();
        RefreshPresetList();
        RefreshAdapters();

        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ==================== WinMain ====================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInst = hInstance;

    // 初始化通用控件
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    // 注册窗口类
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"IPSwitcherWnd";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);

    // 创建窗口
    int w = 530, h = 530;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    HWND hWnd = CreateWindowExW(
        0, L"IPSwitcherWnd", L"IP切换器",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, w, h, NULL, NULL, hInstance, NULL);

    if (!hWnd) return 1;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // 消息循环
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}