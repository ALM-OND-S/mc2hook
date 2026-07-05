#include "ShaderHandler.h"
#include <d3d9.h>
#include <cmath>

// Engine's global D3D device pointer at 0x85836C
static hook::Type<IDirect3DDevice9*> DevicePtr(0x85836C);

typedef HRESULT(WINAPI* CreateVertexShader_t)(IDirect3DDevice9*, const DWORD*, IDirect3DVertexShader9**);
typedef HRESULT(WINAPI* CreatePixelShader_t)(IDirect3DDevice9*, const DWORD*, IDirect3DPixelShader9**);
typedef HRESULT(WINAPI* SetVertexShader_t)(IDirect3DDevice9*, IDirect3DVertexShader9*);
typedef HRESULT(WINAPI* SetPixelShader_t)(IDirect3DDevice9*, IDirect3DPixelShader9*);
typedef HRESULT(WINAPI* SetVSConstF_t)(IDirect3DDevice9*, UINT, const float*, DWORD);
typedef HRESULT(WINAPI* SetRenderState_t)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
typedef HRESULT(WINAPI* SetTransform_t)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);

static CreateVertexShader_t  OrgCreateVertexShader        = nullptr;
static CreatePixelShader_t   OrgCreatePixelShader         = nullptr;
static SetVertexShader_t     OrgSetVertexShader           = nullptr;
static SetPixelShader_t      OrgSetPixelShader            = nullptr;
static SetVSConstF_t         OrgSetVertexShaderConstantF  = nullptr;
static SetRenderState_t      OrgSetRenderState            = nullptr;
static SetTransform_t        OrgSetTransform              = nullptr;

// ps_3_0 has no fixed-function fog, so the body PS lerps fog manually from c2. We capture
// D3DRS_FOGCOLOR and push it to the active upgraded PS; g_ActiveFogReg is that PS's fog register
// (c2 for the body PS, -1 for none). Keeps fog current when the PS cache suppresses a re-bind.
static float g_FogColor[4]  = { 0.5f, 0.5f, 0.5f, 1.0f };
static int   g_ActiveFogReg = -1;

// Scene ambient (VS c3), cached as the engine uploads it and pushed to the body PS's c4.
// Avoids GetVertexShaderConstantF, which forces a GPU pipeline flush.
static float g_CachedAmbient[4] = {};

// Per-submesh material Specular → PS c3. The engine only forwards material Diffuse to a constant, so
// we shim its per-draw callback slot (0x85B34C): read the live material's Specular (+0x20) into c3,
// then tail-call the original. Scoped to the body pass; the engine re-points/clears the slot elsewhere.
typedef void(__cdecl* PerMaterialCb_t)(void* material, void* baseTex, void* mask);
static PerMaterialCb_t g_OrgPerMaterialCb = nullptr;

static bool g_BodyPSActive = false;   // true while the body PS is bound (gates per-draw light upload)

// Light path: the engine's own directional set is tiny (2-3) and churns, so we drive the PS
// ourselves. The gather detour harvests the K nearest light nodes (world pos + colour + range) into
// g_DynLights; UploadLights turns each into a view-space dir + attenuated colour and pushes K to the
// PS. K = MaxDynamicLights (1..KMAX); more lights = fewer selection pops. KMAX must equal MAX_LIGHTS.
#define KMAX 16
struct DynLight { float pos[3]; float rgb[3]; float range; float d2; };
static DynLight g_DynLights[KMAX]    = {};
static int      g_DynCount           = 0;
static float    g_LightQueryPos[3]   = {};   // gather query = car position (cached in the detour)
static int      g_MaxDynLights       = 6;    // [Shader] MaxDynamicLights; K nearest lights (1..16)
static float    g_LightFalloff       = 1.0f; // [Shader] LightFalloff; scales light attenuation

// World→view matrix, captured from the FFP D3DTS_VIEW set each scene. The shader's own matrices only
// reach view space, so we use this to bring the world-space light dirs into view (WorldDirToView).
static float g_ViewMatrix[16]  = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

// ── Shader bytecode ──────────────────────────────────────────────────────────
// Compiled bytecode, swapped in at shader-creation time (zero per-frame overhead). Only the body
// pair is upgraded to SM3.0; the chrome pair is stubbed ({0}) so HasBytecode() is false and the hook
// leaves chrome vanilla (it draws only the menu/garage preview). Re-enable chrome by un-stubbing the
// arrays and re-adding the shaders to config.json.
//   0x6486F8  FresnelHeadlights VS  vs_1_1 → vs_3_0  geometry/material; N + incident to the PS (view-space)
//   0x648B38  projectedmaskcar PS   ps_1_1 → ps_3_0  view-space lighting loop + Cook-Torrance + IBL + fog
// ─────────────────────────────────────────────────────────────────────────────

// Chrome VS/PS (vanilla; bytecode stubbed → not swapped).
static const DWORD VS_675DF0[] = { 0 };
static const DWORD PS_676088[] = { 0 };

