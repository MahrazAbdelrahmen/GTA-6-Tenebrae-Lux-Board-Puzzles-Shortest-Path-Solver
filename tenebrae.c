/* Tenebrae Lux puzzle board and raylib interface. */

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "puzzle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#define TILE        1.0f
#define SHADOW_RES  2048
#define SHADOW_SLOT 10
#define MAX_HIST    512

/* Shaders */

static const char *LIT_VS =
"#version 330\n"
"in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal;\n"
"uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matNormal;\n"
"out vec3 fragPosition; out vec2 fragTexCoord; out vec3 fragNormal;\n"
"void main(){\n"
"  fragPosition = vec3(matModel*vec4(vertexPosition,1.0));\n"
"  fragTexCoord = vertexTexCoord;\n"
"  fragNormal = normalize(vec3(matNormal*vec4(vertexNormal,0.0)));\n"
"  gl_Position = mvp*vec4(vertexPosition,1.0);\n"
"}\n";

static const char *LIT_FS =
"#version 330\n"
"in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal;\n"
"uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
"#define NL 3\n"
"uniform vec3 lightPos[NL]; uniform vec3 lightColor[NL];\n"
"uniform vec3 viewPos; uniform vec3 ambient; uniform vec3 emissive;\n"
"uniform vec3 sunDir; uniform vec3 sunColor;\n"
"uniform mat4 lightVP; uniform sampler2D shadowMap; uniform float shadowRes;\n"
"uniform float blindPhase;\n"
"out vec4 finalColor;\n"
"float blinds(vec2 ls){\n"
"  float s = fract(ls.y*24.0 + blindPhase);\n"
"  float open = smoothstep(0.03,0.10,s)*(1.0 - smoothstep(0.50,0.57,s));\n"
"  float win = smoothstep(0.20,0.25,ls.x)*(1.0-smoothstep(0.78,0.83,ls.x))\n"
"            * smoothstep(0.18,0.23,ls.y)*(1.0-smoothstep(0.80,0.85,ls.y));\n"
"  float mull = smoothstep(0.008,0.014,abs(ls.x-0.52));\n"
"  return open*win*mull;\n"
"}\n"
"void main(){\n"
"  vec4 tex = texture(texture0, fragTexCoord)*colDiffuse;\n"
"  vec3 albedo = pow(tex.rgb, vec3(2.2));\n"
"  vec3 n = normalize(fragNormal);\n"
"  vec3 v = normalize(viewPos - fragPosition);\n"
"  vec3 lit = ambient;\n"
"  for (int i = 0; i < NL; i++){\n"
"    vec3 L = lightPos[i] - fragPosition; float d = length(L); L /= d;\n"
"    float att = 1.0/(1.0 + 0.4*d + 0.25*d*d);\n"
"    float diff = max(dot(n, L), 0.0);\n"
"    float spec = pow(max(dot(n, normalize(L + v)), 0.0), 32.0)*0.3;\n"
"    lit += lightColor[i]*(diff + spec*diff)*att;\n"
"  }\n"
"  vec3 L = -sunDir; float ndl = max(dot(n, L), 0.0);\n"
"  if (ndl > 0.0){\n"
"    vec4 lp = lightVP*vec4(fragPosition, 1.0);\n"
"    lp.xyz = lp.xyz/lp.w*0.5 + 0.5;\n"
"    float bias = max(0.00006*(1.0 - ndl), 0.000015);\n"
"    float sh = 0.0; vec2 ts = vec2(1.0/shadowRes);\n"
"    for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++)\n"
"      sh += (lp.z - bias > texture(shadowMap, lp.xy + ts*vec2(x, y)).r) ? 1.0 : 0.0;\n"
"    float vis = (1.0 - sh/9.0)*blinds(lp.xy);\n"
"    float spec = pow(max(dot(n, normalize(L + v)), 0.0), 48.0)*0.5;\n"
"    lit += sunColor*(ndl + spec)*vis;\n"
"  }\n"
"  vec3 c = albedo*lit + emissive;\n"
"  c *= exp(-max(length(fragPosition.xz - vec2(0.0, 0.3)) - 4.2, 0.0)*0.5);\n"   /* fall-off into darkness */
"  c = c/(1.0 + c);\n"
"  c = pow(c, vec3(1.0/2.2));\n"
"  finalColor = vec4(c, tex.a);\n"
"}\n";

static const char *POST_FS =
"#version 330\n"
"in vec2 fragTexCoord; in vec4 fragColor;\n"
"uniform sampler2D texture0; uniform float time; uniform int mode; uniform vec2 texel;\n"
"out vec4 finalColor;\n"
"float hash(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233)))*43758.5453); }\n"
"void main(){\n"
"  vec2 uv = fragTexCoord;\n"
"  vec3 c = texture(texture0, uv).rgb;\n"
"  vec3 b = vec3(0.0);\n"                                       /* cheap bloom */
"  for (int i = -3; i <= 3; i++) for (int j = -3; j <= 3; j++)\n"
"    b += max(texture(texture0, uv + vec2(i, j)*texel*4.0).rgb - 0.45, 0.0);\n"
"  c += b/49.0*2.2;\n"
"  float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
"  vec3 o;\n"
"  if (mode == 0){ l = smoothstep(0.03, 0.80, l); o = vec3(pow(l, 1.2))*vec3(0.97, 0.99, 1.03); }\n"
"  else if (mode == 1){ l = smoothstep(0.02, 0.88, l); o = vec3(l)*vec3(1.10, 0.92, 0.70); }\n"
"  else { o = mix(vec3(l), c, 0.85); }\n"
"  vec2 d = uv - 0.5;\n"
"  o *= 1.0 - smoothstep(0.32, 0.82, length(d*vec2(1.15, 1.6)));\n"
"  float g = hash(gl_FragCoord.xy + fract(time*7.0)*vec2(311.0, 173.0)) - 0.5;\n"
"  o += g*(mode == 2 ? 0.025 : 0.045);\n"
"  finalColor = vec4(o, 1.0);\n"
"}\n";

static Shader lit, post;
static struct { int emissive, viewPos, lightPos, lightColor, ambient, sunDir, sunColor,
                lightVP, shadowMap, shadowRes, blindPhase; } LU;
static struct { int time, mode, texel; } PU;

/* Assets */

static Model mPieceCube, mPyramid, mTile, mFrame, mPlank, mCandle, mDot;
static Texture2D texGlow, texDust, texShadow, texFrameGlow;
static Font fTitle, fType, fTypeB, fMenuTitle;

static Texture2D GenWood(int seed, Color base, float grainScale, bool vertical)
{
    const int W = 256, H = 256;
    Image noise = GenImagePerlinNoise(W, H, seed * 97, seed * 31, 3.0f);
    Image fine  = GenImageWhiteNoise(W, H, 0.5f);
    Color *np = LoadImageColors(noise);
    Color *fp = LoadImageColors(fine);
    Image img = GenImageColor(W, H, BLACK);
    Color *px = (Color *)img.data;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int i = y * W + x;
        int sx = vertical ? x : x / 10, sy = vertical ? y / 10 : y;
        float n  = np[sy * W + sx].r / 255.0f;
        float n2 = np[(y * 7 % H) * W + (x * 7 % W)].r / 255.0f;
        float along = vertical ? (float)x : (float)y;
        float g = powf(sinf(along * 0.11f * grainScale + n * 5.0f) * 0.5f + 0.5f, 6.0f);
        float k = 0.55f + 0.25f * n - 0.30f * g - 0.08f * n2 + (fp[i].r / 255.0f - 0.5f) * 0.10f;
        if (k < 0) k = 0;
        px[i] = (Color){ (unsigned char)fminf(base.r * k, 255), (unsigned char)fminf(base.g * k, 255),
                         (unsigned char)fminf(base.b * k, 255), 255 };
    }
    UnloadImageColors(np); UnloadImageColors(fp);
    UnloadImage(noise); UnloadImage(fine);
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

