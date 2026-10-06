#include "platform/RenderAPI.h"
#include "switch/render/SwitchGraphicsContext.h"
#include "switch/render/SwitchLegacyRenderer.h"
#include "switch/render/SwitchTextureBatch.h"
#include "switch/SwitchRuntimeDebug.h"
#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>
namespace { int viewport[4]={0,0,1280,720}; }
namespace { GLenum capability(RenderCapability c){switch(c){case RenderCapability::CullFace:return GL_CULL_FACE;case RenderCapability::Blend:return GL_BLEND;case RenderCapability::DepthTest:return GL_DEPTH_TEST;case RenderCapability::PolygonOffsetFill:return GL_POLYGON_OFFSET_FILL;default:return 0;}} GLenum compare(RenderCompare c){switch(c){case RenderCompare::Never:return GL_NEVER;case RenderCompare::Less:return GL_LESS;case RenderCompare::Equal:return GL_EQUAL;case RenderCompare::LessEqual:return GL_LEQUAL;case RenderCompare::Greater:return GL_GREATER;case RenderCompare::NotEqual:return GL_NOTEQUAL;case RenderCompare::GreaterEqual:return GL_GEQUAL;case RenderCompare::Always:return GL_ALWAYS;}return GL_LESS;} GLenum blend(RenderBlendFactor f){switch(f){case RenderBlendFactor::Zero:return GL_ZERO;case RenderBlendFactor::One:return GL_ONE;case RenderBlendFactor::SrcColor:return GL_SRC_COLOR;case RenderBlendFactor::OneMinusSrcColor:return GL_ONE_MINUS_SRC_COLOR;case RenderBlendFactor::SrcAlpha:return GL_SRC_ALPHA;case RenderBlendFactor::OneMinusSrcAlpha:return GL_ONE_MINUS_SRC_ALPHA;case RenderBlendFactor::DstAlpha:return GL_DST_ALPHA;case RenderBlendFactor::OneMinusDstAlpha:return GL_ONE_MINUS_DST_ALPHA;case RenderBlendFactor::DstColor:return GL_DST_COLOR;case RenderBlendFactor::OneMinusDstColor:return GL_ONE_MINUS_DST_COLOR;}return GL_ONE;}}
void renderEnable(RenderCapability c){if(c==RenderCapability::AlphaTest){SwitchLegacyRenderer::alphaTest(true);return;}if(c==RenderCapability::Texture2D){SwitchLegacyRenderer::setTextureEnabled(true);return;}if(c==RenderCapability::Fog){SwitchLegacyRenderer::fogEnabled(true);return;}const GLenum value=capability(c);if(value)glEnable(value);} void renderDisable(RenderCapability c){if(c==RenderCapability::AlphaTest){SwitchLegacyRenderer::alphaTest(false);return;}if(c==RenderCapability::Texture2D){SwitchLegacyRenderer::setTextureEnabled(false);return;}if(c==RenderCapability::Fog){SwitchLegacyRenderer::fogEnabled(false);return;}const GLenum value=capability(c);if(value)glDisable(value);} void renderBlendFunc(RenderBlendFactor source,RenderBlendFactor dest){glBlendFunc(blend(source),blend(dest));} void renderDepthMask(bool enabled){glDepthMask(enabled?GL_TRUE:GL_FALSE);} void renderDepthFunc(RenderCompare function){glDepthFunc(compare(function));} void renderAlphaFunc(RenderCompare function,float reference){SwitchLegacyRenderer::alphaTest(true,function,reference);} void renderCullFace(RenderFace face){glCullFace(face==RenderFace::Front?GL_FRONT:face==RenderFace::Back?GL_BACK:GL_FRONT_AND_BACK);} void renderColorMask(bool r,bool g,bool b,bool a){glColorMask(r,g,b,a);} void switchTrackBoundTexture(GLuint texture); void switchRecordListTexture(GLuint texture); void renderBindTexture(int texture){glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(texture));switchTrackBoundTexture(static_cast<GLuint>(texture));switchRecordListTexture(static_cast<GLuint>(texture));}
void switchTrackActiveUnit(int unit);
void renderSetActiveTextureUnit(int unit)
{
    // OpenGlHelper passes GL_TEXTURE0/GL_TEXTURE1 constants, not zero-based
    // indices. Adding GL_TEXTURE0 a second time produces GL_INVALID_ENUM and
    // leaves the wrong texture unit active for terrain and lightmap binding.
    const GLenum textureUnit = unit >= static_cast<int>(GL_TEXTURE0)
        ? static_cast<GLenum>(unit)
        : static_cast<GLenum>(GL_TEXTURE0 + unit);
    glActiveTexture(textureUnit);
    SwitchLegacyRenderer::activeTextureUnit(static_cast<int>(textureUnit - GL_TEXTURE0));
    switchTrackActiveUnit(static_cast<int>(textureUnit - GL_TEXTURE0));
}
void renderSetClientActiveTextureUnit(int){} void renderSetMultiTextureCoord(int unit,float u,float v){const int index=unit>=static_cast<int>(GL_TEXTURE0)?unit-static_cast<int>(GL_TEXTURE0):unit;SwitchLegacyRenderer::multiTexCoord(index,u,v);} void renderSetLightmapColors(const std::uint32_t*,int){} void renderColor4f(float r,float g,float b,float a){SwitchLegacyRenderer::color(r,g,b,a);} void renderColor3f(float r,float g,float b){SwitchLegacyRenderer::color(r,g,b,1.0f);} void renderNormal3f(float,float,float){}
void renderGenerateTextures(int n,int*t){glGenTextures(n,reinterpret_cast<GLuint*>(t));} void switchForgetTextureMirror(GLuint texture); bool switchBatchSubImage(int level,int x,int y,int w,int h,const void*p); void switchMirrorFullImage(int level,int w,int h,const void*p); bool switchTextureBatchActive();
void renderDeleteTextures(int n,const int*t){for(int i=0;i<n;++i)switchForgetTextureMirror(static_cast<GLuint>(t[i]));glDeleteTextures(n,reinterpret_cast<const GLuint*>(t));} void renderTextureSubImageRgba(int level,int x,int y,int w,int h,const void*p){if(switchBatchSubImage(level,x,y,w,h,p))return;glTexSubImage2D(GL_TEXTURE_2D,level,x,y,w,h,GL_RGBA,GL_UNSIGNED_BYTE,p);} void renderTextureImageRgba(int level,int w,int h,const void*p){glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,p);switchMirrorFullImage(level,w,h,p);} void renderTextureParameters(bool blur,bool mipmaps,bool clamp){if(switchTextureBatchActive())return;glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,mipmaps?(blur?GL_LINEAR_MIPMAP_LINEAR:GL_NEAREST_MIPMAP_LINEAR):(blur?GL_LINEAR:GL_NEAREST));glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,blur?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,clamp?GL_CLAMP_TO_EDGE:GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,clamp?GL_CLAMP_TO_EDGE:GL_REPEAT);}
#if PLATFORM_TEXTURE_QUALITY_CONTROLS
void renderApplyTextureQuality(bool,int,bool,int){}
#endif
int renderGetMaxAnisotropy(){return 1;} int renderGetMaxSamples(){return 1;} bool renderTextureBeginUpload(int,int,int,int,bool,bool,bool,bool){return true;} bool renderTextureIsValid(int t){return t>0;} void renderResetResources(){} void renderFogf(RenderFogParameter p,float v){switch(p){case RenderFogParameter::Start:SwitchLegacyRenderer::fogStart(v);break;case RenderFogParameter::End:SwitchLegacyRenderer::fogEnd(v);break;case RenderFogParameter::Density:SwitchLegacyRenderer::fogDensity(v);break;default:break;}}
void renderFogi(RenderFogParameter p,RenderFogMode m){if(p==RenderFogParameter::Mode){if(m!=RenderFogMode::EyeRadial)SwitchLegacyRenderer::fogMode(static_cast<int>(m));}else if(p==RenderFogParameter::DistanceMode)SwitchLegacyRenderer::fogRadial(m==RenderFogMode::EyeRadial);}
void renderFogColor(const float*c){SwitchLegacyRenderer::fogColor(c);} void renderLightfv(int,RenderLightParameter,const float*){} void renderLightModelAmbient(const float*){} void renderColorMaterial(RenderFace,RenderColorMaterialMode){} void renderShadeModel(RenderShadeModel){}
void renderClear(unsigned int mask){GLbitfield bits=0;if(mask&RenderClearMask::Color)bits|=GL_COLOR_BUFFER_BIT;if(mask&RenderClearMask::Depth)bits|=GL_DEPTH_BUFFER_BIT;if(bits)glClear(bits);} void renderFinishGpu(){glFinish();} void renderSubmitFrame(){glFlush();} void renderClearColor(float r,float g,float b,float a){glClearColor(r,g,b,a);} void renderClearDepth(double d){glClearDepth(d);} void renderPolygonOffset(float factor,float units){glPolygonOffset(factor,units);} void renderLineWidth(float width){glLineWidth(width);} void renderViewport(int x,int y,int w,int h){viewport[0]=x;viewport[1]=y;viewport[2]=w;viewport[3]=h;glViewport(x,y,w,h);} void renderGetViewport(int*v){glGetIntegerv(GL_VIEWPORT,v);}
void renderGetMatrix(RenderMatrixQuery q,float*v){SwitchLegacyRenderer::getMatrix(q,v);} const unsigned char* renderGetString(RenderStringQuery query){GLenum name=GL_VENDOR;if(query==RenderStringQuery::Renderer)name=GL_RENDERER;else if(query==RenderStringQuery::Version)name=GL_VERSION;else if(query==RenderStringQuery::Extensions)name=GL_EXTENSIONS;return glGetString(name);} bool renderSupportsFeature(RenderFeature){return false;} unsigned int renderGetError(){return glGetError();} void renderFogHint(RenderHintMode){} void renderMatrixMode(RenderMatrixMode m){SwitchLegacyRenderer::matrixMode(m);} void renderLoadIdentity(){SwitchLegacyRenderer::loadIdentity();} void renderPushMatrix(){SwitchLegacyRenderer::pushMatrix();} void renderPopMatrix(){SwitchLegacyRenderer::popMatrix();} void renderTranslate(float x,float y,float z){SwitchLegacyRenderer::translate(x,y,z);} void renderRotate(float a,float x,float y,float z){SwitchLegacyRenderer::rotate(a,x,y,z);} void renderScale(float x,float y,float z){SwitchLegacyRenderer::scale(x,y,z);} void renderScaleDouble(double x,double y,double z){SwitchLegacyRenderer::scale(x,y,z);} void renderFrustum(double l,double r,double b,double t,double n,double f){SwitchLegacyRenderer::frustum(l,r,b,t,n,f);} void renderOrtho(double l,double r,double b,double t,double n,double f){SwitchLegacyRenderer::ortho(l,r,b,t,n,f);}
bool renderCopyFramebufferToBoundTexture(int x,int y,int w,int h){if(w<=0||h<=0)return false;glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,x,y,w,h);return glGetError()==GL_NO_ERROR;} void renderSetLegacyPresentationGamma(bool){}
namespace
{
// A list is a run of segments. GL display lists record texture binds; this
// emulation used to append every draw to one mesh and execute binds at
// compile time, so geometry recorded after a bind (connected-texture glass and
// bookshelves, drawn from ctm.png) replayed with the terrain atlas and showed
// random tiles. Each bind made while compiling now opens a segment that
// replays with that texture.
struct SwitchListSegment
{
    RenderCapturedMesh mesh;   // CPU staging while compiling; released after upload
    SwitchLegacyRenderer::RetainedMesh gpu;
    GLuint texture = 0;        // 0: whatever is bound when the list is called
};
struct SwitchDisplayList
{
    std::vector<SwitchListSegment> segments;
    int vertexCount = 0;
    std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
std::unordered_map<int, SwitchDisplayList> displayLists;
std::array<float, 16> savedCompileModelView{};
int compilingList = 0;
GLuint compilingTexture = 0;  // texture bound since the list began, 0 if none
}
GLuint switchBoundTexture2D();
void switchRecordListTexture(GLuint texture){if(compilingList)compilingTexture=texture;}
bool renderCaptureInterleaved(const RenderInterleavedMesh&m,RenderCapturedMesh&o,bool append){if(!m.data||m.count<=0||m.stride<=0)return false;if(!append)o.clear();if(!o.empty()&&(o.stride!=m.stride||o.primitive!=m.primitive))return false;o.stride=m.stride;o.primitive=m.primitive;o.positionShort=m.positionShort;o.hasTexture=m.hasTexture;o.texCoordOffset=m.texCoordOffset;o.hasColor=m.hasColor;o.colorOffset=m.colorOffset;o.hasNormals=m.hasNormals;o.normalOffset=m.normalOffset;o.hasBrightness=m.hasBrightness;o.brightnessOffset=m.brightnessOffset;const auto*src=static_cast<const unsigned char*>(m.data)+(std::size_t)m.first*m.stride;const std::size_t bytes=(std::size_t)m.count*m.stride;const std::size_t old=o.raw.size();o.raw.resize(old+(bytes+3)/4);std::memcpy(reinterpret_cast<unsigned char*>(o.raw.data())+old*4,src,bytes);o.vertexCount+=m.count;return true;}
bool renderDrawInterleaved(const RenderInterleavedMesh&m)
{
    if(compilingList)
    {
        SwitchDisplayList& list=displayLists[compilingList];
        SwitchLegacyRenderer::getMatrix(RenderMatrixQuery::ModelView,list.transform.data());
        // Same texture and vertex layout append to the open segment; anything
        // else opens a new one.
        if(list.segments.empty()||list.segments.back().texture!=compilingTexture||
           !renderCaptureInterleaved(m,list.segments.back().mesh,true))
        {
            list.segments.emplace_back();
            list.segments.back().texture=compilingTexture;
            if(!renderCaptureInterleaved(m,list.segments.back().mesh,true))
            {
                list.segments.pop_back();
                return false;
            }
        }
        list.vertexCount+=m.count;
        return true;
    }
    return SwitchLegacyRenderer::draw(m);
} bool renderDrawCaptured(const RenderCapturedMesh&m){if(m.empty())return false;RenderInterleavedMesh view;view.data=m.raw.data();view.stride=m.stride;view.count=m.vertexCount;view.primitive=m.primitive;view.positionShort=m.positionShort;view.hasTexture=m.hasTexture;view.texCoordOffset=m.texCoordOffset;view.hasColor=m.hasColor;view.colorOffset=m.colorOffset;view.hasNormals=m.hasNormals;view.normalOffset=m.normalOffset;view.hasBrightness=m.hasBrightness;view.brightnessOffset=m.brightnessOffset;return renderDrawInterleaved(view);}

// The shared renderer still uses legacy retained handles for sky and chunk
// bookkeeping. Keep a handle namespace until the Switch terrain renderer owns
// those meshes directly; no desktop GL implementation is pulled into the NRO.
namespace { int nextDisplayList = 1; int nextQuery = 1; }
int renderGenerateDisplayLists(int count){const int first=nextDisplayList;nextDisplayList+=count;return first;}
void renderDeleteDisplayLists(int first,int count)
{
    for(int i=0;i<count;++i)
    {
        const auto found=displayLists.find(first+i);
        if(found==displayLists.end())continue;
        for(SwitchListSegment &segment:found->second.segments)
            SwitchLegacyRenderer::destroyRetained(segment.gpu);
        displayLists.erase(found);
    }
}
void renderBeginDisplayList(int list)
{
    compilingList=list;
    compilingTexture=0;
    SwitchDisplayList &entry=displayLists[list];
    for(SwitchListSegment &segment:entry.segments)
        SwitchLegacyRenderer::destroyRetained(segment.gpu);
    entry=SwitchDisplayList{};
    SwitchLegacyRenderer::getMatrix(RenderMatrixQuery::ModelView,savedCompileModelView.data());
    SwitchLegacyRenderer::matrixMode(RenderMatrixMode::ModelView);
    SwitchLegacyRenderer::loadIdentity();
}
void renderEndDisplayList()
{
    SwitchLegacyRenderer::matrixMode(RenderMatrixMode::ModelView);
    SwitchLegacyRenderer::loadMatrix(savedCompileModelView.data());
    // Upload once; every later call of this list only issues a draw. The CPU
    // copy is dropped afterwards, which also returns that RAM to the heap.
    SwitchDisplayList &entry=displayLists[compilingList];
    for(SwitchListSegment &segment:entry.segments)
        if(SwitchLegacyRenderer::createRetained(segment.mesh,segment.gpu))
            segment.mesh=RenderCapturedMesh{};
    compilingList=0;
    compilingTexture=0;
}
void renderCallDisplayList(int list)
{
    const auto found = displayLists.find(list);
    if (found == displayLists.end())
    {
        switchDebugDisplayListResult(false, false, 0);
        return;
    }
    SwitchLegacyRenderer::pushMatrix();
    SwitchLegacyRenderer::multiplyMatrix(found->second.transform.data());
    const GLuint callerTexture = switchBoundTexture2D();
    GLuint current = callerTexture;
    bool drawn = false;
    for (const SwitchListSegment &segment : found->second.segments)
    {
        const GLuint wanted = segment.texture != 0 ? segment.texture : callerTexture;
        if (wanted != current)
        {
            glBindTexture(GL_TEXTURE_2D, wanted);
            current = wanted;
        }
        drawn |= segment.gpu.vao != 0
            ? SwitchLegacyRenderer::drawRetained(segment.gpu)
            : renderDrawCaptured(segment.mesh);
    }
    if (current != callerTexture)
        glBindTexture(GL_TEXTURE_2D, callerTexture);
    SwitchLegacyRenderer::popMatrix();
    switchDebugDisplayListResult(true, drawn, found->second.vertexCount);
}
void renderCallDisplayLists(int count,const int*lists){for(int i=0;i<count;++i)renderCallDisplayList(lists[i]);}
void renderGenerateOcclusionQueries(int count,int* queries){while(count--)*queries++=nextQuery++;}
void renderBeginOcclusionQuery(int){} void renderEndOcclusionQuery(){}
bool renderOcclusionQueryResultAvailable(int){return true;} unsigned int renderOcclusionQueryResult(int){return 1;}
bool renderReadPixelsRgb(int x,int y,int w,int h,void*p){if(!p||w<=0||h<=0)return false;glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(x,y,w,h,GL_RGB,GL_UNSIGNED_BYTE,p);return glGetError()==GL_NO_ERROR;}

// ---------------------------------------------------------------------------
// Texture sub-image batching (see SwitchTextureBatch.h)
// ---------------------------------------------------------------------------
namespace
{
struct TextureMirror
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels; // RGBA8, level 0
    bool dirty = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // dirty rectangle, exclusive max
};
std::unordered_map<GLuint, TextureMirror> g_mirrors;
std::array<GLuint, 2> g_boundTexture{0, 0};
int g_activeUnit = 0;
bool g_textureBatch = false;

GLuint boundTexture() { return g_boundTexture[static_cast<std::size_t>(g_activeUnit)]; }

void copyRows(TextureMirror &mirror, int x, int y, int w, int h, const void *source)
{
    const auto *src = static_cast<const unsigned char *>(source);
    for (int row = 0; row < h; ++row)
        std::memcpy(&mirror.pixels[(static_cast<std::size_t>(y + row) * mirror.width + x) * 4u],
                    src + static_cast<std::size_t>(row) * w * 4u, static_cast<std::size_t>(w) * 4u);
}

// Mirror of the bound texture, read back from the GPU the first time a batch
// writes to it (one stall per texture for the whole session).
TextureMirror *mirrorForBound()
{
    const GLuint texture = boundTexture();
    if (texture == 0)
        return nullptr;
    auto found = g_mirrors.find(texture);
    if (found != g_mirrors.end())
        return &found->second;
    GLint width = 0, height = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    if (width <= 0 || height <= 0)
        return nullptr;
    TextureMirror &mirror = g_mirrors[texture];
    mirror.width = width;
    mirror.height = height;
    mirror.pixels.resize(static_cast<std::size_t>(width) * height * 4u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, mirror.pixels.data());
    return &mirror;
}
}

