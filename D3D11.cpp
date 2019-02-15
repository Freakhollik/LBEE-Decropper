#if defined (_DEBUG)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <windows.h>
#include <d3d11.h>
#include "MinHook.h"

#include "SharedInc.h"
#include "CgGs.h"
#include "DecalGs.h"


// A release build crashes and I'm not entirely sure why. It's probably caused by the entry points using jmp statements

#define SAFE_RELEASE(x) if (x != NULL) { x->Release(); x = NULL; }

#if defined (_DEBUG)
#define DEBUG_LOG_EN 0
#endif

#if DEBUG_LOG_EN
static FILE * debug;
#define DEBUG(x) fprintf x
#define DEBUGFLUSH fflush(debug)
#else // DEBUG_LOG_EN
#define DEBUG(x)
#define DEBUGFLUSH
#endif // DEBUG_LOG_EN

static HINSTANCE gs_hDLL = 0;


int (__stdcall * _O_D3D11CoreCreateDevice)();
extern "C" void __declspec(naked) __stdcall _I_D3D11CoreCreateDevice()
{
	DEBUG((debug, "D3D11CoreCreateDevice\n"));
	__asm { jmp _O_D3D11CoreCreateDevice }
}

int (__stdcall * _O_D3D11CoreCreateLayeredDevice)();
extern "C" void __declspec(naked) __stdcall _I_D3D11CoreCreateLayeredDevice()
{
	DEBUG((debug, "D3D11CoreCreateLayeredDevice\n"));
	__asm { jmp _O_D3D11CoreCreateLayeredDevice }
}

int (__stdcall * _O_D3D11CoreGetLayeredDeviceSize)();
extern "C" void __declspec(naked) __stdcall _I_D3D11CoreGetLayeredDeviceSize()
{
	DEBUG((debug, "D3D11CoreGetLayeredDeviceSize\n"));
	__asm { jmp _O_D3D11CoreGetLayeredDeviceSize }
}

int (__stdcall * _O_D3D11CoreRegisterLayers)();
extern "C" void __declspec(naked) __stdcall _I_D3D11CoreRegisterLayers()
{
	DEBUG((debug, "D3D11CoreRegisterLayers\n"));
	__asm { jmp _O_D3D11CoreRegisterLayers }
}

// ORIGINAL
/*
int (__stdcall * _O_D3D11CreateDevice)();
extern "C" void __declspec(naked) __stdcall _I_D3D11CreateDevice()
{
	DEBUG((debug, "D3D11CreateDevice\n"));
	__asm { jmp _O_D3D11CreateDevice }
}
*/


DWORD_PTR* pDeviceVTable  = NULL;
DWORD_PTR* pContextVTable = NULL;

