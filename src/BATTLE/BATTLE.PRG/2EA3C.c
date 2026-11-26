#include "common.h"
#include "2EA3C.h"
#include "2EA3CPrivate.h"
#include "146CPrivate.h"
#include "4DEA8Private.h"
#include "gpu.h"
#include "mpd.h"
#include "scratch.h"
#include "vs_inline_c.h"
#include "src/SLUS_010.40/31724.h"
#include <inline_c.h>
#include <libgpu.h>
#include <libgte.h>
#include <memory.h>

#ifdef NON_MATCHING

typedef struct {
    u_short flags;
    short depthOffset;
    int pos[3];
    int rot[3];
    u_int meshOffset;
    MATRIX matrix;
} GeometryObject;

typedef struct {
    int count;
    GeometryObject objects[0];
} GeometrySection;

typedef struct {
    u_int triCount;
    u_int quadCount;
} MeshHeader;

typedef struct {
    short x0;
    short y0;
    short z0;
    signed char dx1;
    signed char dy1;
    signed char dz1;
    signed char dx2;
    signed char dy2;
    signed char dz2;
    u_char r0;
    u_char g0;
    u_char b0;
    u_char code;
    u_char r1;
    u_char g1;
    u_char b1;
    u_char u2;
    u_char r2;
    u_char g2;
    u_char b2;
    u_char v2;
    u_char u0;
    u_char v0;
    u_short clut;
    u_char u1;
    u_char v1;
    u_short tpage;
} MpdTriangle;

typedef struct {
    short x0;
    short y0;
    short z0;
    signed char dx1;
    signed char dy1;
    signed char dz1;
    signed char dx2;
    signed char dy2;
    signed char dz2;
    u_char r0;
    u_char g0;
    u_char b0;
    u_char code;
    u_char r1;
    u_char g1;
    u_char b1;
    u_char u2;
    u_char r2;
    u_char g2;
    u_char b2;
    u_char v2;
    u_char u0;
    u_char v0;
    u_short clut;
    u_char u1;
    u_char v1;
    u_short tpage;
    signed char dx3;
    signed char dy3;
    signed char dz3;
    u_char u3;
    u_char r3;
    u_char g3;
    u_char b3;
    u_char v3;
} MpdQuad;

typedef struct {
    SVECTOR pos;
    u_char u;
    u_char v;
    CVECTOR color;
    DVECTOR sxy;
    short sz;
} SubdivVertex;

typedef struct {
    u_char* nextPrim;
    u_long* ot;
    int depthOffset;
    u_char code;
    u_short clut;
    u_short tpage;
} SubdivContext;

typedef struct {
    SVECTOR pos;
    CVECTOR color;
} SkyboxVertex;

typedef struct {
    VECTOR pos;
    CVECTOR color;
} TransformedSkyboxVertex;

typedef struct {
    u_char v0;
    u_char v1;
    u_char v2;
    u_char pad;
} SkyboxTri;

typedef struct {
    DVECTOR min;
    DVECTOR max;
    DVECTOR v0;
    DVECTOR v1;
    DVECTOR v2;
    CVECTOR c0;
    CVECTOR c1;
    CVECTOR c2;
} LightingTri;

typedef struct {
    CVECTOR defaultColor;
    int count0;
    int count1;
    LightingTri tris[0];
} LightingSection;

static short minOt = 8;
static short maxOt = 2047;
static short fogDepth = 512;
static short clipMinX = 0;
static short clipMinY = 0;
static short clipMaxX = 320;
static short clipMaxY = 224;

static int clampColor(int value)
{
    if (value < 0) {
        return 0;
    }

    if (value > 255) {
        return 255;
    }

    return value;
}

static void unpackSxy(DVECTOR* out, int packed)
{
    out->vx = packed;
    out->vy = packed >> 16;
}

static int packSxy(DVECTOR sxy)
{
    u_int low = sxy.vx & 0xFFFF;
    u_int high = sxy.vy & 0xFFFF;

    return (high << 16) | low;
}

static void rotTrans(SVECTOR* in, VECTOR* out)
{
    gte_ldv0(in);
    gte_rtv0tr2();
    gte_stlvnl(out);
}

static void projectPoint(SVECTOR* in, DVECTOR* outXy, int* outZ)
{
    int sxy;
    int sz;

    gte_ldv0(in);
    gte_rtps2();
    gte_stsxy(&sxy);
    gte_stsz(&sz);
    unpackSxy(outXy, sxy);

    if (outZ != NULL) {
        *outZ = sz;
    }
}

static void projectPoints3(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, DVECTOR* xy0,
    DVECTOR* xy1, DVECTOR* xy2, int* z0, int* z1, int* z2)
{
    int sxy0;
    int sxy1;
    int sxy2;
    int sz0;
    int sz1;
    int sz2;

    gte_ldv3(v0, v1, v2);
    gte_rtpt2();
    gte_stsxy3(&sxy0, &sxy1, &sxy2);
    gte_stsz3(&sz0, &sz1, &sz2);

    if (xy0 != NULL) {
        unpackSxy(xy0, sxy0);
    }

    if (xy1 != NULL) {
        unpackSxy(xy1, sxy1);
    }

    if (xy2 != NULL) {
        unpackSxy(xy2, sxy2);
    }

    if (z0 != NULL) {
        *z0 = sz0;
    }

    if (z1 != NULL) {
        *z1 = sz1;
    }

    if (z2 != NULL) {
        *z2 = sz2;
    }
}

