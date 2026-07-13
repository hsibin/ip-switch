# IP切换器

纯 Win32 API + C 语言编写的 Windows IP 配置切换工具。

## 功能

- 🌐 自动枚举本机所有网络适配器
- 📝 修改 IP 地址、子网掩码、默认网关
- 🖥️ 修改首选 DNS 和备用 DNS
- ⚡ 一键切换为 DHCP 自动获取
- 💾 保存/加载多个预设配置（存储在 `%APPDATA%\IPSwitcher\`）
- 🔒 自动请求管理员权限运行

## 编译方式

### 方式一：GitHub Actions（推荐，无需本地安装编译器）

1. Fork 本项目或推送到你自己的 GitHub 仓库
2. GitHub Actions 自动编译
3. 在 Actions → Artifacts 下载 `IPSwitcher.exe`

### 方式二：本地 MSVC 编译

```batch
rc /fo resource.res resource.rc
cl /O2 /MT /Fe:IPSwitcher.exe IPSwitcher.c resource.res ^
  user32.lib comctl32.lib shell32.lib ole32.lib advapi32.lib ^
  /link /SUBSYSTEM:WINDOWS
```

### 方式三：本地 MinGW 编译

```bash
windres resource.rc -O coff -o resource.res
gcc -O2 -mwindows -o IPSwitcher.exe IPSwitcher.c resource.res \
  -lcomctl32 -lshell32 -lole32
```

## 运行要求

- Windows 7 / 8 / 8.1 / 10 / 11（x64）
- 以管理员身份运行（程序会自动请求提权）

## 文件体积

编译后 `.exe` 约 **50-150 KB**（静态链接，零运行时依赖）。

## 技术栈

- 纯 C 语言（C89/C99）
- 纯 Win32 API（无 MFC / .NET / 第三方库）
- `netsh` 命令行执行网络配置
- JSON 手动解析实现配置存储

## 使用说明

1. 运行 `IPSwitcher.exe`（需要管理员权限）
2. 选择要配置的网络适配器
3. 填写 IP、掩码、网关、DNS 后点击「应用配置」
4. 或输入配置名后点击「保存当前配置」保存为预设
5. 从预设下拉列表中选择配置快速切换

## 许可证

MIT License