typedef void(__stdcall *D3D11DrawIndexedHook) (ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
D3D11DrawIndexedHook phookD3D11DrawIndexed = NULL;

typedef void(__stdcall *D3D11SetPredicationHook) (ID3D11DeviceContext* pContext, ID3D11Predicate *pPredicate, BOOL PredicateValue);
D3D11SetPredicationHook phookD3D11SetPredication = NULL;

ID3D11GeometryShader* pCgGs = NULL;
ID3D11GeometryShader* pDecalGs = NULL;
ID3D11SamplerState* pBlackBorderSampler = NULL;

ID3D11Buffer* pTexCoordsBuff = NULL;
#if USE_D3D11_1
ID3D11UnorderedAccessView* pTexCoordsUav = NULL;
#else
ID3D11ShaderResourceView* pTexCoordsSrv = NULL;
#endif

BOOL frameHasCg = FALSE;

void WINAPI hookD3D11SetPredication(ID3D11DeviceContext* pContext, ID3D11Predicate *pPredicate, BOOL PredicateValue)
{
    // The app calls this at the start and end of every frame. So we'll use this to detect when a frame ends.
    // I don't think they would use it any other time. The app has no use for predication.
    DEBUG((debug, "hookD3D11SetPredication\n"));
    frameHasCg = FALSE;
    phookD3D11SetPredication(pContext, pPredicate, PredicateValue);
    DEBUGFLUSH;
}

void WINAPI hookD3D11DrawIndexed(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
    //return;
    DEBUG((debug, "hookD3D11DrawIndexed\n"));

    ID3D11GeometryShader* pGs = NULL;
    ID3D11ShaderResourceView* pSrv = NULL;
    ID3D11Resource* pResource = NULL;
    D3D11_RESOURCE_DIMENSION resDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    D3D11_TEXTURE2D_DESC texDesc;
    texDesc.Width = 0;
    static const int NumSamplers = 4;
    BOOL restoreGs = FALSE;
    BOOL swapSamplers = FALSE;
    ID3D11SamplerState* pOrigSampler[NumSamplers] = { NULL, NULL, NULL, NULL }; // The sampler slot used seems to change afer recalling the text
    ID3D11ShaderResourceView* pNoSrv = NULL;
    ID3D11Buffer* pNoBuffer = NULL;
    ID3D11BlendState* pBlendState = NULL;
    D3D11_BLEND_DESC blendDesc;
    UINT soOffset = 0;
    blendDesc.RenderTarget[0].BlendEnable = 0;

#if USE_D3D11_1
    ID3D11RenderTargetView* pRtv = NULL;
    ID3D11DepthStencilView* pDsv = NULL;
#else
    BOOL restoreSo = FALSE;
    BOOL restoreGsSrv = FALSE;
#endif

    pContext->GSGetShader(&pGs, NULL, NULL);
    pContext->PSGetShaderResources(0, 1, &pSrv);

    // Detect CG decal
    if (frameHasCg == TRUE)
    {
        pContext->OMGetBlendState(&pBlendState, NULL, NULL);
        if (pBlendState != NULL)
        {
            pBlendState->GetDesc(&blendDesc);

            // Detect the CG decal based on the slightly unusual blend state.
            // Other draw calls appear to keep the Src alpha rather than the dest
            if (blendDesc.RenderTarget[0].BlendEnable == 1 &&
                blendDesc.RenderTarget[0].SrcBlendAlpha == D3D10_BLEND_ZERO &&
                blendDesc.RenderTarget[0].DestBlendAlpha == D3D10_BLEND_ONE)
            {
                DEBUG((debug, "hookD3D11DrawIndexed Decal detected\n"));
                DEBUGFLUSH;

                // Swap in the replacement GS to adjust the vertex positions
                restoreGs = TRUE;
                pContext->GSSetShader(pDecalGs, NULL, NULL);

                // Swap in our replacement sampler.
                // After hiding and recalling text, the app will switch to a point sampler instead of a linear sampler
                // for some reason. This becomes a problem now that the CG is being shrunk down to the window size.
                // To fix this, swap in our linear sampler.
                swapSamplers = TRUE;

#if USE_D3D11_1
                // Add the texture coordinates UAV to the output merger stage.
                // I'm not going to bother unbinding the UAV after the draw call.
                pContext->OMGetRenderTargets(1, &pRtv, &pDsv);
                pContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &pRtv, pDsv, 1, 1, &pTexCoordsUav, NULL);
                SAFE_RELEASE(pRtv);
                SAFE_RELEASE(pDsv);
#else
                restoreGsSrv = TRUE;
                pContext->GSSetShaderResources(0, 1, &pTexCoordsSrv);
#endif
            }
        }

        SAFE_RELEASE(pBlendState)
    }
    // Detect CG
    else if (pSrv != NULL && pGs == NULL && pCgGs != NULL && pDecalGs != NULL && pBlackBorderSampler
#if USE_D3D11_1
        && pTexCoordsUav
#else
        && pTexCoordsBuff
#endif
        )
    {
        pSrv->GetResource(&pResource);
        if (pResource != NULL)
        {
            pResource->GetType(&resDim);
            if (resDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
            {
                static_cast<ID3D11Texture2D*>(pResource)->GetDesc(&texDesc);

                // Detect the 4:3 CG based on the W/H
                if (texDesc.Width == 1280 && texDesc.Height == 960)
                {
                    DEBUG((debug, "hookD3D11DrawIndexed CG detected\n"));
                    DEBUGFLUSH;

                    frameHasCg = TRUE;

                    // Swap in the replacement GS to adjust the texture coordinates
                    restoreGs = TRUE;
                    pContext->GSSetShader(pCgGs, NULL, NULL);

                    // Swap in a sampler with a black border color.
                    swapSamplers = TRUE;
#if USE_D3D11_1
                    // Add the texture coordinates UAV to the output merger stage.
                    // I'm not going to bother unbinding the UAV after the draw call.
                    pContext->OMGetRenderTargets(1, &pRtv, &pDsv);
                    pContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &pRtv, pDsv, 1, 1, &pTexCoordsUav, NULL);
                    SAFE_RELEASE(pRtv);
                    SAFE_RELEASE(pDsv);
#else
                    restoreSo = TRUE;
                    pContext->SOSetTargets(1, &pTexCoordsBuff, &soOffset);
#endif
                }
            }
            SAFE_RELEASE(pResource)
        }
        SAFE_RELEASE(pSrv)
    }
    SAFE_RELEASE(pGs);

    if (swapSamplers)
    {
        pContext->PSGetSamplers(0, NumSamplers, &pOrigSampler[0]);
        for (UINT i = 0; i < NumSamplers; i++)
        {
            pContext->PSSetSamplers(i, 1, &pBlackBorderSampler);
        }

    }

    /////
    phookD3D11DrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
    /////

    if (restoreGs)
    {
        pContext->GSSetShader(NULL, NULL, NULL);
    }

    if (swapSamplers)
    {
        pContext->PSSetSamplers(0, NumSamplers, &pOrigSampler[0]);
        for (int i = 0; i < NumSamplers; i++)
        {
            SAFE_RELEASE(pOrigSampler[i])
        }
    }

#if USE_D3D11_1 == 0
    if (restoreSo)
    {
        pContext->SOSetTargets(1, &pNoBuffer, NULL);
    }

    if (restoreGsSrv)
    {
        pContext->GSSetShaderResources(0, 1, &pNoSrv);
    }
#endif

    DEBUGFLUSH;
}