static int isPolyVisible(DVECTOR sxy0, DVECTOR sxy1, DVECTOR sxy2, DVECTOR sxy3)
{
    if ((sxy0.vy >= clipMaxY) && (sxy1.vy >= clipMaxY) && (sxy2.vy >= clipMaxY)
        && (sxy3.vy >= clipMaxY)) {
        return 0;
    }

    if ((sxy0.vy < clipMinY) && (sxy1.vy < clipMinY) && (sxy2.vy < clipMinY)
        && (sxy3.vy < clipMinY)) {
        return 0;
    }

    if ((sxy0.vx < clipMinX) && (sxy1.vx < clipMinX) && (sxy2.vx < clipMinX)
        && (sxy3.vx < clipMinX)) {
        return 0;
    }

    if ((sxy0.vx >= clipMaxX) && (sxy1.vx >= clipMaxX) && (sxy2.vx >= clipMaxX)
        && (sxy3.vx >= clipMaxX)) {
        return 0;
    }

    return 1;
}

static int computeOtz(u_int depth, int depthOffset, int* outOtz)
{
    int otz = depth >> 2;

    if (otz < minOt) {
        return 0;
    }

    otz += depthOffset;

    if ((otz >= maxOt) || (otz < minOt)) {
        return 0;
    }

    *outOtz = otz;
    return 1;
}

static void midpointVertex(
    SubdivVertex* out, const SubdivVertex* a, const SubdivVertex* b)
{
    out->color.r = (a->color.r + b->color.r) >> 1;
    out->color.g = (a->color.g + b->color.g) >> 1;
    out->color.b = (a->color.b + b->color.b) >> 1;
    // The original leaves the midpoint cd byte untouched in scratchpad memory.
    out->color.cd = 0;
    setVector(&out->pos, (a->pos.vx + b->pos.vx) >> 1, (a->pos.vy + b->pos.vy) >> 1,
        (a->pos.vz + b->pos.vz) >> 1);
    out->u = (a->u + b->u) >> 1;
    out->v = (a->v + b->v) >> 1;
}

static void subdivideTri(
    SubdivContext* ctx, int depth, SubdivVertex* v0, SubdivVertex* v1, SubdivVertex* v2)
{
    SubdivVertex m01;
    SubdivVertex m21;
    SubdivVertex m20;
    int sz0;
    int sz1;
    int sz2;

    if (depth == 0) {
        POLY_GT3* poly = (POLY_GT3*)ctx->nextPrim;
        int maxZ;
        int otz;

        if (!isPolyVisible(v0->sxy, v1->sxy, v2->sxy, v2->sxy)) {
            return;
        }

        maxZ = v0->sz;

        if (maxZ < v1->sz) {
            maxZ = v1->sz;
        }

        if (maxZ < v2->sz) {
            maxZ = v2->sz;
        }

        setXY3(
            poly, v0->sxy.vx, v0->sxy.vy, v1->sxy.vx, v1->sxy.vy, v2->sxy.vx, v2->sxy.vy);

        if (!computeOtz(maxZ, ctx->depthOffset, &otz)) {
            return;
        }

        setPolyGT3(poly);
        AddPrim(&ctx->ot[otz], poly);
        setUV3(poly, v0->u, v0->v, v1->u, v1->v, v2->u, v2->v);
        poly->clut = ctx->clut;
        poly->tpage = ctx->tpage;
        poly->pad2 = 0;
        setRGB0(poly, v0->color.r, v0->color.g, v0->color.b);
        poly->code = ctx->code & ~0x08;
        setRGB1(poly, v1->color.r, v1->color.g, v1->color.b);
        poly->p1 = v1->color.cd;
        setRGB2(poly, v2->color.r, v2->color.g, v2->color.b);
        poly->p2 = v2->color.cd;
        ctx->nextPrim += sizeof(POLY_GT3);
        return;
    }

    midpointVertex(&m01, v0, v1);
    midpointVertex(&m21, v2, v1);
    midpointVertex(&m20, v2, v0);
    projectPoints3(
        &m01.pos, &m21.pos, &m20.pos, &m01.sxy, &m21.sxy, &m20.sxy, &sz0, &sz1, &sz2);
    m01.sz = sz0;
    m21.sz = sz1;
    m20.sz = sz2;

    --depth;
    subdivideTri(ctx, depth, v0, &m01, &m20);
    subdivideTri(ctx, depth, &m21, &m01, v1);
    subdivideTri(ctx, depth, &m21, v2, &m20);
    subdivideTri(ctx, depth, &m01, &m21, &m20);

    subdivideTri(ctx, 0, v2, v0, &m20);
    subdivideTri(ctx, 0, v2, &m21, v1);
    subdivideTri(ctx, 0, &m01, v0, v1);
}

