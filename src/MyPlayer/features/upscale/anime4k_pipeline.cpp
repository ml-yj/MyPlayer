
#include "anime4k_pipeline.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>

namespace
{
static const char* s_vertSrc =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "out vec2 v_texCoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_texCoord = a_pos * 0.5 + 0.5;\n"
    "}\n";

QString ReplaceTexMacros(const QString& code, const QString& name)
{
    QString result = code;

    QString offPat = name + "_texOff(";
    int pos = 0;
    while ((pos = result.indexOf(offPat, pos)) != -1)
    {
        int argStart = pos + offPat.length();
        int depth = 1;
        int i = argStart;
        while (i < result.length() && depth > 0)
        {
            if (result[i] == '(')
                depth++;
            if (result[i] == ')')
                depth--;
            i++;
        }
        const QString expr = result.mid(argStart, i - 1 - argStart);
        const QString repl = QString("texture(u_%1, v_texCoord + (%2) * u_%1_pt)")
            .arg(name, expr);
        result.replace(pos, i - pos, repl);
        pos += repl.length();
    }

    QString texPat = name + "_tex(";
    pos = 0;
    while ((pos = result.indexOf(texPat, pos)) != -1)
    {
        int argStart = pos + texPat.length();
        int depth = 1;
        int i = argStart;
        while (i < result.length() && depth > 0)
        {
            if (result[i] == '(')
                depth++;
            if (result[i] == ')')
                depth--;
            i++;
        }
        const QString expr = result.mid(argStart, i - 1 - argStart);
        const QString repl = QString("texture(u_%1, %2)").arg(name, expr);
        result.replace(pos, i - pos, repl);
        pos += repl.length();
    }

    QRegularExpression rPos(QString("(?<!u_)\\b%1_pos\\b").arg(QRegularExpression::escape(name)));
    result.replace(rPos, "v_texCoord");

    QRegularExpression rPt(QString("(?<!u_)\\b%1_pt\\b").arg(QRegularExpression::escape(name)));
    result.replace(rPt, "u_" + name + "_pt");

    QRegularExpression rSz(QString("(?<!u_)\\b%1_size\\b").arg(QRegularExpression::escape(name)));
    result.replace(rSz, "u_" + name + "_size");

    return result;
}
}

Anime4KGlResources::~Anime4KGlResources()
{
    Cleanup();
}

