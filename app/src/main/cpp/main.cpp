#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <stdlib.h>
#include <cmath>

#define LOG_TAG "NativeApp"
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Vertex shader source
const char* vertexShaderSource = R"(#version 320 es
layout(location = 0) in vec3 aPos;
void main() {
    gl_Position = vec4(aPos, 1.0);
})";

// Fragment shader source
const char* fragmentShaderSource = R"(#version 320 es
precision mediump float;
out vec4 FragColor;
void main() {
    FragColor = vec4(1.0, 1.0, 1.0, 1.0);
})";

struct Engine {
    android_app* app;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint program = 0;
    GLuint VBO = 0;
    GLuint VAO = 0;
};

void init_renderer(Engine* engine);
void draw_frame(Engine* engine);
void cleanup_renderer(Engine* engine);
GLuint compile_shader(GLenum type, const char* source);
GLuint create_program(const char* vs, const char* fs);

// Vertex data for triangle
const float vertices[] = {
     0.0f,  0.5f, 0.0f,  // top
    -0.5f, -0.5f, 0.0f,  // left
     0.5f, -0.5f, 0.0f   // right
};

void android_main(android_app* app) {
    Engine engine{};
    engine.app = app;

    app->userData = &engine;
    app->onAppCmd = [](android_app* app, int32_t cmd) {
        Engine* engine = (Engine*)app->userData;
        switch (cmd) {
            case APP_CMD_INIT_WINDOW:
                init_renderer(engine);
                break;
            case APP_CMD_TERM_WINDOW:
                cleanup_renderer(engine);
                break;
        }
    };

    // Main loop
    while (!app->destroyRequested) {
        int events;
        android_poll_source* source;

        // Process events
        while (ALooper_pollOnce(0, nullptr, &events, (void**)&source) >= 0) {
            if (source) {
                source->process(app, source);
            }
        }

        // Render if display is valid
        if (engine.display != EGL_NO_DISPLAY) {
            draw_frame(&engine);
        }
    }

    // Final cleanup
    cleanup_renderer(&engine);
}

void init_renderer(Engine* engine) {
    // Initialize EGL
    engine->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (engine->display == EGL_NO_DISPLAY) {
        LOG_ERROR("Failed to get EGL display");
        return;
    }

    if (!eglInitialize(engine->display, nullptr, nullptr)) {
        LOG_ERROR("Failed to initialize EGL");
        return;
    }

    // Configure EGL
    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(engine->display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        LOG_ERROR("Failed to choose EGL config");
        cleanup_renderer(engine);
        return;
    }

    // Create surface
    engine->surface = eglCreateWindowSurface(engine->display, config, engine->app->window, nullptr);
    if (engine->surface == EGL_NO_SURFACE) {
        LOG_ERROR("Failed to create EGL surface");
        cleanup_renderer(engine);
        return;
    }

    // Create context
    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    engine->context = eglCreateContext(engine->display, config, EGL_NO_CONTEXT, contextAttribs);
    if (engine->context == EGL_NO_CONTEXT) {
        LOG_ERROR("Failed to create EGL context");
        cleanup_renderer(engine);
        return;
    }

    if (!eglMakeCurrent(engine->display, engine->surface, engine->surface, engine->context)) {
        LOG_ERROR("Failed to make EGL current");
        cleanup_renderer(engine);
        return;
    }

    // Create shader program
    engine->program = create_program(vertexShaderSource, fragmentShaderSource);
    if (!engine->program) {
        LOG_ERROR("Failed to create shader program");
        cleanup_renderer(engine);
        return;
    }

    // Create VAO and VBO
    glGenVertexArrays(1, &engine->VAO);
    glGenBuffers(1, &engine->VBO);

    glBindVertexArray(engine->VAO);
    glBindBuffer(GL_ARRAY_BUFFER, engine->VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    LOG_INFO("Renderer initialized successfully");
}

void draw_frame(Engine* engine) {
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(engine->program);
    glBindVertexArray(engine->VAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    eglSwapBuffers(engine->display, engine->surface);
}

void cleanup_renderer(Engine* engine) {
    if (engine->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        
        if (engine->context != EGL_NO_CONTEXT) {
            eglDestroyContext(engine->display, engine->context);
        }
        
        if (engine->surface != EGL_NO_SURFACE) {
            eglDestroySurface(engine->display, engine->surface);
        }
        
        eglTerminate(engine->display);
    }

    if (engine->program) {
        glDeleteProgram(engine->program);
    }
    
    if (engine->VBO) {
        glDeleteBuffers(1, &engine->VBO);
    }
    
    if (engine->VAO) {
        glDeleteVertexArrays(1, &engine->VAO);
    }

    engine->display = EGL_NO_DISPLAY;
    engine->context = EGL_NO_CONTEXT;
    engine->surface = EGL_NO_SURFACE;
    engine->program = 0;
    engine->VBO = 0;
    engine->VAO = 0;

    LOG_INFO("Renderer cleaned up");
}

GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        LOG_ERROR("Shader compilation error: %s", infoLog);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint create_program(const char* vs, const char* fs) {
    GLuint vertexShader = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint fragmentShader = compile_shader(GL_FRAGMENT_SHADER, fs);

    if (!vertexShader || !fragmentShader) {
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        LOG_ERROR("Program link error: %s", infoLog);
        glDeleteProgram(program);
        return 0;
    }

    return program;
}