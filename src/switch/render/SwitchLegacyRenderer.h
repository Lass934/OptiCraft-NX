#pragma once

#include "platform/RenderAPI.h"

#include <string>

namespace SwitchLegacyRenderer
{
// Geometry uploaded to the GPU once (GL_STATIC_DRAW) and drawn many times.
struct RetainedMesh
{
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int mode = 0;
    int count = 0;
    bool hasColor = false;
    bool hasTexture = false;
    bool hasBrightness = false;
};

bool draw(const RenderInterleavedMesh &mesh);
bool createRetained(const RenderCapturedMesh &mesh, RetainedMesh &out);
bool drawRetained(const RetainedMesh &mesh);
void destroyRetained(RetainedMesh &mesh);
void color(float red, float green, float blue, float alpha);
void alphaTest(bool enabled, RenderCompare function = RenderCompare::Greater, float reference = 0.1f);
void activeTextureUnit(int unit);
// GL_TEXTURE_2D on the active unit; on unit 1 it switches the lightmap.
void setTextureEnabled(bool enabled);
// Current secondary texture coordinate (lightmap block/sky light for models).
void multiTexCoord(int unit, float u, float v);
void fogEnabled(bool enabled);
void fogMode(int mode); // RenderFogMode order: 0 exp, 1 exp2, 2 linear
void fogRadial(bool radial);
void fogStart(float value);
void fogEnd(float value);
void fogDensity(float value);
void fogColor(const float *rgba);
void matrixMode(RenderMatrixMode mode);
void loadIdentity();
void loadMatrix(const float *values);
void multiplyMatrix(const float *values);
void pushMatrix();
void popMatrix();
void translate(float x, float y, float z);
void rotate(float angleDegrees, float x, float y, float z);
void scale(float x, float y, float z);
void frustum(double left, double right, double bottom, double top, double nearValue, double farValue);
void ortho(double left, double right, double bottom, double top, double nearValue, double farValue);
void getMatrix(RenderMatrixQuery query, float *values);
// One-line summary of shader-side state, for Switch diagnostics.
std::string describeState();
void reset();
}