bool Anime4KGlResources::Initialize(QOpenGLFunctions_3_3_Core* gl)
{
    Cleanup();
    gl_ = gl;
    if (!gl_)
        return false;

    vertShader_ = gl_->glCreateShader(GL_VERTEX_SHADER);
    gl_->glShaderSource(vertShader_, 1, &s_vertSrc, nullptr);
    gl_->glCompileShader(vertShader_);

    GLint ok = 0;
    gl_->glGetShaderiv(vertShader_, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        gl_->glGetShaderInfoLog(vertShader_, sizeof(log), nullptr, log);
        qCritical() << "Anime4K vertex shader error:" << log;
        Cleanup();
        return false;
    }

    static const float quadVerts[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f
    };

    gl_->glGenVertexArrays(1, &quadVao_);
    gl_->glGenBuffers(1, &quadVbo_);
    gl_->glBindVertexArray(quadVao_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    gl_->glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glBindVertexArray(0);

    gl_->glGenFramebuffers(1, &fbo_);
    return true;
}

void Anime4KGlResources::Cleanup()
{
    if (!gl_)
        return;

    if (fbo_)
    {
        gl_->glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    if (quadVbo_)
    {
        gl_->glDeleteBuffers(1, &quadVbo_);
        quadVbo_ = 0;
    }
    if (quadVao_)
    {
        gl_->glDeleteVertexArrays(1, &quadVao_);
        quadVao_ = 0;
    }
    if (vertShader_)
    {
        gl_->glDeleteShader(vertShader_);
        vertShader_ = 0;
    }
    gl_ = nullptr;
}

void Anime4KTexturePool::SetFunctions(QOpenGLFunctions_3_3_Core* gl)
{
    gl_ = gl;
}

void Anime4KTexturePool::DeleteAll()
{
    if (!gl_)
        return;

    for (GLuint tex : ownedTextures_)
        gl_->glDeleteTextures(1, &tex);
    ownedTextures_.clear();
    textures_.clear();
    texInfo_.clear();
}

void Anime4KTexturePool::RegisterExternal(const QString& name, GLuint tex, int w, int h, int components)
{
    if (textures_.contains(name))
    {
        const GLuint previous = textures_.value(name);
        if (ownedTextures_.contains(previous) && previous != tex && gl_)
        {
            gl_->glDeleteTextures(1, &previous);
            ownedTextures_.remove(previous);
        }
    }

    textures_[name] = tex;
    texInfo_[name] = { w, h, components };
}

GLuint Anime4KTexturePool::GetOrCreate(const QString& name, int w, int h, int components)
{
    if (!gl_)
        return 0;

    if (textures_.contains(name))
    {
        const Anime4KTexInfo info = texInfo_.value(name);
        const GLuint oldTex = textures_.value(name);
        const bool oldOwned = ownedTextures_.contains(oldTex);
        if (oldOwned && info.w == w && info.h == h && info.comp == components)
            return oldTex;

        if (oldOwned)
        {
            gl_->glDeleteTextures(1, &oldTex);
            ownedTextures_.remove(oldTex);
        }
    }

    const GLenum internalFmt = (components == 1) ? GL_R16F : GL_RGBA16F;
    const GLenum format = (components == 1) ? GL_RED : GL_RGBA;

    GLuint tex = 0;
    gl_->glGenTextures(1, &tex);
    gl_->glBindTexture(GL_TEXTURE_2D, tex);
    gl_->glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, format, GL_FLOAT, nullptr);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    textures_[name] = tex;
    texInfo_[name] = { w, h, components };
    ownedTextures_.insert(tex);
    return tex;
}

GLuint Anime4KTexturePool::Texture(const QString& name, GLuint fallback) const
{
    return textures_.value(name, fallback);
}

Anime4KTexInfo Anime4KTexturePool::Info(const QString& name, const Anime4KTexInfo& fallback) const
{
    return texInfo_.value(name, fallback);
}

bool Anime4KTexturePool::IsOwned(GLuint tex) const
{
    return ownedTextures_.contains(tex);
}

void Anime4KTexturePool::ReplaceOwned(const QString& name, GLuint oldTex, GLuint newTex,
    const Anime4KTexInfo& info)
{
    if (gl_ && ownedTextures_.contains(oldTex))
    {
        gl_->glDeleteTextures(1, &oldTex);
        ownedTextures_.remove(oldTex);
    }

    textures_[name] = newTex;
    texInfo_[name] = info;
    ownedTextures_.insert(newTex);
}

bool Anime4KShaderLibrary::LoadAndCompile(QOpenGLFunctions_3_3_Core* gl, GLuint vertexShader,
    const QStringList& shaderFiles, QVector<Anime4KShaderPass>& passes) const
{
    passes.clear();

    QVector<Anime4KShaderPass> allPasses;
    for (const QString& filePath : shaderFiles)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            qWarning() << "Anime4K: Cannot open shader file:" << filePath;
            continue;
        }

        const QString content = QTextStream(&file).readAll();
        ParseGlslFile(content, allPasses);
        qDebug() << "Anime4K: Loaded" << filePath;
    }

    if (allPasses.isEmpty())
    {
        qWarning() << "Anime4K: No shader passes loaded";
        return false;
    }

    passes = allPasses;
    int compiled = 0;
    for (int i = 0; i < passes.size(); ++i)
    {
        const QString fragSrc = TranslateToGlsl330(passes[i]);
        passes[i].program = CompileProgram(gl, vertexShader, fragSrc);
        if (passes[i].program != 0)
        {
            compiled++;
            qDebug() << "  Pass" << i << "OK:" << passes[i].desc;
        }
        else
        {
            qWarning() << "  Pass" << i << "FAILED:" << passes[i].desc;
        }
    }

    qDebug() << "Anime4K: Compiled" << compiled << "/" << passes.size() << "passes";
    return compiled > 0;
}

void Anime4KShaderLibrary::ReleasePrograms(QOpenGLFunctions_3_3_Core* gl,
    QVector<Anime4KShaderPass>& passes) const
{
    if (!gl)
        return;

    for (Anime4KShaderPass& pass : passes)
    {
        if (pass.program != 0)
        {
            gl->glDeleteProgram(pass.program);
            pass.program = 0;
        }
    }
}

