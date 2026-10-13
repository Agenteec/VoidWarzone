add_library(vw_core STATIC
    core/World.cpp
)
target_include_directories(vw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vw_core PUBLIC VW_VERSION="${PROJECT_VERSION}")
target_link_libraries(vw_core PUBLIC glm::glm EnTT::EnTT)
vw_target_defaults(vw_core)