// FresnelHeadlights VS (vs_3_0). Constants: c4=MVP, c11=material, c15=fog, c16=shaderConst,
// c17-c19=WorldView, c20-c22=normal matrix (→view), c23-c30=headlight projectors.
// Outputs: Specular(material.rgb, shineMax), baseUV, projA/B, ViewNorm (view-space), IncidentDir (view .xyz, fog .w).
static const DWORD VS_6486F8[] = {
    0xFFFE0300, 0x009EFFFE, 0x42415443, 0x0000001C,
    0x0000024C, 0xFFFE0300, 0x00000010, 0x0000001C,
    0x00000108, 0x00000245, 0x0000015C, 0x000F0002,
    0x003E0001, 0x00000168, 0x00000000, 0x00000178,
    0x00040002, 0x00120004, 0x00000180, 0x00000000,
    0x00000190, 0x000B0002, 0x002E0001, 0x00000168,
    0x00000000, 0x0000019B, 0x00140002, 0x00520001,
    0x00000168, 0x00000000, 0x000001A6, 0x00150002,
    0x00560001, 0x00000168, 0x00000000, 0x000001B1,
    0x00160002, 0x005A0001, 0x00000168, 0x00000000,
    0x000001BC, 0x00170002, 0x005E0001, 0x00000168,
    0x00000000, 0x000001CB, 0x00180002, 0x00620001,
    0x00000168, 0x00000000, 0x000001DA, 0x001A0002,
    0x006A0001, 0x00000168, 0x00000000, 0x000001E9,
    0x001B0002, 0x006E0001, 0x00000168, 0x00000000,
    0x000001F8, 0x001C0002, 0x00720001, 0x00000168,
    0x00000000, 0x00000207, 0x001E0002, 0x007A0001,
    0x00000168, 0x00000000, 0x00000216, 0x00100002,
    0x00420001, 0x00000168, 0x00000000, 0x00000224,
    0x00110002, 0x00460001, 0x00000168, 0x00000000,
    0x0000022F, 0x00120002, 0x004A0001, 0x00000168,
    0x00000000, 0x0000023A, 0x00130002, 0x004E0001,
    0x00000168, 0x00000000, 0x6F465F67, 0x72615067,
    0x00736D61, 0x00030001, 0x00040001, 0x00000001,
    0x00000000, 0x564D5F67, 0xABAB0050, 0x00030002,
    0x00040004, 0x00000001, 0x00000000, 0x614D5F67,
    0x69726574, 0x67006C61, 0x726F4E5F, 0x776F526D,
    0x5F670030, 0x6D726F4E, 0x31776F52, 0x4E5F6700,
    0x526D726F, 0x0032776F, 0x72505F67, 0x65546A6F,
    0x6F523178, 0x67003077, 0x6F72505F, 0x7865546A,
    0x776F5231, 0x5F670031, 0x6A6F7250, 0x31786554,
    0x57776F52, 0x505F6700, 0x546A6F72, 0x52327865,
    0x0030776F, 0x72505F67, 0x65546A6F, 0x6F523278,
    0x67003177, 0x6F72505F, 0x7865546A, 0x776F5232,
    0x5F670057, 0x64616853, 0x6F437265, 0x0074736E,
    0x69565F67, 0x6F527765, 0x67003077, 0x6569565F,
    0x776F5277, 0x5F670031, 0x77656956, 0x32776F52,
    0x5F737600, 0x00305F33, 0x7263694D, 0x666F736F,
    0x52282074, 0x4C482029, 0x53204C53, 0x65646168,
    0x6F432072, 0x6C69706D, 0x31207265, 0x00312E30,
    0x0200001F, 0x80000000, 0x900F0000, 0x0200001F,
    0x80000003, 0x900F0001, 0x0200001F, 0x80000005,
    0x900F0002, 0x0200001F, 0x80000000, 0xE00F0000,
    0x0200001F, 0x8001000A, 0xE00F0001, 0x0200001F,
    0x80000005, 0xE0030002, 0x0200001F, 0x80020005,
    0xE00F0003, 0x0200001F, 0x80030005, 0xE00F0004,
    0x0200001F, 0x80040005, 0xE0070005, 0x0200001F,
    0x80050005, 0xE00F0006, 0x03000009, 0xE0010000,
    0xA0E40004, 0x90E40000, 0x03000009, 0xE0020000,
    0xA0E40005, 0x90E40000, 0x03000009, 0xE0040000,
    0xA0E40006, 0x90E40000, 0x03000009, 0xE0080000,
    0xA0E40007, 0x90E40000, 0x03000009, 0x80040000,
    0x90E40000, 0xA0E40013, 0x04000004, 0xE0080006,
    0x80AA0000, 0xA055000F, 0xA000000F, 0x03000008,
    0xE0010005, 0x90E40001, 0xA0E40014, 0x03000008,
    0xE0020005, 0x90E40001, 0xA0E40015, 0x03000008,
    0xE0040005, 0x90E40001, 0xA0E40016, 0x03000009,
    0x80010000, 0x90E40000, 0xA0E40011, 0x03000009,
    0x80020000, 0x90E40000, 0xA0E40012, 0x03000008,
    0x80010001, 0x80E40000, 0x80E40000, 0x02000007,
    0x80010001, 0x80000001, 0x03000005, 0xE0070006,
    0x80E40000, 0x80000001, 0x02000001, 0x80080000,
    0xA0000010, 0x03000009, 0xE0010003, 0x80E40000,
    0xA0E40017, 0x03000009, 0xE0020003, 0x80E40000,
    0xA0E40018, 0x03000009, 0xE0080003, 0x80E40000,
    0xA0E4001A, 0x03000009, 0xE0010004, 0x80E40000,
    0xA0E4001B, 0x03000009, 0xE0020004, 0x80E40000,
    0xA0E4001C, 0x03000009, 0xE0080004, 0x80E40000,
    0xA0E4001E, 0x02000001, 0xE0070001, 0xA0E4000B,
    0x02000001, 0xE0080001, 0xA0AA0010, 0x02000001,
    0xE0030002, 0x90E40002, 0x02000001, 0xE0040003,
    0xA0000010, 0x02000001, 0xE0040004, 0xA0000010,
    0x0000FFFF,
};


