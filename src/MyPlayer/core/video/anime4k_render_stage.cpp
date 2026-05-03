

#include "anime4k_render_stage.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace {

static const char* kBlitVertexShader =
"#version 330 core\n"
"layout(location=0) in vec2 a_pos;\n"
"out vec2 v_uv;\n"
"void main() {\n"
"    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
"    v_uv = a_pos * 0.5 + 0.5;\n"
"}\n";

static const char* kBlitFragmentShader =
"#version 330 core\n"
"in vec2 v_uv;\n"
"out vec4 FragColor;\n"
"uniform sampler2D u_tex;\n"
"void main() {\n"
"    FragColor = vec4(texture(u_tex, v_uv).rgb, 1.0);\n"
"}\n";

QString ResolveAnime4KShaderPath(const QString& appDir, const QString& fileName)
{
    const QString relativePath = "shaders/" + fileName;
    const QString commonRelativePath = "../common/shaders/" + fileName;
    const QStringList candidates = {
        QDir(appDir).filePath(relativePath),
        QDir(appDir).filePath(commonRelativePath),
        QDir(QDir::currentPath()).filePath(relativePath),
        QDir(QDir::currentPath()).filePath(commonRelativePath)
    };

    for (const QString& candidate : candidates)
    {
        const QFileInfo info(QDir::cleanPath(candidate));
        if (info.exists() && info.isFile())
            return info.absoluteFilePath();
    }

    return QDir::cleanPath(candidates.front());
}

}

void Anime4KRenderStage::Initialize(QOpenGLFunctions_3_3_Core* gl, const QString& appDir)
{
    gl_ = gl;
    EnsureBlitResources();

    QStringList shaderFiles;
    shaderFiles << ResolveAnime4KShaderPath(appDir, "Anime4K_Clamp_Highlights.glsl")
                << ResolveAnime4KShaderPath(appDir, "Anime4K_Restore_CNN_M.glsl")
                << ResolveAnime4KShaderPath(appDir, "Anime4K_Upscale_CNN_x2_M.glsl");
    pipeline_.initialize(gl, shaderFiles);
}

void Anime4KRenderStage::Cleanup()
{
    pipeline_.cleanup();
    ReleaseWorkingSet();
    if (blitProgram_)
    {
        gl_->glDeleteProgram(blitProgram_);
        blitProgram_ = 0;
    }
    if (blitVao_)
    {
        gl_->glDeleteVertexArrays(1, &blitVao_);
        blitVao_ = 0;
    }
    if (blitVbo_)
    {
        gl_->glDeleteBuffers(1, &blitVbo_);
        blitVbo_ = 0;
    }
}

void Anime4KRenderStage::ReleaseWorkingSet()
{
    pipeline_.releaseWorkingSet();
    if (inputFbo_)
    {
        gl_->glDeleteFramebuffers(1, &inputFbo_);
        inputFbo_ = 0;
    }
    if (inputTex_)
    {
        gl_->glDeleteTextures(1, &inputTex_);
        inputTex_ = 0;
    }
    inputWidth_ = 0;
    inputHeight_ = 0;
    frameActive_ = false;
}

bool Anime4KRenderStage::BeginFrame(int framebufferWidth, int framebufferHeight,
    int videoWidth, int videoHeight, GLuint defaultFramebuffer)
{
    frameActive_ = !preferLightweightLiveRender_.load(std::memory_order_relaxed) &&
        pipeline_.isEnabled() && pipeline_.isInitialized() &&
        videoWidth > 0 && videoHeight > 0;

    if (frameActive_)
        EnsureInputTarget(videoWidth, videoHeight, frameActive_);

    if (frameActive_ && inputFbo_ && inputTex_)
    {
        gl_->glBindFramebuffer(GL_FRAMEBUFFER, inputFbo_);
        gl_->glViewport(0, 0, videoWidth, videoHeight);
        gl_->glClear(GL_COLOR_BUFFER_BIT);
        return true;
    }

    frameActive_ = false;
    gl_->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebuffer);
    ApplyViewport(framebufferWidth, framebufferHeight, videoWidth, videoHeight);
    gl_->glClear(GL_COLOR_BUFFER_BIT);
    return false;
}

void Anime4KRenderStage::EndFrame(int framebufferWidth, int framebufferHeight,
    int videoWidth, int videoHeight, GLuint defaultFramebuffer)
{
    if (!frameActive_)
        return;

    int outWidth = 0;
    int outHeight = 0;
    GLuint outTexture = pipeline_.process(inputTex_, videoWidth, videoHeight, outWidth, outHeight);
    if (!outTexture || outWidth <= 0 || outHeight <= 0)
    {
        outTexture = inputTex_;
        outWidth = videoWidth;
        outHeight = videoHeight;
    }

    gl_->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebuffer);
    ApplyViewport(framebufferWidth, framebufferHeight, outWidth, outHeight);
    gl_->glClear(GL_COLOR_BUFFER_BIT);

    gl_->glUseProgram(blitProgram_);
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, outTexture);
    gl_->glUniform1i(gl_->glGetUniformLocation(blitProgram_, "u_tex"), 0);
    gl_->glBindVertexArray(blitVao_);
    gl_->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl_->glBindVertexArray(0);
    gl_->glUseProgram(0);

    frameActive_ = false;
}