static Texture2D GenFrameGlow(void)
{
    const int N = 128;
    Image img = GenImageColor(N, N, BLANK);
    Color *px = (Color *)img.data;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
        float d = fminf(fminf(u, 1 - u), fminf(v, 1 - v));       /* distance to edge */
        float a = expf(-fabsf(d - 0.06f) * 60.0f) + 0.10f * (d > 0.06f);
        px[y * N + x] = (Color){ 255, 255, 255, (unsigned char)fminf(a * 255, 255) };
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

static Model MakeModel(Mesh mesh, Texture2D tex)
{
    Model m = LoadModelFromMesh(mesh);
    m.materials[0].shader = lit;
    m.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    return m;
}

static Mesh GenPyramid(float half, float height)
{
    Mesh m = { 0 };
    m.triangleCount = 6;
    m.vertexCount = 18;
    m.vertices  = (float *)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.normals   = (float *)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.texcoords = (float *)MemAlloc(m.vertexCount * 2 * sizeof(float));
    Vector3 apex = { 0, height, 0 };
    Vector3 c[4] = { { -half, 0, -half }, { half, 0, -half }, { half, 0, half }, { -half, 0, half } };
    int v = 0;
    #define PUSH(P, N, U, V_) do { \
        m.vertices[v*3]=P.x; m.vertices[v*3+1]=P.y; m.vertices[v*3+2]=P.z; \
        m.normals[v*3]=N.x; m.normals[v*3+1]=N.y; m.normals[v*3+2]=N.z; \
        m.texcoords[v*2]=U; m.texcoords[v*2+1]=V_; v++; } while (0)
    for (int i = 0; i < 4; i++) {
        Vector3 a = c[i], b = c[(i + 1) % 4];
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(apex, a)));
        Vector3 mid = Vector3Scale(Vector3Add(Vector3Add(a, b), apex), 1.0f / 3.0f);
        mid.y -= height * 0.25f;
        if (Vector3DotProduct(n, mid) >= 0) { PUSH(a, n, 0.0f, 1.0f); PUSH(b, n, 1.0f, 1.0f); PUSH(apex, n, 0.5f, 0.0f); }
        else { n = Vector3Negate(n); PUSH(a, n, 0.0f, 1.0f); PUSH(apex, n, 0.5f, 0.0f); PUSH(b, n, 1.0f, 1.0f); }
    }
    Vector3 down = { 0, -1, 0 };
    PUSH(c[0], down, 0, 0); PUSH(c[1], down, 1, 0); PUSH(c[2], down, 1, 1);
    PUSH(c[0], down, 0, 0); PUSH(c[2], down, 1, 1); PUSH(c[3], down, 0, 1);
    #undef PUSH
    UploadMesh(&m, false);
    return m;
}

