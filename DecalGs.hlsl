#include "GS.hlsl"

[maxvertexcount(3)]
void main(inout TriangleStream<GSInOut> OutStream, triangle GSInOut input[3])
{
    float cgMinY =
#if USE_D3D11_1
        // untested code
        uavTexCoord[0];
#else
        srvTexCoord.Load(24);
#endif

    float cgMaxY =
#if USE_D3D11_1
        // untested code
        uavTexCoord[1];
#else
        srvTexCoord.Load(25);
#endif

    float cgMinV =
#if USE_D3D11_1
        uavTexCoord[2];
#else
        srvTexCoord.Load(26);
#endif

    float cgMaxV =
#if USE_D3D11_1
        uavTexCoord[3];
#else
        srvTexCoord.Load(27);
#endif



#if USE_D3D11_1
    // DEBUG
    /*
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

    uavTexCoord[8] = posMinMaxXY.x;
    uavTexCoord[9] = posMinMaxXY.y;
    uavTexCoord[10] = posMinMaxXY.z;
    uavTexCoord[11] = posMinMaxXY.w;

    uavTexCoord[20] = texMinMaxXY.x;
    uavTexCoord[21] = texMinMaxXY.y;
    uavTexCoord[22] = texMinMaxXY.z;
    uavTexCoord[23] = texMinMaxXY.w;
    */
#endif

    float cgTexHeight = cgMaxV - cgMinV;
    float cgPosHeight = cgMaxY - cgMinY;
    float origPctHeightVisible = (2.0f / cgPosHeight) * cgTexHeight;

    if (cgTexHeight != 1.0f)
    {
        // Scale the CG height so the resize calculations will work
        // Normalize the texture coordinates to the [0, 1] range and adjust the height accordingly.
        float missingTexHeight = 1.0f - cgTexHeight;
        float missingHeightTopPct = cgMinV / missingTexHeight;
        float missingHeightBottomPct = 1.0f - missingHeightTopPct;

        float missingPosHeight = (cgPosHeight / cgTexHeight) - cgPosHeight;
        cgMinY = cgMinY - (missingPosHeight * missingHeightBottomPct);
        cgMaxY = cgMaxY + (missingPosHeight * missingHeightTopPct);

        // Correct other values
        cgTexHeight = cgMaxV - cgMinV;
        cgPosHeight = cgMaxY - cgMinY;
        cgMinV = 0.0f;
        cgMaxV = 1.0f;
    }


    // Resize the decal onto the correct area.
    // Caculate where the decal would land in terms of texture coordinates,
    // then convert those texture coordinates screen space.
    for (int i = 0; i < 3; i++)
    {
        input[i].pos.x *= origPctHeightVisible;

        float origY = input[i].pos.y;
        
        float distFromTopPos = cgMaxY - origY;
        float distFromTopTex = (distFromTopPos / cgPosHeight);
        float recalcPos = -1.0f * ((2 * distFromTopTex) - 1.0f);
        input[i].pos.y = recalcPos;
    }

    // Check if the decal needs to have additional area rendered in the +Y and -Y directions.
    // Check the texture coordinates to ensure that the entire height of the decal is rendered.
    // If the Y texture coords aren't 0 and 1, set them to 0 and 1 and strech the decal
    // accordingly to maintain the correct aspect ratio.
    // Added in version 1.1
    float minTexY = min(min(input[0].b.y, input[1].b.y), input[2].b.y);
    float maxTexY = max(max(input[0].b.y, input[1].b.y), input[2].b.y);
    float minPosY = min(min(input[0].pos.y, input[1].pos.y), input[2].pos.y);
    float maxPosY = max(max(input[0].pos.y, input[1].pos.y), input[2].pos.y);

    float heightScale = (maxPosY - minPosY) / (maxTexY - minTexY);
    if (minTexY > 0.0f)
    {
        for (int i = 0; i < 3; i++)
        {
            if (input[i].b.y == minTexY)
            {
                input[i].b.y = 0.0f;
                input[i].pos.y = input[i].pos.y + (heightScale * minTexY);
            }
        }
    }

    if (maxTexY < 1.0f)
    {
        for (int i = 0; i < 3; i++)
        {
            if (input[i].b.y == maxTexY)
            {
                input[i].b.y = 1.0f;
                input[i].pos.y = input[i].pos.y - (heightScale * (1.0 - maxTexY));
            }
        }
    }

    // Rescale the entire decal if the CG was scaled.
    float scaleFactor = 1.0f + 0.75f - origPctHeightVisible;
    for (int i = 0; i < 3; i++)
    {
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