/*
typedef void(__stdcall *D3D11CreateTexture2D) (ID3D11Device* pDevice, const D3D11_TEXTURE2D_DESC *pDesc,
    const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D);
D3D11CreateTexture2D phookD3D11CreateTexture2D = NULL;

void WINAPI hookD3D11CreateTexture2D(ID3D11Device* pDevice, const D3D11_TEXTURE2D_DESC *pDesc,
    const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D)
{
    phookD3D11CreateTexture2D(pDevice, pDesc, pInitialData, ppTexture2D);
}
*/



extern "C"
{
    // This function is called via function pointer which does not respect __declspec(naked).
    // To solve this we must make this a "normal" function which calls the real D3D11 DLL function
    // in the "normal" manner. We need access to the parameters anyway.
    typedef HRESULT(WINAPI *tD3D11CreateDevice)(_In_opt_ VOID* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
        UINT Flags, _In_opt_ const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVERSION,
        _Out_opt_ ID3D11Device** ppDevice, _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel, _Out_opt_ VOID** ppImmediateContext);
    tD3D11CreateDevice _O_D3D11CreateDevice;

    HRESULT WINAPI _I_D3D11CreateDevice(
        _In_opt_ VOID* pAdapter,
        D3D_DRIVER_TYPE DriverType,
        HMODULE Software,
        UINT Flags,
        _In_opt_ const D3D_FEATURE_LEVEL* pFeatureLevels,
        UINT FeatureLevels,
        UINT SDKVERSION,
        _Out_opt_ ID3D11Device** ppDevice,
        _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
        _Out_opt_ VOID** ppImmediateContext)
    {
        MH_STATUS mhRet;
        HRESULT createDeviceHr;
        HRESULT hr;
        D3D11_SAMPLER_DESC samplerDesc;
        D3D11_BUFFER_DESC bufferDesc;

        // wait for debugger
#if 0
        static BOOL wait = TRUE;
        while (wait);
#endif

#if DEBUG_LOG_EN
        // The debug runtime gets angry about mismatched VS/GS/PS in/out semantics and removes the GS.
        // So we can't use the debug runtime. I hate Microsoft.
        //Flags &= ~(D3D11_CREATE_DEVICE_DEBUG);
        //Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        DEBUG((debug, "D3D11CreateDevice\n"));
#if USE_D3D11_1
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;

        // The D3D version needs to upgraded to 11.1 so that we can access UAVs from GS.
        D3D_FEATURE_LEVEL retFl;
        D3D_FEATURE_LEVEL fl[2] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

        createDeviceHr = _O_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, &fl[0],
            2, SDKVERSION, ppDevice, &retFl, ppImmediateContext);

        if (pFeatureLevel != NULL)
        {
            *pFeatureLevel = retFl;
        }

        if (retFl != D3D_FEATURE_LEVEL_11_1)
        {
            DEBUG((debug, "Could not Create D3D11.1 Device\n"));
        }
#else
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;

        createDeviceHr = _O_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels,
            2, SDKVERSION, ppDevice, pFeatureLevel, ppImmediateContext);
