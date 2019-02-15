//fxc /T gs_5_0 /Fh CgGs.h    /E CgGs    GS.hlsl
//fxc /T gs_5_0 /Fh DecalGs.h /E DecalGs GS.hlsl
#include "SharedInc.h"

// format : minY, maxY, minV, maxV
// offset : uav : 0
//        : srv : 24
#if USE_D3D11_1
RWBuffer<float> uavTexCoord : register(u1);
#else
Buffer<float> srvTexCoord : register(t0);
#endif

struct GSInOut
{
	float4 pos : SV_Position;
	float4 a : a;
	float4 b : b;
	float4 c : c;
	float4 d : d;
	float4 e : e;
	float4 f : f;
};


