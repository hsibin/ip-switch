#include "netutil.h"
#include <objbase.h>
#include <wbemcli.h>
#include <shlobj.h>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "shell32.lib")

// DHCP刷新函数声明
DWORD WINAPI DhcpNotifyConfigChange(
    LPWSTR  ServerName,
    LPWSTR  AdapterName,
    BOOL    IsNewIpAddress,
    DWORD   IpIndex,
    IPAddr  IpAddress,
    DWORD   Lease
);

static PIP_ADAPTER_INFO pAdapterList = NULL;
static int adapterCount = 0;

// 初始化适配器列表
static int InitAdapterList() {
    ULONG bufLen = 0;
    DWORD ret = GetAdaptersInfo(NULL, &bufLen);
    if (ret != ERROR_BUFFER_OVERFLOW) return 0;
    
    pAdapterList = (PIP_ADAPTER_INFO)malloc(bufLen);
    if (!pAdapterList) return 0;
    
    ret = GetAdaptersInfo(pAdapterList, &bufLen);
    if (ret != NO_ERROR) {
        free(pAdapterList);
        pAdapterList = NULL;
        return 0;
    }
    
    adapterCount = 0;
    PIP_ADAPTER_INFO p = pAdapterList;
    while (p) {
        adapterCount++;
        p = p->Next;
    }
    return adapterCount;
}

int EnumNetworkAdapters(HWND hCombo) {
    if (!pAdapterList) InitAdapterList();
    if (!pAdapterList) return 0;
    
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    PIP_ADAPTER_INFO p = pAdapterList;
    int idx = 0;
    while (p) {
        char displayName[MAX_ADAPTER_NAME];
        sprintf(displayName, "%s", p->Description);
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)displayName);
        p = p->Next;
        idx++;
    }
    if (idx > 0) SendMessage(hCombo, CB_SETCURSEL, 0, 0);
    return idx;
}

BOOL GetAdapterGuid(int adapterIndex, char* guidBuf, int bufSize) {
    if (!pAdapterList) InitAdapterList();
    if (!pAdapterList || adapterIndex >= adapterCount) return FALSE;
    
    PIP_ADAPTER_INFO p = pAdapterList;
    for (int i = 0; i < adapterIndex; i++) p = p->Next;
    
    strncpy(guidBuf, p->AdapterName, bufSize-1);
    return TRUE;
}