void Anime4KShaderLibrary::ParseGlslFile(const QString& content, QVector<Anime4KShaderPass>& passes)
{
    QVector<int> descPositions;
    int searchPos = 0;
    while (true)
    {
        const int idx = content.indexOf("//!DESC", searchPos);
        if (idx == -1)
            break;
        descPositions.append(idx);
        searchPos = idx + 7;
    }

    for (int si = 0; si < descPositions.size(); ++si)
    {
        const int start = descPositions[si];
        const int end = (si + 1 < descPositions.size()) ? descPositions[si + 1] : content.length();
        const QString section = content.mid(start, end - start);

        Anime4KShaderPass pass;
        const QStringList lines = section.split('\n');

        int codeStart = -1;
        for (int i = 0; i < lines.size(); ++i)
        {
            const QString line = lines[i].trimmed();
            if (line.startsWith("//!DESC "))
                pass.desc = line.mid(8).trimmed();
            else if (line.startsWith("//!HOOK "))
                pass.hook = line.mid(8).trimmed();
            else if (line.startsWith("//!BIND "))
                pass.binds.append(line.mid(8).trimmed());
            else if (line.startsWith("//!SAVE "))
                pass.save = line.mid(8).trimmed();
            else if (line.startsWith("//!COMPONENTS "))
                pass.components = line.mid(14).trimmed().toInt();
            else if (line.startsWith("//!WIDTH "))
                pass.widthExpr = line.mid(9).trimmed().contains("2 *") ? 2 : 0;
            else if (line.startsWith("//!HEIGHT "))
                pass.heightExpr = line.mid(10).trimmed().contains("2 *") ? 2 : 0;
            else if (!line.startsWith("//!") && !line.isEmpty() && codeStart < 0)
                codeStart = i;
        }

        if (pass.save.isEmpty())
            pass.save = pass.hook;

        if (codeStart >= 0)
        {
            QStringList codeLines;
            for (int i = codeStart; i < lines.size(); ++i)
            {
                if (!lines[i].trimmed().startsWith("//!"))
                    codeLines.append(lines[i]);
            }
            pass.hookBody = codeLines.join('\n');
        }

        if (!pass.hook.isEmpty() && !pass.hookBody.trimmed().isEmpty())
            passes.append(pass);
    }
}

QString Anime4KShaderLibrary::TranslateToGlsl330(const Anime4KShaderPass& pass)
{
    QString resolvedHook = pass.hook;
    if (resolvedHook == "PREKERNEL")
        resolvedHook = "MAIN";

    QStringList texNames;
    for (const QString& bind : pass.binds)
    {
        QString name = bind;
        if (name == "HOOKED")
            name = resolvedHook;
        if (name == "PREKERNEL")
            name = "MAIN";
        if (!texNames.contains(name))
            texNames.append(name);
    }

    QString frag;
    frag += "#version 330 core\n";
    frag += "in vec2 v_texCoord;\n";
    frag += "out vec4 FragColor;\n";

    for (const QString& name : texNames)
    {
        frag += QString("uniform sampler2D u_%1;\n").arg(name);
        frag += QString("uniform vec2 u_%1_pt;\n").arg(name);
        frag += QString("uniform vec2 u_%1_size;\n").arg(name);
    }
    frag += "\n";

    QString body = pass.hookBody;
    body.replace("HOOKED_texOff(", resolvedHook + "_texOff(");
    body.replace("HOOKED_tex(", resolvedHook + "_tex(");
    body.replace("HOOKED_pos", resolvedHook + "_pos");
    body.replace("HOOKED_pt", resolvedHook + "_pt");
    body.replace("HOOKED_size", resolvedHook + "_size");

    QStringList sortedNames = texNames;
    std::sort(sortedNames.begin(), sortedNames.end(),
        [](const QString& a, const QString& b) { return a.length() > b.length(); });

    for (const QString& name : sortedNames)
        body = ReplaceTexMacros(body, name);

    body.replace(QRegularExpression(R"(vec4\s+hook\s*\(\s*\)\s*\{)"), "void main() {");

    const int hookFuncStart = body.indexOf("void main()");
    if (hookFuncStart >= 0)
    {
        int depth = 0;
        int pos = hookFuncStart;
        while (pos < body.length())
        {
            if (body[pos] == '{')
                depth++;
            else if (body[pos] == '}')
            {
                depth--;
                if (depth == 0)
                    break;
            }
            pos++;
        }
        QString mainFunc = body.mid(hookFuncStart, pos - hookFuncStart + 1);
        mainFunc.replace(QRegularExpression(R"(\breturn\b)"), "FragColor =");
        body = body.left(hookFuncStart) + mainFunc + body.mid(pos + 1);
    }

    frag += body;
    return frag;
}