void switchTrackBoundTexture(GLuint texture) { g_boundTexture[static_cast<std::size_t>(g_activeUnit)] = texture; }
GLuint switchBoundTexture2D() { return g_boundTexture[static_cast<std::size_t>(g_activeUnit)]; }
void switchTrackActiveUnit(int unit) { g_activeUnit = unit == 1 ? 1 : 0; }
// Parameters are only skipped for atlases already in the batch: they were set
// when the atlas was uploaded and every re-application is the same values.
// A texture first loaded inside the batch still gets its filter set.
bool switchTextureBatchActive() { return g_textureBatch && g_mirrors.count(boundTexture()) != 0; }
void switchForgetTextureMirror(GLuint texture) { g_mirrors.erase(texture); }

void switchMirrorFullImage(int level, int w, int h, const void *p)
{
    // A full re-upload replaces the texture: keep an existing mirror in step.
    if (level != 0)
        return;
    auto found = g_mirrors.find(boundTexture());
    if (found == g_mirrors.end())
        return;
    TextureMirror &mirror = found->second;
    mirror.width = w;
    mirror.height = h;
    mirror.pixels.assign(static_cast<std::size_t>(w) * h * 4u, 0);
    if (p != nullptr)
        std::memcpy(mirror.pixels.data(), p, mirror.pixels.size());
    mirror.dirty = false;
}

