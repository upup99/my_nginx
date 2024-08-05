# app/CMakeLists.txt
add_library(app_lib
    nginx.cpp
    ngx_c_conf.cpp
    ngx_log.cpp
    ngx_string.cpp
    ngx_printf.cpp
    ngx_setproctitle.cpp
)

# 添加头文件路径
target_include_directories(app_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../_include  # 指向根目录下的 _include
)

# 如果需要链接其他库，使用 target_link_libraries
# target_link_libraries(app_lib ...)

# 添加可执行目标
add_executable(my_nginx_app nginx.cpp)  # 假设 main.cxx 是你的入口文件
target_link_libraries(my_nginx_app app_lib pthread sig_lib logic_lib misc_lib proc_lib net_lib)  # 链接 app_lib
