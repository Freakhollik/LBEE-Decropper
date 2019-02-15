// Version 1.0 - Initial release
// Version 1.1 - Added fix for CGs which have cropped decals
// Version 1.2 - Added fix for heartbeat effect which occurs on a few CGs
#define VERSION 1.2

// MS says Win7 supports D3D11_1. But, after trying this, it appears that using any 11_1 features on win7 won't work. I hate Microsoft.
// Using D3D11_1 will implement the changes using UAVs in geometry shader.
// Without D3D11_1, we have to use stream out and SRVs instead
#define USE_D3D11_1 0
