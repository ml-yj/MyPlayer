#pragma once

#include <QMap>
#include <QOpenGLFunctions_3_3_Core>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

struct Anime4KShaderPass
{
    QString desc;
    QString hook;
    QStringList binds;
    QString save;
    int components = 4;
    int widthExpr = 0;
    int heightExpr = 0;
    QString hookBody;
    GLuint program = 0;
};

struct Anime4KTexInfo
{
    int w = 0;
    int h = 0;
    int comp = 4;
};

constexpr int kA4kMaxOutputWidth = 3840;
constexpr int kA4kMaxOutputHeight = 2160;
constexpr int kA4kMaxOutputPixels = kA4kMaxOutputWidth * kA4kMaxOutputHeight;

class Anime4KGlResources
{
public:
    Anime4KGlResources() = default;
    ~Anime4KGlResources();

    bool Initialize(QOpenGLFunctions_3_3_Core* gl);
    void Cleanup();

    QOpenGLFunctions_3_3_Core* Functions() const { return gl_; }
    GLuint VertexShader() const { return vertShader_; }
    GLuint Framebuffer() const { return fbo_; }
    GLuint QuadVao() const { return quadVao_; }

private:
    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;
    GLuint vertShader_ = 0;
    GLuint fbo_ = 0;
};

class Anime4KTexturePool
{
public:
    void SetFunctions(QOpenGLFunctions_3_3_Core* gl);
    void DeleteAll();
    void RegisterExternal(const QString& name, GLuint tex, int w, int h, int components);
    GLuint GetOrCreate(const QString& name, int w, int h, int components);
    GLuint Texture(const QString& name, GLuint fallback = 0) const;
    Anime4KTexInfo Info(const QString& name, const Anime4KTexInfo& fallback = {}) const;
    bool IsOwned(GLuint tex) const;
    void ReplaceOwned(const QString& name, GLuint oldTex, GLuint newTex, const Anime4KTexInfo& info);

private:
    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    QMap<QString, GLuint> textures_;
    QMap<QString, Anime4KTexInfo> texInfo_;
    QSet<GLuint> ownedTextures_;
};

class Anime4KShaderLibrary
{
public:
    bool LoadAndCompile(QOpenGLFunctions_3_3_Core* gl, GLuint vertexShader,
        const QStringList& shaderFiles, QVector<Anime4KShaderPass>& passes) const;
    void ReleasePrograms(QOpenGLFunctions_3_3_Core* gl, QVector<Anime4KShaderPass>& passes) const;

private:
    static void ParseGlslFile(const QString& content, QVector<Anime4KShaderPass>& passes);
    static QString TranslateToGlsl330(const Anime4KShaderPass& pass);
    static GLuint CompileProgram(QOpenGLFunctions_3_3_Core* gl, GLuint vertexShader,
        const QString& fragSource);
};

class Anime4KPassRunner
{
public:
    GLuint Process(Anime4KGlResources& resources, Anime4KTexturePool& texturePool,
        const QVector<Anime4KShaderPass>& passes, GLuint inputTex, int w, int h,
        int& outW, int& outH, bool& loggedResolutionGuard) const;
};

class Anime4KPipeline
{
public:
    Anime4KPipeline();
    ~Anime4KPipeline();

    bool initialize(QOpenGLFunctions_3_3_Core* gl, const QStringList& shaderFiles);
    void resize(int w, int h);
    GLuint process(GLuint inputTex, int w, int h, int& outW, int& outH);
    void cleanup();
    void releaseWorkingSet();

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    bool isInitialized() const { return m_inited; }

private:
    bool m_enabled = false;
    bool m_inited = false;
    int m_width = 0;
    int m_height = 0;
    bool m_loggedResolutionGuard = false;

    Anime4KGlResources glResources_;
    Anime4KTexturePool texturePool_;
    Anime4KShaderLibrary shaderLibrary_;
    Anime4KPassRunner passRunner_;
    QVector<Anime4KShaderPass> m_passes;
};