static RenderTexture2D LoadShadowmap(int w, int h)
{
    RenderTexture2D t = { 0 };
    t.id = rlLoadFramebuffer();
    t.texture.width = w; t.texture.height = h;
    if (t.id > 0) {
        rlEnableFramebuffer(t.id);
        t.depth.id = rlLoadTextureDepth(w, h, false);
        t.depth.width = w; t.depth.height = h; t.depth.format = 19; t.depth.mipmaps = 1;
        rlFramebufferAttach(t.id, t.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferComplete(t.id);
        rlDisableFramebuffer();
    }
    return t;
}

static Font LoadFontOr(const char *a, const char *b, int size)
{
    Font f = { 0 };
    if (FileExists(a)) f = LoadFontEx(a, size, NULL, 0);
    if (f.texture.id == 0 && b && FileExists(b)) f = LoadFontEx(b, size, NULL, 0);
    if (f.texture.id == 0) return GetFontDefault();
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    return f;
}

/* Scene drawing */

static const Vector3 V0 = { 0, 0, 0 };

static Vector3 CellPos(int i)
{
    return (Vector3){ ((i % GRID) - 1.5f) * TILE, 0.0f, ((i / GRID) - 1.5f) * TILE };
}

static void DrawLit(Model m, Vector3 pos, float yaw, Vector3 scale, Color tint, Vector3 emissive)
{
    SetShaderValue(lit, LU.emissive, &emissive, SHADER_UNIFORM_VEC3);
    DrawModelEx(m, pos, (Vector3){ 0, 1, 0 }, yaw, scale, tint);
}

static void DrawPiece(char p, Vector3 pos, float s, Vector3 glow)
{
    if (p == '-' || s <= 0.001f) return;
    if (p == 'x') {
        static const float size[3] = { 0.80f, 0.62f, 0.44f };
        static const float yaw[3]  = { 4.0f, -7.0f, 11.0f };
        for (int k = 0; k < 3; k++) {
            float h = 0.19f;
            DrawLit(mPieceCube, (Vector3){ pos.x, pos.y + (h * k + h * 0.5f) * s, pos.z }, yaw[k],
                    (Vector3){ size[k] * s, h * s, size[k] * s }, WHITE, glow);
        }
        DrawLit(mDot, (Vector3){ pos.x, pos.y + 0.57f * s, pos.z }, 11.0f,
                (Vector3){ 0.06f * s, 0.012f * s, 0.06f * s }, (Color){ 25, 25, 25, 255 }, V0);
    } else if (p == 't') {
        DrawLit(mPyramid, pos, 45.0f, (Vector3){ s, s, s }, WHITE, glow);
    } else if (p == 'v') {
        DrawLit(mPieceCube, (Vector3){ pos.x, pos.y + 0.045f * s, pos.z }, 0,
                (Vector3){ 0.86f * s, 0.09f * s, 0.86f * s }, (Color){ 225, 220, 215, 255 }, glow);
        DrawLit(mDot, (Vector3){ pos.x, pos.y + 0.09f * s, pos.z }, 0,
                (Vector3){ 0.06f * s, 0.01f * s, 0.06f * s }, (Color){ 30, 30, 30, 255 }, V0);
    }
}

static void FlatQuad(Texture2D tex, Vector3 c, float size, float y, Color col)
{
    float h = size * 0.5f;
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
        rlColor4ub(col.r, col.g, col.b, col.a);
        rlNormal3f(0, 1, 0);
        rlTexCoord2f(0, 0); rlVertex3f(c.x - h, y, c.z - h);
        rlTexCoord2f(0, 1); rlVertex3f(c.x - h, y, c.z + h);
        rlTexCoord2f(1, 1); rlVertex3f(c.x + h, y, c.z + h);
        rlTexCoord2f(1, 0); rlVertex3f(c.x + h, y, c.z - h);
    rlEnd();
    rlSetTexture(0);
}

typedef struct { Vector3 pos; float height; } Candle;
static const Candle candles[3] = {
    { { -3.6f, -0.22f,  1.0f }, 1.3f },
    { {  3.9f, -0.22f, -2.6f }, 1.7f },
    { { -1.7f, -0.22f, -3.3f }, 1.1f },
};

typedef enum { ANIM_NONE, ANIM_SLIDE, ANIM_BREAK } AnimPhase;

typedef struct {
    const Board *show;
    AnimPhase phase;
    float t, time;
    int from, to, selected;
    unsigned victims;
} View;

static void PieceTransform(const View *v, int i, Vector3 *pos, float *scale, Vector3 *glow)
{
    *pos = CellPos(i); *scale = 1.0f; *glow = V0;
    if (v->phase == ANIM_SLIDE && i == v->from) {
        float e = v->t * v->t * (3 - 2 * v->t);
        *pos = Vector3Lerp(CellPos(v->from), CellPos(v->to), e);
        pos->y = sinf(v->t * PI) * 0.06f;
    }
    if (v->phase == ANIM_BREAK && (v->victims & (1u << i))) {
        float t = v->t;
        *scale = 1.0f - t * t;
        pos->y = -0.25f * t;
        *glow = (Vector3){ 1.4f * (1 - t), 1.4f * (1 - t), 1.4f * (1 - t) };
    }
    if (v->phase == ANIM_NONE && i == v->selected) pos->y = 0.08f + 0.03f * sinf(v->time * 4);
}

static void DrawWorld(const View *v, bool shadowPass)
{
    /* pallet planks */
    for (int i = -6; i <= 6; i++) {
        float jitter = sinf(i * 12.9898f) * 0.15f;
        DrawLit(mPlank, (Vector3){ jitter, -0.32f, i * 0.72f }, (float)(i % 3) * 0.6f,
                (Vector3){ 14.0f, 0.2f, 0.62f }, WHITE, V0);
    }
    /* board frame + rim */
    DrawLit(mFrame, (Vector3){ 0, -0.12f, 0 }, 0, (Vector3){ 4.45f, 0.2f, 4.45f }, WHITE, V0);
    float e = 2.2f;
    Color rim = { 150, 140, 130, 255 };
    DrawLit(mFrame, (Vector3){ 0, 0,  e }, 0, (Vector3){ 4.45f, 0.08f, 0.1f }, rim, V0);
    DrawLit(mFrame, (Vector3){ 0, 0, -e }, 0, (Vector3){ 4.45f, 0.08f, 0.1f }, rim, V0);
    DrawLit(mFrame, (Vector3){  e, 0, 0 }, 0, (Vector3){ 0.1f, 0.08f, 4.45f }, rim, V0);
    DrawLit(mFrame, (Vector3){ -e, 0, 0 }, 0, (Vector3){ 0.1f, 0.08f, 4.45f }, rim, V0);
    /* tiles */
    for (int i = 0; i < CELLS; i++) {
        Vector3 p = CellPos(i);
        DrawLit(mTile, (Vector3){ p.x, -0.05f, p.z }, (i * 7 % 4) * 90.0f,
                (Vector3){ 0.97f, 0.1f, 0.97f }, WHITE, V0);
    }
    /* soft contact shadows (candle light has no shadow map) */
    if (!shadowPass) {
        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        for (int i = 0; i < CELLS; i++) {
            char p = v->show->c[i];
            if (p == '-' || p == 'v') continue;
            Vector3 pos; float s; Vector3 g;
            PieceTransform(v, i, &pos, &s, &g);
            FlatQuad(texShadow, pos, 1.2f * s, 0.004f, (Color){ 255, 255, 255, 120 });
        }
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
    }
    /* pieces */
    for (int i = 0; i < CELLS; i++) {
        char p = v->show->c[i];
        if (p == '-') continue;
        Vector3 pos; float s; Vector3 g;
        PieceTransform(v, i, &pos, &s, &g);
        DrawPiece(p, pos, s, g);
    }
    /* candles */
    for (int i = 0; i < 3; i++) {
        Candle c = candles[i];
        DrawLit(mCandle, c.pos, 0, (Vector3){ 1, c.height, 1 }, WHITE, (Vector3){ 0.07f, 0.06f, 0.05f });
        DrawLit(mDot, (Vector3){ c.pos.x, c.pos.y + c.height, c.pos.z }, 0,
                (Vector3){ 0.012f, 0.08f, 0.012f }, (Color){ 20, 20, 20, 255 }, V0);
    }
}

/* Match the shader mask for lit dust. */
static float Smooth(float a, float b, float x) { float t = Clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static float BlindMask(Vector2 ls, float phase)
{
    float s = ls.y * 24.0f + phase; s -= floorf(s);
    float open = Smooth(0.03f, 0.10f, s) * (1 - Smooth(0.50f, 0.57f, s));
    float win = Smooth(0.20f, 0.25f, ls.x) * (1 - Smooth(0.78f, 0.83f, ls.x)) *
                Smooth(0.18f, 0.23f, ls.y) * (1 - Smooth(0.80f, 0.85f, ls.y));
    return open * win * Smooth(0.008f, 0.014f, fabsf(ls.x - 0.52f));
}

/* Particles */

#define MAX_DUST   220
#define MAX_EMBERS 400
typedef struct { Vector3 p; float phase, speed; } Mote;
typedef struct { Vector3 p, v; float life, max, size; } Ember;
static Mote  dust[MAX_DUST];
static Ember embers[MAX_EMBERS];

static void SpawnEmbers(Vector3 at, int n)
{
    for (int k = 0; k < MAX_EMBERS && n > 0; k++) {
        if (embers[k].life > 0) continue;
        float a = GetRandomValue(0, 628) / 100.0f, sp = GetRandomValue(20, 120) / 100.0f;
        embers[k].p = (Vector3){ at.x + GetRandomValue(-30, 30) / 100.0f, at.y + GetRandomValue(0, 40) / 100.0f,
                                 at.z + GetRandomValue(-30, 30) / 100.0f };
        embers[k].v = (Vector3){ cosf(a) * sp, GetRandomValue(80, 220) / 100.0f, sinf(a) * sp };
        embers[k].max = embers[k].life = GetRandomValue(60, 160) / 100.0f;
        embers[k].size = GetRandomValue(4, 10) / 100.0f;
        n--;
    }
}

/* UI helpers */

static const Color INK    = { 249, 243, 229, 255 };
static const Color DIM    = { 212, 202, 187, 255 };
static const Color FAINT  = { 128, 122, 113, 255 };
static const Color AMBER  = { 255, 176, 72, 255 };
static const Color BLOOD  = { 255, 127, 110, 255 };

enum { UI_BODY = 30, UI_LABEL = 36, UI_KEY = 28, UI_TITLE = 58 };

static void Text(Font f, const char *s, float x, float y, float size, Color c)
{
    DrawTextEx(f, s, (Vector2){ roundf(x), roundf(y) }, size, 0, c);
}
static float TextW(Font f, const char *s, float size) { return MeasureTextEx(f, s, size, 0).x; }

typedef enum {
    B_PREV, B_NEXT, B_UNDO, B_RESET, B_HINT, B_SOLVE, B_OPTIONS, TOOL_COUNT,
    B_EDIT, B_INPUT, B_STATS, B_SOLUTION, B_FILM, B_SAVE, B_RELOAD
} Action;

static Rectangle LayoutToolbar(int sw, int sh, Rectangle buttons[TOOL_COUNT])
{
    int cols = sw < 420 ? 3 : sw < 860 ? 4 : TOOL_COUNT;
    int rows = (TOOL_COUNT + cols - 1) / cols;
    float width = fminf(sw - 16, cols * 142 + 12);
    float height = rows * 62 + 12;
    Rectangle bar = { (sw - width) / 2, sh - height - 10, width, height };
    for (int i = 0; i < TOOL_COUNT; i++) {
        int row = i / cols, count = (int)fminf(cols, TOOL_COUNT - row * cols);
        float step = (width - 12) / count;
        buttons[i] = (Rectangle){ bar.x + 8 + (i % cols) * step, bar.y + 7 + row * 62,
                                  step - 4, 58 };
    }
    return bar;
}

static void Button(Rectangle r, const char *label, bool accent, bool enabled)
{
    bool hot = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    Color bg = (Color){ 41, 38, 35, 255 };
    if (hot) bg = (Color){ 77, 68, 54, 255 };
    if (enabled && accent) bg = hot ? (Color){ 255, 201, 121, 255 } : AMBER;
    DrawRectangleRounded(r, 0.20f, 6, bg);
    Color fg = !enabled ? FAINT : accent ? BLACK : INK;
    Text(fTypeB, label, r.x + (r.width - TextW(fTypeB, label, UI_LABEL)) / 2,
         r.y + (r.height - UI_LABEL) / 2, UI_LABEL, fg);
}

static void DrawGlyph(char p, float cx, float cy, float s, Color c)
{
    if (p == 'x') {
        DrawRectangleLinesEx((Rectangle){ cx - s * 0.42f, cy - s * 0.42f, s * 0.84f, s * 0.84f }, 1.5f, c);
        DrawRectangleLinesEx((Rectangle){ cx - s * 0.27f, cy - s * 0.27f, s * 0.54f, s * 0.54f }, 1.5f, c);
        DrawRectangle((int)(cx - s * 0.1f), (int)(cy - s * 0.1f), (int)(s * 0.2f), (int)(s * 0.2f), c);
    } else if (p == 't') {
        DrawTriangleLines((Vector2){ cx, cy - s * 0.42f }, (Vector2){ cx - s * 0.42f, cy + s * 0.38f },
                          (Vector2){ cx + s * 0.42f, cy + s * 0.38f }, c);
        DrawLineV((Vector2){ cx, cy - s * 0.42f }, (Vector2){ cx, cy + s * 0.38f }, Fade(c, 0.5f));
    } else if (p == 'v') {
        DrawRectangleRec((Rectangle){ cx - s * 0.42f, cy - s * 0.12f, s * 0.84f, s * 0.24f }, Fade(c, 0.85f));
    }
}

static void DrawMiniBoard(const Board *b, float x, float y, float cell, unsigned highlight)
{
    for (int i = 0; i < CELLS; i++) {
        float cx = x + (i % GRID) * cell, cy = y + (i / GRID) * cell;
        DrawRectangleRec((Rectangle){ cx + 1, cy + 1, cell - 2, cell - 2 },
                         (highlight & (1u << i)) ? (Color){ 70, 48, 20, 255 } : (Color){ 22, 22, 22, 255 });
        DrawGlyph(b->c[i], cx + cell / 2, cy + cell / 2, cell * 0.8f, INK);
    }
}

static void FormatClock(double s, char *out, int n)
{
    if (s < 0) s = 0;
    int m = (int)(s / 60);
    snprintf(out, n, "%02d:%02d", m, (int)s % 60);
}

/* Game state */

static PuzzleSet set;
static int       cur;
static Board     initial, board;
static Board     hist[MAX_HIST];
static int       histLen;
static int       remain[MAX_HIST + 1];    /* optimal-remaining after k moves (-1 dead end) */
static int       moveCount, par, hintsUsed, undosUsed;
static double    caseStart, caseEnd;
static bool      solved;
static Solution  sol;
static float     titleTimer, stampTimer;

static int       selected = -1;
static bool      hintShown;
static Move      hint;
static Solution  autoSol;
static int       autoIndex = -1;

static AnimPhase phase = ANIM_NONE;
static float     animT;
static int       animFrom, animTo;
static unsigned  animVictims;
static Board     animBefore, animSlid, animAfter;

static char      msg[200];
static float     msgTimer;
static char      puzzlePath[260] = "puzzles.txt";

static void Toast(const char *s, float secs) { snprintf(msg, sizeof msg, "%s", s); msgTimer = secs; }

static void LoadCase(int idx)
{
    if (set.count == 0) return;
    cur = (idx % set.count + set.count) % set.count;
    initial = board = set.items[cur].board;
    histLen = moveCount = hintsUsed = undosUsed = 0;
    sol = Solve(&board);
    par = sol.status == 1 ? sol.length : -1;
    remain[0] = par;
    caseStart = GetTime(); caseEnd = 0;
    solved = false;
    titleTimer = 3.4f; stampTimer = 0;
    selected = -1; hintShown = false; autoIndex = -1;
    phase = ANIM_NONE;
}

static void AddCase(const char *name, const Board *b)
{
    PuzzleSetAdd(&set, name, b, 0);
    LoadCase(set.count - 1);
}

static bool StartMove(int from, int dir)
{
    int to; unsigned v; Board s, o;
    if (!DoMove(&board, from, dir, &s, &o, &to, &v)) return false;
    if (histLen < MAX_HIST) hist[histLen++] = board;
    animBefore = board; animSlid = s; animAfter = o;
    animFrom = from; animTo = to; animVictims = v;
    phase = ANIM_SLIDE; animT = 0;
    selected = -1; hintShown = false;
    if (moveCount < MAX_HIST) moveCount++;
    return true;
}

static void OnMoveCommitted(void)
{
    sol = Solve(&board);
    if (moveCount <= MAX_HIST) remain[moveCount] = sol.status == 1 ? sol.length : -1;
    if (BoardIsEmpty(&board) && !solved) {
        solved = true; caseEnd = GetTime(); stampTimer = 0.001f;
        autoIndex = -1;
    }
}

/* Overlays */

static bool revealSolution, optionsOpen;

static void Panel(Rectangle r)
{
    DrawRectangleRounded(r, 12.0f / fminf(r.width, r.height), 8, (Color){ 10, 10, 10, 215 });
}

enum { STAT_STEP = 38, STAT_RULE = 12 };

static float StatsHeight(void)
{
    int lines = revealSolution && sol.status == 1 && sol.length > 0 ? sol.length : 1;
    return 32 + 8 * STAT_STEP + 2 * STAT_RULE + (lines + (set.warnCount > 0)) * STAT_STEP;
}

static void DrawStats(Rectangle r, double now, float scroll)
{
    Panel(r);
    float x = r.x + 18, y = r.y + 16 - scroll, w = r.width - 36;
    char buf[96], tm[16];
    BeginScissorMode((int)r.x, (int)r.y + 4, (int)r.width, (int)r.height - 8);
    #define ROW(K, V, C) do { Text(fType, K, x, y, UI_BODY, DIM); Text(fTypeB, V, x + w - TextW(fTypeB, V, UI_BODY), y, UI_BODY, C); y += STAT_STEP; } while (0)
    #define RULE() do { DrawRectangle((int)x, (int)y + 4, (int)w, 1, (Color){ 76, 70, 61, 255 }); y += STAT_RULE; } while (0)

    snprintf(buf, sizeof buf, "%d", moveCount);                         ROW("Moves", buf, INK);
    if (par >= 0) snprintf(buf, sizeof buf, "%d", par); else snprintf(buf, sizeof buf, "-");
    ROW("Best possible", buf, INK);
    if (sol.status == 1) snprintf(buf, sizeof buf, "%d", sol.length);
    else snprintf(buf, sizeof buf, sol.status == 0 ? "stuck" : "?");
    ROW("Moves left", buf, sol.status == 0 ? BLOOD : INK);
    FormatClock((solved ? caseEnd : now) - caseStart, tm, sizeof tm);  ROW("Time", tm, INK);
    snprintf(buf, sizeof buf, "%d", hintsUsed);                         ROW("Hints used", buf, INK);
    RULE();
    snprintf(buf, sizeof buf, "%d", sol.explored);                      ROW("States searched", buf, INK);
    snprintf(buf, sizeof buf, "%.1f ms", sol.ms);                       ROW("Search time", buf, INK);
    snprintf(buf, sizeof buf, "%.1f", sol.expanded ? (float)sol.edges / sol.expanded : 0.0f);
    ROW("Avg. branching", buf, INK);
    RULE();

    if (!revealSolution) Text(fType, "V to show solution", x, y, UI_BODY, DIM);
    else if (sol.status != 1) Text(fType, "No solution from here", x, y, UI_BODY, BLOOD);
    else if (sol.length == 0) Text(fType, "Solved", x, y, UI_BODY, INK);
    else {
        Board b = board;
        for (int i = 0; i < sol.length; i++) {
            char mv[64];
            FormatMove(&b, sol.moves[i], mv, sizeof mv);
            snprintf(buf, sizeof buf, "%d. %s", i + 1, mv);
            Text(fType, buf, x, y, UI_BODY, i == 0 ? AMBER : INK);
            y += STAT_STEP;
            DoMove(&b, sol.moves[i].from, sol.moves[i].dir, NULL, &b, NULL, NULL);
        }
    }
    if (set.warnCount) {
        if (!revealSolution || sol.status != 1 || sol.length == 0) y += STAT_STEP;
        snprintf(buf, sizeof buf, "%d file warning%s", set.warnCount, set.warnCount > 1 ? "s" : "");
        Text(fType, buf, x, y, UI_BODY, AMBER);
    }
    EndScissorMode();
    if (StatsHeight() > r.height) {
        float ratio = r.height / StatsHeight();
        DrawRectangle((int)(r.x + r.width - 5), (int)(r.y + scroll * ratio), 3, (int)(r.height * ratio), DIM);
    }
    #undef ROW
    #undef RULE
}

/* board input dialog */
static bool inputOpen;
static char inputBuf[160];
static int  inputLen;
static float inputScroll;

static void InputOpen(void)
{
    inputOpen = true;
    inputScroll = 0;
    char s[CELLS + GRID];
    BoardToString(&board, s);
    snprintf(inputBuf, sizeof inputBuf, "%s", s);
    inputLen = (int)strlen(inputBuf);
    while (GetCharPressed() > 0) { }          /* Discard the opening keystroke. */
}

static void InputUpdateAndDraw(int sw, int sh, bool justOpened)
{
    int ch;
    while ((ch = GetCharPressed()) > 0)
        if (ch >= 32 && ch < 127 && inputLen < (int)sizeof inputBuf - 1) { inputBuf[inputLen++] = (char)ch; inputBuf[inputLen] = 0; }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && inputLen > 0) inputBuf[--inputLen] = 0;
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_V)) {
        const char *clip = GetClipboardText();
        for (; clip && *clip && inputLen < (int)sizeof inputBuf - 1; clip++)
            inputBuf[inputLen++] = (*clip == '\n' || *clip == '\r') ? '/' : *clip;
        inputBuf[inputLen] = 0;
    }

    char name[48] = "Custom";                 /* optional "Name: board" */
    const char *boardText = inputBuf;
    const char *colon = strchr(inputBuf, ':');
    if (colon) {
        int n = (int)(colon - inputBuf);
        if (n > 47) n = 47;
        if (n > 0) { memcpy(name, inputBuf, n); name[n] = 0; }
        boardText = colon + 1;
    }
    Board preview; char err[96];
    int cells = ParseBoardString(boardText, &preview, err, sizeof err);
    bool valid = (cells == CELLS && !err[0]);
    Solution ps = { 0 };
    if (valid) ps = Solve(&preview);

    if (IsKeyPressed(KEY_ESCAPE)) inputOpen = false;
    if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && valid && !justOpened) {
        AddCase(name, &preview);
        inputOpen = false;
        if (ps.status != 1) Toast("Loaded - this board has no solution", 3);
    }

    float width = fminf(740, sw - 16);
    bool narrow = width < 625;
    float height = narrow ? 478 : 410;
    float visible = fminf(height, sh - 16);
    inputScroll = Clamp(inputScroll - GetMouseWheelMove() * 44, 0, height - visible);
    Rectangle r = { (sw - width) / 2, (sh - visible) / 2, width, visible };
    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.70f));
    DrawRectangleRounded(r, 0.05f, 8, (Color){ 30, 28, 25, 255 });
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    float x = r.x + 20, y = r.y + 8 - inputScroll;
    Text(fTitle, "Enter a board", x, y, UI_TITLE, INK);
    y += 72;
    if (narrow) {
        Text(fType, "x block, t pyramid, v plate", x, y, UI_BODY, DIM);
        y += 35;
        Text(fType, "- empty, / next row", x, y, UI_BODY, DIM);
    } else Text(fType, "x block, t pyramid, v plate, - empty, / next row", x, y, UI_BODY, DIM);
    y += 46;

    Rectangle box = { x, y, r.width - 40, 54 };
    DrawRectangleRounded(box, 0.2f, 6, (Color){ 48, 44, 39, 255 });
    DrawRectangleRoundedLines(box, 0.2f, 6, AMBER);
    const char *shown = inputBuf;
    while (*shown && TextW(fType, shown, UI_LABEL) > box.width - 32) shown++;
    Text(fType, shown, box.x + 12, box.y + 8, UI_LABEL, INK);
    if (fmodf((float)GetTime(), 1.0f) < 0.55f)
        DrawRectangle((int)(box.x + 15 + TextW(fType, shown, UI_LABEL)), (int)box.y + 11, 2, 32, AMBER);
    y += 68;

    DrawMiniBoard(&preview, x, y, 32, 0);
    float tx = narrow ? x : x + 152, ty = narrow ? y + 132 : y + 45;
    char buf[96];
    if (valid) {
        if (ps.status == 1) snprintf(buf, sizeof buf, "Solvable in %d moves", ps.length);
        else snprintf(buf, sizeof buf, ps.status == 0 ? "No solution" : "Too large to search");
        Text(fTypeB, buf, tx, ty, UI_BODY, ps.status == 1 ? INK : BLOOD);
    } else Text(fType, err, tx, ty, UI_BODY, cells < 0 || cells > CELLS ? BLOOD : DIM);

    Text(fType, "Enter: load   Esc: cancel   Ctrl+V: paste", x, r.y + height - 45 - inputScroll, UI_KEY, DIM);
    EndScissorMode();
}