static void subdivideQuad(SubdivContext* ctx, int depth, SubdivVertex* v0,
    SubdivVertex* v1, SubdivVertex* v2, SubdivVertex* v3)
{
    SubdivVertex m01;
    SubdivVertex m31;
    SubdivVertex m32;
    SubdivVertex m02;
    SubdivVertex mc;
    int sz0;
    int sz1;
    int sz2;

    if (depth == 0) {
        POLY_GT4* poly = (POLY_GT4*)ctx->nextPrim;
        int maxZ;
        int otz;

        if (!isPolyVisible(v0->sxy, v1->sxy, v2->sxy, v3->sxy)) {
            return;
        }

        maxZ = v0->sz;

        if (maxZ < v1->sz) {
            maxZ = v1->sz;
        }

        if (maxZ < v2->sz) {
            maxZ = v2->sz;
        }

        if (maxZ < v3->sz) {
            maxZ = v3->sz;
        }

        setXY4(poly, v0->sxy.vx, v0->sxy.vy, v1->sxy.vx, v1->sxy.vy, v2->sxy.vx,
            v2->sxy.vy, v3->sxy.vx, v3->sxy.vy);

        if (!computeOtz(maxZ, ctx->depthOffset, &otz)) {
            return;
        }

        setPolyGT4(poly);
        AddPrim(&ctx->ot[otz], poly);
        setUV4(poly, v0->u, v0->v, v1->u, v1->v, v2->u, v2->v, v3->u, v3->v);
        poly->clut = ctx->clut;
        poly->tpage = ctx->tpage;
        poly->pad2 = 0;
        poly->pad3 = 0;
        setRGB0(poly, v0->color.r, v0->color.g, v0->color.b);
        poly->code = ctx->code;
        setRGB1(poly, v1->color.r, v1->color.g, v1->color.b);
        poly->p1 = v1->color.cd;
        setRGB2(poly, v2->color.r, v2->color.g, v2->color.b);
        poly->p2 = v2->color.cd;
        setRGB3(poly, v3->color.r, v3->color.g, v3->color.b);
        poly->p3 = v3->color.cd;
        ctx->nextPrim += sizeof(POLY_GT4);
        return;
    }

    midpointVertex(&m01, v0, v1);
    midpointVertex(&mc, v2, v1);
    midpointVertex(&m31, v3, v1);
    projectPoints3(
        &m01.pos, &mc.pos, &m31.pos, &m01.sxy, &mc.sxy, &m31.sxy, &sz0, &sz1, &sz2);
    m01.sz = sz0;
    mc.sz = sz1;
    m31.sz = sz2;

    midpointVertex(&m32, v3, v2);
    midpointVertex(&m02, v0, v2);
    projectPoints3(
        &m32.pos, &m02.pos, &m31.pos, &m32.sxy, &m02.sxy, NULL, &sz0, &sz1, NULL);
    m32.sz = sz0;
    m02.sz = sz1;

    --depth;
    subdivideQuad(ctx, depth, v0, &m01, &m02, &mc);
    subdivideQuad(ctx, depth, v1, &m01, &m31, &mc);
    subdivideQuad(ctx, depth, v3, &m32, &m31, &mc);
    subdivideQuad(ctx, depth, v2, &m32, &m02, &mc);

    subdivideTri(ctx, 0, v2, &m32, v3);
    subdivideTri(ctx, 0, v2, v0, &m02);
    subdivideTri(ctx, 0, &m01, v0, v1);
    subdivideTri(ctx, 0, v3, &m31, v1);
}