// ── projectedmaskcar PS — ps_3_0 (IBL, rep-i0 light loop, manual fog c2) ──
// Dynamic loop: count in i0 (SetPixelShaderConstantI), view-space dirs c33.., colors c17.., ambient c4.
static const DWORD PS_648B38[] = {
    0xFFFF0300, 0x0092FFFE, 0x42415443, 0x0000001C,
    0x0000021B, 0xFFFF0300, 0x0000000C, 0x0000001C,
    0x00000108, 0x00000214, 0x0000010C, 0x00040002,
    0x00120001, 0x00000118, 0x00000000, 0x00000128,
    0x00020002, 0x000A0001, 0x00000118, 0x00000000,
    0x00000133, 0x00110002, 0x00460010, 0x00000140,
    0x00000000, 0x00000150, 0x00000001, 0x00000001,
    0x00000160, 0x00000000, 0x00000170, 0x00210002,
    0x00860010, 0x00000180, 0x00000000, 0x00000190,
    0x00030002, 0x000E0001, 0x00000118, 0x00000000,
    0x0000019E, 0x00000002, 0x00020001, 0x00000118,
    0x00000000, 0x000001A8, 0x00010002, 0x00060001,
    0x00000118, 0x00000000, 0x000001B2, 0x00000003,
    0x00020001, 0x000001BC, 0x00000000, 0x000001CC,
    0x00010003, 0x00060001, 0x000001D4, 0x00000000,
    0x000001E4, 0x00020003, 0x000A0001, 0x000001EC,
    0x00000000, 0x000001FC, 0x00030003, 0x000E0001,
    0x00000204, 0x00000000, 0x6D415F67, 0x6E656962,
    0xABAB0074, 0x00030001, 0x00040001, 0x00000001,
    0x00000000, 0x6F465F67, 0x6C6F4367, 0x6700726F,
    0x67694C5F, 0x6F437468, 0x00726F6C, 0x00030001,
    0x00040001, 0x00000010, 0x00000000, 0x694C5F67,
    0x43746867, 0x746E756F, 0xABABAB00, 0x00020000,
    0x00010001, 0x00000001, 0x00000000, 0x694C5F67,
    0x44746867, 0x53567269, 0xABABAB00, 0x00030001,
    0x00040001, 0x00000010, 0x00000000, 0x614D5F67,
    0x65705374, 0x616C7563, 0x5F670072, 0x61726150,
    0x0030736D, 0x61505F67, 0x736D6172, 0x5F730031,
    0x65736142, 0xABABAB00, 0x000C0004, 0x00010001,
    0x00000001, 0x00000000, 0x75435F73, 0xAB006562,
    0x000E0004, 0x00010001, 0x00000001, 0x00000000,
    0x614D5F73, 0x00416B73, 0x000C0004, 0x00010001,
    0x00000001, 0x00000000, 0x614D5F73, 0x00426B73,
    0x000C0004, 0x00010001, 0x00000001, 0x00000000,
    0x335F7370, 0x4D00305F, 0x6F726369, 0x74666F73,
    0x29522820, 0x534C4820, 0x6853204C, 0x72656461,
    0x6D6F4320, 0x656C6970, 0x30312072, 0xAB00312E,
    0x05000051, 0xA00F0005, 0xBF266666, 0x3F800000,
    0xBF800000, 0x00000000, 0x05000051, 0xA00F0006,
    0x80000000, 0xBF800000, 0xC0000000, 0xC0400000,
    0x05000051, 0xA00F0007, 0xC0800000, 0xC0A00000,
    0xC0C00000, 0xC0E00000, 0x05000051, 0xA00F0008,
    0xC1000000, 0xC1100000, 0xC1200000, 0xC1300000,
    0x05000051, 0xA00F0009, 0xC1400000, 0xC1500000,
    0xC1600000, 0xC1700000, 0x05000051, 0xA00F000A,
    0x3F000000, 0x42800000, 0xC1147AE1, 0x00000000,
    0x05000051, 0xA00F000B, 0xC1800000, 0x40490FDB,
    0xB727C5AC, 0x47C35000, 0x05000051, 0xA00F000C,
    0xBF800000, 0xBCE147AE, 0xBF126E98, 0x3CB43958,
    0x05000051, 0xA00F000D, 0x3F800000, 0x3D2E147B,
    0x3F851EB8, 0xBD23D70A, 0x05000051, 0xA00F000E,
    0xBF851EB8, 0x3F851EB8, 0x00000000, 0x00000000,
    0x0200001F, 0x8001000A, 0x900F0000, 0x0200001F,
    0x80000005, 0x90030001, 0x0200001F, 0x80020005,
    0x900F0002, 0x0200001F, 0x80030005, 0x900F0003,
    0x0200001F, 0x80040005, 0x90070004, 0x0200001F,
    0x80050005, 0x900F0005, 0x0200001F, 0x90000000,
    0xA00F0800, 0x0200001F, 0x98000000, 0xA00F0801,
    0x0200001F, 0x90000000, 0xA00F0802, 0x0200001F,
    0x90000000, 0xA00F0803, 0x03000042, 0x800F0000,
    0x90E40001, 0xA0E40800, 0x03010042, 0x800F0001,
    0x90E40002, 0xA0E40802, 0x03010042, 0x800F0002,
    0x90E40003, 0xA0E40803, 0x02000024, 0x80070003,
    0x90E40004, 0x03000008, 0x80080002, 0x91E40005,
    0x91E40005, 0x02000007, 0x80080002, 0x80FF0002,
    0x03000005, 0x80070004, 0x80FF0002, 0x91E40005,
    0x02000024, 0x80070005, 0x90E40005, 0x03000008,
    0x80080003, 0x80E40005, 0x80E40003, 0x03000002,
    0x80080003, 0x80FF0003, 0x80FF0003, 0x04000004,
    0x80070005, 0x80E40003, 0x81FF0003, 0x80E40005,
    0x03000008, 0x80180003, 0x80E40003, 0x80E40004,
    0x03000005, 0x80070006, 0x80E40000, 0x90E40000,
    0x02000001, 0x80180004, 0x90FF0000, 0x04000004,
    0x80080004, 0x80FF0004, 0xA0000005, 0xA0550005,
    0x03000005, 0x80080006, 0x80FF0004, 0x80FF0004,
    0x03000005, 0x80010007, 0x80FF0006, 0x80FF0006,
    0x03000002, 0x80020007, 0x81FF0004, 0xA0550005,
    0x0300000B, 0x80010008, 0x80550007, 0xA0000000,
    0x03000002, 0x80020007, 0x81FF0003, 0xA0550005,
    0x03000005, 0x80040007, 0x80550007, 0x80550007,
    0x03000005, 0x80040007, 0x80AA0007, 0x80AA0007,
    0x03000005, 0x80020007, 0x80AA0007, 0x80550007,
    0x04000012, 0x80010009, 0x80550007, 0x80000008,
    0xA0000000, 0x03000002, 0x80020007, 0x81000009,
    0xA0550005, 0x04000004, 0x80040007, 0x80FF0006,
    0x80FF0006, 0xA0AA0005, 0x02000001, 0x800A0008,
    0xA0E40005, 0x03000005, 0x80080007, 0x80FF0003,
    0x80FF0003, 0x04000004, 0x80080006, 0x80FF0006,
    0x81FF0006, 0xA0550005, 0x04000004, 0x80080007,
    0x80FF0007, 0x80FF0006, 0x80000007, 0x02000007,
    0x80080007, 0x80FF0007, 0x02000006, 0x80080007,
    0x80FF0007, 0x02000001, 0x80070009, 0xA0FF0005,
    0x02000001, 0x8007000A, 0xA0FF0005, 0x02000001,
    0x80010008, 0xA0FF0005, 0x01000026, 0xF0E40000,
    0x02000001, 0x80040008, 0xA100000B, 0x0203002D,
    0x80000008, 0x80AA0008, 0x03000002, 0x800F000B,
    0x80000008, 0xA0E40006, 0x03000002, 0x800F000C,
    0x80000008, 0xA0E40007, 0x03000002, 0x800F000D,
    0x80000008, 0xA0E40008, 0x03000002, 0x800F000E,
    0x80000008, 0xA0E40009, 0x04000058, 0x8007000F,
    0x8C00000B, 0xA0E40021, 0x80FF0008, 0x04000058,
    0x8007000F, 0x8C55000B, 0xA0E40022, 0x80E4000F,
    0x04000058, 0x8007000F, 0x8CAA000B, 0xA0E40023,
    0x80E4000F, 0x04000058, 0x8007000F, 0x8CFF000B,
    0xA0E40024, 0x80E4000F, 0x04000058, 0x8007000F,
    0x8C00000C, 0xA0E40025, 0x80E4000F, 0x04000058,
    0x8007000F, 0x8C55000C, 0xA0E40026, 0x80E4000F,
    0x04000058, 0x8007000F, 0x8CAA000C, 0xA0E40027,
    0x80E4000F, 0x04000058, 0x8007000F, 0x8CFF000C,
    0xA0E40028, 0x80E4000F, 0x04000058, 0x8007000F,
    0x8C00000D, 0xA0E40029, 0x80E4000F, 0x04000058,
    0x8007000F, 0x8C55000D, 0xA0E4002A, 0x80E4000F,
    0x04000058, 0x8007000F, 0x8CAA000D, 0xA0E4002B,
    0x80E4000F, 0x04000058, 0x8007000F, 0x8CFF000D,
    0xA0E4002C, 0x80E4000F, 0x04000058, 0x8007000F,
    0x8C00000E, 0xA0E4002D, 0x80E4000F, 0x04000058,
    0x8007000F, 0x8C55000E, 0xA0E4002E, 0x80E4000F,
    0x04000058, 0x8007000F, 0x8CAA000E, 0xA0E4002F,
    0x80E4000F, 0x04000058, 0x8007000F, 0x8CFF000E,
    0xA0E40030, 0x80E4000F, 0x03000008, 0x80040008,
    0x80E40003, 0x80E4000F, 0x0300000B, 0x80080009,
    0x80AA0008, 0xA0FF0005, 0x04000058, 0x80070010,
    0x8C00000B, 0xA0E40011, 0x80FF0008, 0x04000058,
    0x80070010, 0x8C55000B, 0xA0E40012, 0x80E40010,
    0x04000058, 0x8007000B, 0x8CAA000B, 0xA0E40013,
    0x80E40010, 0x04000058, 0x8007000B, 0x8CFF000B,
    0xA0E40014, 0x80E4000B, 0x04000058, 0x8007000B,
    0x8C00000C, 0xA0E40015, 0x80E4000B, 0x04000058,
    0x8007000B, 0x8C55000C, 0xA0E40016, 0x80E4000B,
    0x04000058, 0x8007000B, 0x8CAA000C, 0xA0E40017,
    0x80E4000B, 0x04000058, 0x8007000B, 0x8CFF000C,
    0xA0E40018, 0x80E4000B, 0x04000058, 0x8007000B,
    0x8C00000D, 0xA0E40019, 0x80E4000B, 0x04000058,
    0x8007000B, 0x8C55000D, 0xA0E4001A, 0x80E4000B,
    0x04000058, 0x8007000B, 0x8CAA000D, 0xA0E4001B,
    0x80E4000B, 0x04000058, 0x8007000B, 0x8CFF000D,
    0xA0E4001C, 0x80E4000B, 0x04000058, 0x8007000B,
    0x8C00000E, 0xA0E4001D, 0x80E4000B, 0x04000058,
    0x8007000B, 0x8C55000E, 0xA0E4001E, 0x80E4000B,
    0x04000058, 0x8007000B, 0x8CAA000E, 0xA0E4001F,
    0x80E4000B, 0x04000058, 0x8007000B, 0x8CFF000E,
    0xA0E40020, 0x80E4000B, 0x04000004, 0x80070009,
    0x80FF0009, 0x80E4000B, 0x80E40009, 0x04000004,
    0x8007000C, 0x91E40005, 0x80FF0002, 0x80E4000F,
    0x02000024, 0x8007000D, 0x80E4000C, 0x03000008,
    0x80040008, 0x80E40003, 0x80E4000D, 0x0300000B,
    0x8008000A, 0x80AA0008, 0xA0FF0005, 0x03000008,
    0x80040008, 0x80E40004, 0x80E4000D, 0x03000005,
    0x8008000A, 0x80FF000A, 0x80FF000A, 0x04000004,
    0x8008000A, 0x80FF000A, 0x80AA0007, 0xA0550005,
    0x03000005, 0x8008000A, 0x80FF000A, 0x80FF000A,
    0x03000005, 0x8008000A, 0x80FF000A, 0xA055000B,
    0x02000006, 0x8008000A, 0x80FF000A, 0x03000005,
    0x8008000A, 0x80000007, 0x80FF000A, 0x03000002,
    0x8008000B, 0x81AA0008, 0xA0550005, 0x04000058,
    0x80040008, 0x80AA0008, 0x80FF000B, 0xA0550005,
    0x03000005, 0x8008000B, 0x80AA0008, 0x80AA0008,
    0x03000005, 0x8008000B, 0x80FF000B, 0x80FF000B,
    0x03000005, 0x80040008, 0x80AA0008, 0x80FF000B,
    0x04000012, 0x8008000B, 0x80AA0008, 0x80550008,
    0xA0000000, 0x03000005, 0x80040008, 0x80FF0009,
    0x80FF0009, 0x04000004, 0x80040008, 0x80AA0008,
    0x80FF0006, 0x80000007, 0x02000007, 0x80040008,
    0x80AA0008, 0x02000006, 0x80040008, 0x80AA0008,
    0x03000005, 0x80040008, 0x80FF0003, 0x80AA0008,
    0x04000004, 0x80040008, 0x80FF0009, 0x80FF0007,
    0x80AA0008, 0x03000002, 0x8001000C, 0x80AA0008,
    0xA0AA000B, 0x02000006, 0x80040008, 0x80AA0008,
    0x04000058, 0x80040008, 0x8000000C, 0x80AA0008,
    0xA0FF000B, 0x03000005, 0x80040008, 0x80AA0008,
    0x80FF000A, 0x03000005, 0x80040008, 0x80AA0008,
    0xA000000A, 0x0300000A, 0x8008000A, 0x80AA0008,
    0xA055000A, 0x03000005, 0x80040008, 0x80FF000B,
    0x80FF000A, 0x03000005, 0x8007000B, 0x80E4000B,
    0x80AA0008, 0x04000004, 0x8007000B, 0x80E4000B,
    0x80FF0009, 0x80E4000A, 0x04000058, 0x8007000A,
    0x81FF0009, 0x80E4000A, 0x80E4000B, 0x03000002,
    0x80010008, 0x80000008, 0xA0550005, 0x00000027,
    0x03000002, 0x80070003, 0x80E40009, 0xA0E40004,
    0x03000005, 0x80070003, 0x80E40006, 0x80E40003,
    0x02000001, 0x80040004, 0xA0AA0000, 0x03000005,
    0x80070004, 0x80AA0004, 0xA0E40003, 0x03000005,
    0x80070004, 0x80E40004, 0x80E4000A, 0x03000005,
    0x80080005, 0x80FF0004, 0xA0550001, 0x0300005F,
    0x800F0005, 0x80E40005, 0xA0E40801, 0x02000001,
    0x800F0006, 0xA0E4000C, 0x04000004, 0x800F0006,
    0x80FF0004, 0x80E40006, 0xA0E4000D, 0x03000005,
    0x80080002, 0x80000006, 0x80000006, 0x03000005,
    0x80080003, 0x80FF0003, 0xA0AA000A, 0x0200000E,
    0x80080003, 0x80FF0003, 0x0300000A, 0x80080004,
    0x80FF0003, 0x80FF0002, 0x04000004, 0x80080002,
    0x80FF0004, 0x80000006, 0x80550006, 0x04000004,
    0x80030006, 0x80FF0002, 0xA0E4000E, 0x80EE0006,
    0x04000004, 0x80080002, 0xA0000000, 0x80000006,
    0x80550006, 0x03000005, 0x80070005, 0x80FF0002,
    0x80E40005, 0x03000005, 0x80070005, 0x80E40005,
    0xA0550000, 0x04000004, 0x80070003, 0x80550007,
    0x80E40003, 0x80E40005, 0x04000004, 0x80070001,
    0x80E40002, 0x80FF0001, 0x80E40001, 0x03000005,
    0x80070001, 0x80E40001, 0x80E40003, 0x04000004,
    0x80070001, 0x80E40001, 0xA0FF0000, 0x81E40003,
    0x04000004, 0x80070001, 0xA0000001, 0x80E40001,
    0x80E40003, 0x04000004, 0x80070000, 0x80E40004,
    0x80E40000, 0x80E40001, 0x02000001, 0x80110001,
    0x90FF0005, 0x03000002, 0x80070000, 0x80E40000,
    0xA1E40002, 0x04000004, 0x80170800, 0x80000001,
    0x80E40000, 0xA0E40002, 0x02000001, 0x80080800,
    0x80FF0000, 0x0000FFFF,
};

