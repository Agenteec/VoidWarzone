#include "ui/ImGuiLayer.h"

#include <algorithm>
#include <cstring>

#include <SDL3/SDL.h>
#include <bx/math.h>
#include <imgui.h>
#include <spdlog/spdlog.h>


#include "icons_font_awesome.ttf.h"
#include "render/Shaders.h"
#include "roboto_regular.ttf.h"

namespace vw::client {

namespace {

ImTextureID toId(bgfx::TextureHandle h) {
    return static_cast<ImTextureID>(h.idx) + 1;
}

bgfx::TextureHandle fromId(ImTextureID id) {
    return {static_cast<std::uint16_t>(id - 1)};
}

ImGuiKey mapKey(SDL_Keycode key) {
    switch (key) {
    case SDLK_TAB: return ImGuiKey_Tab;
    case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow;
    case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow;
    case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End;
    case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace;
    case SDLK_RETURN: return ImGuiKey_Enter;
    case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDLK_ESCAPE: return ImGuiKey_Escape;
    case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_A: return ImGuiKey_A;
    case SDLK_C: return ImGuiKey_C;
    case SDLK_V: return ImGuiKey_V;
    case SDLK_X: return ImGuiKey_X;
    case SDLK_Y: return ImGuiKey_Y;
    case SDLK_Z: return ImGuiKey_Z;
    default: return ImGuiKey_None;
    }
}

const char* clipboardGet(ImGuiContext*) {
    static std::string text;
    char* clip = SDL_GetClipboardText();
    text = clip ? clip : "";
    SDL_free(clip);
    return text.c_str();
}

void clipboardSet(ImGuiContext*, const char* text) {
    SDL_SetClipboardText(text);
}

}

bool ImGuiLayer::init(SDL_Window* window, const Loader& load) {
    window_ = window;
    program_ = createImguiProgram();
    if (!bgfx::isValid(program_)) {
        spdlog::error("no imgui shaders for this renderer");
        return false;
    }
    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    layout_.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendRendererName = "vw_bgfx";
    io.BackendPlatformName = "vw_sdl3";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    auto& platform = ImGui::GetPlatformIO();
    platform.Platform_GetClipboardTextFn = clipboardGet;
    platform.Platform_SetClipboardTextFn = clipboardSet;

    ImFontConfig base;
    base.FontDataOwnedByAtlas = false;
    ImFontConfig icons;
    icons.FontDataOwnedByAtlas = false;
    icons.MergeMode = true;
    icons.GlyphMinAdvanceX = 17.0f;
    auto* robotoRegular = const_cast<std::uint8_t*>(s_robotoRegularTtf);
    auto* fontAwesome = const_cast<std::uint8_t*>(s_iconsFontAwesomeTtf);
    regular_ = io.Fonts->AddFontFromMemoryTTF(robotoRegular, static_cast<int>(sizeof(s_robotoRegularTtf)), 17.0f, &base);
    io.Fonts->AddFontFromMemoryTTF(fontAwesome, static_cast<int>(sizeof(s_iconsFontAwesomeTtf)), 17.0f, &icons);
    // monochrome emoji (Noto Emoji, OFL) merged behind the text and icon fonts; needs IMGUI_USE_WCHAR32
    ImFontConfig emoji;
    emoji.FontDataOwnedByAtlas = false;
    emoji.MergeMode = true;
    try {
        emojiData_ = load("data/fonts/NotoEmoji-Regular.ttf");
        io.Fonts->AddFontFromMemoryTTF(emojiData_.data(), static_cast<int>(emojiData_.size()), 17.0f, &emoji);
    } catch (const std::exception& e) {
        spdlog::warn("emoji font: {}", e.what());
    }
    try {
        boldData_ = load("data/fonts/Roboto-Bold.ttf");
        bold_ = io.Fonts->AddFontFromMemoryTTF(boldData_.data(), static_cast<int>(boldData_.size()), 17.0f, &base);
        io.Fonts->AddFontFromMemoryTTF(fontAwesome, static_cast<int>(sizeof(s_iconsFontAwesomeTtf)), 17.0f, &icons);
        if (!emojiData_.empty())
            io.Fonts->AddFontFromMemoryTTF(emojiData_.data(), static_cast<int>(emojiData_.size()), 17.0f, &emoji);
    } catch (const std::exception& e) {
        spdlog::warn("bold font: {}", e.what());
        bold_ = regular_;
    }
    initialized_ = true;
    return true;
}

void ImGuiLayer::shutdown() {
    if (!initialized_)
        return;
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        if (tex->TexID != ImTextureID_Invalid) {
            bgfx::destroy(fromId(tex->TexID));
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    ImGui::DestroyContext();
    if (bgfx::isValid(sampler_))
        bgfx::destroy(sampler_);
    if (bgfx::isValid(program_))
        bgfx::destroy(program_);
    initialized_ = false;
}

void ImGuiLayer::applyStyle(float scale) {
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 5.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = {14.0f, 12.0f};
    style.FramePadding = {12.0f, 7.0f};
    style.ItemSpacing = {9.0f, 8.0f};
    style.ItemInnerSpacing = {7.0f, 6.0f};
    style.ScrollbarSize = 14.0f;
    style.WindowTitleAlign = {0.5f, 0.5f};
    style.ButtonTextAlign = {0.5f, 0.5f};

    const ImVec4 bg{0.045f, 0.055f, 0.085f, 0.92f};
    const ImVec4 panel{0.08f, 0.1f, 0.15f, 1.0f};
    const ImVec4 accent{0.24f, 0.72f, 1.0f, 1.0f};
    const ImVec4 accentDim{0.14f, 0.38f, 0.56f, 1.0f};
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = {0.9f, 0.93f, 0.97f, 1.0f};
    c[ImGuiCol_TextDisabled] = {0.48f, 0.53f, 0.62f, 1.0f};
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = {0.0f, 0.0f, 0.0f, 0.0f};
    c[ImGuiCol_PopupBg] = {0.06f, 0.07f, 0.11f, 0.97f};
    c[ImGuiCol_Border] = {0.24f, 0.72f, 1.0f, 0.28f};
    c[ImGuiCol_FrameBg] = panel;
    c[ImGuiCol_FrameBgHovered] = {0.12f, 0.16f, 0.24f, 1.0f};
    c[ImGuiCol_FrameBgActive] = {0.15f, 0.22f, 0.32f, 1.0f};
    c[ImGuiCol_TitleBg] = {0.06f, 0.08f, 0.13f, 1.0f};
    c[ImGuiCol_TitleBgActive] = {0.08f, 0.13f, 0.2f, 1.0f};
    c[ImGuiCol_Button] = {0.11f, 0.15f, 0.23f, 1.0f};
    c[ImGuiCol_ButtonHovered] = accentDim;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = {0.12f, 0.2f, 0.3f, 1.0f};
    c[ImGuiCol_HeaderHovered] = accentDim;
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = {0.5f, 0.85f, 1.0f, 1.0f};
    c[ImGuiCol_Separator] = {0.24f, 0.72f, 1.0f, 0.22f};
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_TableHeaderBg] = {0.08f, 0.12f, 0.19f, 1.0f};
    c[ImGuiCol_TableRowBgAlt] = {1.0f, 1.0f, 1.0f, 0.03f};
    c[ImGuiCol_NavCursor] = accent;
    style.ScaleAllSizes(scale);
    ImGui::GetStyle() = style;
}

void ImGuiLayer::processEvent(const SDL_Event& e, float pixelDensity, int width, int height) {
    ImGuiIO& io = ImGui::GetIO();
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (e.motion.which == SDL_TOUCH_MOUSEID)
            break;
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(e.motion.x * pixelDensity, e.motion.y * pixelDensity);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (e.button.which == SDL_TOUCH_MOUSEID)
            break;
        const int button = e.button.button == SDL_BUTTON_LEFT ? 0 : e.button.button == SDL_BUTTON_RIGHT ? 1 : e.button.button == SDL_BUTTON_MIDDLE ? 2 : -1;
        if (button >= 0) {
            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            io.AddMousePosEvent(e.button.x * pixelDensity, e.button.y * pixelDensity);
            io.AddMouseButtonEvent(button, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        io.AddMouseWheelEvent(e.wheel.x, e.wheel.y);
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(e.tfinger.x * static_cast<float>(width), e.tfinger.y * static_cast<float>(height));
        if (e.type != SDL_EVENT_FINGER_MOTION)
            io.AddMouseButtonEvent(0, e.type == SDL_EVENT_FINGER_DOWN);
        break;
    case SDL_EVENT_TEXT_INPUT:
        io.AddInputCharactersUTF8(e.text.text);
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = e.type == SDL_EVENT_KEY_DOWN;
        io.AddKeyEvent(ImGuiMod_Ctrl, (e.key.mod & SDL_KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (e.key.mod & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (e.key.mod & SDL_KMOD_ALT) != 0);
        if (const ImGuiKey key = mapKey(e.key.key); key != ImGuiKey_None)
            io.AddKeyEvent(key, down);
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        io.AddFocusEvent(e.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
        break;
    default:
        break;
    }
}

void ImGuiLayer::beginFrame(float dt, int width, int height, float scale) {
    if (scale != styleScale_) {
        applyStyle(scale);
        styleScale_ = scale;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(width), static_cast<float>(height)};
    io.DeltaTime = std::max(dt, 1e-4f);
    ImGui::GetStyle().FontSizeBase = 17.0f * scale;
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame(bgfx::ViewId view) {
    ImGui::Render();
    render(ImGui::GetDrawData(), view);
    const bool wants = ImGui::GetIO().WantTextInput;
    if (wants != textInput_) {
        textInput_ = wants;
        if (wants)
            SDL_StartTextInput(window_);
        else
            SDL_StopTextInput(window_);
    }
}

bool ImGuiLayer::wantsMouse() const {
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::wantsText() const {
    return ImGui::GetIO().WantTextInput;
}

void ImGuiLayer::render(ImDrawData* data, bgfx::ViewId view) {
    if (data->Textures)
        for (ImTextureData* tex : *data->Textures) {
            if (tex->Status == ImTextureStatus_WantCreate) {
                const bgfx::TextureHandle h = bgfx::createTexture2D(static_cast<std::uint16_t>(tex->Width), static_cast<std::uint16_t>(tex->Height), false,
                                                                    1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
                bgfx::updateTexture2D(h, 0, 0, 0, 0, static_cast<std::uint16_t>(tex->Width), static_cast<std::uint16_t>(tex->Height),
                                      bgfx::copy(tex->GetPixels(), static_cast<std::uint32_t>(tex->GetSizeInBytes())));
                tex->SetTexID(toId(h));
                tex->SetStatus(ImTextureStatus_OK);
            } else if (tex->Status == ImTextureStatus_WantUpdates) {
                const bgfx::TextureHandle h = fromId(tex->TexID);
                for (const ImTextureRect& r : tex->Updates) {
                    const std::uint32_t row = static_cast<std::uint32_t>(r.w) * static_cast<std::uint32_t>(tex->BytesPerPixel);
                    const bgfx::Memory* mem = bgfx::alloc(row * r.h);
                    for (int y = 0; y < r.h; ++y)
                        std::memcpy(mem->data + static_cast<std::size_t>(y) * row, tex->GetPixelsAt(r.x, r.y + y), row);
                    bgfx::updateTexture2D(h, 0, 0, r.x, r.y, r.w, r.h, mem);
                }
                tex->SetStatus(ImTextureStatus_OK);
            } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
                bgfx::destroy(fromId(tex->TexID));
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
        }

    const float width = data->DisplaySize.x;
    const float height = data->DisplaySize.y;
    if (width <= 0.0f || height <= 0.0f)
        return;
    float ortho[16];
    bx::mtxOrtho(ortho, 0.0f, width, height, 0.0f, 0.0f, 1000.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(view, nullptr, ortho);
    bgfx::setViewRect(view, 0, 0, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height));
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::touch(view);

    for (const ImDrawList* list : data->CmdLists) {
        const auto vertices = static_cast<std::uint32_t>(list->VtxBuffer.size());
        const auto indices = static_cast<std::uint32_t>(list->IdxBuffer.size());
        if (bgfx::getAvailTransientVertexBuffer(vertices, layout_) < vertices || bgfx::getAvailTransientIndexBuffer(indices) < indices)
            break;
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, vertices, layout_);
        bgfx::allocTransientIndexBuffer(&tib, indices, sizeof(ImDrawIdx) == 4);
        std::memcpy(tvb.data, list->VtxBuffer.Data, vertices * sizeof(ImDrawVert));
        std::memcpy(tib.data, list->IdxBuffer.Data, indices * sizeof(ImDrawIdx));

        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                cmd.UserCallback(list, &cmd);
                continue;
            }
            if (cmd.ElemCount == 0)
                continue;
            const float x0 = std::max(cmd.ClipRect.x, 0.0f);
            const float y0 = std::max(cmd.ClipRect.y, 0.0f);
            const float x1 = std::min(cmd.ClipRect.z, width);
            const float y1 = std::min(cmd.ClipRect.w, height);
            if (x1 <= x0 || y1 <= y0)
                continue;
            bgfx::setScissor(static_cast<std::uint16_t>(x0), static_cast<std::uint16_t>(y0), static_cast<std::uint16_t>(x1 - x0),
                             static_cast<std::uint16_t>(y1 - y0));
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
            bgfx::setTexture(0, sampler_, fromId(cmd.GetTexID()));
            bgfx::setVertexBuffer(0, &tvb, cmd.VtxOffset, vertices);
            bgfx::setIndexBuffer(&tib, cmd.IdxOffset, cmd.ElemCount);
            bgfx::submit(view, program_);
        }
    }
}

}
