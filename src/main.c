#include <windows.h>
#include <commctrl.h>
#include "netutil.h"

#define IDC_ADAPTER_COMBO 1001
#define IDC_IP_EDIT 1002
#define IDC_MASK_EDIT 1003
#define IDC_GATEWAY_EDIT 1004
#define IDC_DNS1_EDIT 1005
#define IDC_DNS2_EDIT 1006
#define IDC_DHCP_CHECK 1007
#define IDC_PROFILE_NAME 1008
#define IDC_PROFILE_COMBO 1009
#define IDC_SAVE_BTN 1010
#define IDC_LOAD_BTN 1011
#define IDC_DELETE_BTN 1012
#define IDC_APPLY_BTN 1013

#define IDD_MAIN 101

HINSTANCE hInst;
char profileFile[MAX_PATH];

void GetProfilePath() {
    GetModuleFileNameA(NULL, profileFile, MAX_PATH);
    char* p = strrchr(profileFile, '\\');
    if (p) {
        *(p+1) = '\0';
        strcat(profileFile, "profiles.ini");
    }
}

BOOL CALLBACK DlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            // 设置窗口图标（无图标资源时自动降级为默认）
            HICON hIcon = LoadIcon(hInst, MAKEINTRESOURCE(1));
            if (hIcon) {
                SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            }
            
            // 枚举适配器
            EnumNetworkAdapters(GetDlgItem(hWnd, IDC_ADAPTER_COMBO));
            
            // 加载配置列表
            GetProfilePath();
            LoadProfileNames(profileFile, GetDlgItem(hWnd, IDC_PROFILE_COMBO));
            
            // 检查管理员权限
            if (!IsAdmin()) {
                MessageBox(hWnd, "当前未以管理员身份运行，修改IP可能失败！\n将自动重启并提升权限。", "提示", MB_ICONWARNING);
                RunAsAdmin();
                EndDialog(hWnd, 0);
            }
            return TRUE;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_DHCP_CHECK: {
                    BOOL checked = IsDlgButtonChecked(hWnd, IDC_DHCP_CHECK) == BST_CHECKED;
                    EnableWindow(GetDlgItem(hWnd, IDC_IP_EDIT), !checked);
                    EnableWindow(GetDlgItem(hWnd, IDC_MASK_EDIT), !checked);
                    EnableWindow(GetDlgItem(hWnd, IDC_GATEWAY_EDIT), !checked);
                    break;
                }
                
                case IDC_SAVE_BTN: {
                    char name[MAX_PROFILE_NAME];
                    GetDlgItemTextA(hWnd, IDC_PROFILE_NAME, name, sizeof(name));
                    if (strlen(name) == 0) {
                        MessageBox(hWnd, "请输入配置名称", "错误", MB_ICONERROR);
                        break;
                    }
                    
                    IPProfile profile;
                    strncpy(profile.profileName, name, MAX_PROFILE_NAME-1);
                    GetDlgItemTextA(hWnd, IDC_IP_EDIT, profile.ipAddr, MAX_IP_STR);
                    GetDlgItemTextA(hWnd, IDC_MASK_EDIT, profile.subnetMask, MAX_IP_STR);
                    GetDlgItemTextA(hWnd, IDC_GATEWAY_EDIT, profile.gateway, MAX_IP_STR);
                    GetDlgItemTextA(hWnd, IDC_DNS1_EDIT, profile.dnsPrimary, MAX_IP_STR);
                    GetDlgItemTextA(hWnd, IDC_DNS2_EDIT, profile.dnsSecondary, MAX_IP_STR);
                    profile.isDhcp = IsDlgButtonChecked(hWnd, IDC_DHCP_CHECK) == BST_CHECKED;
                    
                    if (SaveProfile(profileFile, &profile)) {
                        LoadProfileNames(profileFile, GetDlgItem(hWnd, IDC_PROFILE_COMBO));
                        MessageBox(hWnd, "配置保存成功", "提示", MB_OK);
                    } else {
                        MessageBox(hWnd, "保存失败", "错误", MB_ICONERROR);
                    }
                    break;
                }
                
                case IDC_LOAD_BTN: {
                    char name[MAX_PROFILE_NAME];
                    int sel = SendMessage(GetDlgItem(hWnd, IDC_PROFILE_COMBO), CB_GETCURSEL, 0, 0);
                    if (sel == CB_ERR) break;
                    SendMessageA(GetDlgItem(hWnd, IDC_PROFILE_COMBO), CB_GETLBTEXT, sel, (LPARAM)name);
                    
                    IPProfile profile;
                    if (LoadProfile(profileFile, name, &profile)) {
                        SetDlgItemTextA(hWnd, IDC_IP_EDIT, profile.ipAddr);
                        SetDlgItemTextA(hWnd, IDC_MASK_EDIT, profile.subnetMask);
                        SetDlgItemTextA(hWnd, IDC_GATEWAY_EDIT, profile.gateway);
                        SetDlgItemTextA(hWnd, IDC_DNS1_EDIT, profile.dnsPrimary);
                        SetDlgItemTextA(hWnd, IDC_DNS2_EDIT, profile.dnsSecondary);
                        CheckDlgButton(hWnd, IDC_DHCP_CHECK, profile.isDhcp ? BST_CHECKED : BST_UNCHECKED);
                        SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(IDC_DHCP_CHECK, 0), 0);
                    }
                    break;
                }
                
                case IDC_DELETE_BTN: {
                    char name[MAX_PROFILE_NAME];
                    int sel = SendMessage(GetDlgItem(hWnd, IDC_PROFILE_COMBO), CB_GETCURSEL, 0, 0);
                    if (sel == CB_ERR) break;
                    SendMessageA(GetDlgItem(hWnd, IDC_PROFILE_COMBO), CB_GETLBTEXT, sel, (LPARAM)name);
                    
                    if (MessageBox(hWnd, "确定删除该配置？", "确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        DeleteProfile(profileFile, name);
                        LoadProfileNames(profileFile, GetDlgItem(hWnd, IDC_PROFILE_COMBO));
                    }
                    break;
                }
                
                case IDC_APPLY_BTN: {
                    int adapterSel = SendMessage(GetDlgItem(hWnd, IDC_ADAPTER_COMBO), CB_GETCURSEL, 0, 0);
                    if (adapterSel == CB_ERR) {
                        MessageBox(hWnd, "请选择网络适配器", "错误", MB_ICONERROR);
                        break;
                    }
                    
                    char guid[256];
                    GetAdapterGuid(adapterSel, guid, sizeof(guid));
                    
                    BOOL isDhcp = IsDlgButtonChecked(hWnd, IDC_DHCP_CHECK) == BST_CHECKED;
                    if (isDhcp) {
                        if (EnableDHCP(guid)) {
                            MessageBox(hWnd, "已切换为DHCP自动获取", "成功", MB_OK);
                        } else {
                            MessageBox(hWnd, "设置失败，请以管理员身份运行", "错误", MB_ICONERROR);
                        }
                    } else {
                        char ip[MAX_IP_STR], mask[MAX_IP_STR], gw[MAX_IP_STR];
                        char dns1[MAX_IP_STR], dns2[MAX_IP_STR];
                        GetDlgItemTextA(hWnd, IDC_IP_EDIT, ip, sizeof(ip));
                        GetDlgItemTextA(hWnd, IDC_MASK_EDIT, mask, sizeof(mask));
                        GetDlgItemTextA(hWnd, IDC_GATEWAY_EDIT, gw, sizeof(gw));
                        GetDlgItemTextA(hWnd, IDC_DNS1_EDIT, dns1, sizeof(dns1));
                        GetDlgItemTextA(hWnd, IDC_DNS2_EDIT, dns2, sizeof(dns2));
                        
                        if (strlen(ip) == 0 || strlen(mask) == 0) {
                            MessageBox(hWnd, "IP和掩码不能为空", "错误", MB_ICONERROR);
                            break;
                        }
                        
                        BOOL ok = SetStaticIP(guid, ip, mask, gw);
                        if (strlen(dns1) > 0) {
                            ok &= SetDNS(guid, dns1, dns2);
                        }
                        
                        if (ok) {
                            MessageBox(hWnd, "IP配置应用成功", "成功", MB_OK);
                        } else {
                            MessageBox(hWnd, "部分配置失败，请检查权限", "警告", MB_ICONWARNING);
                        }
                    }
                    break;
                }
                
                case IDOK:
                    EndDialog(hWnd, 0);
                    return TRUE;
            }
            break;
        }
        
        case WM_CLOSE:
            EndDialog(hWnd, 0);
            return TRUE;
    }
    return FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    hInst = hInstance;
    InitCommonControls();
    return DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, DlgProc);
}
