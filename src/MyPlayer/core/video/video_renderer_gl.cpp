

#include "video_renderer_gl.h"

#include <QDebug>
#include <cstring>

#include "anime4k_render_stage.h"
#include "cuda_interop_manager.h"
#include "video_frame_bridge.h"

extern "C" {
#include <libavutil/pixfmt.h>
}

void ColorPipeline::GetColorParams(int colorspace, int colorRange, bool isHdr,
    float matOut[9], float offsetOut[3], float& hdrPeak)
{
    const bool limited = (colorRange != AVCOL_RANGE_JPEG);
    if (limited)
    {
        offsetOut[0] = -16.0f / 255.0f;
        offsetOut[1] = -128.0f / 255.0f;
        offsetOut[2] = -128.0f / 255.0f;
    }
    else
    {
        offsetOut[0] = 0.0f;
        offsetOut[1] = -0.5f;
        offsetOut[2] = -0.5f;
    }

    if (colorspace == AVCOL_SPC_BT2020_NCL ||
        colorspace == AVCOL_SPC_BT2020_CL || isHdr)
    {
        const float m[] = {
            1.168f,  1.168f,  1.168f,
            0.000f, -0.188f,  2.148f,
            1.684f, -0.652f,  0.000f
        };
        std::memcpy(matOut, m, sizeof(m));
        hdrPeak = 200.0f;
    }
    else if (colorspace == AVCOL_SPC_BT709 ||
        colorspace == AVCOL_SPC_UNSPECIFIED)
    {
        const float m[] = {
            1.164f,  1.164f,  1.164f,
            0.000f, -0.213f,  2.112f,
            1.793f, -0.533f,  0.000f
        };
        std::memcpy(matOut, m, sizeof(m));
        hdrPeak = 1.0f;
    }
    else
    {
        const float m[] = {
            1.164f,  1.164f,  1.164f,
            0.000f, -0.392f,  2.017f,
            1.596f, -0.813f,  0.000f
        };
        std::memcpy(matOut, m, sizeof(m));
        hdrPeak = 1.0f;
    }
}

void ColorPipeline::SetBrightness(float value)
{
    std::lock_guard<std::mutex> lock(mux_);
    values_.brightness = value;
}

void ColorPipeline::SetContrast(float value)
{
    std::lock_guard<std::mutex> lock(mux_);
    values_.contrast = value;
}

void ColorPipeline::SetSaturation(float value)
{
    std::lock_guard<std::mutex> lock(mux_);
    values_.saturation = value;
}

void ColorPipeline::SetBCS(float brightness, float contrast, float saturation)
{
    std::lock_guard<std::mutex> lock(mux_);
    values_.brightness = brightness;
    values_.contrast = contrast;
    values_.saturation = saturation;
}

ColorPipeline::Snapshot ColorPipeline::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(mux_);
    return values_;
}