#endif

        if (*ppImmediateContext != NULL && phookD3D11DrawIndexed == NULL
#if USE_D3D11_1
            && retFl == D3D_FEATURE_LEVEL_11_1
#endif
            )
        {
            // Get the VTable and Hook DrawIndexed
            pContextVTable = (DWORD_PTR*)(*ppImmediateContext);
            pContextVTable = (DWORD_PTR*)pContextVTable[0];

            mhRet = MH_CreateHook((DWORD_PTR*)pContextVTable[12], hookD3D11DrawIndexed, reinterpret_cast<void**>(&phookD3D11DrawIndexed));
            if (mhRet != MH_OK)
            {
                DEBUG((debug, "Error. MH_CreateHook DrawIndexed Failed.\n"));
            }

            mhRet = MH_EnableHook((DWORD_PTR*)pContextVTable[12]);
            if (mhRet != MH_OK)
            {
                DEBUG((debug, "Error. MH_EnableHook DrawIndexed Failed.\n"));
            }

            mhRet = MH_CreateHook((DWORD_PTR*)pContextVTable[30], hookD3D11SetPredication, reinterpret_cast<void**>(&phookD3D11SetPredication));
            if (mhRet != MH_OK)
            {
                DEBUG((debug, "Error. MH_CreateHook SetPredication Failed.\n"));
            }

            mhRet = MH_EnableHook((DWORD_PTR*)pContextVTable[30]);
            if (mhRet != MH_OK)
            {
                DEBUG((debug, "Error. MH_EnableHook SetPredication Failed.\n"));
            }

            // Create the Decrop GS now since we have the device pointer
#if USE_D3D11_1
            hr = (*ppDevice)->CreateGeometryShader(&g_CgGs[0], sizeof(g_CgGs), NULL, &pCgGs);
#else
            D3D11_SO_DECLARATION_ENTRY pSoDecl[7] =
            {
                { 0, "SV_POSITION", 0, 0, 4, 0 },
                { 0, "a", 0, 0, 4, 0 },
                { 0, "b", 0, 0, 4, 0 },
                { 0, "c", 0, 0, 4, 0 },
                { 0, "d", 0, 0, 4, 0 },
                { 0, "e", 0, 0, 4, 0 },
                { 0, "f", 0, 0, 4, 0 },
            };


            hr = (*ppDevice)->CreateGeometryShaderWithStreamOutput(&g_CgGs[0], sizeof(g_CgGs), &pSoDecl[0], 7, NULL, 0, 0, NULL, &pCgGs);
#endif
            if (hr != S_OK || pCgGs == NULL)
            {
                DEBUG((debug, "Error. CgGs compile failed.\n"));
            }

            hr = (*ppDevice)->CreateGeometryShader(&g_DecalGs[0], sizeof(g_DecalGs), NULL, &pDecalGs);
            if (hr != S_OK || pDecalGs == NULL)
            {
                DEBUG((debug, "Error. DecalGs compile failed.\n"));
            }

            samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
            samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
            samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
            samplerDesc.MipLODBias = 0.0f;
            samplerDesc.MaxAnisotropy = 1;
            samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            samplerDesc.BorderColor[0] = 0.0f;
            samplerDesc.BorderColor[1] = 0.0f;
            samplerDesc.BorderColor[2] = 0.0f;
            samplerDesc.BorderColor[3] = 0.0f;
            samplerDesc.MinLOD = 0.0f;
            samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
            hr = (*ppDevice)->CreateSamplerState(&samplerDesc, &pBlackBorderSampler);
            if (hr != S_OK || pBlackBorderSampler == NULL)
            {
                DEBUG((debug, "Error. Black border color sampler create failed.\n"));
            }

            bufferDesc.ByteWidth =
#if USE_D3D11_1
                sizeof(float)* 256;
#else
                sizeof(float)* 256;
#endif
            bufferDesc.Usage = D3D11_USAGE_DEFAULT;
            bufferDesc.BindFlags =
#if USE_D3D11_1
                D3D11_BIND_UNORDERED_ACCESS;
#else
                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_STREAM_OUTPUT;
#endif
            bufferDesc.CPUAccessFlags = 0;
            bufferDesc.MiscFlags = 0;
            bufferDesc.StructureByteStride = sizeof(float);
            hr = (*ppDevice)->CreateBuffer(&bufferDesc, NULL, &pTexCoordsBuff);
            if (hr != S_OK || pTexCoordsBuff == NULL)
            {
                DEBUG((debug, "Error. Tex Coords Buffer Create Failed.\n"));
            }

#if USE_D3D11_1
            uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = bufferDesc.ByteWidth / sizeof(float);;
            uavDesc.Buffer.Flags = 0;
            hr = (*ppDevice)->CreateUnorderedAccessView(pTexCoordsBuff, &uavDesc, &pTexCoordsUav);
            if (hr != S_OK || pTexCoordsUav == NULL)
            {
                DEBUG((debug, "Error. Tex Coords UAV Create Failed.\n"));
            }
#else
            srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = bufferDesc.ByteWidth / sizeof(float);
            hr = (*ppDevice)->CreateShaderResourceView(pTexCoordsBuff, &srvDesc, &pTexCoordsSrv);
            if (hr != S_OK || pTexCoordsSrv == NULL)
            {
                DEBUG((debug, "Error. Tex Coords SRV Create Failed.\n"));
            }
#endif
        }

        DEBUGFLUSH;
        return createDeviceHr;
    }
}






int (__stdcall * _O_D3D11CreateDeviceAndSwapChain)();
extern "C" void __declspec(naked) __stdcall _I_D3D11CreateDeviceAndSwapChain()
{
	DEBUG((debug, "D3D11CreateDeviceAndSwapChain\n"));
	__asm { jmp _O_D3D11CreateDeviceAndSwapChain }
}

