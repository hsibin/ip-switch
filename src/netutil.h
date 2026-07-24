#ifndef NETUTIL_H
#define NETUTIL_H

#include <windows.h>
#include <iphlpapi.h>
#include <stdio.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#define MAX_ADAPTER_NAME 256
#define MAX_PROFILE_NAME 64
#define MAX_IP_STR 16

typedef struct {
    char profileName[MAX_PROFILE_NAME];
    char ipAddr[MAX_IP_STR];
    char subnetMask[MAX_IP_STR];
    char gateway[MAX_IP_STR];
    char dnsPrimary[MAX_IP_STR];
    char dnsSecondary[MAX_IP_STR];
    BOOL isDhcp;
} IPProfile;

// 枚举网络适配器
int EnumNetworkAdapters(HWND hCombo);

// 获取适配器GUID
BOOL GetAdapterGuid(int adapterIndex, char* guidBuf, int bufSize);

// 设置静态IP地址
BOOL SetStaticIP(const char* adapterGuid, const char* ip, const char* mask, const char* gateway);

// 设置DNS服务器
BOOL SetDNS(const char* adapterGuid, const char* primaryDns, const char* secondaryDns);

// 启用DHCP
BOOL EnableDHCP(const char* adapterGuid);

// 保存配置到INI文件
BOOL SaveProfile(const char* filePath, IPProfile* profile);

// 从INI加载所有配置名
int LoadProfileNames(const char* filePath, HWND hCombo);

// 加载单个配置
BOOL LoadProfile(const char* filePath, const char* profileName, IPProfile* profile);

// 删除配置
BOOL DeleteProfile(const char* filePath, const char* profileName);

// 检查是否管理员权限
BOOL IsAdmin();

// 提升为管理员权限
void RunAsAdmin();

#endif