namespace {

constexpr int kVertexLocation = 3;
constexpr int kTexCoordLocation = 4;

static const char* kVertexShaderSource =
"#version 330 core\n"
"layout(location = 3) in vec2 vertexIn;\n"
"layout(location = 4) in vec2 textureIn;\n"
"out vec2 textureOut;\n"
"void main() {\n"
"    gl_Position = vec4(vertexIn, 0.0, 1.0);\n"
"    textureOut = textureIn;\n"
"}\n";

static const char* kFragmentShaderSource =
"#version 330 core\n"
"in vec2 textureOut;\n"
"out vec4 FragColor;\n"
"uniform sampler2D u_tex_y;\n"
"uniform sampler2D u_tex_u;\n"
"uniform sampler2D u_tex_v;\n"
"uniform int u_semiPlanar;\n"
"uniform mat3 u_colorMat;\n"
"uniform vec3 u_yuvOffset;\n"
"uniform int u_doToneMap;\n"
"uniform float u_hdrPeak;\n"
"uniform float u_brightness;\n"
"uniform float u_contrast;\n"
"uniform float u_saturation;\n"
"vec3 applyBCS(vec3 color, float bright, float cont, float sat) {\n"
"    color += bright;\n"
"    color = (color - 0.5) * cont + 0.5;\n"
"    float lum = dot(color, vec3(0.299, 0.587, 0.114));\n"
"    color = mix(vec3(lum), color, sat);\n"
"    return color;\n"
"}\n"
"vec3 pq_eotf(vec3 x) {\n"
"    const float m1i = 1.0 / 0.1593017578125;\n"
"    const float m2i = 1.0 / 78.84375;\n"
"    const float c1 = 0.8359375;\n"
"    const float c2 = 18.8515625;\n"
"    const float c3 = 18.6875;\n"
"    vec3 xp = pow(max(x, vec3(0.0)), vec3(m2i));\n"
"    vec3 num = max(xp - c1, vec3(0.0));\n"
"    vec3 den = c2 - c3 * xp;\n"
"    return pow(num / den, vec3(m1i));\n"
"}\n"
"void main() {\n"
"    float y = texture(u_tex_y, textureOut).r;\n"
"    float u;\n"
"    float v;\n"
"    if (u_semiPlanar == 1) {\n"
"        vec2 uv = texture(u_tex_u, textureOut).rg;\n"
"        u = uv.r;\n"
"        v = uv.g;\n"
"    } else {\n"
"        u = texture(u_tex_u, textureOut).r;\n"
"        v = texture(u_tex_v, textureOut).r;\n"
"    }\n"
"    vec3 rgb = u_colorMat * (vec3(y, u, v) + u_yuvOffset);\n"
"    rgb = applyBCS(rgb, u_brightness, u_contrast, u_saturation);\n"
"    rgb = clamp(rgb, 0.0, 1.0);\n"
"    if (u_doToneMap == 1) {\n"
"        vec3 lin = pq_eotf(rgb);\n"
"        lin *= u_hdrPeak;\n"
"        rgb = lin / (lin + vec3(1.0));\n"
"        rgb = pow(max(rgb, vec3(0.0)), vec3(1.0 / 2.2));\n"
"    }\n"
"    FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);\n"
"}\n";

}

GLVideoRenderer::GLVideoRenderer()
    : vboVertices_(QOpenGLBuffer::VertexBuffer)
    , vboTexCoords_(QOpenGLBuffer::VertexBuffer)
{
}

