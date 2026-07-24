cmake_minimum_required(VERSION 3.15)
project(IpSwitcher C)

set(CMAKE_C_STANDARD 99)

# 资源文件
set(RESOURCE_FILES src/resource.rc)

add_executable(IpSwitcher WIN32
    src/main.c
    src/netutil.c
    ${RESOURCE_FILES}
)

# 链接依赖库
target_link_libraries(IpSwitcher
    iphlpapi
    ws2_32
    wbemuuid
    shell32
    comctl32
    ole32
    oleaut32
    uuid
)

# MSVC UTF-8编码支持
if(MSVC)
    target_compile_options(IpSwitcher PRIVATE /utf-8)
endif()