static void DrawSolved(int sw, int sh)
{
    if (!solved || stampTimer <= 0) return;
    float a = fminf(stampTimer * 3.0f, 1.0f);
    char sub[64];
    if (moveCount == par) snprintf(sub, sizeof sub, "%d moves (optimal)", moveCount);
    else snprintf(sub, sizeof sub, "%d moves (best %d)", moveCount, par);
    float cy = sh * 0.13f;
    Text(fTitle, "Solved", (sw - TextW(fTitle, "Solved", UI_TITLE)) / 2, cy, UI_TITLE, Fade(INK, a));
    Text(fType, sub, (sw - TextW(fType, sub, UI_BODY)) / 2, cy + 67, UI_BODY, Fade(DIM, a));
}

static const struct { Action action; const char *label, *key; } options[] = {
    { B_FILM, "Colour filter", "N" }, { B_STATS, "Statistics", "Tab" },
    { B_SOLUTION, "Solution steps", "V" }, { B_EDIT, "Edit board", "E" },
    { B_INPUT, "Enter board", "I" }, { B_SAVE, "Save board", "Ctrl+S" },
    { B_RELOAD, "Reload puzzles", "F5" }, { B_OPTIONS, "Back to puzzle", "Esc" }
};
#define OPTION_COUNT ((int)(sizeof options / sizeof options[0]))

