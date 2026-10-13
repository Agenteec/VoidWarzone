include(CPM)

CPMAddPackage(NAME glm GITHUB_REPOSITORY g-truc/glm GIT_TAG 1.0.1
    OPTIONS "GLM_BUILD_TESTS OFF" "GLM_BUILD_INSTALL OFF")

CPMAddPackage(NAME EnTT GITHUB_REPOSITORY skypjack/entt GIT_TAG v3.15.0)

CPMAddPackage(NAME spdlog GITHUB_REPOSITORY gabime/spdlog GIT_TAG v1.16.0
    OPTIONS "SPDLOG_INSTALL OFF")

CPMAddPackage(NAME nlohmann_json GITHUB_REPOSITORY nlohmann/json GIT_TAG v3.12.0
    OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF")

CPMAddPackage(NAME libjuice GITHUB_REPOSITORY paullouisageneau/libjuice GIT_TAG v1.7.3
    EXCLUDE_FROM_ALL YES OPTIONS "NO_TESTS ON")

CPMAddPackage(NAME reliable GITHUB_REPOSITORY mas-bandwidth/reliable GIT_TAG v1.4.4
    DOWNLOAD_ONLY YES)
if(reliable_ADDED)
    add_library(reliable STATIC ${reliable_SOURCE_DIR}/reliable.c)
    target_include_directories(reliable SYSTEM PUBLIC ${reliable_SOURCE_DIR})
    if(MSVC)
        target_compile_definitions(reliable PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()

if(VW_BUILD_MASTER)
    CPMAddPackage(NAME asio GITHUB_REPOSITORY chriskohlhoff/asio GIT_TAG asio-1-36-0
        DOWNLOAD_ONLY YES)
    if(asio_ADDED)
        add_library(asio INTERFACE)
        target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
        target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
        if(WIN32)
            target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0A00)
            target_link_libraries(asio INTERFACE ws2_32 mswsock)
        endif()
    endif()
endif()

if(VW_BUILD_CLIENT)
    CPMAddPackage(NAME SDL3 GITHUB_REPOSITORY libsdl-org/SDL GIT_TAG release-3.4.16
        OPTIONS "SDL_SHARED OFF" "SDL_STATIC ON" "SDL_TEST_LIBRARY OFF" "SDL_EXAMPLES OFF")

    if(ANDROID)
        set(_vw_bgfx_tools OFF)
    else()
        set(_vw_bgfx_tools ON)
    endif()
    CPMAddPackage(NAME bgfx GITHUB_REPOSITORY bkaradzic/bgfx.cmake GIT_TAG v1.164.9539-588
        OPTIONS "BGFX_BUILD_EXAMPLES OFF" "BGFX_BUILD_TESTS OFF" "BGFX_BUILD_TOOLS ${_vw_bgfx_tools}"
                "BGFX_BUILD_TOOLS_SHADER ${_vw_bgfx_tools}" "BGFX_BUILD_TOOLS_BIN2C OFF"
                "BGFX_BUILD_TOOLS_GEOMETRY OFF" "BGFX_BUILD_TOOLS_TEXTURE OFF"
                "BGFX_INSTALL OFF" "BGFX_CUSTOM_TARGETS OFF")
    include(${bgfx_SOURCE_DIR}/cmake/bgfxToolUtils.cmake)
endif()

if(VW_BUILD_TESTS)
    CPMAddPackage(NAME doctest GITHUB_REPOSITORY doctest/doctest GIT_TAG v2.4.11
        OPTIONS "DOCTEST_NO_INSTALL ON")
endif()