static void renderMesh(SubdivContext* ctx, const MeshHeader* mesh, int vertexScale,
    int subdivLevel, int enableSubdiv)
{
    // The original primes the first record and reads ahead at mesh boundaries.
    const MpdTriangle* tri = (const MpdTriangle*)(mesh + 1);
    const MpdQuad* quad;
    u_int packedCounts = mesh->triCount | (mesh->quadCount << 16);
    u_int triCount = packedCounts & 0xFFFF;
    u_int quadCount = packedCounts >> 16;
    u_int fogThreshold = fogDepth;
    u_int i;

    for (i = 0; i < triCount; ++i, ++tri) {
        SVECTOR p0;
        SVECTOR p1;
        SVECTOR p2;
        DVECTOR sxy0;
        DVECTOR sxy1;
        DVECTOR sxy2;
        int sz0;
        int sz1;
        int sz2;
        int area;
        u_int absArea;
        int maxZ;
        u_int depthU;
        int subdivDepth;

        setVector(&p0, tri->x0, tri->y0, tri->z0);
        setVector(&p1, tri->x0 + tri->dx1 * vertexScale, tri->y0 + tri->dy1 * vertexScale,
            tri->z0 + tri->dz1 * vertexScale);
        setVector(&p2, tri->x0 + tri->dx2 * vertexScale, tri->y0 + tri->dy2 * vertexScale,
            tri->z0 + tri->dz2 * vertexScale);

        projectPoints3(&p0, &p1, &p2, &sxy0, &sxy1, &sxy2, &sz0, &sz1, &sz2);
        area = NormalClip(packSxy(sxy0), packSxy(sxy1), packSxy(sxy2));

        if (area < 0) {
            if (!(tri->code & 1)) {
                continue;
            }

            area = -area;
        }

        if (!isPolyVisible(sxy0, sxy1, sxy2, sxy2)) {
            continue;
        }

        maxZ = sz0;

        if (maxZ < sz1) {
            maxZ = sz1;
        }

        if (maxZ < sz2) {
            maxZ = sz2;
        }

        depthU = maxZ;
        absArea = area;
        subdivDepth = subdivLevel - (depthU >> 9);

        if ((enableSubdiv & 1) && (absArea >= 512) && (subdivDepth > 0)) {
            SubdivVertex v0;
            SubdivVertex v1;
            SubdivVertex v2;

            v0.pos = p0;
            v0.u = tri->u0;
            v0.v = tri->v0;
            v0.color.r = tri->r0;
            v0.color.g = tri->g0;
            v0.color.b = tri->b0;
            v0.color.cd = 1;
            v0.sxy = sxy0;
            v0.sz = sz0;

            v1.pos = p1;
            v1.u = tri->u1;
            v1.v = tri->v1;
            v1.color.r = tri->r1;
            v1.color.g = tri->g1;
            v1.color.b = tri->b1;
            v1.color.cd = 1;
            v1.sxy = sxy1;
            v1.sz = sz1;

            v2.pos = p2;
            v2.u = tri->u2;
            v2.v = tri->v2;
            v2.color.r = tri->r2;
            v2.color.g = tri->g2;
            v2.color.b = tri->b2;
            v2.color.cd = 1;
            v2.sxy = sxy2;
            v2.sz = sz2;

            ctx->code = tri->code & ~1;
            ctx->clut = tri->clut;
            ctx->tpage = tri->tpage;
            subdivideTri(ctx, subdivDepth, &v0, &v1, &v2);
        } else {
            POLY_GT3* poly = (POLY_GT3*)ctx->nextPrim;
            int otz;
            u_int otzU;
            u_char u0;
            u_char v0;
            u_char u1;
            u_char v1;
            u_char u2;
            u_char v2;
            u_short tpage;

            setXY3(poly, sxy0.vx, sxy0.vy, sxy1.vx, sxy1.vy, sxy2.vx, sxy2.vy);

            if (!computeOtz(depthU, ctx->depthOffset, &otz)) {
                continue;
            }

            setPolyGT3(poly);
            AddPrim(&ctx->ot[otz], poly);
            setRGB0(poly, tri->r0, tri->g0, tri->b0);
            poly->code = tri->code & ~1;
            setRGB1(poly, tri->r1, tri->g1, tri->b1);
            poly->p1 = tri->u2;
            setRGB2(poly, tri->r2, tri->g2, tri->b2);
            poly->p2 = tri->v2;

            u0 = tri->u0;
            v0 = tri->v0;
            u1 = tri->u1;
            v1 = tri->v1;
            u2 = tri->u2;
            v2 = tri->v2;
            tpage = tri->tpage;
            otzU = otz;

            if (otzU >= fogThreshold) {
                u0 >>= 1;
                v0 >>= 1;
                u1 >>= 1;
                v1 >>= 1;
                u2 >>= 1;
                v2 >>= 1;
                --tpage;
            }

            setUV3(poly, u0, v0, u1, v1, u2, v2);
            poly->clut = tri->clut;
            poly->tpage = tpage;
            poly->pad2 = 0;
            ctx->nextPrim += sizeof(POLY_GT3);
        }
    }

    quad = (const MpdQuad*)tri;

    for (i = 0; i < quadCount; ++i, ++quad) {
        SVECTOR p0;
        SVECTOR p1;
        SVECTOR p2;
        SVECTOR p3;
        DVECTOR sxy0;
        DVECTOR sxy1;
        DVECTOR sxy2;
        DVECTOR sxy3;
        int sz0;
        int sz1;
        int sz2;
        int sz3;
        int area;
        u_int areaUnsigned;
        int maxZ;
        u_int depthU;
        int visible;
        int subdivDepth;

        setVector(&p0, quad->x0, quad->y0, quad->z0);
        setVector(&p1, quad->x0 + quad->dx1 * vertexScale,
            quad->y0 + quad->dy1 * vertexScale, quad->z0 + quad->dz1 * vertexScale);
        setVector(&p2, quad->x0 + quad->dx2 * vertexScale,
            quad->y0 + quad->dy2 * vertexScale, quad->z0 + quad->dz2 * vertexScale);
        setVector(&p3, quad->x0 + quad->dx3 * vertexScale,
            quad->y0 + quad->dy3 * vertexScale, quad->z0 + quad->dz3 * vertexScale);

        projectPoints3(&p0, &p1, &p2, &sxy0, &sxy1, &sxy2, &sz0, &sz1, &sz2);
        area = NormalClip(packSxy(sxy0), packSxy(sxy1), packSxy(sxy2));
        visible = (quad->code & 1) || (area >= 0);

        projectPoint(&p3, &sxy3, &sz3);

        if (!isPolyVisible(sxy0, sxy1, sxy2, sxy3)) {
            continue;
        }

        if (!visible) {
            area = NormalClip(packSxy(sxy3), packSxy(sxy2), packSxy(sxy1));

            if (area < 0) {
                continue;
            }
        }

        maxZ = sz0;

        if (maxZ < sz1) {
            maxZ = sz1;
        }

        if (maxZ < sz2) {
            maxZ = sz2;
        }

        // Potential bug in original, needs investigating.
        // Back-facing double-sided quads skip negation and always pass the unsigned >=
        // 512 subdivision check, and quad subdivision depth ignores sz3.
        depthU = maxZ;
        areaUnsigned = area;
        subdivDepth = subdivLevel - (depthU >> 9);

        if ((enableSubdiv & 1) && (areaUnsigned >= 512) && (subdivDepth > 0)) {
            SubdivVertex v0;
            SubdivVertex v1;
            SubdivVertex v2;
            SubdivVertex v3;

            v0.pos = p0;
            v0.u = quad->u0;
            v0.v = quad->v0;
            v0.color.r = quad->r0;
            v0.color.g = quad->g0;
            v0.color.b = quad->b0;
            v0.color.cd = 1;
            v0.sxy = sxy0;
            v0.sz = sz0;

            v1.pos = p1;
            v1.u = quad->u1;
            v1.v = quad->v1;
            v1.color.r = quad->r1;
            v1.color.g = quad->g1;
            v1.color.b = quad->b1;
            v1.color.cd = 1;
            v1.sxy = sxy1;
            v1.sz = sz1;

            v2.pos = p2;
            v2.u = quad->u2;
            v2.v = quad->v2;
            v2.color.r = quad->r2;
            v2.color.g = quad->g2;
            v2.color.b = quad->b2;
            v2.color.cd = 1;
            v2.sxy = sxy2;
            v2.sz = sz2;

            v3.pos = p3;
            v3.u = quad->u3;
            v3.v = quad->v3;
            v3.color.r = quad->r3;
            v3.color.g = quad->g3;
            v3.color.b = quad->b3;
            v3.color.cd = 1;
            v3.sxy = sxy3;
            v3.sz = sz3;

            ctx->code = quad->code & ~1;
            ctx->clut = quad->clut;
            ctx->tpage = quad->tpage;
            subdivideQuad(ctx, subdivDepth, &v0, &v1, &v2, &v3);
        } else {
            POLY_GT4* poly = (POLY_GT4*)ctx->nextPrim;
            int otz;
            u_int otzU;
            u_char u0;
            u_char v0;
            u_char u1;
            u_char v1;
            u_char u2;
            u_char v2;
            u_char u3;
            u_char v3;
            u_short tpage;

            if (maxZ < sz3) {
                maxZ = sz3;
            }

            depthU = maxZ;
            setXY3(poly, sxy0.vx, sxy0.vy, sxy1.vx, sxy1.vy, sxy2.vx, sxy2.vy);

            if (!computeOtz(depthU, ctx->depthOffset, &otz)) {
                continue;
            }

            setPolyGT4(poly);
            AddPrim(&ctx->ot[otz], poly);
            poly->x3 = sxy3.vx;
            poly->y3 = sxy3.vy;
            setRGB0(poly, quad->r0, quad->g0, quad->b0);
            poly->code = quad->code & ~1;
            setRGB1(poly, quad->r1, quad->g1, quad->b1);
            poly->p1 = quad->u2;
            setRGB2(poly, quad->r2, quad->g2, quad->b2);
            poly->p2 = quad->v2;
            setRGB3(poly, quad->r3, quad->g3, quad->b3);
            poly->p3 = quad->v3;

            u0 = quad->u0;
            v0 = quad->v0;
            u1 = quad->u1;
            v1 = quad->v1;
            u2 = quad->u2;
            v2 = quad->v2;
            u3 = quad->u3;
            v3 = quad->v3;
            tpage = quad->tpage;
            otzU = otz;

            if (otzU >= fogThreshold) {
                u0 >>= 1;
                v0 >>= 1;
                u1 >>= 1;
                v1 >>= 1;
                u2 >>= 1;
                v2 >>= 1;
                u3 >>= 1;
                v3 >>= 1;
                --tpage;
            }

            setUV4(poly, u0, v0, u1, v1, u2, v2, u3, v3);
            poly->clut = quad->clut;
            poly->tpage = tpage;
            poly->pad2 = 0;
            poly->pad3 = 0;
            ctx->nextPrim += sizeof(POLY_GT4);
        }
    }
}

