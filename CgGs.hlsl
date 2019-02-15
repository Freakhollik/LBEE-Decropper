#include "GS.hlsl"

[maxvertexcount(3)]
void main(inout TriangleStream<GSInOut> OutStream, triangle GSInOut input[3])
{
    float4 posMinMaxXY;
    posMinMaxXY.x = min(min(input[0].pos.x, input[1].pos.x), input[2].pos.x);
    posMinMaxXY.y = max(max(input[0].pos.x, input[1].pos.x), input[2].pos.x);
    posMinMaxXY.z = min(min(input[0].pos.y, input[1].pos.y), input[2].pos.y);
    posMinMaxXY.w = max(max(input[0].pos.y, input[1].pos.y), input[2].pos.y);

    float4 texMinMaxXY;
    texMinMaxXY.x = min(min(input[0].b.x, input[1].b.x), input[2].b.x);
    texMinMaxXY.y = max(max(input[0].b.x, input[1].b.x), input[2].b.x);
    texMinMaxXY.z = min(min(input[0].b.y, input[1].b.y), input[2].b.y);
    texMinMaxXY.w = max(max(input[0].b.y, input[1].b.y), input[2].b.y);


    float origPosHeight = (posMinMaxXY.w - posMinMaxXY.z);
    float origTexHeight = (texMinMaxXY.w - texMinMaxXY.z);
    float origPctHeightVisible = (2.0f / origPosHeight) * origTexHeight;

    float scaleFactor = 1.0f + 0.75f - origPctHeightVisible;

#if USE_D3D11_1
    // The two triangles may fight over this unordered write. But that
    // shouldn't matter since the data is the same.
    uavTexCoord[0] = posMinMaxXY.z;
    uavTexCoord[1] = posMinMaxXY.w;
    uavTexCoord[2] = texMinMaxXY.z;
    uavTexCoord[3] = texMinMaxXY.w;

    // DEBUG
    /*
    uavTexCoord[8] = posMinMaxXY.x;
    uavTexCoord[9] = posMinMaxXY.y;
    uavTexCoord[10] = posMinMaxXY.z;
    uavTexCoord[11] = posMinMaxXY.w;

    uavTexCoord[20] = texMinMaxXY.x;
    uavTexCoord[21] = texMinMaxXY.y;
    uavTexCoord[22] = texMinMaxXY.z;
    uavTexCoord[23] = texMinMaxXY.w;
    */
#else


    for (int i = 0; i < 3; i++)
    {
        input[i].f.x = posMinMaxXY.z;
        input[i].f.y = posMinMaxXY.w;
        input[i].f.z = texMinMaxXY.z;
        input[i].f.w = texMinMaxXY.w;
    }
#endif

    for (int i = 0; i < 3; i++)
    {
        // window size : 1280x720
        // CG size: 1280x960
        // Orig tex coord X [0, 1]
        // New tex coord X  [-1/6, 7/6]
        // Orig tex coord Y [0, 6/8] + C. 0 <= C <= 2/8
        // New tex coord Y  [0, 1]
        // Orig pos X [-1, 1]
        // Orig pos Y [-1, 1]

        if (input[i].b.x < 0.5f)	{ input[i].b.x = -(1.0f / 6.0f);    input[i].pos.x = -1.0f; }
        if (input[i].b.x > 0.5f)	{ input[i].b.x = (7.0f / 6.0f);     input[i].pos.x =  1.0f; }
        if (input[i].b.y < 0.5f)	{ input[i].b.y = 0.0f;              input[i].pos.y =  1.0f; }
        if (input[i].b.y > 0.5f)	{ input[i].b.y = 1.0f;              input[i].pos.y = -1.0f; }


        input[i].pos.xy = input[i].pos.xy * scaleFactor.xx;
    }


#if USE_D3D11_1
    // DEBUG

    /*
    float4 finalPosMinMaxXY;
    finalPosMinMaxXY.x = min(min(input[0].pos.x, input[1].pos.x), input[2].pos.x);
    finalPosMinMaxXY.y = max(max(input[0].pos.x, input[1].pos.x), input[2].pos.x);
    finalPosMinMaxXY.z = min(min(input[0].pos.y, input[1].pos.y), input[2].pos.y);
    finalPosMinMaxXY.w = max(max(input[0].pos.y, input[1].pos.y), input[2].pos.y);

    float4 finalTexMinMaxXY;
    finalTexMinMaxXY.x = min(min(input[0].b.x, input[1].b.x), input[2].b.x);
    finalTexMinMaxXY.y = max(max(input[0].b.x, input[1].b.x), input[2].b.x);
    finalTexMinMaxXY.z = min(min(input[0].b.y, input[1].b.y), input[2].b.y);
    finalTexMinMaxXY.w = max(max(input[0].b.y, input[1].b.y), input[2].b.y);


    uavTexCoord[12] = finalPosMinMaxXY.x;
    uavTexCoord[13] = finalPosMinMaxXY.y;
    uavTexCoord[14] = finalPosMinMaxXY.z;
    uavTexCoord[15] = finalPosMinMaxXY.w;

    uavTexCoord[24] = finalTexMinMaxXY.x;
    uavTexCoord[25] = finalTexMinMaxXY.y;
    uavTexCoord[26] = finalTexMinMaxXY.z;
    uavTexCoord[27] = finalTexMinMaxXY.w;
    */
#endif


    OutStream.Append(input[0]);
    OutStream.Append(input[1]);
    OutStream.Append(input[2]);
}
