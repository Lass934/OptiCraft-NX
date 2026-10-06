#include "switch/render/SwitchLegacyRenderer.h"
#include "switch/SwitchRuntimeDebug.h"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace SwitchLegacyRenderer
{
namespace
{
using Matrix = std::array<float, 16>;

Matrix identity()
{
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

Matrix multiply(const Matrix &a, const Matrix &b)
{
    Matrix result{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return result;
}

std::vector<Matrix> g_modelView{identity()};
std::vector<Matrix> g_projection{identity()};
// Fixed-function GL keeps one texture matrix stack per texture unit. The
// lightmap pass loads scale(1/256)+translate(8) into unit 1's matrix; with a
// single shared stack that transform landed on unit 0 too and squeezed every
// terrain/entity UV into one texel of the atlas.
std::array<std::vector<Matrix>, 2> g_texture{
    std::vector<Matrix>{identity()}, std::vector<Matrix>{identity()}};
int g_activeTextureUnit = 0;
// GL_TEXTURE_2D enable per unit. Unit 0 texturing still follows the mesh's own
// texture coordinates; unit 1 is the lightmap and only applies while enabled.
std::array<bool, 2> g_textureEnabled{true, false};
std::array<float, 2> g_lightCoord{240.0f, 240.0f};

struct FogState
{
    bool enabled = false;
    bool radial = false;
    int mode = 2; // linear
    float start = 0.0f;
    float end = 1.0f;
    float density = 1.0f;
    std::array<float, 3> color{0.0f, 0.0f, 0.0f};
} g_fog;
std::vector<Matrix> *g_current = &g_modelView;
std::array<float, 4> g_color{1, 1, 1, 1};
bool g_alphaTest = false;
float g_alphaReference = 0.1f;
RenderCompare g_alphaFunction = RenderCompare::Greater;
GLuint g_program = 0;
GLuint g_vao = 0;
GLuint g_vbo = 0;
// g_vbo is a stream ring: immediate draws append to it and wrap by orphaning
// the storage. Re-specifying the buffer on every draw (glBufferData per
// heart, glyph or particle) made the driver allocate fresh storage each time.
constexpr GLsizeiptr kStreamBytes = 4 * 1024 * 1024;
GLsizeiptr g_streamOffset = kStreamBytes; // first draw allocates

// Looked up once after linking. glGetUniformLocation is a string lookup in the
// driver; doing ten of them per draw call was a fixed cost on every chunk,
// entity part and GUI quad.
struct UniformLocations
{
    GLint modelView = -1, projection = -1, textureMatrix = -1, constantColor = -1;
    GLint hasColor = -1, hasTexture = -1, alphaTest = -1, alphaReference = -1;
    GLint alphaFunction = -1, texture0 = -1;
    GLint lightmapMatrix = -1, constantLightCoord = -1, hasBrightness = -1, lightmap = -1;
    GLint lightmapEnabled = -1, fogRadial = -1, fogEnabled = -1, fogMode = -1;
    GLint fogStart = -1, fogEnd = -1, fogDensity = -1, fogColor = -1;
} g_uniforms;

constexpr const char *kVertexShader = R"glsl(
#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 texCoord;
layout(location=2) in vec4 color;
layout(location=3) in vec2 lightCoord;
uniform mat4 modelView;
uniform mat4 projection;
uniform mat4 textureMatrix;
uniform mat4 lightmapMatrix;
uniform vec4 constantColor;
uniform vec2 constantLightCoord;
uniform bool hasColor;
uniform bool hasBrightness;
uniform bool fogRadial;
out vec2 fragmentTexCoord;
out vec2 fragmentLightCoord;
out vec4 fragmentColor;
out float fragmentFogDistance;
// Overlays (grass side tile 38, block-break cracks) are drawn on exactly the
// same vertices as the face below them and rely on GL_LEQUAL. Without
// `invariant` GLSL does not promise the two draws produce identical depth.
invariant gl_Position;
void main() {
    vec4 eye = modelView * vec4(position, 1.0);
    gl_Position = projection * eye;
    fragmentTexCoord = (textureMatrix * vec4(texCoord, 0.0, 1.0)).xy;
    // Unit 1 in fixed-function terms: the per-vertex block/sky light pair
    // (Tessellator brightness) or, for models, the current multitexcoord,
    // through the lightmap's own texture matrix (1/256 scale, +8 offset).
    vec2 light = hasBrightness ? lightCoord : constantLightCoord;
    fragmentLightCoord = (lightmapMatrix * vec4(light, 0.0, 1.0)).xy;
    fragmentColor = hasColor ? color : constantColor;
    fragmentFogDistance = fogRadial ? length(eye.xyz) : abs(eye.z);
}
)glsl";

constexpr const char *kFragmentShader = R"glsl(
#version 330 core
in vec2 fragmentTexCoord;
in vec2 fragmentLightCoord;
in vec4 fragmentColor;
in float fragmentFogDistance;
uniform sampler2D texture0;
uniform sampler2D lightmap;
uniform bool hasTexture;
uniform bool lightmapEnabled;
uniform bool alphaTest;
uniform float alphaReference;
uniform int alphaFunction;
uniform bool fogEnabled;
uniform int fogMode;      // 0 exp, 1 exp2, 2 linear (RenderFogMode order)
uniform float fogStart;
uniform float fogEnd;
uniform float fogDensity;
uniform vec3 fogColor;
out vec4 outputColor;
void main() {
    outputColor = fragmentColor * (hasTexture ? texture(texture0, fragmentTexCoord) : vec4(1.0));
    if (lightmapEnabled)
        outputColor.rgb *= texture(lightmap, fragmentLightCoord).rgb;
    bool pass = alphaFunction == 0 ? false :
                alphaFunction == 1 ? outputColor.a <  alphaReference :
                alphaFunction == 2 ? outputColor.a == alphaReference :
                alphaFunction == 3 ? outputColor.a <= alphaReference :
                alphaFunction == 4 ? outputColor.a >  alphaReference :
                alphaFunction == 5 ? outputColor.a != alphaReference :
                alphaFunction == 6 ? outputColor.a >= alphaReference : true;
    if (alphaTest && !pass) discard;
    if (fogEnabled) {
        float d = fragmentFogDistance;
        float f = fogMode == 2 ? (fogEnd - d) / max(fogEnd - fogStart, 1.0e-4)
                : fogMode == 1 ? exp(-(fogDensity * d) * (fogDensity * d))
                : exp(-fogDensity * d);
        outputColor.rgb = mix(fogColor, outputColor.rgb, clamp(f, 0.0, 1.0));
    }
}
)glsl";

GLuint compileShader(GLenum type, const char *source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return shader;
    char message[1024]{};
    glGetShaderInfoLog(shader, sizeof(message), nullptr, message);
    std::fprintf(stderr, "Switch GL shader: %s\n", message);
    // stderr is invisible without nxlink; keep the reason on the SD card.
    char note[1100];
    std::snprintf(note, sizeof(note), "shader compile failed: %s", message);
    switchDebugNote(note);
    glDeleteShader(shader);
    return 0;
}

void resetUniformCache(bool programBound);

bool initialize()
{
    if (g_program != 0) return true;
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vertex == 0 || fragment == 0)
    {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return false;
    }
    g_program = glCreateProgram();
    glAttachShader(g_program, vertex);
    glAttachShader(g_program, fragment);
    glLinkProgram(g_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = GL_FALSE;
    glGetProgramiv(g_program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        char message[1024]{};
        glGetProgramInfoLog(g_program, sizeof(message), nullptr, message);
        std::fprintf(stderr, "Switch GL program: %s\n", message);
        char note[1100];
        std::snprintf(note, sizeof(note), "shader link failed: %s", message);
        switchDebugNote(note);
        reset();
        return false;
    }
    g_uniforms.modelView = glGetUniformLocation(g_program, "modelView");
    g_uniforms.projection = glGetUniformLocation(g_program, "projection");
    g_uniforms.textureMatrix = glGetUniformLocation(g_program, "textureMatrix");
    g_uniforms.constantColor = glGetUniformLocation(g_program, "constantColor");
    g_uniforms.hasColor = glGetUniformLocation(g_program, "hasColor");
    g_uniforms.hasTexture = glGetUniformLocation(g_program, "hasTexture");
    g_uniforms.alphaTest = glGetUniformLocation(g_program, "alphaTest");
    g_uniforms.alphaReference = glGetUniformLocation(g_program, "alphaReference");
    g_uniforms.alphaFunction = glGetUniformLocation(g_program, "alphaFunction");
    g_uniforms.texture0 = glGetUniformLocation(g_program, "texture0");
    g_uniforms.lightmapMatrix = glGetUniformLocation(g_program, "lightmapMatrix");
    g_uniforms.constantLightCoord = glGetUniformLocation(g_program, "constantLightCoord");
    g_uniforms.hasBrightness = glGetUniformLocation(g_program, "hasBrightness");
    g_uniforms.lightmap = glGetUniformLocation(g_program, "lightmap");
    g_uniforms.lightmapEnabled = glGetUniformLocation(g_program, "lightmapEnabled");
    g_uniforms.fogRadial = glGetUniformLocation(g_program, "fogRadial");
    g_uniforms.fogEnabled = glGetUniformLocation(g_program, "fogEnabled");
    g_uniforms.fogMode = glGetUniformLocation(g_program, "fogMode");
    g_uniforms.fogStart = glGetUniformLocation(g_program, "fogStart");
    g_uniforms.fogEnd = glGetUniformLocation(g_program, "fogEnd");
    g_uniforms.fogDensity = glGetUniformLocation(g_program, "fogDensity");
    g_uniforms.fogColor = glGetUniformLocation(g_program, "fogColor");
    glUseProgram(g_program);
    glUniform1i(g_uniforms.texture0, 0);
    glUniform1i(g_uniforms.lightmap, 1);
    resetUniformCache(true);
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    return true;
}

// Last value sent for each uniform. The program is the only one this backend
// uses, so its uniform state persists between draws; most draws (a run of
// terrain lists, a row of HUD icons) only change the modelview, so skipping
// unchanged uploads removes ~20 GL calls from nearly every draw.
struct UniformCache
{
    bool valid = false;
    bool programBound = false;
    Matrix modelView{}, projection{}, textureMatrix{}, lightmapMatrix{};
    std::array<float, 4> color{};
    std::array<float, 2> lightCoord{};
    std::array<float, 3> fogColor{};
    int hasColor = -1, hasTexture = -1, hasBrightness = -1, alphaTest = -1, alphaFunction = -1;
    int lightmapEnabled = -1, fogEnabled = -1, fogRadial = -1, fogMode = -1;
    float alphaReference = -1.0f, fogStart = -1.0f, fogEnd = -1.0f, fogDensity = -1.0f;
} g_sent;

void resetUniformCache(bool programBound)
{
    g_sent = UniformCache{};
    g_sent.programBound = programBound;
}

template <typename T>
bool changed(T &cached, const T &value, bool force)
{
    if (!force && cached == value)
        return false;
    cached = value;
    return true;
}

void applyUniforms(bool hasColor, bool hasTexture, bool hasBrightness)
{
    if (!g_sent.programBound)
    {
        glUseProgram(g_program);
        g_sent.programBound = true;
    }
    const bool force = !g_sent.valid;
    g_sent.valid = true;
    if (changed(g_sent.modelView, g_modelView.back(), force))
        glUniformMatrix4fv(g_uniforms.modelView, 1, GL_FALSE, g_sent.modelView.data());
    if (changed(g_sent.projection, g_projection.back(), force))
        glUniformMatrix4fv(g_uniforms.projection, 1, GL_FALSE, g_sent.projection.data());
    // The shader samples unit 0 only, so only unit 0's matrix applies to UVs.
    if (changed(g_sent.textureMatrix, g_texture[0].back(), force))
        glUniformMatrix4fv(g_uniforms.textureMatrix, 1, GL_FALSE, g_sent.textureMatrix.data());
    if (changed(g_sent.color, g_color, force))
        glUniform4fv(g_uniforms.constantColor, 1, g_sent.color.data());
    if (changed(g_sent.hasColor, hasColor ? 1 : 0, force))
        glUniform1i(g_uniforms.hasColor, g_sent.hasColor);
    if (changed(g_sent.hasTexture, hasTexture ? 1 : 0, force))
        glUniform1i(g_uniforms.hasTexture, g_sent.hasTexture);
    if (changed(g_sent.alphaTest, g_alphaTest ? 1 : 0, force))
        glUniform1i(g_uniforms.alphaTest, g_sent.alphaTest);
    if (changed(g_sent.alphaReference, g_alphaReference, force))
        glUniform1f(g_uniforms.alphaReference, g_sent.alphaReference);
    if (changed(g_sent.alphaFunction, static_cast<int>(g_alphaFunction), force))
        glUniform1i(g_uniforms.alphaFunction, g_sent.alphaFunction);
    if (changed(g_sent.lightmapMatrix, g_texture[1].back(), force))
        glUniformMatrix4fv(g_uniforms.lightmapMatrix, 1, GL_FALSE, g_sent.lightmapMatrix.data());
    if (changed(g_sent.lightCoord, g_lightCoord, force))
        glUniform2fv(g_uniforms.constantLightCoord, 1, g_sent.lightCoord.data());
    if (changed(g_sent.hasBrightness, hasBrightness ? 1 : 0, force))
        glUniform1i(g_uniforms.hasBrightness, g_sent.hasBrightness);
    if (changed(g_sent.lightmapEnabled, g_textureEnabled[1] ? 1 : 0, force))
        glUniform1i(g_uniforms.lightmapEnabled, g_sent.lightmapEnabled);
    if (changed(g_sent.fogEnabled, g_fog.enabled ? 1 : 0, force))
        glUniform1i(g_uniforms.fogEnabled, g_sent.fogEnabled);
    if (changed(g_sent.fogRadial, g_fog.radial ? 1 : 0, force))
        glUniform1i(g_uniforms.fogRadial, g_sent.fogRadial);
    if (changed(g_sent.fogMode, g_fog.mode, force))
        glUniform1i(g_uniforms.fogMode, g_sent.fogMode);
    if (changed(g_sent.fogStart, g_fog.start, force))
        glUniform1f(g_uniforms.fogStart, g_sent.fogStart);
    if (changed(g_sent.fogEnd, g_fog.end, force))
        glUniform1f(g_uniforms.fogEnd, g_sent.fogEnd);
    if (changed(g_sent.fogDensity, g_fog.density, force))
        glUniform1f(g_uniforms.fogDensity, g_sent.fogDensity);
    if (changed(g_sent.fogColor, g_fog.color, force))
        glUniform3fv(g_uniforms.fogColor, 1, g_sent.fogColor.data());
}

// Attribute layout of the bound GL_ARRAY_BUFFER, recorded into the bound VAO.
void setAttributes(int stride, bool positionShort, bool hasTexture, int texCoordOffset,
                   bool hasColor, int colorOffset, bool hasBrightness, int brightnessOffset)
{
    if (hasBrightness)
    {
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_SHORT, GL_FALSE, stride,
                              reinterpret_cast<const void *>(static_cast<uintptr_t>(brightnessOffset)));
    }
    else glDisableVertexAttribArray(3);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, positionShort ? GL_SHORT : GL_FLOAT, GL_FALSE, stride, nullptr);
    if (hasTexture)
    {
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void *>(static_cast<uintptr_t>(texCoordOffset)));
    }
    else glDisableVertexAttribArray(1);
    if (hasColor)
    {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                              reinterpret_cast<const void *>(static_cast<uintptr_t>(colorOffset)));
    }
    else glDisableVertexAttribArray(2);
}