BOOL SetStaticIP(const char* adapterGuid, const char* ip, const char* mask, const char* gateway) {
    HRESULT hr;
    IWbemLocator* pLoc = NULL;
    IWbemServices* pSvc = NULL;
    
    hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr)) return FALSE;
    
    hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    if (FAILED(hr)) { CoUninitialize(); return FALSE; }
    
    hr = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hr)) { CoUninitialize(); return FALSE; }
    
    BSTR ns = SysAllocString(L"ROOT\\CIMV2");
    hr = pLoc->lpVtbl->ConnectServer(pLoc, ns, NULL, NULL, 0, NULL, NULL, NULL, &pSvc);
    SysFreeString(ns);
    pLoc->lpVtbl->Release(pLoc);
    
    if (FAILED(hr)) { CoUninitialize(); return FALSE; }
    CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    
    wchar_t wql[512];
    wsprintfW(wql, L"SELECT * FROM Win32_NetworkAdapterConfiguration WHERE SettingID='%S'", adapterGuid);
    BSTR query = SysAllocString(wql);
    BSTR lang = SysAllocString(L"WQL");
    
    IEnumWbemClassObject* pEnum = NULL;
    pSvc->lpVtbl->ExecQuery(pSvc, lang, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnum);
    SysFreeString(query);
    SysFreeString(lang);
    
    IWbemClassObject* pObj = NULL;
    ULONG retCnt = 0;
    BOOL success = FALSE;
    
    while (pEnum->lpVtbl->Next(pEnum, WBEM_INFINITE, 1, &pObj, &retCnt) == S_OK && retCnt == 1) {
        VARIANT pathVar;
        VariantInit(&pathVar);
        pObj->lpVtbl->Get(pObj, L"__PATH", 0, &pathVar, NULL, NULL);
        
        // 1. 设置IP和掩码
        IWbemClassObject *pInClass = NULL, *pInInst = NULL;
        pSvc->lpVtbl->GetMethod(pSvc, L"Win32_NetworkAdapterConfiguration", 0, L"EnableStatic", &pInClass, NULL);
        pInClass->lpVtbl->SpawnInstance(pInClass, 0, &pInInst);
        
        SAFEARRAY *ipArr = SafeArrayCreateVector(VT_BSTR, 0, 1);
        SAFEARRAY *maskArr = SafeArrayCreateVector(VT_BSTR, 0, 1);
        long idx = 0;
        BSTR ipBstr = SysAllocStringByteLen(ip, strlen(ip));
        BSTR maskBstr = SysAllocStringByteLen(mask, strlen(mask));
        SafeArrayPutElement(ipArr, &idx, ipBstr);
        SafeArrayPutElement(maskArr, &idx, maskBstr);
        
        VARIANT vIp, vMask;
        VariantInit(&vIp); vIp.vt = VT_ARRAY | VT_BSTR; vIp.parray = ipArr;
        VariantInit(&vMask); vMask.vt = VT_ARRAY | VT_BSTR; vMask.parray = maskArr;
        
        pInInst->lpVtbl->Put(pInInst, L"IPAddress", 0, &vIp, 0);
        pInInst->lpVtbl->Put(pInInst, L"SubnetMask", 0, &vMask, 0);
        
        VARIANT ret; VariantInit(&ret);
        pSvc->lpVtbl->ExecMethod(pSvc, pathVar.bstrVal, L"EnableStatic", 0, NULL, pInInst, &ret, NULL);
        VariantClear(&ret);
        
        SysFreeString(ipBstr); SysFreeString(maskBstr);
        SafeArrayDestroy(ipArr); SafeArrayDestroy(maskArr);
        pInInst->lpVtbl->Release(pInInst);
        pInClass->lpVtbl->Release(pInClass);
        
        // 2. 设置网关
        pSvc->lpVtbl->GetMethod(pSvc, L"Win32_NetworkAdapterConfiguration", 0, L"SetGateways", &pInClass, NULL);
        pInClass->lpVtbl->SpawnInstance(pInClass, 0, &pInInst);
        
        SAFEARRAY *gwArr = SafeArrayCreateVector(VT_BSTR, 0, 1);
        SAFEARRAY *metricArr = SafeArrayCreateVector(VT_I4, 0, 1);
        idx = 0;
        BSTR gwBstr = SysAllocStringByteLen(gateway, strlen(gateway));
        LONG metric = 1;
        SafeArrayPutElement(gwArr, &idx, gwBstr);
        SafeArrayPutElement(metricArr, &idx, &metric);
        
        VARIANT vGw, vMetric;
        VariantInit(&vGw); vGw.vt = VT_ARRAY | VT_BSTR; vGw.parray = gwArr;
        VariantInit(&vMetric); vMetric.vt = VT_ARRAY | VT_I4; vMetric.parray = metricArr;
        
        pInInst->lpVtbl->Put(pInInst, L"DefaultIPGateway", 0, &vGw, 0);
        pInInst->lpVtbl->Put(pInInst, L"GatewayCostMetric", 0, &vMetric, 0);
        
        pSvc->lpVtbl->ExecMethod(pSvc, pathVar.bstrVal, L"SetGateways", 0, NULL, pInInst, &ret, NULL);
        VariantClear(&ret);
        
        SysFreeString(gwBstr);
        SafeArrayDestroy(gwArr); SafeArrayDestroy(metricArr);
        pInInst->lpVtbl->Release(pInInst);
        pInClass->lpVtbl->Release(pInClass);
        
        VariantClear(&pathVar);
        pObj->lpVtbl->Release(pObj);
        success = TRUE;
    }
    
    pEnum->lpVtbl->Release(pEnum);
    pSvc->lpVtbl->Release(pSvc);
    CoUninitialize();
    return success;
}