void func_8009723C(void* arg0, int arg1)
{
    GeometrySection* section = arg0;
    u_char* startPrim = VS_SCRATCH->nextPrim;
    SubdivContext ctx;
    int i;

    ctx.nextPrim = startPrim;
    ctx.ot = VS_SCRATCH->depthOt;

    // The original loop is post-tested and assumes count > 0.
    for (i = 0; i < section->count; ++i) {
        GeometryObject* obj = &section->objects[i];
        u_short flags = obj->flags;
        u_int viewFacing = VS_SCRATCH->firstPersonViewFacing;
        const MeshHeader* mesh;
        int vertexScale;
        int subdivLevel;

        if (ctx.nextPrim - startPrim >= 65536) {
            break;
        }

        if (flags & 0x1100) {
            continue;
        }

        if (((flags >> 15) & 1) & arg1) {
            continue;
        }

        if (!(viewFacing >> 16) && ((~flags) & viewFacing & 0xFF)) {
            continue;
        }

        vertexScale = (flags & 0x800) ? 1 : 8;
        subdivLevel = (flags >> 13) & 3;
        ctx.depthOffset = obj->depthOffset;

        SetRotMatrix(&obj->matrix);
        SetTransMatrix(&obj->matrix);
        mesh = (const MeshHeader*)((u_char*)arg0 + obj->meshOffset);
        renderMesh(&ctx, mesh, vertexScale, subdivLevel, arg1);
    }

    VS_SCRATCH->nextPrim = ctx.nextPrim;
}

void func_80098160(int minZ) { minOt = minZ; }

void func_8009817C(int maxZ) { maxOt = maxZ; }

void setFogDepth(int depth) { fogDepth = depth; }

void func_800981B8(int minX, int minY, int maxX, int maxY)
{
    clipMinX = minX;
    clipMinY = minY;
    clipMaxX = maxX;
    clipMaxY = maxY;
}

void func_8009820C(void* arg0)
{
    const SkyboxVertex* srcVerts = arg0;
    const SkyboxTri* tris = (const SkyboxTri*)(srcVerts + 34);
    TransformedSkyboxVertex verts[34];
    u_long* ot = VS_SCRATCH->depthOt;
    POLY_G3* poly = VS_SCRATCH->nextPrim;
    int i;

    for (i = 0; i < 34; ++i) {
        SVECTOR pos = srcVerts[i].pos;

        rotTrans(&pos, &verts[i].pos);
        verts[i].color = srcVerts[i].color;
    }

    for (i = 0; i < 64; ++i) {
        const TransformedSkyboxVertex* v0 = &verts[tris[i].v0];
        const TransformedSkyboxVertex* v1 = &verts[tris[i].v1];
        const TransformedSkyboxVertex* v2 = &verts[tris[i].v2];
        int x0;
        int x1;
        int x2;
        int y0;
        int y1;
        int y2;

        if ((v0->pos.vz <= 0) && (v1->pos.vz <= 0) && (v2->pos.vz <= 0)) {
            continue;
        }

        x0 = (u_int)v0->pos.vx + 160;
        x1 = (u_int)v1->pos.vx + 160;
        x2 = (u_int)v2->pos.vx + 160;
        y0 = (u_int)v0->pos.vy + 120;
        y1 = (u_int)v1->pos.vy + 120;
        y2 = (u_int)v2->pos.vy + 120;

        if ((x0 < 0) && (x1 < 0) && (x2 < 0)) {
            continue;
        }

        if ((x0 >= 320) && (x1 >= 320) && (x2 >= 320)) {
            continue;
        }

        if ((y0 < 0) && (y1 < 0) && (y2 < 0)) {
            continue;
        }

        if ((y0 >= 224) && (y1 >= 224) && (y2 >= 224)) {
            continue;
        }

        setPolyG3(poly);
        setXY3(poly, x0, y0, x1, y1, x2, y2);
        setRGB0(poly, v0->color.r, v0->color.g, v0->color.b);
        poly->code = v0->color.cd;
        setRGB1(poly, v1->color.r, v1->color.g, v1->color.b);
        poly->pad1 = v1->color.cd;
        setRGB2(poly, v2->color.r, v2->color.g, v2->color.b);
        poly->pad2 = v2->color.cd;
        AddPrim(&ot[2047], poly++);
    }

    VS_SCRATCH->nextPrim = poly;
}

