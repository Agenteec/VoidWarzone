add_executable(vw_master main.cpp)
target_link_libraries(vw_master PRIVATE vw_net asio LibJuice::LibJuiceStatic spdlog::spdlog)
vw_target_defaults(vw_master)
