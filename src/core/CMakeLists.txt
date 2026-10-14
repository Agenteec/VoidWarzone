add_library(vw_core STATIC
    core/GameData.cpp
    core/Heroes.cpp
    core/Map.cpp
    core/Movement.cpp
    core/Build.cpp
    core/Pathfinding.cpp
    core/World.cpp
    core/WorldDefense.cpp
)
target_include_directories(vw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vw_core PUBLIC VW_VERSION="${PROJECT_VERSION}")
target_link_libraries(vw_core PUBLIC glm::glm EnTT::EnTT PRIVATE nlohmann_json::nlohmann_json)
vw_target_defaults(vw_core)