void func_800983F8(void* arg0)
{
    // Potential bug in original, needs investigating.
    // The original saves $s3 and $s4 to 0x1F800390/0x1F800394 and restores them from
    // 0x1F800398/0x1F80039C.
    GeometrySection* section = arg0;
    int roll = VS_SCRATCH->camera.angles.vz;
    u_int farClip = VS_SCRATCH->camera.farClip;
    int sinProd = rsin(roll) * farClip;
    int cosProd = rcos(roll) * farClip;
    int sinRoll = sinProd >> 12;
    int cosRoll = cosProd >> 12;
    VECTOR zeroVec;
    int i;

    setVector(&zeroVec, 0, 0, 0);

    // The original loop is post-tested and assumes count > 0.
    for (i = 0; i < section->count; ++i) {
        GeometryObject* obj = &section->objects[i];

        if (obj->flags & 0x100) {
            continue;
        }

        if (!(obj->flags & 0x400)) {
            VECTOR pos;
            VECTOR rot;

            setVector(&pos, obj->pos[0], obj->pos[1], obj->pos[2]);
            setVector(&rot, obj->rot[0], obj->rot[1], obj->rot[2]);
            func_800985AC(&pos, &rot, &obj->matrix);
        } else {
            MATRIX tempMatrix;
            SVECTOR pos;
            VECTOR trans;

            func_800985AC(&zeroVec, &zeroVec, &tempMatrix);
            SetRotMatrix(&tempMatrix);
            SetTransMatrix(&tempMatrix);
            setVector(&pos, obj->pos[0], obj->pos[1], obj->pos[2]);
            rotTrans(&pos, &trans);
            obj->matrix.t[0] = trans.vx;
            obj->matrix.t[1] = trans.vy;
            obj->matrix.t[2] = trans.vz;

            obj->matrix.m[0][0] = cosRoll;
            obj->matrix.m[0][1] = -sinRoll;
            obj->matrix.m[1][0] = sinRoll;
            obj->matrix.m[1][1] = cosRoll;
            obj->matrix.m[2][2] = VS_SCRATCH->camera.farClip;

            SetRotMatrix(&VS_SCRATCH->viewMatrix);
            SetTransMatrix(&VS_SCRATCH->viewMatrix);
        }
    }
}

void func_800985AC(VECTOR* pos, VECTOR* rot, MATRIX* out)
{
    SVECTOR angles;
    SVECTOR translation;
    VECTOR trans;

    setVector(&angles, rot->vx, rot->vy, rot->vz);
    RotMatrixYXZ_gte(&angles, out);
    func_80041C68(&VS_SCRATCH->viewMatrix, out);
    setVector(&translation, pos->vx, pos->vy, pos->vz);
    rotTrans(&translation, &trans);
    out->t[0] = trans.vx;
    out->t[1] = trans.vy;
    out->t[2] = trans.vz;
}

void func_80098648(VECTOR* pos, VECTOR* rot, MATRIX* out)
{
    MATRIX scale;
    MATRIX view = VS_SCRATCH->viewMatrix;
    SVECTOR angles;
    SVECTOR translation;
    VECTOR trans;
    int farClip = VS_SCRATCH->camera.farClip;
    int invScale = (farClip != 0) ? ((ONE * ONE) / farClip) : -1;

    memset(&scale, 0, sizeof scale);
    scale.m[0][0] = invScale;
    scale.m[1][1] = invScale;
    scale.m[2][2] = invScale;

    setVector(&angles, rot->vx, rot->vy, rot->vz);
    RotMatrixYXZ_gte(&angles, out);
    func_80041C68(&scale, &view);
    func_80041C68(&view, out);
    setVector(&translation, pos->vx, pos->vy, pos->vz);
    rotTrans(&translation, &trans);
    out->t[0] = trans.vx;
    out->t[1] = trans.vy;
    out->t[2] = trans.vz;
}

void func_80098B1C(void)
{
    // The original tail-jumps directly to sectionE->unk14 with $ra and argument
    // registers intact; assumes a no-argument call since no callers or populated
    // section E entries were found.
    void (*entry)(void) = (void (*)(void))vs_battle_roomData.sectionE->unk14;

    entry();
}

static int interpolateLightTri(int xz, CVECTOR* outColor, const LightingTri* tri)
{
    int v0 = packSxy(tri->v0);
    int v1 = packSxy(tri->v1);
    int v2 = packSxy(tri->v2);
    int area = NormalClip(v0, v2, v1);
    int w0;
    int w2;
    int w1;
    u_int areaU;
    u_int weight0;
    u_int weight1;
    u_int weight2;

    if (area <= 0) {
        return 0;
    }

    w0 = NormalClip(xz, v2, v1);

    if (w0 < 0) {
        return 0;
    }

    w2 = NormalClip(xz, v1, v0);

    if (w2 < 0) {
        return 0;
    }

    areaU = area;
    weight0 = w0;
    weight2 = w2;
    w1 = areaU - weight0 - weight2;

    if (w1 < 0) {
        return 0;
    }

    weight1 = w1;

    outColor->r = clampColor(
        (tri->c0.r * weight0 + tri->c2.r * weight2 + tri->c1.r * weight1) / areaU);
    outColor->g = clampColor(
        (tri->c0.g * weight0 + tri->c2.g * weight2 + tri->c1.g * weight1) / areaU);
    outColor->b = clampColor(
        (tri->c0.b * weight0 + tri->c2.b * weight2 + tri->c1.b * weight1) / areaU);
    return 1;
}