int (__stdcall * _O_D3DKMTCloseAdapter)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTCloseAdapter()
{
	DEBUG((debug, "D3DKMTCloseAdapter\n"));
	__asm { jmp _O_D3DKMTCloseAdapter }
}

int (__stdcall * _O_D3DKMTCreateAllocation)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTCreateAllocation()
{
	DEBUG((debug, "D3DKMTCreateAllocation\n"));
	__asm { jmp _O_D3DKMTCreateAllocation }
}

int (__stdcall * _O_D3DKMTCreateContext)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTCreateContext()
{
	DEBUG((debug, "D3DKMTCreateContext\n"));
	__asm { jmp _O_D3DKMTCreateContext }
}

int (__stdcall * _O_D3DKMTCreateDevice)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTCreateDevice()
{
	DEBUG((debug, "D3DKMTCreateDevice\n"));
	__asm { jmp _O_D3DKMTCreateDevice }
}

int (__stdcall * _O_D3DKMTCreateSynchronizationObject)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTCreateSynchronizationObject()
{
	DEBUG((debug, "D3DKMTCreateSynchronizationObject\n"));
	__asm { jmp _O_D3DKMTCreateSynchronizationObject }
}

int (__stdcall * _O_D3DKMTDestroyAllocation)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTDestroyAllocation()
{
	DEBUG((debug, "D3DKMTDestroyAllocation\n"));
	__asm { jmp _O_D3DKMTDestroyAllocation }
}

int (__stdcall * _O_D3DKMTDestroyContext)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTDestroyContext()
{
	DEBUG((debug, "D3DKMTDestroyContext\n"));
	__asm { jmp _O_D3DKMTDestroyContext }
}

int (__stdcall * _O_D3DKMTDestroyDevice)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTDestroyDevice()
{
	DEBUG((debug, "D3DKMTDestroyDevice\n"));
	__asm { jmp _O_D3DKMTDestroyDevice }
}

int (__stdcall * _O_D3DKMTDestroySynchronizationObject)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTDestroySynchronizationObject()
{
	DEBUG((debug, "D3DKMTDestroySynchronizationObject\n"));
	__asm { jmp _O_D3DKMTDestroySynchronizationObject }
}

int (__stdcall * _O_D3DKMTEscape)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTEscape()
{
	DEBUG((debug, "D3DKMTEscape\n"));
	__asm { jmp _O_D3DKMTEscape }
}

int (__stdcall * _O_D3DKMTGetContextSchedulingPriority)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTGetContextSchedulingPriority()
{
	DEBUG((debug, "D3DKMTGetContextSchedulingPriority\n"));
	__asm { jmp _O_D3DKMTGetContextSchedulingPriority }
}

int (__stdcall * _O_D3DKMTGetDeviceState)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTGetDeviceState()
{
	DEBUG((debug, "D3DKMTGetDeviceState\n"));
	__asm { jmp _O_D3DKMTGetDeviceState }
}

int (__stdcall * _O_D3DKMTGetDisplayModeList)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTGetDisplayModeList()
{
	DEBUG((debug, "D3DKMTGetDisplayModeList\n"));
	__asm { jmp _O_D3DKMTGetDisplayModeList }
}

int (__stdcall * _O_D3DKMTGetMultisampleMethodList)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTGetMultisampleMethodList()
{
	DEBUG((debug, "D3DKMTGetMultisampleMethodList\n"));
	__asm { jmp _O_D3DKMTGetMultisampleMethodList }
}

int (__stdcall * _O_D3DKMTGetRuntimeData)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTGetRuntimeData()
{
	DEBUG((debug, "D3DKMTGetRuntimeData\n"));
	__asm { jmp _O_D3DKMTGetRuntimeData }
}

int (__stdcall * _O_D3DKMTGetSharedPrimaryHandle)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTGetSharedPrimaryHandle()
{
	DEBUG((debug, "D3DKMTGetSharedPrimaryHandle\n"));
	__asm { jmp _O_D3DKMTGetSharedPrimaryHandle }
}

int (__stdcall * _O_D3DKMTLock)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTLock()
{
	DEBUG((debug, "D3DKMTLock\n"));
	__asm { jmp _O_D3DKMTLock }
}

int (__stdcall * _O_D3DKMTOpenAdapterFromHdc)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTOpenAdapterFromHdc()
{
	DEBUG((debug, "D3DKMTOpenAdapterFromHdc\n"));
	__asm { jmp _O_D3DKMTOpenAdapterFromHdc }
}

int (__stdcall * _O_D3DKMTOpenResource)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTOpenResource()
{
	DEBUG((debug, "D3DKMTOpenResource\n"));
	__asm { jmp _O_D3DKMTOpenResource }
}