GLuint Anime4KShaderLibrary::CompileProgram(QOpenGLFunctions_3_3_Core* gl, GLuint vertexShader,
    const QString& fragSource)
{
    GLuint fs = gl->glCreateShader(GL_FRAGMENT_SHADER);
    const QByteArray fsBytes = fragSource.toUtf8();
    const char* fsSrc = fsBytes.constData();
    gl->glShaderSource(fs, 1, &fsSrc, nullptr);
    gl->glCompileShader(fs);

    GLint ok = 0;
    gl->glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        gl->glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
        qWarning() << "Anime4K frag compile error:" << log;
        qWarning() << "--- Source ---\n" << fragSource.left(500);
        gl->glDeleteShader(fs);
        return 0;
    }

    GLuint prog = gl->glCreateProgram();
    gl->glAttachShader(prog, vertexShader);
    gl->glAttachShader(prog, fs);
    gl->glLinkProgram(prog);

    gl->glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        gl->glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        qWarning() << "Anime4K link error:" << log;
        gl->glDeleteShader(fs);
        gl->glDeleteProgram(prog);
        return 0;
    }

    gl->glDeleteShader(fs);
    return prog;
}

GLuint Anime4KPassRunner::Process(Anime4KGlResources& resources, Anime4KTexturePool& texturePool,
    const QVector<Anime4KShaderPass>& passes, GLuint inputTex, int w, int h,
    int& outW, int& outH, bool& loggedResolutionGuard) const
{
    QOpenGLFunctions_3_3_Core* gl = resources.Functions();
    if (!gl || passes.isEmpty())
    {
        outW = w;
        outH = h;
        return inputTex;
    }

    texturePool.RegisterExternal("MAIN", inputTex, w, h, 4);

    GLint prevFBO = 0;
    gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, resources.Framebuffer());

    for (const Anime4KShaderPass& pass : passes)
    {
        if (pass.program == 0)
            continue;

        QString resolvedHook = pass.hook;
        if (resolvedHook == "PREKERNEL")
            resolvedHook = "MAIN";

        QString saveTarget = pass.save;
        if (saveTarget == "PREKERNEL")
            saveTarget = "MAIN";

        const Anime4KTexInfo hookInfo = texturePool.Info(resolvedHook, { w, h, 4 });
        const int baseW = (hookInfo.w > 0) ? hookInfo.w : w;
        const int baseH = (hookInfo.h > 0) ? hookInfo.h : h;
        const int passW = (pass.widthExpr == 2) ? baseW * 2 : baseW;
        const int passH = (pass.heightExpr == 2) ? baseH * 2 : baseH;

        const bool upscalePass = passW > baseW || passH > baseH;
        if (upscalePass &&
            (passW > kA4kMaxOutputWidth ||
             passH > kA4kMaxOutputHeight ||
             (static_cast<long long>(passW) * static_cast<long long>(passH)) > kA4kMaxOutputPixels))
        {
            if (!loggedResolutionGuard)
            {
                qWarning() << "Anime4K: skipping upscale pass to avoid oversized working set:"
                           << pass.desc << passW << "x" << passH;
                loggedResolutionGuard = true;
            }
            continue;
        }

        bool conflict = false;
        for (const QString& bind : pass.binds)
        {
            QString resolved = bind;
            if (resolved == "HOOKED")
                resolved = resolvedHook;
            if (resolved == "PREKERNEL")
                resolved = "MAIN";
            if (resolved == saveTarget)
            {
                conflict = true;
                break;
            }
        }

        GLuint outputTex = 0;
        GLuint conflictOldTex = 0;
        if (conflict && texturePool.Texture(saveTarget, 0) != 0)
        {
            conflictOldTex = texturePool.Texture(saveTarget, 0);

            const GLenum intFmt = (pass.components == 1) ? GL_R16F : GL_RGBA16F;
            const GLenum fmt = (pass.components == 1) ? GL_RED : GL_RGBA;

            gl->glGenTextures(1, &outputTex);
            gl->glBindTexture(GL_TEXTURE_2D, outputTex);
            gl->glTexImage2D(GL_TEXTURE_2D, 0, intFmt, passW, passH, 0, fmt, GL_FLOAT, nullptr);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        else
        {
            outputTex = texturePool.GetOrCreate(saveTarget, passW, passH, pass.components);
        }

        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTex, 0);
        gl->glViewport(0, 0, passW, passH);
        gl->glUseProgram(pass.program);

        QSet<QString> bound;
        int unit = 0;
        for (const QString& bind : pass.binds)
        {
            QString texName = bind;
            if (texName == "HOOKED")
                texName = resolvedHook;
            if (texName == "PREKERNEL")
                texName = "MAIN";
            if (bound.contains(texName))
                continue;
            bound.insert(texName);

            const GLuint texId = texturePool.Texture(texName, 0);
            if (texId == 0)
                continue;

            gl->glActiveTexture(GL_TEXTURE0 + unit);
            gl->glBindTexture(GL_TEXTURE_2D, texId);

            QByteArray uName = ("u_" + texName).toUtf8();
            GLint loc = gl->glGetUniformLocation(pass.program, uName.constData());
            if (loc >= 0)
                gl->glUniform1i(loc, unit);

            const Anime4KTexInfo info = texturePool.Info(texName, { w, h, 4 });
            QByteArray uPt = ("u_" + texName + "_pt").toUtf8();
            loc = gl->glGetUniformLocation(pass.program, uPt.constData());
            if (loc >= 0)
                gl->glUniform2f(loc, 1.0f / info.w, 1.0f / info.h);

            QByteArray uSz = ("u_" + texName + "_size").toUtf8();
            loc = gl->glGetUniformLocation(pass.program, uSz.constData());
            if (loc >= 0)
                gl->glUniform2f(loc, static_cast<float>(info.w), static_cast<float>(info.h));

            unit++;
        }

        gl->glBindVertexArray(resources.QuadVao());
        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        if (conflict && conflictOldTex != 0)
            texturePool.ReplaceOwned(saveTarget, conflictOldTex, outputTex, { passW, passH, pass.components });
    }

    gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
    gl->glBindVertexArray(0);
    gl->glUseProgram(0);

    const Anime4KTexInfo finalInfo = texturePool.Info("MAIN", { w, h, 4 });
    outW = finalInfo.w;
    outH = finalInfo.h;
    return texturePool.Texture("MAIN", inputTex);
}