// Body PS constants. c0/c1 = tunables (below); c3 = per-material Specular; c4 = ambient;
// c17.. = light colours; c33.. = view-space light dirs; i0 = light count.
static float g_PS648B38[8] = {
 /* c0.x Reflectance (F0)       */ 0.04f,   // dielectric clearcoat F0; per-material Specular (c3) modulates
 /* c0.y EnvmapStrength         */ 6.00f,   // envmap (s1 env cube) reflection strength
 /* c0.z SpecularIntensity      */ 1.00f,   // direct Cook-Torrance spec scale (× material Specular c3)
 /* c0.w HeadlightMaskScale     */ 3.50f,   // fx_headlight_mask (s2) projected-mask gain on the body
 /* c1.x HeadlightMaskOnBody    */ 1.50f,   // headlight-mask projection blend (0=off; >1 over-applies)
 /* c1.y MAX_MIP                */ 0.00f,   // set at first body bind from cube GetLevelCount()
 /* c1.z (unused)               */ 0.00f,
 /* c1.w (unused)               */ 0.00f
};

static float g_PS676088[8] = { 1.00f, 15.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f };

static IDirect3DPixelShader9*  sPS_648B38_handle = nullptr;
static IDirect3DPixelShader9*  sPS_676088_handle = nullptr;