GLenum primitive(RenderPrimitive value)
{
    switch (value)
    {
        case RenderPrimitive::Points: return GL_POINTS;
        case RenderPrimitive::Lines: return GL_LINES;
        case RenderPrimitive::LineLoop: return GL_LINE_LOOP;
        case RenderPrimitive::LineStrip: return GL_LINE_STRIP;
        case RenderPrimitive::Triangles: return GL_TRIANGLES;
        case RenderPrimitive::TriangleStrip: return GL_TRIANGLE_STRIP;
        case RenderPrimitive::TriangleFan: return GL_TRIANGLE_FAN;
        default: return 0;
    }
}

void apply(const Matrix &matrix)
{
    g_current->back() = multiply(g_current->back(), matrix);
}
}

bool draw(const RenderInterleavedMesh &mesh)
{
    const GLenum mode = primitive(mesh.primitive);
    if (!mesh.data || mesh.stride <= 0 || mesh.count <= 0 || mode == 0 || !initialize()) return false;
    const auto *bytes = static_cast<const unsigned char *>(mesh.data) +
                        static_cast<std::size_t>(mesh.first) * mesh.stride;
    applyUniforms(mesh.hasColor, mesh.hasTexture, mesh.hasBrightness);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    const GLsizeiptr size = static_cast<GLsizeiptr>(mesh.count) * mesh.stride;
    GLint first = 0;
    if (size > kStreamBytes)
    {
        // Larger than the ring: one-off storage, and the ring starts over.
        glBufferData(GL_ARRAY_BUFFER, size, bytes, GL_STREAM_DRAW);
        g_streamOffset = kStreamBytes;
    }
    else
    {
        // Vertex-aligned so the draw can start at a vertex index.
        GLsizeiptr start = (g_streamOffset + mesh.stride - 1) / mesh.stride * mesh.stride;
        if (start + size > kStreamBytes)
        {
            // Orphan: the driver keeps the old storage alive for queued draws.
            glBufferData(GL_ARRAY_BUFFER, kStreamBytes, nullptr, GL_STREAM_DRAW);
            start = 0;
        }
        // Unsynchronized is safe: no range is written twice before the next
        // orphan, so the GPU can never be reading what this overwrites.
        void *target = glMapBufferRange(GL_ARRAY_BUFFER, start, size,
            GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
        if (target != nullptr)
        {
            std::memcpy(target, bytes, static_cast<std::size_t>(size));
            glUnmapBuffer(GL_ARRAY_BUFFER);
        }
        else
        {
            glBufferSubData(GL_ARRAY_BUFFER, start, size, bytes);
        }
        first = static_cast<GLint>(start / mesh.stride);
        g_streamOffset = start + size;
    }
    setAttributes(mesh.stride, mesh.positionShort, mesh.hasTexture, mesh.texCoordOffset,
                  mesh.hasColor, mesh.colorOffset, mesh.hasBrightness, mesh.brightnessOffset);
    glDrawArrays(mode, first, mesh.count);
    glBindVertexArray(0);
    // No glGetError here: on this driver it can wait for the GPU, once per draw.
    return true;
}

bool createRetained(const RenderCapturedMesh &mesh, RetainedMesh &out)
{
    out = RetainedMesh{};
    const GLenum mode = primitive(mesh.primitive);
    if (mesh.empty() || mesh.stride <= 0 || mesh.vertexCount <= 0 || mode == 0 || !initialize())
        return false;
    glGenVertexArrays(1, &out.vao);
    glGenBuffers(1, &out.vbo);
    glBindVertexArray(out.vao);
    glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertexCount) * mesh.stride,
                 mesh.raw.data(), GL_STATIC_DRAW);
    setAttributes(mesh.stride, mesh.positionShort, mesh.hasTexture, mesh.texCoordOffset,
                  mesh.hasColor, mesh.colorOffset, mesh.hasBrightness, mesh.brightnessOffset);
    glBindVertexArray(0);
    out.hasBrightness = mesh.hasBrightness;
    out.mode = mode;
    out.count = mesh.vertexCount;
    out.hasColor = mesh.hasColor;
    out.hasTexture = mesh.hasTexture;
    return true;
}

