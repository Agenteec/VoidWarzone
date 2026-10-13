add_library(vw_net STATIC
    net/NetLibrary.cpp
)
target_include_directories(vw_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vw_net PUBLIC vw_core PRIVATE reliable LibJuice::LibJuiceStatic spdlog::spdlog)
vw_target_defaults(vw_net)