enum { OPTION_STEP = 57, OPTION_GAP = 24, OPTION_HEADER = 100,
       OPTIONS_HEIGHT = OPTION_HEADER + OPTION_COUNT * OPTION_STEP + 2 * OPTION_GAP };

static Rectangle OptionsBounds(int sw, int sh)
{
    float margin = Clamp(sw * 0.075f, 20, 144);
    float width = fminf(440, sw - 2 * margin);
    float height = fminf(OPTIONS_HEIGHT, sh - 24);
    return (Rectangle){ margin, (sh - height) / 2, width, height };
}

static Rectangle OptionList(Rectangle menu)
{
    return (Rectangle){ menu.x, menu.y + OPTION_HEADER, menu.width, menu.height - OPTION_HEADER };
}

static Rectangle OptionRow(Rectangle list, int index, float scroll)
{
    float gap = index >= 3 ? OPTION_GAP : 0;
    if (options[index].action == B_OPTIONS) gap += OPTION_GAP;
    return (Rectangle){ list.x, list.y + index * OPTION_STEP + gap - scroll, list.width, OPTION_STEP };
}

static void DrawOptions(Rectangle menu, int focus, float scroll, bool edit, bool stats, int film)
{
    static const char *filters[] = { "Noir", "Sepia", "Colour" };
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int edge = (int)(menu.x + menu.width);
    DrawRectangleGradientH(0, 0, edge, sh, (Color){ 16, 15, 13, 253 }, (Color){ 16, 15, 13, 234 });
    DrawRectangleGradientH(edge, 0, sw - edge, sh, (Color){ 16, 15, 13, 234 }, (Color){ 16, 15, 13, 48 });
    Text(fMenuTitle, "Options", menu.x, menu.y, 78, INK);

    Rectangle list = OptionList(menu);
    BeginScissorMode((int)list.x, (int)list.y, (int)list.width, (int)list.height);
    for (int i = 0; i < OPTION_COUNT; i++) {
        Rectangle row = OptionRow(list, i, scroll);
        const char *label = options[i].label, *value = NULL;
        switch (options[i].action) {
        case B_FILM: value = filters[film]; break;
        case B_STATS: value = stats ? "On" : "Off"; break;
        case B_SOLUTION: value = stats && revealSolution ? "On" : "Off"; break;
        case B_EDIT: if (edit) label = "Finish editing"; break;
        default: break;
        }
        float y = row.y + (row.height - UI_LABEL) / 2;
        Color ink = focus == i ? AMBER : INK;
        Text(fType, label, row.x, y, UI_LABEL, ink);
        if (focus == i)
            DrawRectangle((int)row.x, (int)(y + UI_LABEL + 1), (int)TextW(fType, label, UI_LABEL), 2, ink);
        const char *detail = value ? value : options[i].key;
        int size = value ? UI_BODY : UI_KEY;
        Color detailInk = value ? INK : DIM;
        Text(fType, detail, row.x + row.width - TextW(fType, detail, size),
             row.y + (row.height - size) / 2, size, detailInk);
    }
    EndScissorMode();
    float contentHeight = OPTIONS_HEIGHT - OPTION_HEADER;
    if (list.height < contentHeight) {
        float ratio = list.height / contentHeight;
        DrawRectangle((int)(list.x + list.width + 9), (int)(list.y + scroll * ratio),
                      2, (int)(list.height * ratio), DIM);
    }
}

/* Puzzle files and command line */

static int RunCli(const char *path)
{
    PuzzleSet s = { 0 };
    if (!strcmp(path, "puzzles.txt") && !FileExists(path)) WriteDefaultPuzzleFile(path);
    int n = LoadPuzzleFile(path, &s);
    if (n < 0) { fprintf(stderr, "cannot read %s\n", path); return 1; }
    printf("%s: %d puzzle%s, %d warning%s\n", path, n, n == 1 ? "" : "s", s.warnCount, s.warnCount == 1 ? "" : "s");
    for (int i = 0; i < s.warnCount && i < MAX_WARNINGS; i++) printf("  warning: %s\n", s.warn[i]);
    for (int i = 0; i < s.count; i++) {
        char str[CELLS + GRID];
        BoardToString(&s.items[i].board, str);
        printf("\n[%d] %s   %s\n", i + 1, s.items[i].name, str);
        Solution so = Solve(&s.items[i].board);
        if (so.status == 1) {
            printf("    SOLVED in %d moves  (%d positions, %.2f ms)\n", so.length, so.explored, so.ms);
            Board b = s.items[i].board;
            for (int k = 0; k < so.length; k++) {
                char mv[64];
                FormatMove(&b, so.moves[k], mv, sizeof mv);
                printf("    %2d. %s\n", k + 1, mv);
                DoMove(&b, so.moves[k].from, so.moves[k].dir, NULL, &b, NULL, NULL);
            }
        } else if (so.status == 0) printf("    UNSOLVABLE  (all %d reachable positions checked)\n", so.explored);
        else printf("    search too large\n");
    }
    PuzzleSetClear(&s);
    return 0;
}

static void LoadSetFrom(const char *path, bool announce)
{
    PuzzleSet ns = { 0 };
    int n = LoadPuzzleFile(path, &ns);
    if (n <= 0) {
        if (announce) Toast(n < 0 ? TextFormat("Can't read %s", GetFileName(path))
                                  : TextFormat("No boards found in %s", GetFileName(path)), 3);
        PuzzleSetClear(&ns);
        return;
    }
    PuzzleSetClear(&set);
    set = ns;
    snprintf(puzzlePath, sizeof puzzlePath, "%s", set.path);
    LoadCase(0);
    if (announce) Toast(TextFormat("Loaded %d puzzles from %s", n, GetFileName(path)), 3);
}

/* Main */

int main(int argc, char **argv)
{
    if (!PuzzleInit()) return 1;

    const char *shotFile = NULL, *boardArg = NULL, *fileArg = NULL;
    int startMode = 0, shotStats = 0, shotInput = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--solve")) return RunCli(i + 1 < argc ? argv[i + 1] : "puzzles.txt");
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shotFile = argv[++i];
        else if (!strcmp(argv[i], "--board") && i + 1 < argc) boardArg = argv[++i];
        else if (!strcmp(argv[i], "--film") && i + 1 < argc) startMode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--stats")) shotStats = 1;
        else if (!strcmp(argv[i], "--input")) shotInput = 1;
        else fileArg = argv[i];
    }

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1400, 820, "Tenebrae Lux");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

#ifdef _WIN32
    fMenuTitle = LoadFontOr("C:/Windows/Fonts/georgia.ttf", "C:/Windows/Fonts/times.ttf", 88);
    fTitle = LoadFontOr("C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/arialbd.ttf", 72);
    fType  = LoadFontOr("C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf", 40);
    fTypeB = LoadFontOr("C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/arialbd.ttf", 44);
#else
    const char *sans = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    const char *bold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    fMenuTitle = LoadFontOr("/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf", "/usr/share/fonts/dejavu/DejaVuSerif.ttf", 88);
    fTitle = LoadFontOr(bold, "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf", 72);
    fType = LoadFontOr(sans, "/usr/share/fonts/dejavu/DejaVuSans.ttf", 40);
    fTypeB = LoadFontOr(bold, "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf", 44);