int (__stdcall * _O_D3DKMTPresent)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTPresent()
{
	DEBUG((debug, "D3DKMTPresent\n"));
	__asm { jmp _O_D3DKMTPresent }
}

int (__stdcall * _O_D3DKMTQueryAdapterInfo)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTQueryAdapterInfo()
{
	DEBUG((debug, "D3DKMTQueryAdapterInfo\n"));
	__asm { jmp _O_D3DKMTQueryAdapterInfo }
}

int (__stdcall * _O_D3DKMTQueryAllocationResidency)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTQueryAllocationResidency()
{
	DEBUG((debug, "D3DKMTQueryAllocationResidency\n"));
	__asm { jmp _O_D3DKMTQueryAllocationResidency }
}

int (__stdcall * _O_D3DKMTQueryResourceInfo)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTQueryResourceInfo()
{
	DEBUG((debug, "D3DKMTQueryResourceInfo\n"));
	__asm { jmp _O_D3DKMTQueryResourceInfo }
}

int (__stdcall * _O_D3DKMTRender)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTRender()
{
	DEBUG((debug, "D3DKMTRender\n"));
	__asm { jmp _O_D3DKMTRender }
}

int (__stdcall * _O_D3DKMTSetAllocationPriority)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTSetAllocationPriority()
{
	DEBUG((debug, "D3DKMTSetAllocationPriority\n"));
	__asm { jmp _O_D3DKMTSetAllocationPriority }
}

int (__stdcall * _O_D3DKMTSetContextSchedulingPriority)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTSetContextSchedulingPriority()
{
	DEBUG((debug, "D3DKMTSetContextSchedulingPriority\n"));
	__asm { jmp _O_D3DKMTSetContextSchedulingPriority }
}

int (__stdcall * _O_D3DKMTSetDisplayMode)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTSetDisplayMode()
{
	DEBUG((debug, "D3DKMTSetDisplayMode\n"));
	__asm { jmp _O_D3DKMTSetDisplayMode }
}

int (__stdcall * _O_D3DKMTSetDisplayPrivateDriverFormat)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTSetDisplayPrivateDriverFormat()
{
	DEBUG((debug, "D3DKMTSetDisplayPrivateDriverFormat\n"));
	__asm { jmp _O_D3DKMTSetDisplayPrivateDriverFormat }
}

int (__stdcall * _O_D3DKMTSetGammaRamp)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTSetGammaRamp()
{
	DEBUG((debug, "D3DKMTSetGammaRamp\n"));
	__asm { jmp _O_D3DKMTSetGammaRamp }
}

int (__stdcall * _O_D3DKMTSetVidPnSourceOwner)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTSetVidPnSourceOwner()
{
	DEBUG((debug, "D3DKMTSetVidPnSourceOwner\n"));
	__asm { jmp _O_D3DKMTSetVidPnSourceOwner }
}

int (__stdcall * _O_D3DKMTSignalSynchronizationObject)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTSignalSynchronizationObject()
{
	DEBUG((debug, "D3DKMTSignalSynchronizationObject\n"));
	__asm { jmp _O_D3DKMTSignalSynchronizationObject }
}

int (__stdcall * _O_D3DKMTUnlock)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTUnlock()
{
	DEBUG((debug, "D3DKMTUnlock\n"));
	__asm { jmp _O_D3DKMTUnlock }
}

int (__stdcall * _O_D3DKMTWaitForSynchronizationObject)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTWaitForSynchronizationObject()
{
	DEBUG((debug, "D3DKMTWaitForSynchronizationObject\n"));
	__asm { jmp _O_D3DKMTWaitForSynchronizationObject }
}

int (__stdcall * _O_D3DKMTWaitForVerticalBlankEvent)();
extern "C" void __declspec(naked) __stdcall _I_D3DKMTWaitForVerticalBlankEvent()
{
	DEBUG((debug, "D3DKMTWaitForVerticalBlankEvent\n"));
	__asm { jmp _O_D3DKMTWaitForVerticalBlankEvent }
}

int (__stdcall * _O_D3DPerformance_BeginEvent)();
extern "C" void __declspec(naked) __stdcall _I_D3DPerformance_BeginEvent()
{
	DEBUG((debug, "D3DPerformance_BeginEvent\n"));
	__asm { jmp _O_D3DPerformance_BeginEvent }
}

int (__stdcall * _O_D3DPerformance_EndEvent)();
extern "C" void __declspec(naked) __stdcall _I_D3DPerformance_EndEvent()
{
	DEBUG((debug, "D3DPerformance_EndEvent\n"));
	__asm { jmp _O_D3DPerformance_EndEvent }
}

