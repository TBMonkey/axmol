/****************************************************************************
 Copyright (c) 2018-2019 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2019-present Simdsoft Limited.

 https://axmol.dev/

 SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "axmol/rhi/opengl/ShaderModuleGL.h"

#include "axmol/platform/PlatformMacros.h"
#include "axmol/base/Macros.h"
#include "axmol/tlx/utility.hpp"
#include "axmol/rhi/opengl/OpenGLState.h"
#include "axmol/platform/FileUtils.h"
#include "axmol/tlx/hash.hpp"

#include <cstdlib>
#include <fstream>

namespace ax::rhi::gl
{

ShaderModuleImpl::ShaderModuleImpl(ShaderStage stage, Data& data) : ShaderModule(stage, data)
{
    compileShader();
}

ShaderModuleImpl::~ShaderModuleImpl()
{
    deleteShader();
}

void ShaderModuleImpl::recompileShader()
{
    compileShader();
}

// The driver's info log quotes a line and a type and never names the artifact, and the log line above carries the
// whole source at a size nobody diffs by eye. On failure, also write those exact bytes to a file whose name says
// the stage, the size and a hash — so `diff` against an archive chunk answers "did the driver see these bytes?"
static const char* glFailedStageName(GLenum shaderType)
{
    switch (shaderType)
    {
    case GL_VERTEX_SHADER:
        return "vertex";
    case GL_FRAGMENT_SHADER:
        return "fragment";
    case GL_COMPUTE_SHADER:
        return "compute";
    default:
        return "unknown";
    }
}

static void dumpFailedShaderSource(GLenum shaderType, const uint8_t* code, size_t size)
{
    const auto hash = tlx::hash32_bytes(code, size);

    // ShaderModule has no name, and the backend is handed bytes rather than paths; the byte count identifies
    // one compute kernel from another (1,063 vs 14,254) without plumbing a name through every backend.
    auto dir = FileUtils::getInstance()->getWritablePath();
    if (const char* overrideDir = ::getenv("AX_GL_FAILED_DIR"); overrideDir && *overrideDir)
        dir = overrideDir;
    if (dir.empty())
        dir = ".";
    else if (dir.back() != '/' && dir.back() != '\\')
        dir += '/';

    const auto path = dir + fmt::format("gl_compile_failed_{}_{}_{:08x}.glsl", glFailedStageName(shaderType), size, hash);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out)
        out.write(reinterpret_cast<const char*>(code), static_cast<std::streamsize>(size));

    AXLOGE("axmol:  stage={}, bytes={}, source_hash32={:08x}, source={}", glFailedStageName(shaderType), size, hash,
           out ? path : std::string("<not written>"));
}

void ShaderModuleImpl::compileShader()
{
    GLenum shaderType = _stage == ShaderStage::VERTEX     ? GL_VERTEX_SHADER
                        : _stage == ShaderStage::FRAGMENT ? GL_FRAGMENT_SHADER
                                                          : GL_COMPUTE_SHADER;
    _shader           = glCreateShader(shaderType);
    if (!_shader)
        return;

    const GLchar* sourcePtr = reinterpret_cast<const GLchar*>(_codeSpan.data());
    GLint sourceLen         = static_cast<GLint>(_codeSpan.size());
    glShaderSource(_shader, 1, &sourcePtr, &sourceLen);
    glCompileShader(_shader);

    GLint status = 0;
    glGetShaderiv(_shader, GL_COMPILE_STATUS, &status);

    _compiled = !!status;
    if (!_compiled)
    {
        GLint logLength = 0;
        glGetShaderiv(_shader, GL_INFO_LOG_LENGTH, &logLength);

        if (logLength > 1)
        {
            auto errorLog = tlx::make_unique_for_overwrite<char[]>(static_cast<size_t>(logLength));
            glGetShaderInfoLog(_shader, logLength, nullptr, (GLchar*)errorLog.get());
            AXLOGE("axmol:ERROR: Failed to compile shader, detail: {}\n{}", errorLog.get(),
                   std::string_view{reinterpret_cast<char*>(_codeSpan.data()), _codeSpan.size()});
            dumpFailedShaderSource(shaderType, _codeSpan.data(), _codeSpan.size());
        }
        else
        {
            AXLOGE("axmol:ERROR: Failed to compile shader without errors.");
        }

        deleteShader();
    }
}

void ShaderModuleImpl::deleteShader()
{
    if (_shader)
    {
        glDeleteShader(_shader);
        _shader = 0;
    }
}

}  // namespace ax::rhi::gl