#endif

    /* shaders */
    lit = LoadShaderFromMemory(LIT_VS, LIT_FS);
    LU.emissive   = GetShaderLocation(lit, "emissive");
    LU.viewPos    = GetShaderLocation(lit, "viewPos");
    LU.lightPos   = GetShaderLocation(lit, "lightPos");
    LU.lightColor = GetShaderLocation(lit, "lightColor");
    LU.ambient    = GetShaderLocation(lit, "ambient");
    LU.sunDir     = GetShaderLocation(lit, "sunDir");
    LU.sunColor   = GetShaderLocation(lit, "sunColor");
    LU.lightVP    = GetShaderLocation(lit, "lightVP");
    LU.shadowMap  = GetShaderLocation(lit, "shadowMap");
    LU.shadowRes  = GetShaderLocation(lit, "shadowRes");
    LU.blindPhase = GetShaderLocation(lit, "blindPhase");
    post = LoadShaderFromMemory(NULL, POST_FS);
    PU.time  = GetShaderLocation(post, "time");
    PU.mode  = GetShaderLocation(post, "mode");
    PU.texel = GetShaderLocation(post, "texel");

    float shadowRes = SHADOW_RES;
    SetShaderValue(lit, LU.shadowRes, &shadowRes, SHADER_UNIFORM_FLOAT);

    /* textures + models */
    Texture2D texTile  = GenWood(1, (Color){ 120, 108, 96, 255 }, 1.0f, false);
    Texture2D texPiece = GenWood(2, (Color){ 215, 200, 182, 255 }, 2.0f, true);
    Texture2D texFrame = GenWood(3, (Color){ 90, 78, 66, 255 }, 0.6f, false);
    Texture2D texPlank = GenWood(4, (Color){ 105, 95, 85, 255 }, 0.3f, true);
    Image wax = GenImageColor(4, 4, (Color){ 235, 228, 215, 255 });
    Texture2D texWax = LoadTextureFromImage(wax); UnloadImage(wax);
    Image dark = GenImageColor(4, 4, (Color){ 60, 60, 60, 255 });
    Texture2D texDark = LoadTextureFromImage(dark); UnloadImage(dark);
    Image gi = GenImageGradientRadial(128, 128, 0.0f, (Color){ 255, 215, 150, 255 }, (Color){ 255, 120, 30, 0 });
    texGlow = LoadTextureFromImage(gi); UnloadImage(gi);
    Image di = GenImageGradientRadial(32, 32, 0.0f, WHITE, (Color){ 255, 255, 255, 0 });
    texDust = LoadTextureFromImage(di); UnloadImage(di);
    Image si = GenImageGradientRadial(128, 128, 0.15f, (Color){ 0, 0, 0, 255 }, (Color){ 0, 0, 0, 0 });
    texShadow = LoadTextureFromImage(si); UnloadImage(si);
    texFrameGlow = GenFrameGlow();

    mPieceCube = MakeModel(GenMeshCube(1, 1, 1), texPiece);
    mPyramid   = MakeModel(GenPyramid(0.40f, 0.78f), texPiece);
    mTile      = MakeModel(GenMeshCube(1, 1, 1), texTile);
    mFrame     = MakeModel(GenMeshCube(1, 1, 1), texFrame);
    mPlank     = MakeModel(GenMeshCube(1, 1, 1), texPlank);
    mCandle    = MakeModel(GenMeshCylinder(0.28f, 1.0f, 24), texWax);
    mDot       = MakeModel(GenMeshCylinder(1.0f, 1.0f, 16), texDark);

    RenderTexture2D shadowMap = LoadShadowmap(SHADOW_RES, SHADOW_RES);
    RenderTexture2D sceneRT = { 0 };
    int rtW = 0, rtH = 0;

    /* moonlight through blinds */
    Vector3 sunDir = Vector3Normalize((Vector3){ -0.62f, -0.72f, 0.42f });
    Camera3D lightCam = { 0 };
    lightCam.target = (Vector3){ 0, 0, 0 };
    lightCam.position = Vector3Scale(sunDir, -15.0f);
    lightCam.up = (Vector3){ 0, 1, 0 };
    lightCam.fovy = 11.0f;
    lightCam.projection = CAMERA_ORTHOGRAPHIC;
    Matrix lightVP = MatrixIdentity();

    for (int i = 0; i < MAX_DUST; i++)
        dust[i] = (Mote){ { GetRandomValue(-450, 450) / 100.0f, GetRandomValue(5, 300) / 100.0f, GetRandomValue(-350, 350) / 100.0f },
                          GetRandomValue(0, 628) / 100.0f, GetRandomValue(30, 100) / 100.0f };

    /* camera */
    float yaw = 0.0f, pitch = 0.86f, dist = 7.4f;
    Camera3D cam = { 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 42.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    /* puzzles */
    if (fileArg) snprintf(puzzlePath, sizeof puzzlePath, "%s", fileArg);
    else if (!FileExists(puzzlePath)) {
        const char *alt = TextFormat("%spuzzles.txt", GetApplicationDirectory());
        if (FileExists(alt)) snprintf(puzzlePath, sizeof puzzlePath, "%s", alt);
        else WriteDefaultPuzzleFile(puzzlePath);
    }
    LoadSetFrom(puzzlePath, false);
    if (set.count == 0) {
        Board b; char err[96];
        ParseBoardString("xxxx/----/t--t/-vv-", &b, err, sizeof err);
        PuzzleSetAdd(&set, "Opening", &b, 0);
        LoadCase(0);
    }
    if (boardArg) {
        Board b; char err[96];
        if (ParseBoardString(boardArg, &b, err, sizeof err) == CELLS && !err[0]) AddCase("Command line", &b);
        else Toast(TextFormat("--board: %s", err), 5);
    }

    int   filmMode = (startMode < 0 || startMode > 2) ? 0 : startMode;
    bool  showStats = shotStats, editMode = false;
    int   frame = 0, optionFocus = 0;
    float optionScroll = 0, statsScroll = 0;
    if (shotInput) InputOpen();
    if (shotFile) titleTimer = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        float time = (float)GetTime();
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        /* Resize the scene buffer with the window. */
        int ss = (sw <= 1920) ? 2 : 1;
        if (rtW != sw * ss || rtH != sh * ss) {
            if (sceneRT.id) UnloadRenderTexture(sceneRT);
            rtW = sw * ss; rtH = sh * ss;
            sceneRT = LoadRenderTexture(rtW, rtH);
            SetTextureFilter(sceneRT.texture, TEXTURE_FILTER_BILINEAR);
        }

        /* files dropped on the window */
        if (IsFileDropped()) {
            FilePathList fl = LoadDroppedFiles();
            if (fl.count > 0) LoadSetFrom(fl.paths[0], true);
            UnloadDroppedFiles(fl);
        }

        Rectangle buttons[TOOL_COUNT];
        Rectangle bar = LayoutToolbar(sw, sh, buttons);
        float statsY = sw < 860 ? 116 : 16;
        float statsW = fminf(450, sw - 16);
        float statsH = fmaxf(12, fminf(StatsHeight(), bar.y - statsY - 8));
        Rectangle statsRect = { sw - statsW - 8, statsY, statsW, statsH };
        bool overStats = showStats && CheckCollisionPointRec(GetMousePosition(), statsRect);
        if (overStats && !inputOpen && !optionsOpen) statsScroll -= GetMouseWheelMove() * STAT_STEP;
        statsScroll = Clamp(statsScroll, 0, StatsHeight() - statsH > 0 ? StatsHeight() - statsH : 0);

        /* camera */
        if (!inputOpen && !optionsOpen) {
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                Vector2 d = GetMouseDelta();
                yaw -= d.x * 0.006f;
                pitch = Clamp(pitch + d.y * 0.006f, 0.25f, 1.45f);
            }
            if (!overStats) dist = Clamp(dist - GetMouseWheelMove() * 0.5f, 4.0f, 14.0f);
        }
        float drift = 0.025f * sinf(time * 0.17f);
        cam.target = (Vector3){ showStats ? 0.7f : 0.0f, 0.0f, 0.25f };
        cam.position = (Vector3){ cam.target.x + sinf(yaw + drift) * cosf(pitch) * dist, sinf(pitch) * dist,
                                  0.25f + cosf(yaw + drift) * cosf(pitch) * dist };

        /* picking */
        Ray ray = GetScreenToWorldRay(GetMousePosition(), cam);
        int hover = -1; float best = 1e9f;
        for (int i = 0; i < CELLS; i++) {
            Vector3 p = CellPos(i);
            float top = (board.c[i] == 'x') ? 0.6f : (board.c[i] == 't') ? 0.8f : 0.1f;
            BoundingBox bb = { { p.x - 0.5f, -0.1f, p.z - 0.5f }, { p.x + 0.5f, top, p.z + 0.5f } };
            RayCollision rc = GetRayCollisionBox(ray, bb);
            if (rc.hit && rc.distance < best) { best = rc.distance; hover = i; }
        }
        unsigned markers = 0; int markerDir[CELLS] = { 0 };
        if (selected >= 0 && phase == ANIM_NONE && !editMode)
            for (int d = 0; d < 4; d++) {
                int to = SlideTarget(&board, selected, d);
                if (to >= 0) { markers |= 1u << to; markerDir[to] = d; }
            }

        /* buttons */
        Rectangle menu = OptionsBounds(sw, sh);
        Rectangle list = OptionList(menu);
        float maxScroll = OPTIONS_HEIGHT - menu.height;
        optionScroll = Clamp(optionScroll, 0, maxScroll);
        Vector2 mouse = GetMousePosition();
        bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        bool modal = optionsOpen || inputOpen;
        bool overUI = modal || CheckCollisionPointRec(mouse, bar) ||
                      (showStats && CheckCollisionPointRec(mouse, statsRect));
        bool idle = phase == ANIM_NONE;
        bool canSolve = !editMode && idle && sol.status == 1 && sol.length > 0;
        bool enabled[TOOL_COUNT] = {
            idle && !editMode, idle && !editMode, idle && !editMode && histLen > 0,
            idle, canSolve, canSolve || autoIndex >= 0, true
        };

        int action = -1;
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (!inputOpen) {
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_F1)) {
                optionsOpen = !optionsOpen;
                optionFocus = 0;
                optionScroll = 0;
            } else if (optionsOpen && click && !CheckCollisionPointRec(mouse, menu)) {
                optionsOpen = false;
            } else if (optionsOpen) {
                float wheel = GetMouseWheelMove();
                optionScroll = Clamp(optionScroll - wheel * OPTION_STEP, 0, maxScroll);
                if (wheel && list.height >= OPTION_STEP) {
                    while (optionFocus < OPTION_COUNT - 1 && OptionRow(list, optionFocus, optionScroll).y < list.y)
                        optionFocus++;
                    while (optionFocus > 0 && OptionRow(list, optionFocus, optionScroll).y + OPTION_STEP > list.y + list.height)
                        optionFocus--;
                }
                int step = IsKeyPressed(KEY_DOWN) - IsKeyPressed(KEY_UP);
                if (step) {
                    optionFocus = (optionFocus + OPTION_COUNT + step) % OPTION_COUNT;
                    Rectangle row = OptionRow(list, optionFocus, optionScroll);
                    if (row.y < list.y) optionScroll += row.y - list.y;
                    else if (row.y + row.height > list.y + list.height)
                        optionScroll += row.y + row.height - list.y - list.height;
                }
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) action = options[optionFocus].action;
                for (int i = 0; i < OPTION_COUNT; i++) {
                    if (CheckCollisionPointRec(mouse, list) && CheckCollisionPointRec(mouse, OptionRow(list, i, optionScroll))) {
                        if (click || wheel || Vector2LengthSqr(GetMouseDelta()) > 0) optionFocus = i;
                        if (click) action = options[i].action;
                    }
                }
            } else {
                if (IsKeyPressed(KEY_LEFT_BRACKET))  action = B_PREV;
                if (IsKeyPressed(KEY_RIGHT_BRACKET)) action = B_NEXT;
                if (IsKeyPressed(KEY_H)) action = B_HINT;
                if (IsKeyPressed(KEY_SPACE)) action = B_SOLVE;
                if (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_BACKSPACE)) action = B_UNDO;
                if (IsKeyPressed(KEY_R)) action = B_RESET;
                for (int i = 0; i < TOOL_COUNT; i++)
                    if (click && CheckCollisionPointRec(mouse, buttons[i]) && enabled[i]) action = i;

            }
            if (IsKeyPressed(KEY_E)) action = B_EDIT;
            if (IsKeyPressed(KEY_I)) action = B_INPUT;
            if (IsKeyPressed(KEY_TAB)) action = B_STATS;
            if (IsKeyPressed(KEY_N)) action = B_FILM;
            if (IsKeyPressed(KEY_V) && !ctrl) action = B_SOLUTION;
            if (IsKeyPressed(KEY_F5)) action = B_RELOAD;
            if (ctrl && IsKeyPressed(KEY_S)) action = B_SAVE;
        }

        if (autoIndex >= 0 && action >= 0 && action != B_STATS && action != B_FILM &&
            action != B_SOLUTION && action != B_OPTIONS) {
            autoIndex = -1;
            if (action == B_SOLVE) action = -1;
        }
        if (action == B_OPTIONS) {
            optionsOpen = !optionsOpen;
            optionFocus = 0;
            optionScroll = 0;
        } else if (action == B_SAVE) {
            if (AppendPuzzleFile(puzzlePath, set.items[cur].name, editMode ? &board : &initial))
                Toast(TextFormat("Saved to %s", GetFileName(puzzlePath)), 2);
            else Toast("Could not save board", 3);
            optionsOpen = false;
        } else if (action == B_RELOAD) {
            LoadSetFrom(puzzlePath, true);
            optionsOpen = false;
        } else if (action == B_STATS) showStats = !showStats;
        else if (action == B_SOLUTION) {
            revealSolution = !(showStats && revealSolution);
            showStats = true;
        } else if (action == B_FILM) filmMode = (filmMode + 1) % 3;
        else if (action == B_INPUT && idle) { optionsOpen = false; editMode = false; InputOpen(); }
        else if (action == B_EDIT && idle) {
            optionsOpen = false;
            editMode = !editMode; selected = -1; hintShown = false;
            if (!editMode) {
                if (memcmp(board.c, set.items[cur].board.c, CELLS) == 0) LoadCase(cur);
                else {
                    char nm[32];
                    snprintf(nm, sizeof nm, "Custom %d", set.count + 1);
                    AddCase(nm, &board);
                }
            }
        } else if (editMode && !modal && !optionsOpen) {
            bool changed = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !overUI && hover >= 0) {
                const char *cycle = "-xtv";
                board.c[hover] = cycle[(strchr(cycle, board.c[hover]) - cycle + 1) % 4];
                changed = true;
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) && !overUI && hover >= 0) { board.c[hover] = '-'; changed = true; }
            if (action == B_RESET) { memset(board.c, '-', CELLS); changed = true; }
            if (changed) sol = Solve(&board);
        } else if (action == B_PREV && idle) LoadCase(cur - 1);
        else if (action == B_NEXT && idle) LoadCase(cur + 1);
        else if (idle && !modal && !optionsOpen && !inputOpen) {
            if (action == B_HINT) {
                if (sol.status == 1 && sol.length > 0) {
                    hint = sol.moves[0]; hintShown = true; selected = hint.from; hintsUsed++;
                } else if (sol.status == 0) Toast("No solution from here", 2);
            } else if (action == B_SOLVE) {
                if (sol.status == 1 && sol.length > 0) { autoSol = sol; autoIndex = 0; }
                else if (sol.status == 0) Toast("No solution from here", 2);
            } else if (action == B_UNDO) {
                if (histLen > 0) {
                    board = hist[--histLen]; moveCount--; undosUsed++;
                    solved = false; stampTimer = 0;
                    sol = Solve(&board);
                }
                selected = -1; hintShown = false;
            } else if (action == B_RESET) {
                int h = hintsUsed, u = undosUsed;
                LoadCase(cur); titleTimer = 0; hintsUsed = h; undosUsed = u;
            } else if (autoIndex >= 0) {
                if (autoIndex < autoSol.length) {
                    Move m = autoSol.moves[autoIndex++];
                    StartMove(m.from, m.dir);
                }
                if (autoIndex >= autoSol.length) autoIndex = -1;
            } else {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !overUI) {
                    if (hover >= 0 && (markers & (1u << hover))) StartMove(selected, markerDir[hover]);
                    else if (hover >= 0 && (board.c[hover] == 'x' || board.c[hover] == 't'))
                        selected = (selected == hover) ? -1 : hover;
                    else selected = -1;
                }
                if (selected >= 0 && phase == ANIM_NONE && !ctrl) {
                    int d = -1;
                    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) d = 0;
                    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) d = 1;
                    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) d = 2;
                    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) d = 3;
                    if (d >= 0) StartMove(selected, d);
                }
            }
        }

        /* animation */
        if (phase == ANIM_SLIDE) {
            int dc = abs(animTo % GRID - animFrom % GRID) + abs(animTo / GRID - animFrom / GRID);
            animT += dt * 6.0f / (0.6f + 0.4f * dc);
            if (animT >= 1) {
                animT = 0;
                if (animVictims) {
                    phase = ANIM_BREAK;
                    for (int i = 0; i < CELLS; i++)
                        if (animVictims & (1u << i)) SpawnEmbers(CellPos(i), 40);
                } else { phase = ANIM_NONE; board = animAfter; OnMoveCommitted(); }
            }
        } else if (phase == ANIM_BREAK) {
            animT += dt * 2.5f;
            if (animT >= 1) { phase = ANIM_NONE; animT = 0; board = animAfter; OnMoveCommitted(); }
        }
        if (msgTimer > 0) msgTimer -= dt;
        if (titleTimer > 0) titleTimer -= dt;
        if (stampTimer > 0) stampTimer += dt;

        for (int i = 0; i < MAX_DUST; i++) {
            Mote *m = &dust[i];
            m->p.x += dt * 0.05f * m->speed;
            m->p.y += sinf(time * 0.4f + m->phase) * dt * 0.03f;
            m->p.z += cosf(time * 0.3f + m->phase) * dt * 0.02f;
            if (m->p.x > 4.5f) m->p.x = -4.5f;
        }
        for (int i = 0; i < MAX_EMBERS; i++) {
            Ember *e = &embers[i];
            if (e->life <= 0) continue;
            e->life -= dt;
            e->v.y -= dt * 1.2f;
            e->v = Vector3Scale(e->v, 1.0f - dt * 1.5f);
            e->p = Vector3Add(e->p, Vector3Scale(e->v, dt));
            if (e->p.y < 0.02f) { e->p.y = 0.02f; e->v.y *= -0.3f; }
        }

        /* lights */
        float lp[9], lc[9];
        for (int i = 0; i < 3; i++) {
            float f = 0.85f + 0.08f * sinf(time * 9.1f + i * 3) + 0.05f * sinf(time * 23.7f + i * 7) + 0.03f * sinf(time * 3.3f + i);
            Vector3 p = candles[i].pos;
            lp[i * 3] = p.x + 0.02f * sinf(time * 13 + i); lp[i * 3 + 1] = p.y + candles[i].height + 0.25f; lp[i * 3 + 2] = p.z;
            lc[i * 3] = 1.9f * f; lc[i * 3 + 1] = 1.25f * f; lc[i * 3 + 2] = 0.65f * f;
        }
        SetShaderValueV(lit, LU.lightPos, lp, SHADER_UNIFORM_VEC3, 3);
        SetShaderValueV(lit, LU.lightColor, lc, SHADER_UNIFORM_VEC3, 3);
        Vector3 amb = { 0.012f, 0.012f, 0.016f };
        Vector3 sunCol = { 1.55f, 1.65f, 1.9f };
        float blindPhase = 0.04f * sinf(time * 0.35f);
        SetShaderValue(lit, LU.ambient, &amb, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, LU.sunDir, &sunDir, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, LU.sunColor, &sunCol, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, LU.blindPhase, &blindPhase, SHADER_UNIFORM_FLOAT);
        SetShaderValue(lit, LU.viewPos, &cam.position, SHADER_UNIFORM_VEC3);

        View view = { 0 };
        view.show = (phase == ANIM_SLIDE) ? &animBefore : (phase == ANIM_BREAK) ? &animSlid : &board;
        view.phase = phase; view.t = animT; view.time = time;
        view.from = animFrom; view.to = animTo; view.victims = animVictims; view.selected = selected;

        /* PASS 1: shadow map */
        Matrix lv, lpj;
        BeginTextureMode(shadowMap);
            ClearBackground(WHITE);
            BeginMode3D(lightCam);
                lv = rlGetMatrixModelview();
                lpj = rlGetMatrixProjection();
                DrawWorld(&view, true);
            EndMode3D();
        EndTextureMode();
        lightVP = MatrixMultiply(lv, lpj);

        /* PASS 2: scene */
        int slot = SHADOW_SLOT;
        BeginTextureMode(sceneRT);
            ClearBackground((Color){ 4, 4, 5, 255 });
            SetShaderValueMatrix(lit, LU.lightVP, lightVP);
            rlEnableShader(lit.id);
            rlActiveTextureSlot(slot);
            rlEnableTexture(shadowMap.depth.id);
            rlSetUniform(LU.shadowMap, &slot, SHADER_UNIFORM_INT, 1);
            BeginMode3D(cam);
                DrawWorld(&view, false);
                /* Light dust through the blinds. */
                rlDrawRenderBatchActive();
                rlDisableDepthMask();
                BeginBlendMode(BLEND_ADDITIVE);
                for (int i = 0; i < MAX_DUST; i++) {
                    Vector3 q = Vector3Transform(dust[i].p, lightVP);
                    float m = BlindMask((Vector2){ q.x * 0.5f + 0.5f, q.y * 0.5f + 0.5f }, blindPhase);
                    DrawBillboard(cam, texDust, dust[i].p, 0.035f, Fade(WHITE, 0.04f + 0.55f * m));
                }
                EndBlendMode();
                rlDrawRenderBatchActive();
                rlEnableDepthMask();
            EndMode3D();
        EndTextureMode();
        rlActiveTextureSlot(slot);
        rlDisableTexture();
        rlActiveTextureSlot(0);

        /* PASS 3: grade + accents + UI */
        BeginDrawing();
        ClearBackground(BLACK);

        BeginShaderMode(post);
            Vector2 texel = { (float)ss / rtW, (float)ss / rtH };
            SetShaderValue(post, PU.time, &time, SHADER_UNIFORM_FLOAT);
            SetShaderValue(post, PU.mode, &filmMode, SHADER_UNIFORM_INT);
            SetShaderValue(post, PU.texel, &texel, SHADER_UNIFORM_VEC2);
            DrawTexturePro(sceneRT.texture, (Rectangle){ 0, 0, (float)rtW, (float)-rtH },
                           (Rectangle){ 0, 0, (float)sw, (float)sh }, (Vector2){ 0, 0 }, 0, WHITE);
        EndShaderMode();

        /* Keep amber accents outside the grading pass. */
        BeginMode3D(cam);
            rlDisableDepthTest();
            BeginBlendMode(BLEND_ADDITIVE);
            for (int i = 0; i < CELLS; i++) {
                Vector3 p = CellPos(i);
                if (markers & (1u << i)) {
                    float k = 0.65f + 0.35f * sinf(time * 5.0f);
                    FlatQuad(texFrameGlow, p, 0.96f, 0.01f, Fade(AMBER, k));
                } else if (editMode && i == hover) FlatQuad(texFrameGlow, p, 0.96f, 0.01f, Fade(AMBER, 0.6f));
                else if (i == hover && phase == ANIM_NONE && (board.c[i] == 'x' || board.c[i] == 't'))
                    FlatQuad(texFrameGlow, p, 0.96f, 0.01f, (Color){ 120, 110, 100, 120 });
                if (i == selected && phase == ANIM_NONE) FlatQuad(texFrameGlow, p, 0.96f, 0.01f, AMBER);
            }
            if (hintShown && phase == ANIM_NONE) {
                int to = SlideTarget(&board, hint.from, hint.dir);
                if (to >= 0) {
                    Vector3 a = CellPos(hint.from), b = CellPos(to);
                    a.y = b.y = 1.05f + 0.05f * sinf(time * 4);
                    DrawCylinderEx(a, Vector3Lerp(a, b, 0.8f), 0.035f, 0.035f, 8, Fade(AMBER, 0.8f));
                    DrawCylinderEx(Vector3Lerp(a, b, 0.8f), b, 0.11f, 0.0f, 8, AMBER);
                }
            }
            for (int i = 0; i < 3; i++) {
                Candle c = candles[i];
                float f = 0.9f + 0.1f * sinf(time * 17 + i * 5);
                Vector3 fp = { c.pos.x + 0.01f * sinf(time * 11 + i), c.pos.y + c.height + 0.17f, c.pos.z };
                DrawBillboard(cam, texGlow, fp, 0.30f * f, WHITE);
                DrawBillboard(cam, texGlow, fp, 0.14f, (Color){ 255, 255, 240, 255 });
                DrawBillboard(cam, texGlow, fp, 2.2f * f, (Color){ 110, 60, 20, 70 });
            }
            for (int i = 0; i < MAX_EMBERS; i++) {
                Ember *e = &embers[i];
                if (e->life <= 0) continue;
                float k = e->life / e->max;
                DrawBillboard(cam, texGlow, e->p, e->size * (0.5f + k), Fade(WHITE, k));
            }
            EndBlendMode();
            rlDrawRenderBatchActive();
            rlEnableDepthTest();
        EndMode3D();

        if (optionsOpen) {
            DrawOptions(menu, optionFocus, optionScroll, editMode, showStats, filmMode);
        } else {
            /* HUD */
            Text(fTitle, set.items[cur].name, 24, 12, UI_TITLE, INK);
            {
                char line[128];
                Color lc2 = DIM;
                if (editMode) snprintf(line, sizeof line, "Edit: click to cycle pieces. E to finish.");
                else if (autoIndex >= 0) snprintf(line, sizeof line, "Solving %d/%d", autoIndex, autoSol.length);
                else if (sol.status == 0) { snprintf(line, sizeof line, "No solution. Z to undo."); lc2 = BLOOD; }
                else snprintf(line, sizeof line, "Moves %d  |  Best %d  |  Puzzle %d/%d", moveCount, par, cur + 1, set.count);
                Text(fType, line, 26, 78, UI_BODY, lc2);
            }

            if (showStats && !inputOpen) DrawStats(statsRect, GetTime(), statsScroll);
            DrawSolved(sw, sh);

            if (msgTimer > 0) {
                float a = fminf(msgTimer, 1.0f);
                Text(fType, msg, (sw - TextW(fType, msg, UI_BODY)) / 2, bar.y - 44, UI_BODY, Fade(INK, a));
            }

            const char *labels[TOOL_COUNT] = { "Prev", "Next", "Undo", editMode ? "Clear" : "Reset",
                                              "Hint", autoIndex >= 0 ? "Stop" : "Solve", "Options" };
            static const char *keys[TOOL_COUNT] = { "[", "]", "Z", "R", "H", "Space", "Esc" };
            Panel(bar);
            for (int i = 0; i < TOOL_COUNT; i++) {
                Button(buttons[i], labels[i], i == B_SOLVE, enabled[i] && !inputOpen);
                if (!modal && msgTimer <= 0 && CheckCollisionPointRec(mouse, buttons[i])) {
                    float w = TextW(fTypeB, keys[i], UI_KEY);
                    Rectangle tip = { buttons[i].x + (buttons[i].width - w) / 2 - 8, bar.y - 44, w + 16, 40 };
                    Panel(tip);
                    Text(fTypeB, keys[i], tip.x + 8, tip.y + 6, UI_KEY, DIM);
                }
            }
        }
        if (inputOpen) InputUpdateAndDraw(sw, sh, action == B_INPUT);

        EndDrawing();
        if (shotFile && ++frame == 100) {
            Image screenshot = LoadImageFromScreen();
            ExportImage(screenshot, shotFile);
            UnloadImage(screenshot);
            break;
        }
    }

    if (fMenuTitle.texture.id != GetFontDefault().texture.id) UnloadFont(fMenuTitle);
    UnloadRenderTexture(sceneRT);
    CloseWindow();
    PuzzleSetClear(&set);
    PuzzleShutdown();
    return 0;
}
