set(VW_CLIENT_SOURCES
    main.cpp
    App.cpp
    Prediction.cpp
    Visuals.cpp
    render/Camera.cpp
    render/Mesh.cpp
    render/Pie.cpp
    render/Renderer.cpp
    render/Shaders.cpp
)

if(ANDROID)
    add_library(vw_client SHARED ${VW_CLIENT_SOURCES})
    set_target_properties(vw_client PROPERTIES OUTPUT_NAME main)
else()
    add_executable(vw_client ${VW_CLIENT_SOURCES})
endif()

set(VW_SHADER_EXPORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders/generated)
if(TARGET bgfx::shaderc)
    set(VW_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
else()
    set(VW_SHADER_DIR ${VW_SHADER_EXPORT_DIR})
endif()
if(TARGET bgfx::shaderc)
    bgfx_compile_shaders(TYPE VERTEX SHADERS shaders/vs_mesh.sc shaders/vs_tex.sc
        VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc
        OUTPUT_DIR ${VW_SHADER_DIR} INCLUDE_DIRS ${bgfx_SOURCE_DIR}/bgfx/src
        OUT_FILES_VAR VW_VS_FILES AS_HEADERS)
    bgfx_compile_shaders(TYPE FRAGMENT SHADERS shaders/fs_mesh.sc shaders/fs_tex.sc
        VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc
        OUTPUT_DIR ${VW_SHADER_DIR} INCLUDE_DIRS ${bgfx_SOURCE_DIR}/bgfx/src
        OUT_FILES_VAR VW_FS_FILES AS_HEADERS)
    target_sources(vw_client PRIVATE ${VW_VS_FILES} ${VW_FS_FILES})
    add_custom_target(vw_export_shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${VW_SHADER_DIR} ${VW_SHADER_EXPORT_DIR}
        DEPENDS ${VW_VS_FILES} ${VW_FS_FILES})
endif()

target_include_directories(vw_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${VW_SHADER_DIR})
target_link_libraries(vw_client PRIVATE vw_core vw_net vw_host SDL3::SDL3 bgfx bimg_decode bimg bx nlohmann_json::nlohmann_json spdlog::spdlog)
if(ANDROID)
    target_link_libraries(vw_client PRIVATE EGL GLESv3 android log)
endif()
vw_target_defaults(vw_client)
vw_copy_data(vw_client)
