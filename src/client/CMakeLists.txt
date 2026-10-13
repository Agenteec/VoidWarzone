if(ANDROID)
    add_library(vw_client SHARED main.cpp)
    set_target_properties(vw_client PROPERTIES OUTPUT_NAME main)
else()
    add_executable(vw_client main.cpp)
endif()

target_link_libraries(vw_client PRIVATE vw_core vw_net SDL3::SDL3 bgfx bx spdlog::spdlog)
vw_target_defaults(vw_client)