bool switchBatchSubImage(int level, int x, int y, int w, int h, const void *p)
{
    if (level != 0 || p == nullptr || w <= 0 || h <= 0)
        return false;
    if (!g_textureBatch)
    {
        // Direct upload; still keep a mirror (if any) consistent with it.
        auto found = g_mirrors.find(boundTexture());
        if (found != g_mirrors.end() && x >= 0 && y >= 0 &&
            x + w <= found->second.width && y + h <= found->second.height)
            copyRows(found->second, x, y, w, h, p);
        return false;
    }
    TextureMirror *mirror = mirrorForBound();
    if (mirror == nullptr || x < 0 || y < 0 || x + w > mirror->width || y + h > mirror->height)
        return false;
    copyRows(*mirror, x, y, w, h, p);
    if (!mirror->dirty)
    {
        mirror->dirty = true;
        mirror->x0 = x; mirror->y0 = y; mirror->x1 = x + w; mirror->y1 = y + h;
    }
    else
    {
        mirror->x0 = std::min(mirror->x0, x);
        mirror->y0 = std::min(mirror->y0, y);
        mirror->x1 = std::max(mirror->x1, x + w);
        mirror->y1 = std::max(mirror->y1, y + h);
    }
    return true;
}

void switchBeginTextureBatch()
{
    g_textureBatch = true;
}

void switchEndTextureBatch()
{
    g_textureBatch = false;
    const GLuint restore = boundTexture();
    bool uploaded = false;
    for (auto &entry : g_mirrors)
    {
        TextureMirror &mirror = entry.second;
        if (!mirror.dirty)
            continue;
        mirror.dirty = false;
        glBindTexture(GL_TEXTURE_2D, entry.first);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, mirror.width);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, mirror.x0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, mirror.y0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, mirror.x0, mirror.y0, mirror.x1 - mirror.x0, mirror.y1 - mirror.y0,
                        GL_RGBA, GL_UNSIGNED_BYTE, mirror.pixels.data());
        uploaded = true;
    }
    if (uploaded)
    {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, restore);
    }
}
