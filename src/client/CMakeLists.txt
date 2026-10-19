set(VW_CLIENT_SOURCES
    main.cpp
    App.cpp
    Prediction.cpp
    Visuals.cpp
    render/Camera.cpp
    render/Mesh.cpp
    render/Pie.cpp
    render/Renderer.cpp
    render/Gltf.cpp
    render/Shaders.cpp
    ui/ImGuiLayer.cpp
    Ui.cpp
    UiDesktop.cpp
    UiDock.cpp
    UiDebug.cpp
    Fow.cpp
    UiGroups.cpp
    UiFx.cpp
    City.cpp
    Editor.cpp
    UiCampaign.cpp
    UiGuide.cpp
    UiMenu.cpp
)

# bgfx ships Dear ImGui with an imconfig.h tied to its own allocators, so build a copy with a plain config
set(VW_IMGUI_SOURCE ${bgfx_SOURCE_DIR}/bgfx/3rdparty/dear-imgui)
set(VW_IMGUI_DIR ${CMAKE_CURRENT_BINARY_DIR}/imgui)
foreach(file imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui.h imgui_internal.h imstb_rectpack.h imstb_textedit.h imstb_truetype.h)
    configure_file(${VW_IMGUI_SOURCE}/${file} ${VW_IMGUI_DIR}/${file} COPYONLY)
endforeach()
foreach(file vs_ocornut_imgui.bin.h fs_ocornut_imgui.bin.h roboto_regular.ttf.h icons_font_awesome.ttf.h)
    configure_file(${bgfx_SOURCE_DIR}/bgfx/examples/common/imgui/${file} ${CMAKE_CURRENT_BINARY_DIR}/imgui_assets/${file} COPYONLY)
endforeach()
file(CONFIGURE OUTPUT ${VW_IMGUI_DIR}/imconfig.h CONTENT "#pragma once
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
")
add_library(vw_imgui STATIC ${VW_IMGUI_DIR}/imgui.cpp ${VW_IMGUI_DIR}/imgui_draw.cpp ${VW_IMGUI_DIR}/imgui_tables.cpp ${VW_IMGUI_DIR}/imgui_widgets.cpp)
target_include_directories(vw_imgui PUBLIC ${VW_IMGUI_DIR} PRIVATE ${bgfx_SOURCE_DIR}/bgfx/3rdparty)

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
    bgfx_compile_shaders(TYPE VERTEX SHADERS shaders/vs_mesh.sc shaders/vs_tex.sc shaders/vs_terrain.sc shaders/vs_shadow.sc shaders/vs_shadow_tex.sc shaders/vs_portal.sc shaders/vs_sprite.sc
        VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc
        OUTPUT_DIR ${VW_SHADER_DIR} INCLUDE_DIRS ${bgfx_SOURCE_DIR}/bgfx/src ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        OUT_FILES_VAR VW_VS_FILES AS_HEADERS)
    bgfx_compile_shaders(TYPE FRAGMENT SHADERS shaders/fs_mesh.sc shaders/fs_tex.sc shaders/fs_terrain.sc shaders/fs_shadow.sc shaders/fs_shadow_tex.sc shaders/fs_portal.sc shaders/fs_sprite.sc
        VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc
        OUTPUT_DIR ${VW_SHADER_DIR} INCLUDE_DIRS ${bgfx_SOURCE_DIR}/bgfx/src ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        OUT_FILES_VAR VW_FS_FILES AS_HEADERS)
    target_sources(vw_client PRIVATE ${VW_VS_FILES} ${VW_FS_FILES})
    add_custom_target(vw_export_shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${VW_SHADER_DIR} ${VW_SHADER_EXPORT_DIR}
        DEPENDS ${VW_VS_FILES} ${VW_FS_FILES})
endif()

target_include_directories(vw_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${VW_SHADER_DIR} ${CMAKE_CURRENT_BINARY_DIR}/imgui_assets
                                              ${bgfx_SOURCE_DIR}/bgfx/3rdparty/iconfontheaders)
target_link_libraries(vw_client PRIVATE cgltf vw_imgui vw_core vw_net vw_host SDL3::SDL3 bgfx bimg_decode bimg bx nlohmann_json::nlohmann_json spdlog::spdlog)
if(ANDROID)
    target_link_libraries(vw_client PRIVATE EGL GLESv3 android log)
endif()
vw_target_defaults(vw_client)
vw_copy_data(vw_client)