bool GLVideoRenderer::Initialize()
{
    if (initialized_)
        return true;
    if (!initializeOpenGLFunctions())
    {
        qCritical("XVideoWidget: Failed to initialize OpenGL 3.3 Core functions");
        return false;
    }

    qDebug() << "OpenGL version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));
    qDebug() << "GLSL version:" << reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    qDebug() << "Renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER));

    if (!program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShaderSource))
    {
        qCritical("Vertex shader compile failed: %s", qPrintable(program_.log()));
        return false;
    }
    if (!program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderSource))
    {
        qCritical("Fragment shader compile failed: %s", qPrintable(program_.log()));
        return false;
    }

    program_.bindAttributeLocation("vertexIn", kVertexLocation);
    program_.bindAttributeLocation("textureIn", kTexCoordLocation);
    if (!program_.link())
    {
        qCritical("Shader program link failed: %s", qPrintable(program_.log()));
        return false;
    }

    if (!vao_.create())
    {
        qCritical("XVideoWidget: Failed to create VAO");
        return false;
    }

    vao_.bind();

    static const GLfloat vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f
    };
    vboVertices_.create();
    vboVertices_.bind();
    vboVertices_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    vboVertices_.allocate(vertices, sizeof(vertices));
    glEnableVertexAttribArray(kVertexLocation);
    glVertexAttribPointer(kVertexLocation, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    vboVertices_.release();

    static const GLfloat texCoords[] = {
        0.0f, 1.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f
    };
    vboTexCoords_.create();
    vboTexCoords_.bind();
    vboTexCoords_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    vboTexCoords_.allocate(texCoords, sizeof(texCoords));
    glEnableVertexAttribArray(kTexCoordLocation);
    glVertexAttribPointer(kTexCoordLocation, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    vboTexCoords_.release();
    vao_.release();

    uTexY_ = program_.uniformLocation("u_tex_y");
    uTexU_ = program_.uniformLocation("u_tex_u");
    uTexV_ = program_.uniformLocation("u_tex_v");
    uSemiPlanar_ = program_.uniformLocation("u_semiPlanar");
    uColorMat_ = program_.uniformLocation("u_colorMat");
    uYuvOffset_ = program_.uniformLocation("u_yuvOffset");
    uDoToneMap_ = program_.uniformLocation("u_doToneMap");
    uHdrPeak_ = program_.uniformLocation("u_hdrPeak");
    uBrightness_ = program_.uniformLocation("u_brightness");
    uContrast_ = program_.uniformLocation("u_contrast");
    uSaturation_ = program_.uniformLocation("u_saturation");

    glGenTextures(3, texs_);
    for (int i = 0; i < 3; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, texs_[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    program_.release();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    initialized_ = true;
    return true;
}

void GLVideoRenderer::Cleanup()
{
    if (texs_[0] != 0)
    {
        glDeleteTextures(3, texs_);
        texs_[0] = texs_[1] = texs_[2] = 0;
    }
    vao_.destroy();
    vboVertices_.destroy();
    vboTexCoords_.destroy();
    program_.removeAllShaders();
    initialized_ = false;
}

void GLVideoRenderer::Resize(int width, int height)
{
    glViewport(0, 0, width, height);
}

QOpenGLFunctions_3_3_Core* GLVideoRenderer::Functions()
{
    return this;
}

void GLVideoRenderer::Render(VideoFrameState& state,
    CudaInteropBridge& cudaInterop,
    Anime4KRenderStage& anime4kStage,
    const ColorPipeline& colorPipeline,
    int widgetWidth,
    int widgetHeight,
    qreal devicePixelRatio,
    unsigned int defaultFramebuffer)
{
    const int framebufferWidth = static_cast<int>(widgetWidth * devicePixelRatio);
    const int framebufferHeight = static_cast<int>(widgetHeight * devicePixelRatio);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    const bool a4kActive = anime4kStage.BeginFrame(
        framebufferWidth, framebufferHeight, state.width, state.height, defaultFramebuffer);
    const auto finishFrame = [&]() {
        if (a4kActive)
        {
            anime4kStage.EndFrame(framebufferWidth, framebufferHeight,
                state.width, state.height, defaultFramebuffer);
        }
    };

    if (cudaInterop.HasContext() && state.isInited &&
        (cudaInterop.HasPendingFrame() || cudaInterop.HasValidTextures()))
    {
        if (RenderCuda(state, cudaInterop, colorPipeline))
        {
            finishFrame();
            return;
        }

        if (cudaInterop.TransferPendingFrameToState(state))
        {
            RenderCpu(state, colorPipeline);
            finishFrame();
            return;
        }
    }

    if (!state.isInited || !state.datas[0] || state.curFmt < 0)
    {
        finishFrame();
        return;
    }

    RenderCpu(state, colorPipeline);
    finishFrame();
}

void GLVideoRenderer::RenderWithBoundTextures(const VideoFrameState& state,
    const ColorPipeline& colorPipeline, bool semiPlanar)
{
    if (!initialized_ || !program_.isLinked() || !vao_.isCreated())
        return;

    const auto colorSnapshot = colorPipeline.GetSnapshot();
    float colorMatrix[9];
    float yuvOffset[3];
    float hdrPeak = 1.0f;
    ColorPipeline::GetColorParams(
        state.curColorspace, state.curColorRange, state.curIsHDR,
        colorMatrix, yuvOffset, hdrPeak);

    program_.bind();
    vao_.bind();
    glUniform1i(uSemiPlanar_, semiPlanar ? 1 : 0);
    glUniformMatrix3fv(uColorMat_, 1, GL_FALSE, colorMatrix);
    glUniform3f(uYuvOffset_, yuvOffset[0], yuvOffset[1], yuvOffset[2]);
    glUniform1i(uDoToneMap_, state.curIsHDR ? 1 : 0);
    glUniform1f(uHdrPeak_, hdrPeak);
    glUniform1f(uBrightness_, colorSnapshot.brightness);
    glUniform1f(uContrast_, colorSnapshot.contrast);
    glUniform1f(uSaturation_, colorSnapshot.saturation);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    vao_.release();
    program_.release();
}

bool GLVideoRenderer::RenderCuda(VideoFrameState& state,
    CudaInteropBridge& cudaInterop, const ColorPipeline& colorPipeline)
{
    if (!initialized_)
        return false;

    if (!cudaInterop.EnsureInteropTextures(this, texs_, state))
        return false;
    if (!cudaInterop.UploadPendingFrame(state) || !cudaInterop.HasValidTextures())
        return false;

    program_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texs_[0]);
    glUniform1i(uTexY_, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texs_[1]);
    glUniform1i(uTexU_, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texs_[2]);
    glUniform1i(uTexV_, 2);
    program_.release();

    RenderWithBoundTextures(state, colorPipeline, true);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return true;
}

void GLVideoRenderer::RenderCpu(VideoFrameState& state, const ColorPipeline& colorPipeline)
{
    if (!initialized_)
        return;

    const bool semiPlanar = IsSemiPlanarFormat(state.curFmt);
    const bool bit10 = Is10BitFormat(state.curFmt);
    const GLenum internalY = bit10 ? GL_R16 : GL_R8;
    const GLenum internalUV = bit10 ? GL_RG16 : GL_RG8;
    const GLenum internalU = bit10 ? GL_R16 : GL_R8;
    const GLenum typeValue = bit10 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
    const int uvWidth = state.width / 2;
    const int uvHeight = state.height / 2;

    if (state.texNeedRebuild)
    {
        glDeleteTextures(3, texs_);
        glGenTextures(3, texs_);
        for (int i = 0; i < 3; ++i)
        {
            glBindTexture(GL_TEXTURE_2D, texs_[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        glBindTexture(GL_TEXTURE_2D, texs_[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, internalY,
            state.width, state.height, 0, GL_RED, typeValue, nullptr);

        if (semiPlanar)
        {
            glBindTexture(GL_TEXTURE_2D, texs_[1]);
            glTexImage2D(GL_TEXTURE_2D, 0, internalUV,
                uvWidth, uvHeight, 0, GL_RG, typeValue, nullptr);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, texs_[1]);
            glTexImage2D(GL_TEXTURE_2D, 0, internalU,
                uvWidth, uvHeight, 0, GL_RED, typeValue, nullptr);
            glBindTexture(GL_TEXTURE_2D, texs_[2]);
            glTexImage2D(GL_TEXTURE_2D, 0, internalU,
                uvWidth, uvHeight, 0, GL_RED, typeValue, nullptr);
        }

        state.texNeedRebuild = false;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    program_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texs_[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, state.width, state.height,
        GL_RED, typeValue, state.datas[0]);
    glUniform1i(uTexY_, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texs_[1]);
    if (semiPlanar)
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uvWidth, uvHeight,
            GL_RG, typeValue, state.datas[1]);
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uvWidth, uvHeight,
            GL_RED, typeValue, state.datas[1]);
    }
    glUniform1i(uTexU_, 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texs_[2]);
    if (!semiPlanar && state.datas[2])
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uvWidth, uvHeight,
            GL_RED, typeValue, state.datas[2]);
    }
    glUniform1i(uTexV_, 2);
    program_.release();

    RenderWithBoundTextures(state, colorPipeline, semiPlanar);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}
