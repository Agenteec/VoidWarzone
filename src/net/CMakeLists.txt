add_library(vw_net STATIC
    net/Connection.cpp
    net/Messages.cpp
    net/NetClient.cpp
    net/NetLibrary.cpp
    net/Snapshot.cpp
    net/Socket.cpp
)
target_include_directories(vw_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vw_net PUBLIC vw_core PRIVATE reliable LibJuice::LibJuiceStatic spdlog::spdlog)
if(WIN32)
    target_link_libraries(vw_net PUBLIC ws2_32)
    target_compile_definitions(vw_net PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
vw_target_defaults(vw_net)