int (__stdcall * _O_D3DPerformance_GetStatus)();
extern "C" void __declspec(naked) __stdcall _I_D3DPerformance_GetStatus()
{
	DEBUG((debug, "D3DPerformance_GetStatus\n"));
	__asm { jmp _O_D3DPerformance_GetStatus }
}

int (__stdcall * _O_D3DPerformance_SetMarker)();
extern "C" void __declspec(naked) __stdcall _I_D3DPerformance_SetMarker()
{
	DEBUG((debug, "D3DPerformance_SetMarker\n"));
	__asm { jmp _O_D3DPerformance_SetMarker }
}

int (__stdcall * _O_EnableFeatureLevelUpgrade)();
extern "C" void __declspec(naked) __stdcall _I_EnableFeatureLevelUpgrade()
{
	DEBUG((debug, "EnableFeatureLevelUpgrade\n"));
	__asm { jmp _O_EnableFeatureLevelUpgrade }
}

int (__stdcall * _O_OpenAdapter10)();
extern "C" void __declspec(naked) __stdcall _I_OpenAdapter10()
{
	DEBUG((debug, "OpenAdapter10\n"));
	__asm { jmp _O_OpenAdapter10 }
}

int (__stdcall * _O_OpenAdapter10_2)();
extern "C" void __declspec(naked) __stdcall _I_OpenAdapter10_2()
{
	DEBUG((debug, "OpenAdapter10_2\n"));
	__asm { jmp _O_OpenAdapter10_2 }
}

BOOL WINAPI DllMain(HINSTANCE hI, DWORD reason, LPVOID notUsed)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
#if DEBUG_LOG_EN
        if(!debug)
        {
            char tempPath[MAX_PATH];
            GetTempPath(sizeof(tempPath), tempPath);
            strcat_s(tempPath, "D3D11_DLL_SHIM_DEBUG.txt");
            debug = fopen(tempPath, "w");
        }