void Anime4KRenderStage::SetEnabled(bool enabled)
{
    pipeline_.setEnabled(enabled);
}

void Anime4KRenderStage::SetPreferLiveRendering(bool enabled)
{
    preferLightweightLiveRender_.store(enabled, std::memory_order_relaxed);
}

bool Anime4KRenderStage::IsEnabled() const
{
    return pipeline_.isEnabled();
}

QString Anime4KRenderStage::BackendSummary() const
{
    if (!pipeline_.isEnabled())
        return "OFF";
    if (pipeline_.isInitialized())
        return "GPU (OpenGL)";
    return "GPU pending";
}

void Anime4KRenderStage::GetOutputSize(int inputWidth, int inputHeight, int& outWidth, int& outHeight) const
{
    if (pipeline_.isEnabled() && inputWidth > 0 && inputHeight > 0)
    {
        outWidth = inputWidth * 2;
        outHeight = inputHeight * 2;
        return;
    }

    outWidth = inputWidth;
    outHeight = inputHeight;
}

void Anime4KRenderStage::EnsureBlitResources()
{
    if (blitProgram_ != 0)
        return;

    GLuint vertexShader = gl_->glCreateShader(GL_VERTEX_SHADER);
    gl_->glShaderSource(vertexShader, 1, &kBlitVertexShader, nullptr);
    gl_->glCompileShader(vertexShader);

    GLuint fragmentShader = gl_->glCreateShader(GL_FRAGMENT_SHADER);
    gl_->glShaderSource(fragmentShader, 1, &kBlitFragmentShader, nullptr);
    gl_->glCompileShader(fragmentShader);

    blitProgram_ = gl_->glCreateProgram();
    gl_->glAttachShader(blitProgram_, vertexShader);
    gl_->glAttachShader(blitProgram_, fragmentShader);
    gl_->glLinkProgram(blitProgram_);
    gl_->glDeleteShader(vertexShader);
    gl_->glDeleteShader(fragmentShader);

    static const float quad[] = {
        -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,  1.0f
    };

    gl_->glGenVertexArrays(1, &blitVao_);
    gl_->glGenBuffers(1, &blitVbo_);
    gl_->glBindVertexArray(blitVao_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, blitVbo_);
    gl_->glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glBindVertexArray(0);
}

void Anime4KRenderStage::EnsureInputTarget(int videoWidth, int videoHeight, bool& active)
{
    if (inputWidth_ == videoWidth && inputHeight_ == videoHeight &&
        inputFbo_ != 0 && inputTex_ != 0)
    {
        return;
    }

    if (inputFbo_)
    {
        gl_->glDeleteFramebuffers(1, &inputFbo_);
        inputFbo_ = 0;
    }
    if (inputTex_)
    {
        gl_->glDeleteTextures(1, &inputTex_);
        inputTex_ = 0;
    }

    gl_->glGenTextures(1, &inputTex_);
    gl_->glBindTexture(GL_TEXTURE_2D, inputTex_);
    gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, videoWidth, videoHeight, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gl_->glGenFramebuffers(1, &inputFbo_);
    gl_->glBindFramebuffer(GL_FRAMEBUFFER, inputFbo_);
    gl_->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, inputTex_, 0);
    if (gl_->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        gl_->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl_->glDeleteFramebuffers(1, &inputFbo_);
        gl_->glDeleteTextures(1, &inputTex_);
        inputFbo_ = 0;
        inputTex_ = 0;
        inputWidth_ = 0;
        inputHeight_ = 0;
        active = false;
        return;
    }

    inputWidth_ = videoWidth;
    inputHeight_ = videoHeight;
}

void Anime4KRenderStage::ApplyViewport(int framebufferWidth, int framebufferHeight, int videoWidth, int videoHeight)
{
    if (videoWidth <= 0 || videoHeight <= 0)
    {
        gl_->glViewport(0, 0, framebufferWidth, framebufferHeight);
        return;
    }

    const double videoAspect = static_cast<double>(videoWidth) / static_cast<double>(videoHeight);
    const double framebufferAspect = static_cast<double>(framebufferWidth) / static_cast<double>(framebufferHeight);

    int viewportX = 0;
    int viewportY = 0;
    int viewportWidth = framebufferWidth;
    int viewportHeight = framebufferHeight;
    if (videoAspect > framebufferAspect)
    {
        viewportWidth = framebufferWidth;
        viewportHeight = static_cast<int>(framebufferWidth / videoAspect);
        viewportY = (framebufferHeight - viewportHeight) / 2;
    }
    else
    {
        viewportHeight = framebufferHeight;
        viewportWidth = static_cast<int>(framebufferHeight * videoAspect);
        viewportX = (framebufferWidth - viewportWidth) / 2;
    }

    gl_->glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
}
