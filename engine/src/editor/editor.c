#include "eisenfront/editor.h"

#include <cimgui_impl.h>

#include <SDL3/SDL.h>

#define EDITOR_LOG_CATEGORY "editor"

static ImGuiContext *s_imgui_context = nullptr;

void editor_process_raw_event(void *native_event, void *userdata) {
    (void)userdata;
    if (s_imgui_context == nullptr) {
        return;
    }
    ImGui_ImplSDL3_ProcessEvent((const SDL_Event *)native_event);
}

Result editor_init(Window *window, GraphicsContext *context) {
    if (window == nullptr || context == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (s_imgui_context != nullptr) {
        return RESULT_ERROR_ALREADY_INITIALIZED;
    }

    ImGuiContext *imgui_context = igCreateContext(nullptr);
    if (imgui_context == nullptr) {
        LOG_ERROR(EDITOR_LOG_CATEGORY, "igCreateContext() failed");
        return RESULT_ERROR_MODULE_INIT_FAILED;
    }
    igSetCurrentContext(imgui_context);

    ImGuiIO *io = igGetIO_Nil();
    io->IniFilename = nullptr; /* see the file header comment on layout persistence */

    SDL_Window   *sdl_window = (SDL_Window *)window_get_native_handle(window);
    SDL_GLContext gl_context = (SDL_GLContext)graphics_context_get_native_handle(context);

    if (!ImGui_ImplSDL3_InitForOpenGL(sdl_window, gl_context)) {
        LOG_ERROR(EDITOR_LOG_CATEGORY, "ImGui_ImplSDL3_InitForOpenGL() failed");
        igDestroyContext(imgui_context);
        return RESULT_ERROR_MODULE_INIT_FAILED;
    }
    if (!ImGui_ImplOpenGL3_Init(nullptr)) {
        LOG_ERROR(EDITOR_LOG_CATEGORY, "ImGui_ImplOpenGL3_Init() failed");
        ImGui_ImplSDL3_Shutdown();
        igDestroyContext(imgui_context);
        return RESULT_ERROR_MODULE_INIT_FAILED;
    }

    window_set_raw_event_callback(editor_process_raw_event, nullptr);

    s_imgui_context = imgui_context;
    LOG_INFO(EDITOR_LOG_CATEGORY, "Editor initialized");
    return RESULT_OK;
}

void editor_shutdown(void) {
    if (s_imgui_context == nullptr) {
        return;
    }
    window_set_raw_event_callback(nullptr, nullptr);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    igDestroyContext(s_imgui_context);
    s_imgui_context = nullptr;
}

void editor_new_frame(void) {
    ASSERT(s_imgui_context != nullptr);
    if (s_imgui_context == nullptr) {
        return;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    igNewFrame();
}

void editor_render(void) {
    ASSERT(s_imgui_context != nullptr);
    if (s_imgui_context == nullptr) {
        return;
    }
    igRender();
    ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());
}

bool editor_wants_capture_mouse(void) {
    if (s_imgui_context == nullptr) {
        return false;
    }
    return igGetIO_Nil()->WantCaptureMouse;
}

bool editor_wants_capture_keyboard(void) {
    if (s_imgui_context == nullptr) {
        return false;
    }
    return igGetIO_Nil()->WantCaptureKeyboard;
}