int func_80098B38(int xz, int layer, CVECTOR* outColor)
{
    CVECTOR defaultColor;
    const LightingSection* lighting;
    const LightingTri* tri;
    int posX;
    int posZ;
    u_int count;
    u_int i;

    defaultColor.r = 128;
    defaultColor.g = 128;
    defaultColor.b = 128;
    defaultColor.cd = 0;

    if ((vs_battle_roomData.unk0 == 0) || (vs_battle_roomData.lightingSection == NULL)) {
        *outColor = defaultColor;
        return 0;
    }

    lighting = vs_battle_roomData.lightingSection;
    defaultColor = lighting->defaultColor;
    posX = (short)xz;
    posZ = xz >> 16;

    if (layer == 0) {
        count = lighting->count0;
        tri = lighting->tris;
    } else {
        count = lighting->count1;
        tri = lighting->tris + lighting->count0;
    }

    for (i = 0; i < count; ++i, ++tri) {
        if ((posX >= tri->min.vx) && (posX <= tri->max.vx) && (posZ >= tri->min.vy)
            && (posZ <= tri->max.vy)) {
            if (interpolateLightTri(xz, outColor, tri)) {
                return 1;
            }
        }
    }

    *outColor = defaultColor;
    return 0;
}

static void tintColor(
    u_char* r, u_char* g, u_char* b, u_char cd, int dr, int dg, int db, int desat)
{
    int red = *r;
    int green = *g;
    int blue = *b;

    if (desat != 0) {
        CVECTOR color;
        int gray = red * 5 + green * 9 + blue * 2;

        color.r = red;
        color.g = green;
        color.b = blue;
        color.cd = cd;
        gte_ldfcdir(gray, gray, gray);
        DpqColor(&color, desat, &color);
        red = color.r;
        green = color.g;
        blue = color.b;
    }

    *r = clampColor(red + dr);
    *g = clampColor(green + dg);
    *b = clampColor(blue + db);
}

void func_80098D6C(void* start, void* end, D_800F1BB0_t* colorMod, int flags)
{
    u_char* cur = start;
    int dr;
    int dg;
    int db;
    int desat;
    int extra;

    if (start == end) {
        return;
    }

    dr = colorMod->unk0 * 4;
    dg = colorMod->unk1 * 4;
    db = colorMod->unk2 * 4;
    desat = colorMod->unk4;
    extra = (flags & 2) << 2;

    while (cur != end) {
        int len = getlen(cur);

        if (len == 9) {
            POLY_GT3* poly = (POLY_GT3*)cur;

            tintColor(&poly->r0, &poly->g0, &poly->b0, poly->code, dr, dg, db, desat);
            tintColor(&poly->r1, &poly->g1, &poly->b1, poly->p1, dr, dg, db, desat);
            tintColor(&poly->r2, &poly->g2, &poly->b2, poly->p2, dr, dg, db, desat);
            cur += sizeof(POLY_GT3) + extra;
        } else if (len == 12) {
            POLY_GT4* poly = (POLY_GT4*)cur;

            tintColor(&poly->r0, &poly->g0, &poly->b0, poly->code, dr, dg, db, desat);
            tintColor(&poly->r1, &poly->g1, &poly->b1, poly->p1, dr, dg, db, desat);
            tintColor(&poly->r2, &poly->g2, &poly->b2, poly->p2, dr, dg, db, desat);
            tintColor(&poly->r3, &poly->g3, &poly->b3, poly->p3, dr, dg, db, desat);
            cur += sizeof(POLY_GT4) + extra;
        } else {
            break;
        }
    }
}

vs_RoomName* vs_battle_initSceneAndGetRoomNames(void* arg0)
{
    vs_Scene* scene = arg0;
    void* data = &scene->rooms[scene->roomCount];
    int i;

    // The original loops are post-tested and assume roomCount > 0.
    for (i = 0; i < scene->roomCount; ++i) {
        u_long size = (u_long)scene->rooms[i].dataAddress;

        scene->rooms[i].dataAddress = data;
        data += size;
    }

    return data;
}