// Returns false if the bytecode pointer is null or its first DWORD is zero (stub/unset).
static bool HasBytecode(const DWORD* p) { return p && *p; }

// Rotate a world-space direction into view space with the captured view matrix (D3D row-vector
// convention: out = w · M, dotting the columns). Identity until the matrix is captured → passthrough.
static void WorldDirToView(const float* w, float* out)
{
    const float* m = g_ViewMatrix;   // row-major: m[row*4+col]
    out[0] = w[0]*m[0] + w[1]*m[4] + w[2]*m[8];
    out[1] = w[0]*m[1] + w[1]*m[5] + w[2]*m[9];
    out[2] = w[0]*m[2] + w[1]*m[6] + w[2]*m[10];
}

static void UploadLights(IDirect3DDevice9* pDevice)
{
    pDevice->SetPixelShaderConstantF(4, g_CachedAmbient, 1);   // c4 = scene ambient (VS c3)

    // K nearest harvested light nodes → per-light dir (world→view bridged) + attenuated colour.
    int K = g_DynCount;
    if (K > g_MaxDynLights) K = g_MaxDynLights;
    if (K > KMAX)           K = KMAX;

    float cols[4 * KMAX] = {};
    float dirs[4 * KMAX] = {};
    for (int i = 0; i < K; ++i)
    {
        const DynLight& dl = g_DynLights[i];
        float dx = dl.pos[0] - g_LightQueryPos[0];
        float dy = dl.pos[1] - g_LightQueryPos[1];
        float dz = dl.pos[2] - g_LightQueryPos[2];                  // car → light (toward-light = L), WORLD
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        float inv = (len > 1e-6f) ? 1.0f / len : 0.0f;
        float wL[3] = { dx * inv, dy * inv, dz * inv };
        WorldDirToView(wL, dirs + i * 4);                           // WORLD → VIEW (match view-space N)

        // Smooth range falloff: saturate(1 - d²/range²)², scaled by LightFalloff.
        float r2 = dl.range * dl.range;
        float a  = (r2 > 1e-6f) ? (1.0f - dl.d2 / r2) : 0.0f;
        if (a < 0.0f) a = 0.0f;
        a = a * a * g_LightFalloff;
        cols[i * 4 + 0] = dl.rgb[0] * a;
        cols[i * 4 + 1] = dl.rgb[1] * a;
        cols[i * 4 + 2] = dl.rgb[2] * a;
    }

    if (K > 0)
    {
        pDevice->SetPixelShaderConstantF(17, cols, K);             // c17.. colors  (c17-c32, up to 16)
        pDevice->SetPixelShaderConstantF(33, dirs, K);             // c33.. dirs    (c33-c48, up to 16)
    }
    const int i0[4] = { K, 0, 0, 0 };
    pDevice->SetPixelShaderConstantI(0, i0, 1);                    // i0 = live light count
}