bool drawRetained(const RetainedMesh &mesh)
{
    if (mesh.vao == 0 || mesh.count <= 0 || !initialize())
        return false;
    applyUniforms(mesh.hasColor, mesh.hasTexture, mesh.hasBrightness);
    glBindVertexArray(mesh.vao);
    glDrawArrays(mesh.mode, 0, mesh.count);
    glBindVertexArray(0);
    return true;
}

void destroyRetained(RetainedMesh &mesh)
{
    if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
    if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
    mesh = RetainedMesh{};
}

void color(float r, float g, float b, float a) { g_color = {r, g, b, a}; }
void alphaTest(bool enabled, RenderCompare function, float reference) { g_alphaTest = enabled; g_alphaFunction = function; g_alphaReference = reference; }
void setTextureEnabled(bool enabled) { g_textureEnabled[g_activeTextureUnit] = enabled; }
void multiTexCoord(int unit, float u, float v)
{
    if (unit == 1)
        g_lightCoord = {u, v};
}
void fogEnabled(bool enabled) { g_fog.enabled = enabled; }
void fogMode(int mode) { g_fog.mode = mode; }
void fogRadial(bool radial) { g_fog.radial = radial; }
void fogStart(float value) { g_fog.start = value; }
void fogEnd(float value) { g_fog.end = value; }
void fogDensity(float value) { g_fog.density = value; }
void fogColor(const float *rgba)
{
    if (rgba != nullptr)
        g_fog.color = {rgba[0], rgba[1], rgba[2]};
}
void activeTextureUnit(int unit)
{
    const int selected = std::clamp(unit, 0, static_cast<int>(g_texture.size()) - 1);
    if (g_current == &g_texture[g_activeTextureUnit])
        g_current = &g_texture[selected];
    g_activeTextureUnit = selected;
}
void matrixMode(RenderMatrixMode mode) { g_current = mode == RenderMatrixMode::Projection ? &g_projection : mode == RenderMatrixMode::Texture ? &g_texture[g_activeTextureUnit] : &g_modelView; }
void loadIdentity() { g_current->back() = identity(); }
void loadMatrix(const float *values)
{
    if (values != nullptr)
        std::copy(values, values + 16, g_current->back().begin());
}
void multiplyMatrix(const float *values)
{
    if (values == nullptr) return;
    Matrix matrix{};
    std::copy(values, values + 16, matrix.begin());
    apply(matrix);
}
void pushMatrix() { g_current->push_back(g_current->back()); }
void popMatrix() { if (g_current->size() > 1) g_current->pop_back(); }