Anime4KPipeline::Anime4KPipeline() = default;

Anime4KPipeline::~Anime4KPipeline()
{
    cleanup();
}

bool Anime4KPipeline::initialize(QOpenGLFunctions_3_3_Core* gl, const QStringList& shaderFiles)
{
    cleanup();
    if (!glResources_.Initialize(gl))
        return false;

    texturePool_.SetFunctions(glResources_.Functions());
    m_inited = shaderLibrary_.LoadAndCompile(glResources_.Functions(), glResources_.VertexShader(),
        shaderFiles, m_passes);
    return m_inited;
}

void Anime4KPipeline::resize(int w, int h)
{
    if (w == m_width && h == m_height)
        return;
    m_width = w;
    m_height = h;
    m_loggedResolutionGuard = false;
    texturePool_.DeleteAll();
}

void Anime4KPipeline::releaseWorkingSet()
{
    texturePool_.DeleteAll();
    m_width = 0;
    m_height = 0;
    m_loggedResolutionGuard = false;
}

GLuint Anime4KPipeline::process(GLuint inputTex, int w, int h, int& outW, int& outH)
{
    if (!m_inited || m_passes.isEmpty())
    {
        outW = w;
        outH = h;
        return inputTex;
    }

    if (w != m_width || h != m_height)
    {
        texturePool_.DeleteAll();
        m_width = w;
        m_height = h;
        m_loggedResolutionGuard = false;
    }

    return passRunner_.Process(glResources_, texturePool_, m_passes, inputTex, w, h,
        outW, outH, m_loggedResolutionGuard);
}

void Anime4KPipeline::cleanup()
{
    texturePool_.DeleteAll();
    shaderLibrary_.ReleasePrograms(glResources_.Functions(), m_passes);
    m_passes.clear();
    glResources_.Cleanup();
    m_inited = false;
    m_width = 0;
    m_height = 0;
    m_loggedResolutionGuard = false;
}
