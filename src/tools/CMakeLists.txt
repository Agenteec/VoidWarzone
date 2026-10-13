add_executable(vw_bot bot.cpp)
target_link_libraries(vw_bot PRIVATE vw_core vw_net spdlog::spdlog)
vw_target_defaults(vw_bot)
vw_copy_data(vw_bot)