void translate(float x, float y, float z)
{
    Matrix matrix = identity(); matrix[12] = x; matrix[13] = y; matrix[14] = z; apply(matrix);
}
void scale(float x, float y, float z)
{
    Matrix matrix = identity(); matrix[0] = x; matrix[5] = y; matrix[10] = z; apply(matrix);
}
void rotate(float degrees, float x, float y, float z)
{
    const float length = std::sqrt(x*x + y*y + z*z); if (length == 0) return;
    x /= length; y /= length; z /= length;
    const float radians = degrees * 0.01745329251994329577f;
    const float c = std::cos(radians), s = std::sin(radians), t = 1-c;
    Matrix m = {t*x*x+c,t*x*y+s*z,t*x*z-s*y,0, t*x*y-s*z,t*y*y+c,t*y*z+s*x,0,
                t*x*z+s*y,t*y*z-s*x,t*z*z+c,0, 0,0,0,1}; apply(m);
}
void frustum(double l,double r,double b,double t,double n,double f)
{
    Matrix m{}; m[0]=2*n/(r-l);m[5]=2*n/(t-b);m[8]=(r+l)/(r-l);m[9]=(t+b)/(t-b);
    m[10]=-(f+n)/(f-n);m[11]=-1;m[14]=-(2*f*n)/(f-n);apply(m);
}
void ortho(double l,double r,double b,double t,double n,double f)
{
    Matrix m=identity();m[0]=2/(r-l);m[5]=2/(t-b);m[10]=-2/(f-n);
    m[12]=-(r+l)/(r-l);m[13]=-(t+b)/(t-b);m[14]=-(f+n)/(f-n);apply(m);
}
void getMatrix(RenderMatrixQuery query, float *values)
{
    const Matrix &m=query==RenderMatrixQuery::Projection?g_projection.back():query==RenderMatrixQuery::Texture?g_texture[g_activeTextureUnit].back():g_modelView.back();
    std::memcpy(values,m.data(),sizeof(Matrix));
}
std::string describeState()
{
    char text[256];
    const Matrix &t = g_texture[0].back();
    std::snprintf(text, sizeof(text),
        "program=%u unit=%d tex0=[%.4g %.4g %.4g %.4g] color=%.2f,%.2f,%.2f,%.2f alphaTest=%d func=%d ref=%.2f",
        g_program, g_activeTextureUnit, t[0], t[5], t[12], t[13],
        g_color[0], g_color[1], g_color[2], g_color[3],
        g_alphaTest ? 1 : 0, static_cast<int>(g_alphaFunction), g_alphaReference);
    return text;
}
void reset()
{
    g_sent = UniformCache{};
    if (g_vbo) glDeleteBuffers(1,&g_vbo); if (g_vao) glDeleteVertexArrays(1,&g_vao); if (g_program) glDeleteProgram(g_program);
    g_vbo=0;g_vao=0;g_program=0;
    g_streamOffset = kStreamBytes;
}
}