BOOL SetDNS(const char* adapterGuid, const char* primaryDns, const char* secondaryDns) {
    char regPath[512];
    sprintf(regPath, "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\%s", adapterGuid);
    
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return FALSE;
    }
    
    char dnsStr[128];
    if (strlen(secondaryDns) > 0) {
        sprintf(dnsStr, "%s,%s", primaryDns, secondaryDns);
    } else {
        strcpy(dnsStr, primaryDns);
    }
    
    BOOL ret = RegSetValueExA(hKey, "NameServer", 0, REG_SZ, (BYTE*)dnsStr, strlen(dnsStr)+1) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    
    // 刷新网络配置
    WCHAR wGuid[256];
    MultiByteToWideChar(CP_ACP, 0, adapterGuid, -1, wGuid, 256);
    DhcpNotifyConfigChange(NULL, wGuid, TRUE, 0, inet_addr("127.0.0.1"), 0);
    return ret;
}

BOOL EnableDHCP(const char* adapterGuid) {
    char regPath[512];
    sprintf(regPath, "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\%s", adapterGuid);
    
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return FALSE;
    }
    
    DWORD enable = 1;
    RegSetValueExA(hKey, "EnableDHCP", 0, REG_DWORD, (BYTE*)&enable, sizeof(DWORD));
    RegDeleteValueA(hKey, "IPAddress");
    RegDeleteValueA(hKey, "SubnetMask");
    RegDeleteValueA(hKey, "DefaultGateway");
    RegDeleteValueA(hKey, "NameServer");
    RegCloseKey(hKey);
    
    // 刷新DHCP租约
    char cmd[256];
    sprintf(cmd, "ipconfig /renew \"%s\" >nul 2>nul", adapterGuid);
    WinExec(cmd, SW_HIDE);
    return TRUE;
}

BOOL SaveProfile(const char* filePath, IPProfile* profile) {
    return WritePrivateProfileStringA(profile->profileName, "IP", profile->ipAddr, filePath)
        && WritePrivateProfileStringA(profile->profileName, "Mask", profile->subnetMask, filePath)
        && WritePrivateProfileStringA(profile->profileName, "Gateway", profile->gateway, filePath)
        && WritePrivateProfileStringA(profile->profileName, "DNS1", profile->dnsPrimary, filePath)
        && WritePrivateProfileStringA(profile->profileName, "DNS2", profile->dnsSecondary, filePath)
        && WritePrivateProfileStringA(profile->profileName, "DHCP", profile->isDhcp ? "1" : "0", filePath);
}

int LoadProfileNames(const char* filePath, HWND hCombo) {
    char buf[65535];
    GetPrivateProfileSectionNamesA(buf, sizeof(buf), filePath);
    
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    int count = 0;
    char* p = buf;
    while (*p) {
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)p);
        p += strlen(p) + 1;
        count++;
    }
    if (count > 0) SendMessage(hCombo, CB_SETCURSEL, 0, 0);
    return count;
}

BOOL LoadProfile(const char* filePath, const char* profileName, IPProfile* profile) {
    strncpy(profile->profileName, profileName, MAX_PROFILE_NAME-1);
    GetPrivateProfileStringA(profileName, "IP", "", profile->ipAddr, MAX_IP_STR, filePath);
    GetPrivateProfileStringA(profileName, "Mask", "", profile->subnetMask, MAX_IP_STR, filePath);
    GetPrivateProfileStringA(profileName, "Gateway", "", profile->gateway, MAX_IP_STR, filePath);
    GetPrivateProfileStringA(profileName, "DNS1", "", profile->dnsPrimary, MAX_IP_STR, filePath);
    GetPrivateProfileStringA(profileName, "DNS2", "", profile->dnsSecondary, MAX_IP_STR, filePath);
    profile->isDhcp = GetPrivateProfileIntA(profileName, "DHCP", 0, filePath);
    return TRUE;
}

BOOL DeleteProfile(const char* filePath, const char* profileName) {
    return WritePrivateProfileSectionA(profileName, NULL, filePath);
}

BOOL IsAdmin() {
    BOOL isAdmin = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elev;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &cbSize)) {
            isAdmin = elev.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return isAdmin;
}

void RunAsAdmin() {
    char szPath[MAX_PATH];
    GetModuleFileNameA(NULL, szPath, MAX_PATH);
    ShellExecuteA(NULL, "runas", szPath, NULL, NULL, SW_SHOWNORMAL);
}
