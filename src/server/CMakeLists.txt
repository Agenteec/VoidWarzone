add_executable(vw_server main.cpp)
target_link_libraries(vw_server PRIVATE vw_core vw_net spdlog::spdlog)
vw_target_defaults(vw_server)
vw_copy_data(vw_server)
