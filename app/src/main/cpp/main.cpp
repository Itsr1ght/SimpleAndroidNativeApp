#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <stdlib.h>
#include <cmath>
#include <memory>

#define LOG_TAG "RealNativeApp"
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Shader source code
namespace Shaders {
    // Vertex shader source
    constexpr char vertexShaderSource[] = R"(#version 320 es
    layout(location = 0) in vec3 aPos;
    void main() {
        gl_Position = vec4(aPos, 1.0);
    })";

    // Fragment shader source
    constexpr char fragmentShaderSource[] = R"(#version 320 es
    precision mediump float;
    out vec4 FragColor;
    void main() {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
    })";
}

// Shader utility class
class ShaderProgram {
private:
    GLuint mProgramId = 0;

    GLuint compileShader(GLenum type, const char* source) {
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

public:
    ShaderProgram() = default;
    ~ShaderProgram() {
        if (mProgramId) {
            glDeleteProgram(mProgramId);
        }
    }

    bool initialize(const char* vertexSource, const char* fragmentSource) {
        GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
        GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);

        if (!vertexShader || !fragmentShader) {
            return false;
        }

        mProgramId = glCreateProgram();
        glAttachShader(mProgramId, vertexShader);
        glAttachShader(mProgramId, fragmentShader);
        glLinkProgram(mProgramId);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        GLint success;
        glGetProgramiv(mProgramId, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(mProgramId, 512, nullptr, infoLog);
            LOG_ERROR("Program link error: %s", infoLog);
            glDeleteProgram(mProgramId);
            mProgramId = 0;
            return false;
        }

        return true;
    }

    void use() const {
        glUseProgram(mProgramId);
    }

    GLuint getProgramId() const {
        return mProgramId;
    }
};

// Triangle mesh class
class TriangleMesh {
private:
    GLuint mVAO = 0;
    GLuint mVBO = 0;
    
    // Vertex data for triangle
    static constexpr float vertices[] = {
         0.0f,  0.5f, 0.0f,  // top
        -0.5f, -0.5f, 0.0f,  // left
         0.5f, -0.5f, 0.0f   // right
    };

public:
    TriangleMesh() = default;
    ~TriangleMesh() {
        cleanup();
    }

    bool initialize() {
        glGenVertexArrays(1, &mVAO);
        glGenBuffers(1, &mVBO);

        glBindVertexArray(mVAO);
        glBindBuffer(GL_ARRAY_BUFFER, mVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
        
        return (mVAO != 0 && mVBO != 0);
    }

    void draw() const {
        glBindVertexArray(mVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }

    void cleanup() {
        if (mVBO) {
            glDeleteBuffers(1, &mVBO);
            mVBO = 0;
        }
        
        if (mVAO) {
            glDeleteVertexArrays(1, &mVAO);
            mVAO = 0;
        }
    }
};

// EGL renderer class
class EGLRenderer {
private:
    android_app* mApp = nullptr;
    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLSurface mSurface = EGL_NO_SURFACE;
    EGLContext mContext = EGL_NO_CONTEXT;
    ShaderProgram mShaderProgram;
    TriangleMesh mTriangle;
    
public:
    EGLRenderer(android_app* app) : mApp(app) {}
    
    ~EGLRenderer() {
        cleanup();
    }
    
    bool initialize() {
        // Initialize EGL
        mDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (mDisplay == EGL_NO_DISPLAY) {
            LOG_ERROR("Failed to get EGL display");
            return false;
        }

        if (!eglInitialize(mDisplay, nullptr, nullptr)) {
            LOG_ERROR("Failed to initialize EGL");
            return false;
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
        if (!eglChooseConfig(mDisplay, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
            LOG_ERROR("Failed to choose EGL config");
            return false;
        }

        // Create surface
        mSurface = eglCreateWindowSurface(mDisplay, config, mApp->window, nullptr);
        if (mSurface == EGL_NO_SURFACE) {
            LOG_ERROR("Failed to create EGL surface");
            return false;
        }

        // Create context
        const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        mContext = eglCreateContext(mDisplay, config, EGL_NO_CONTEXT, contextAttribs);
        if (mContext == EGL_NO_CONTEXT) {
            LOG_ERROR("Failed to create EGL context");
            return false;
        }

        if (!eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) {
            LOG_ERROR("Failed to make EGL current");
            return false;
        }

        // Create shader program
        if (!mShaderProgram.initialize(Shaders::vertexShaderSource, Shaders::fragmentShaderSource)) {
            LOG_ERROR("Failed to create shader program");
            return false;
        }

        // Create triangle mesh
        if (!mTriangle.initialize()) {
            LOG_ERROR("Failed to initialize triangle mesh");
            return false;
        }

        LOG_INFO("Renderer initialized successfully");
        return true;
    }
    
    void drawFrame() {
        if (mDisplay == EGL_NO_DISPLAY) {
            return;
        }
        
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        mShaderProgram.use();
        mTriangle.draw();

        eglSwapBuffers(mDisplay, mSurface);
    }
    
    void cleanup() {
        // Cleanup mesh resources
        mTriangle.cleanup();
        
        // Cleanup EGL
        if (mDisplay != EGL_NO_DISPLAY) {
            eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            
            if (mContext != EGL_NO_CONTEXT) {
                eglDestroyContext(mDisplay, mContext);
                mContext = EGL_NO_CONTEXT;
            }
            
            if (mSurface != EGL_NO_SURFACE) {
                eglDestroySurface(mDisplay, mSurface);
                mSurface = EGL_NO_SURFACE;
            }
            
            eglTerminate(mDisplay);
            mDisplay = EGL_NO_DISPLAY;
        }

        LOG_INFO("Renderer cleaned up");
    }
    
    bool isInitialized() const {
        return mDisplay != EGL_NO_DISPLAY;
    }
};

// Main application class
class NativeApp {
private:
    android_app* mApp = nullptr;
    std::unique_ptr<EGLRenderer> mRenderer;
    
    static void handleAppCommand(android_app* app, int32_t cmd) {
        auto* nativeApp = static_cast<NativeApp*>(app->userData);
        nativeApp->onAppCmd(cmd);
    }
    
    void onAppCmd(int32_t cmd) {
        switch (cmd) {
            case APP_CMD_INIT_WINDOW:
                if (mApp->window != nullptr) {
                    mRenderer = std::make_unique<EGLRenderer>(mApp);
                    mRenderer->initialize();
                }
                break;
                
            case APP_CMD_TERM_WINDOW:
                if (mRenderer) {
                    mRenderer->cleanup();
                    mRenderer.reset();
                }
                break;
        }
    }
    
public:
    NativeApp(android_app* app) : mApp(app) {
        mApp->userData = this;
        mApp->onAppCmd = handleAppCommand;
    }
    
    void run() {
        while (!mApp->destroyRequested) {
            // Process events
            int events;
            android_poll_source* source;
            while (ALooper_pollOnce(0, nullptr, &events, (void**)&source) >= 0) {
                if (source) {
                    source->process(mApp, source);
                }
            }

            // Render if initialized
            if (mRenderer && mRenderer->isInitialized()) {
                mRenderer->drawFrame();
            }
        }
    }
};

void android_main(android_app* app) {
    NativeApp nativeApp(app);
    nativeApp.run();
}