static HRESULT WINAPI Hook_CreateVertexShader(IDirect3DDevice9* pDevice, const DWORD* pFunction, IDirect3DVertexShader9** ppShader)
{
    const DWORD* finalBytecode = pFunction;
    const char*  label         = nullptr;

    if      (pFunction == reinterpret_cast<const DWORD*>(0x006486F8) && HasBytecode(VS_6486F8))
    { finalBytecode = VS_6486F8; label = "FresnelHeadlights VS (0x6486F8) [vs_3_0]"; }
    else if (pFunction == reinterpret_cast<const DWORD*>(0x00675DF0) && HasBytecode(VS_675DF0))
    { finalBytecode = VS_675DF0;     label = "CubeMapFresnel VS (0x675DF0) [vs_2_0]"; }
    else if (pFunction == reinterpret_cast<const DWORD*>(0x00675868))
    { label = "UnlitSkinned (0x675868) [vs_1_1]"; }
    else if (pFunction == reinterpret_cast<const DWORD*>(0x00675950))
    { label = "LitSkinned (0x675950) [vs_1_1]"; }
    else if (pFunction == reinterpret_cast<const DWORD*>(0x00675B30))
    { label = "LitSkinned3 (0x675B30) [vs_1_1]"; }

    HRESULT hr = OrgCreateVertexShader(pDevice, finalBytecode, ppShader);

    if (FAILED(hr) && finalBytecode != pFunction)
    {
        hook_output("[Shader] VS upgrade FAILED (0x%08X): %s — reverting", hr, label ? label : "?");
        hr = OrgCreateVertexShader(pDevice, pFunction, ppShader);
    }

    return hr;   // VS handle not needed — no per-bind VS constants (geometry/material only)
}

static HRESULT WINAPI Hook_CreatePixelShader(IDirect3DDevice9* pDevice, const DWORD* pFunction, IDirect3DPixelShader9** ppShader)
{
    const DWORD* finalBytecode = pFunction;
    const char*  label         = nullptr;

    if      (pFunction == reinterpret_cast<const DWORD*>(0x00676088) && HasBytecode(PS_676088))
    { finalBytecode = PS_676088; label = "FresnelPS (0x676088) [ps_2_a]"; }
    else if (pFunction == reinterpret_cast<const DWORD*>(0x00648B38) && HasBytecode(PS_648B38))
    { finalBytecode = PS_648B38; label = "projectedmaskcar PS (0x648B38) [ps_3_0]"; }
    else if (pFunction == reinterpret_cast<const DWORD*>(0x00648A10))
    { label = "projectedmask (0x648A10) [ps_1_1]"; }
    else if (pFunction == reinterpret_cast<const DWORD*>(0x00648AA0))
    { label = "projectedmaskwindows (0x648AA0) [ps_1_1]"; }
    else if (pFunction == reinterpret_cast<const DWORD*>(0x00648BB8))
    { label = "projectedmaskdetailmap (0x648BB8) [ps_1_1]"; }

    HRESULT hr = OrgCreatePixelShader(pDevice, finalBytecode, ppShader);

    if (FAILED(hr) && finalBytecode != pFunction)
    {
        hook_output("[Shader] PS upgrade FAILED (0x%08X): %s — reverting", hr, label ? label : "?");
        hr = OrgCreatePixelShader(pDevice, pFunction, ppShader);
    }

    if (SUCCEEDED(hr) && ppShader && *ppShader)
    {
        if (finalBytecode == PS_648B38) sPS_648B38_handle = *ppShader;
        // Record the chrome handle ONLY when we actually swapped it (keyed on finalBytecode, not
        // pFunction — while the chrome bytecode is stubbed, finalBytecode never == PS_676088, so the
        // handle stays null and the dormant chrome branch never fires). Un-stub the array to re-enable.
        if (finalBytecode == PS_676088) sPS_676088_handle = *ppShader;
    }

    return hr;
}

static HRESULT WINAPI Hook_SetVertexShader(IDirect3DDevice9* pDevice, IDirect3DVertexShader9* pShader)
{
    return OrgSetVertexShader(pDevice, pShader);
}

// Per-material callback shim: push the submesh material's Specular to PS c3, then run the engine's
// original callback.
static void __cdecl Hook_PerMaterialCb(void* material, void* baseTex, void* mask)
{
    if (g_BodyPSActive && material)
    {
        IDirect3DDevice9* dev = DevicePtr.get();
        if (dev)
            dev->SetPixelShaderConstantF(3, (const float*)((const char*)material + 0x20), 1);  // Specular (+0x20) → c3
    }
    if (g_OrgPerMaterialCb)
        g_OrgPerMaterialCb(material, baseTex, mask);
}

// Point the engine's per-draw material callback slot (0x85B34C) at our shim, saving the original.
// The engine re-points/clears the slot at pass boundaries, so we re-assert it per draw while the
// body PS is active.
static inline void EnsurePerMaterialHook()
{
    void** slot = (void**)0x85B34C;
    void*  cur  = *slot;
    if (cur && cur != (void*)&Hook_PerMaterialCb)      // skip if cleared or already ours
    {
        g_OrgPerMaterialCb = (PerMaterialCb_t)cur;
        *slot = (void*)&Hook_PerMaterialCb;
    }
}

static HRESULT WINAPI Hook_SetVertexShaderConstantF(IDirect3DDevice9* pDevice, UINT StartReg, const float* pData, DWORD Count)
{
    // Cache the scene ambient (VS c3) as the engine uploads it, for the body PS's c4.
    if (StartReg <= 3 && (StartReg + Count) > 3)
        memcpy(g_CachedAmbient, pData + (3 - StartReg) * 4, 16);

    // Refresh the body-PS light set per draw. The PS is bound once for back-to-back traffic bodies, so
    // pushing only at the PS bind would strand them all on one snapshot. The per-draw normal-row upload
    // (c20-c22) fires once per model, after the detour has harvested that model's lights — re-push here.
    if (g_BodyPSActive && StartReg <= 22 && (StartReg + Count) > 20)
    {
        UploadLights(pDevice);
        EnsurePerMaterialHook();
    }

    return OrgSetVertexShaderConstantF(pDevice, StartReg, pData, Count);
}