void func_80098F8C(void* arg0)
{
    vs_Scene* scene = arg0;
    u_long* ot = VS_SCRATCH->depthOt;
    u_char* prim = VS_SCRATCH->nextPrim;
    MATRIX trans;
    u_short blink = D_800F1CDC;
    DR_MODE* mode;
    int roomIdx;

    trans.t[0] = 0;
    trans.t[1] = 0;
    trans.t[2] = 256;
    SetTransMatrix(&trans);

    // The original loop is post-tested and assumes roomCount > 0.
    for (roomIdx = 0; roomIdx < scene->roomCount; ++roomIdx) {
        vs_Room* room = &scene->rooms[roomIdx];
        const u_int* cursor;
        const SVECTOR* verts;
        DVECTOR screenPts[258];
        int count;
        int i;

        if (!room->visited) {
            continue;
        }

        cursor = room->dataAddress;
        count = *cursor;
        ++cursor;

        if (count == 0) {
            continue;
        }

        verts = (const SVECTOR*)cursor;

        for (i = 0; i < count; i += 3) {
            SVECTOR v0 = verts[i];
            SVECTOR v1 = verts[i + 1];
            SVECTOR v2 = verts[i + 2];

            projectPoints3(&v0, &v1, &v2, &screenPts[i], &screenPts[i + 1],
                &screenPts[i + 2], NULL, NULL, NULL);
        }

        cursor = (const u_int*)(verts + count);

        count = *cursor;
        ++cursor;

        if (roomIdx == 0) {
            for (i = 0; i < count; ++i) {
                u_int idx = cursor[i];
                POLY_F3* poly = (POLY_F3*)prim;
                DVECTOR pt0 = screenPts[idx & 0xFF];
                DVECTOR pt1 = screenPts[(idx >> 8) & 0xFF];
                DVECTOR pt2 = screenPts[(idx >> 16) & 0xFF];

                setPolyF3(poly);
                setSemiTrans(poly, 1);
                setRGB0(poly, 0, 127, 63);
                setXY3(poly, pt0.vx, pt0.vy, pt1.vx, pt1.vy, pt2.vx, pt2.vy);
                AddPrim(&ot[5], poly);
                prim += sizeof(POLY_F3);
            }
        }

        cursor += count;
        count = *cursor;
        ++cursor;

        if (roomIdx == 0) {
            for (i = 0; i < count; ++i) {
                u_int idx = cursor[i];
                POLY_F4* poly = (POLY_F4*)prim;
                DVECTOR pt0 = screenPts[idx & 0xFF];
                DVECTOR pt1 = screenPts[(idx >> 8) & 0xFF];
                DVECTOR pt2 = screenPts[(idx >> 16) & 0xFF];
                DVECTOR pt3 = screenPts[idx >> 24];

                setPolyF4(poly);
                setSemiTrans(poly, 1);
                setRGB0(poly, 0, 127, 63);
                setXY4(
                    poly, pt0.vx, pt0.vy, pt1.vx, pt1.vy, pt3.vx, pt3.vy, pt2.vx, pt2.vy);
                AddPrim(&ot[5], poly);
                prim += sizeof(POLY_F4);
            }
        }

        cursor += count;
        count = *cursor;
        ++cursor;

        for (i = 0; i < count; ++i) {
            u_int idx = cursor[i];
            LINE_F2* line = (LINE_F2*)prim;
            DVECTOR pt0 = screenPts[idx & 0xFF];
            DVECTOR pt1 = screenPts[(idx >> 8) & 0xFF];

            setLineF2(line);
            setSemiTrans(line, 1);
            setRGB0(line, 0, 127, 255);
            setXY2(line, pt0.vx, pt0.vy, pt1.vx, pt1.vy);
            AddPrim(&ot[5], line);
            prim += sizeof(LINE_F2);
        }

        cursor += count;

        if (roomIdx != 0) {
            continue;
        }

        count = *cursor;
        ++cursor;

        for (i = 0; i < count; ++i) {
            u_int idx = cursor[i];
            LINE_F2* line = (LINE_F2*)prim;
            DVECTOR pt0 = screenPts[idx & 0xFF];
            DVECTOR pt1 = screenPts[(idx >> 8) & 0xFF];

            setLineF2(line);
            setSemiTrans(line, 1);
            setRGB0(line, 0, 95, 47);
            setXY2(line, pt0.vx, pt0.vy, pt1.vx, pt1.vy);
            AddPrim(&ot[5], line);
            prim += sizeof(LINE_F2);
        }

        cursor += count;
        count = *cursor;
        ++cursor;

        for (i = 0; i < count; ++i) {
            const vs_RoomDoor* door = (const vs_RoomDoor*)&cursor[i];
            DVECTOR pt = screenPts[door->vertex];

            if ((door->unk3 != 0) && (getStateFlag(0x33F + door->unk3) == 0)) {
                SPRT_8* sprt = (SPRT_8*)prim;

                setSprt8(sprt);
                setRGB0(sprt, 128, 128, 128);
                setXY0(sprt, pt.vx - 3, pt.vy - 4);
                setUV0(sprt, 216, 152);
                setClut(sprt, 896, 223);
                AddPrim(&ot[4], sprt);
                prim += sizeof(SPRT_8);
            } else {
                TILE* tile;
                u_char red;
                u_char green;
                u_char blue;

                if (door->flags & 0x10) {
                    if (!(blink & 0x20)) {
                        continue;
                    }

                    red = 255;
                    green = 0;
                    blue = 127;
                } else if (door->flags & 4) {
                    red = 0;
                    green = 127;
                    blue = 255;
                } else if ((door->flags & 0x1F) == 0) {
                    red = 127;
                    green = 127;
                    blue = 127;
                } else {
                    continue;
                }

                tile = (TILE*)prim;
                setTile(tile);
                setRGB0(tile, red, green, blue);
                setXY0(tile, pt.vx, pt.vy - 1);
                setWH(tile, 1, 3);
                AddPrim(&ot[4], tile);
                prim += sizeof(TILE);

                tile = (TILE*)prim;
                setTile(tile);
                setRGB0(tile, red, green, blue);
                setXY0(tile, pt.vx - 1, pt.vy);
                setWH(tile, 3, 1);
                AddPrim(&ot[4], tile);
                prim += sizeof(TILE);
            }
        }
    }

    mode = (DR_MODE*)prim;
    SetDrawMode(mode, 0, 1, getTPage(0, 0, 768, 0), NULL);
    AddPrim(&ot[5], mode);
    prim += 16;

    mode = (DR_MODE*)prim;
    SetDrawMode(mode, 0, 1, getTPage(0, 0, 768, 0), NULL);
    AddPrim(&ot[4], mode);
    prim += 16;

    VS_SCRATCH->nextPrim = prim;
}

void vs_battle_getGeomOffset(vs_GeomOffset* offset)
{
    int offsetX;
    int offsetY;

    // The original shifts OFX and OFY logically (srl 16), zero-extending them.
    gte_ReadGeomOffset(&offsetX, &offsetY);
    offset->x = offsetX & 0xFFFF;
    offset->y = offsetY & 0xFFFF;
}

void vs_battle_setGeomOffset(vs_GeomOffset* offset)
{
    SetGeomOffset(offset->x, offset->y);
}

u_char func_80099514(u_int x, int z, int layer)
{
    MpdRoomCollisionMapSection* map = vs_battle_roomData.collisionMapSection;
    int layerSize = map->unk6;
    short* cells = (short*)(map + 1) + layer * layerSize;
    signed char* blockData = (signed char*)(map + 1) + layerSize * 4;
    u_int tileZ = z;
    short cell = cells[(tileZ >> 2) * map->width + (x >> 2)];
    int blockIndex = cell & 0x7FF;
    int baseHeight = (cell & 0xF800) >> 9;
    signed char sub = blockData[blockIndex * 16 + (z & 3) * 4 + (x & 3)];

    return ((sub + baseHeight) & 0x7F) | (sub & 0x80);
}

#endif
