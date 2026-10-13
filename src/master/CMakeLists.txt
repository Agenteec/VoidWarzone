add_library(vw_masterlib STATIC
    master/Lobby.cpp
    master/MasterServer.cpp
)
target_include_directories(vw_masterlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vw_masterlib PUBLIC vw_net asio PRIVATE spdlog::spdlog)
vw_target_defaults(vw_masterlib)

add_executable(vw_master main.cpp)
target_link_libraries(vw_master PRIVATE vw_masterlib spdlog::spdlog)
vw_target_defaults(vw_master)