#endif // DEBUG_LOG_EN
        DEBUG((debug, "DllMain\n"));

        char realDLL[MAX_PATH];
        (void)GetSystemDirectory(realDLL, sizeof(realDLL) - 1);
        strcat_s(realDLL, MAX_PATH, "\\D3D11.dll");
        gs_hDLL = LoadLibrary(realDLL);
        if (!gs_hDLL)
            return FALSE;

        _O_D3D11CoreCreateDevice = GetProcAddress(gs_hDLL, "D3D11CoreCreateDevice");
        _O_D3D11CoreCreateLayeredDevice = GetProcAddress(gs_hDLL, "D3D11CoreCreateLayeredDevice");
        _O_D3D11CoreGetLayeredDeviceSize = GetProcAddress(gs_hDLL, "D3D11CoreGetLayeredDeviceSize");
        _O_D3D11CoreRegisterLayers = GetProcAddress(gs_hDLL, "D3D11CoreRegisterLayers");
        _O_D3D11CreateDevice = (tD3D11CreateDevice)GetProcAddress(gs_hDLL, "D3D11CreateDevice");
        _O_D3D11CreateDeviceAndSwapChain = GetProcAddress(gs_hDLL, "D3D11CreateDeviceAndSwapChain");
        _O_D3DKMTCloseAdapter = GetProcAddress(gs_hDLL, "D3DKMTCloseAdapter");
        _O_D3DKMTCreateAllocation = GetProcAddress(gs_hDLL, "D3DKMTCreateAllocation");
        _O_D3DKMTCreateContext = GetProcAddress(gs_hDLL, "D3DKMTCreateContext");
        _O_D3DKMTCreateDevice = GetProcAddress(gs_hDLL, "D3DKMTCreateDevice");
        _O_D3DKMTCreateSynchronizationObject = GetProcAddress(gs_hDLL, "D3DKMTCreateSynchronizationObject");
        _O_D3DKMTDestroyAllocation = GetProcAddress(gs_hDLL, "D3DKMTDestroyAllocation");
        _O_D3DKMTDestroyContext = GetProcAddress(gs_hDLL, "D3DKMTDestroyContext");
        _O_D3DKMTDestroyDevice = GetProcAddress(gs_hDLL, "D3DKMTDestroyDevice");
        _O_D3DKMTDestroySynchronizationObject = GetProcAddress(gs_hDLL, "D3DKMTDestroySynchronizationObject");
        _O_D3DKMTEscape = GetProcAddress(gs_hDLL, "D3DKMTEscape");
        _O_D3DKMTGetContextSchedulingPriority = GetProcAddress(gs_hDLL, "D3DKMTGetContextSchedulingPriority");
        _O_D3DKMTGetDeviceState = GetProcAddress(gs_hDLL, "D3DKMTGetDeviceState");
        _O_D3DKMTGetDisplayModeList = GetProcAddress(gs_hDLL, "D3DKMTGetDisplayModeList");
        _O_D3DKMTGetMultisampleMethodList = GetProcAddress(gs_hDLL, "D3DKMTGetMultisampleMethodList");
        _O_D3DKMTGetRuntimeData = GetProcAddress(gs_hDLL, "D3DKMTGetRuntimeData");
        _O_D3DKMTGetSharedPrimaryHandle = GetProcAddress(gs_hDLL, "D3DKMTGetSharedPrimaryHandle");
        _O_D3DKMTLock = GetProcAddress(gs_hDLL, "D3DKMTLock");
        _O_D3DKMTOpenAdapterFromHdc = GetProcAddress(gs_hDLL, "D3DKMTOpenAdapterFromHdc");
        _O_D3DKMTOpenResource = GetProcAddress(gs_hDLL, "D3DKMTOpenResource");
        _O_D3DKMTPresent = GetProcAddress(gs_hDLL, "D3DKMTPresent");
        _O_D3DKMTQueryAdapterInfo = GetProcAddress(gs_hDLL, "D3DKMTQueryAdapterInfo");
        _O_D3DKMTQueryAllocationResidency = GetProcAddress(gs_hDLL, "D3DKMTQueryAllocationResidency");
        _O_D3DKMTQueryResourceInfo = GetProcAddress(gs_hDLL, "D3DKMTQueryResourceInfo");
        _O_D3DKMTRender = GetProcAddress(gs_hDLL, "D3DKMTRender");
        _O_D3DKMTSetAllocationPriority = GetProcAddress(gs_hDLL, "D3DKMTSetAllocationPriority");
        _O_D3DKMTSetContextSchedulingPriority = GetProcAddress(gs_hDLL, "D3DKMTSetContextSchedulingPriority");
        _O_D3DKMTSetDisplayMode = GetProcAddress(gs_hDLL, "D3DKMTSetDisplayMode");
        _O_D3DKMTSetDisplayPrivateDriverFormat = GetProcAddress(gs_hDLL, "D3DKMTSetDisplayPrivateDriverFormat");
        _O_D3DKMTSetGammaRamp = GetProcAddress(gs_hDLL, "D3DKMTSetGammaRamp");
        _O_D3DKMTSetVidPnSourceOwner = GetProcAddress(gs_hDLL, "D3DKMTSetVidPnSourceOwner");
        _O_D3DKMTSignalSynchronizationObject = GetProcAddress(gs_hDLL, "D3DKMTSignalSynchronizationObject");
        _O_D3DKMTUnlock = GetProcAddress(gs_hDLL, "D3DKMTUnlock");
        _O_D3DKMTWaitForSynchronizationObject = GetProcAddress(gs_hDLL, "D3DKMTWaitForSynchronizationObject");
        _O_D3DKMTWaitForVerticalBlankEvent = GetProcAddress(gs_hDLL, "D3DKMTWaitForVerticalBlankEvent");
        _O_D3DPerformance_BeginEvent = GetProcAddress(gs_hDLL, "D3DPerformance_BeginEvent");
        _O_D3DPerformance_EndEvent = GetProcAddress(gs_hDLL, "D3DPerformance_EndEvent");
        _O_D3DPerformance_GetStatus = GetProcAddress(gs_hDLL, "D3DPerformance_GetStatus");
        _O_D3DPerformance_SetMarker = GetProcAddress(gs_hDLL, "D3DPerformance_SetMarker");
        _O_EnableFeatureLevelUpgrade = GetProcAddress(gs_hDLL, "EnableFeatureLevelUpgrade");
        _O_OpenAdapter10 = GetProcAddress(gs_hDLL, "OpenAdapter10");
        _O_OpenAdapter10_2 = GetProcAddress(gs_hDLL, "OpenAdapter10_2");

        MH_STATUS mhRet = MH_Initialize();
        if (mhRet != MH_OK)
        {
            DEBUG((debug, "Error. MH_Initialize() Failed.\n"));
        }
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        MH_STATUS mhRet = MH_Uninitialize();
        if (mhRet != MH_OK)
        {
            DEBUG((debug, "Error. MH_Uninitialize() Failed.\n"));
        }

        mhRet = MH_DisableHook((DWORD_PTR*)pContextVTable[12]);
        if (mhRet != MH_OK)
        {
            DEBUG((debug, "Error. MH_DisableHook DrawIndexed Failed.\n"));
        }

        mhRet = MH_DisableHook((DWORD_PTR*)pContextVTable[30]);
        if (mhRet != MH_OK)
        {
            DEBUG((debug, "Error. MH_DisableHook SetPredication Failed.\n"));
        }


#if DEBUG_LOG_EN
        if(debug)
            fclose(debug);
#endif // DEBUG_LOG_EN

        FreeLibrary(gs_hDLL);

    }
    return TRUE;
}