static HRESULT WINAPI Hook_SetPixelShader(IDirect3DDevice9* pDevice, IDirect3DPixelShader9* pShader)
{
    HRESULT hr = OrgSetPixelShader(pDevice, pShader);
    if (SUCCEEDED(hr) && pShader)
    {
        if (pShader == sPS_648B38_handle)
        {
            g_ActiveFogReg  = 2;     // body PS reads manual fog from c2
            g_BodyPSActive  = true;  // gate the per-draw light upload
            EnsurePerMaterialHook(); // per-submesh material Specular → c3

            // Resolve MAX_MIP (c1.y) once from the env cube's level count (roughness→LOD needs a mip
            // chain). GenerateMipSubLevels is a no-op unless the cube was created autogen-mipmap, in
            // which case MAX_MIP stays 0 → sharp reflections only.
            static bool s_maxMipResolved = false;
            if (!s_maxMipResolved)
            {
                IDirect3DBaseTexture9* tex = nullptr;
                if (SUCCEEDED(pDevice->GetTexture(1, &tex)) && tex)
                {
                    if (tex->GetType() == D3DRTYPE_CUBETEXTURE)
                    {
                        DWORD levels = tex->GetLevelCount();
                        if (levels <= 1) { tex->GenerateMipSubLevels(); levels = tex->GetLevelCount(); }
                        g_PS648B38[5] = (levels > 1) ? float(levels - 1) : 0.0f;  // c1.y = MAX_MIP
                        s_maxMipResolved = true;
                    }
                    tex->Release();
                }
            }

            pDevice->SetPixelShaderConstantF(0, g_PS648B38,          2);  // c0/c1 tunable params (c1.y=MAX_MIP)
            pDevice->SetPixelShaderConstantF(2, g_FogColor,          1);  // c2 = fog color (ps_3_0 manual fog)
            static const float kWhiteSpec[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            pDevice->SetPixelShaderConstantF(3, kWhiteSpec,          1);  // c3 seed = white; the shim refines it per submesh

            UploadLights(pDevice);   // c4 ambient, c17.. colours, c33.. dirs, i0 count (re-pushed per draw)

            // Force material Diffuse (c11) white: textured body submeshes skip the engine's c11 upload
            // and would otherwise inherit a stale value that tints the paint.
            static const float kWhiteC11[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            pDevice->SetVertexShaderConstantF(11, kWhiteC11, 1);
        }
        else if (pShader == sPS_676088_handle)
        {
            // Dormant while chrome is stubbed (handle stays null). Live only if chrome is re-enabled.
            g_ActiveFogReg = -1;  // chrome uses FFP fog
            g_BodyPSActive = false;
            pDevice->SetPixelShaderConstantF(0, g_PS676088, 1);
        }
        else
        {
            g_ActiveFogReg = -1;  // not one of ours — no manual-fog push
            g_BodyPSActive = false;
        }
    }
    return hr;
}

// Capture D3DRS_FOGCOLOR and push it to the active PS's fog register, so a mid-pass change reaches
// the cached PS. Colour is 0xAARRGGBB.
static HRESULT WINAPI Hook_SetRenderState(IDirect3DDevice9* pDevice, D3DRENDERSTATETYPE State, DWORD Value)
{
    if (State == D3DRS_FOGCOLOR)
    {
        g_FogColor[0] = ((Value >> 16) & 0xFF) / 255.0f;
        g_FogColor[1] = ((Value >>  8) & 0xFF) / 255.0f;
        g_FogColor[2] = ((Value >>  0) & 0xFF) / 255.0f;
        g_FogColor[3] = ((Value >> 24) & 0xFF) / 255.0f;
        if (g_ActiveFogReg >= 0)
            pDevice->SetPixelShaderConstantF(g_ActiveFogReg, g_FogColor, 1);
    }
    return OrgSetRenderState(pDevice, State, Value);
}

// Capture the FFP view matrix (world→view) for the light world→view bridge (see WorldDirToView).
// The engine sets D3DTS_VIEW while drawing FFP world geometry; the main-scene camera is set before
// the vehicle body pass (env-cube faces also set it, but they precede the body draw).
static HRESULT WINAPI Hook_SetTransform(IDirect3DDevice9* pDevice, D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix)
{
    if (State == D3DTS_VIEW && pMatrix)
        memcpy(g_ViewMatrix, pMatrix, sizeof(g_ViewMatrix));
    return OrgSetTransform(pDevice, State, pMatrix);
}

// Per-cell nearest-N light gather. Replaces the engine's own insert (0x518100), whose flawed
// eviction leaves an unstable set that flickers as the car moves. This does a correct gather:
// append while under cap, else evict the true farthest iff the new source is closer. It also
// harvests our own nearest-K set for the PS lighting. State carries across the cell block in the
// engine's globals: count at 0x6C2638 (read at entry, written at exit), worst-slot/threshold at
// 0x6C262C/30/34 (kept coherent); out[]/dist[] are the caller's buffers.
//
// __fastcall(this→ECX, dummy→EDX, 6 stack args) matches the engine's __thiscall + retn 18h.
struct LightNode { float x, y, z; LightNode* next; };   // +0x00 pos, +0x0C next

static void __fastcall Hook_sub_518100(
    void* thisptr, void* /*edx*/,
    int col, int row, const float* query, void** out, float* dist2, int capN)
{
    if (!thisptr) return;
    int    gridWidth   = *(int*)   ((char*)thisptr + 0x420);
    void** bucketArray = *(void***)((char*)thisptr + 0x21C);
    if (!bucketArray) return;

    LightNode* node = (LightNode*)bucketArray[col + row * gridWidth];

    int count = *(int*)0x6C2638;            // carry-in: sources filled so far (across cells)
    const float qx = query[0], qy = query[1], qz = query[2];

    // Cache the query (= car pos) for UploadLights, and reset our nearest-K set at the first cell.
    g_LightQueryPos[0] = qx; g_LightQueryPos[1] = qy; g_LightQueryPos[2] = qz;
    if (count == 0) g_DynCount = 0;
    int dynK = g_MaxDynLights; if (dynK > KMAX) dynK = KMAX;

    for (; node; node = node->next)
    {
        if (!(qy < node->y))                 // eligibility: source must be above the query point
            continue;
        float dx = node->x - qx, dy = node->y - qy, dz = node->z - qz;
        float d2 = dx * dx + dy * dy + dz * dz;

        // Harvest this node into our nearest-K set (pos + colour P[7..9] + range P[0xB], P=*(node+0x1C)).
        {
            int slot = -1;
            if (g_DynCount < dynK) slot = g_DynCount++;
            else
            {
                int wi = 0;
                for (int i = 1; i < dynK; ++i)
                    if (g_DynLights[i].d2 > g_DynLights[wi].d2) wi = i;
                if (d2 < g_DynLights[wi].d2) slot = wi;
            }
            if (slot >= 0)
            {
                DynLight& dl = g_DynLights[slot];
                dl.pos[0] = node->x; dl.pos[1] = node->y; dl.pos[2] = node->z;
                dl.d2 = d2;
                const float* P = *(const float* const*)((const char*)node + 0x1C);
                if (P) { dl.rgb[0] = P[7]; dl.rgb[1] = P[8]; dl.rgb[2] = P[9]; dl.range = P[0xB]; }
                else   { dl.rgb[0] = dl.rgb[1] = dl.rgb[2] = 1.0f; dl.range = 100.0f; }
            }
        }

        if (count < capN)                    // room → append (engine's own out[]/dist[] set)
        {
            out[count]   = node;
            dist2[count] = d2;
            ++count;
        }
        else if (capN > 0)                   // full → evict the true farthest if we're closer
        {
            int wi = 0;
            for (int i = 1; i < capN; ++i)
                if (dist2[i] > dist2[wi]) wi = i;
            if (d2 < dist2[wi]) { out[wi] = node; dist2[wi] = d2; }
        }
    }

    *(int*)0x6C2638 = count;                 // carry-out: count

    int wi = 0;                              // keep worst-slot/threshold carry coherent
    for (int i = 1; i < count; ++i)
        if (dist2[i] > dist2[wi]) wi = i;
    *(int*)0x6C262C   = (count > 0) ? wi : 0;
    *(float*)0x6C2630 = (count > 0) ? dist2[wi] : 0.0f;
    *(float*)0x6C2634 = *(float*)0x6C2630;
}

// Overwrite the gather's entry (0x518100) with a 5-byte jmp to our version. Full replacement — no
// trampoline; the leftover bytes at 0x518105-06 are never reached.
static void InstallGatherDetour()
{
    BYTE* p = (BYTE*)0x518100;
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)((BYTE*)&Hook_sub_518100 - (p + 5));
    VirtualProtect(p, 5, old, &old);
}

static void __cdecl Hook_ShaderInitializationStage()
{
    IDirect3DDevice9* device = DevicePtr.get();
    if (device)
    {
        void** vtable = *(void***)device;

        OrgSetRenderState           = (SetRenderState_t)vtable[57];
        OrgSetTransform             = (SetTransform_t)vtable[44];
        OrgCreateVertexShader       = (CreateVertexShader_t)vtable[91];
        OrgSetVertexShader          = (SetVertexShader_t)vtable[92];
        OrgSetVertexShaderConstantF = (SetVSConstF_t)vtable[94];
        OrgCreatePixelShader        = (CreatePixelShader_t)vtable[106];
        OrgSetPixelShader           = (SetPixelShader_t)vtable[107];

        DWORD old;
        VirtualProtect(&vtable[44],  sizeof(void*), PAGE_EXECUTE_READWRITE, &old); vtable[44]  = &Hook_SetTransform;            VirtualProtect(&vtable[44],  sizeof(void*), old, &old);
        VirtualProtect(&vtable[57],  sizeof(void*), PAGE_EXECUTE_READWRITE, &old); vtable[57]  = &Hook_SetRenderState;          VirtualProtect(&vtable[57],  sizeof(void*), old, &old);
        VirtualProtect(&vtable[91],  sizeof(void*), PAGE_EXECUTE_READWRITE, &old); vtable[91]  = &Hook_CreateVertexShader;       VirtualProtect(&vtable[91],  sizeof(void*), old, &old);
        VirtualProtect(&vtable[92],  sizeof(void*), PAGE_EXECUTE_READWRITE, &old); vtable[92]  = &Hook_SetVertexShader;           VirtualProtect(&vtable[92],  sizeof(void*), old, &old);
        VirtualProtect(&vtable[94],  sizeof(void*), PAGE_EXECUTE_READWRITE, &old); vtable[94]  = &Hook_SetVertexShaderConstantF; VirtualProtect(&vtable[94],  sizeof(void*), old, &old);
        VirtualProtect(&vtable[106], sizeof(void*), PAGE_EXECUTE_READWRITE, &old); vtable[106] = &Hook_CreatePixelShader;         VirtualProtect(&vtable[106], sizeof(void*), old, &old);
        VirtualProtect(&vtable[107], sizeof(void*), PAGE_EXECUTE_READWRITE, &old); vtable[107] = &Hook_SetPixelShader;            VirtualProtect(&vtable[107], sizeof(void*), old, &old);
    }

    typedef void(__cdecl* InitAllShaders_t)();
    ((InitAllShaders_t)0x5FC170)();
}

void ShaderHandler::Install()
{
    if (!HookConfig::GetBool("Shader", "ShaderEnable", false))   // default OFF — opt-in via mc2hook.ini
    {
        hook_output("[Shader] Disabled in config");
        return;
    }

    // mc2hook.ini [Shader] tunables (pushed to the body PS at bind).
    g_PS648B38[0] = HookConfig::GetFloat("Shader", "Reflectance",         0.04f);  // F0 (dielectric clearcoat)
    g_PS648B38[1] = HookConfig::GetFloat("Shader", "EnvmapStrength",      5.00f);  // env-cube reflection strength
    g_PS648B38[2] = HookConfig::GetFloat("Shader", "SpecularIntensity",   2.00f);  // direct specular scale
    g_PS648B38[3] = HookConfig::GetFloat("Shader", "MaskScale",           3.50f);  // headlight-mask gain on the body
    g_PS648B38[4] = HookConfig::GetFloat("Shader", "HeadlightIntensity",  1.00f);   // headlight-mask blend (0=off)

    // K nearest lights driven to the PS (1..16); higher K = fewer selection pops.
    g_MaxDynLights = (int)HookConfig::GetFloat("Shader", "MaxDynamicLights", 8.0f);
    g_LightFalloff = HookConfig::GetFloat     ("Shader", "LightFalloff",     1.0f);
    if (g_MaxDynLights < 1)    g_MaxDynLights = 1;
    if (g_MaxDynLights > KMAX) g_MaxDynLights = KMAX;

    g_PS676088[0] = g_PS648B38[0];  // chrome inherits (dormant)
    g_PS676088[1] = g_PS648B38[1];

    // Replace the engine's flickery light gather with our stable version (also harvests our light set).
    InstallGatherDetour();

    InstallCallback("ShaderHandler::EarlyInit", "Intercept early engine shader setup",
        &Hook_ShaderInitializationStage, { cb::call(0x5F15EC) });

    hook_output("[Shader] Installed");
}
