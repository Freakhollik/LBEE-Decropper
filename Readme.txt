0. Introduction
This project is used to resize the CGs in Little Busters! English Edition so that they are not cropped from 4:3 to fill inside a 16:9 window. Instead they will be fit inside the window with black borders on the left and right.

1. Hook
The first step is to have the patch DLL intercept the D3D11 calls. From there we can adjust them to change the rendering. This is done with the patch D3D11 DLL that first finds the real D3D11 Dll and and gets the address of all that DLL's functions. This is done in DllMain(). Then after the device is created, we get the VTable of the DeviceContext and use Minhook to override the DrawIndexed() and SetPredication() functions. All of the work to adjust the rendering is done in DrawIndexed. The SetPredication hook is only used to detect a frame transition.

2. D3D11

In DrawIndexed(), we need to be able to detect when LBEE is rendering a 4:3 CG that has been cropped to 16:9. We do this by checking that the dimensions of PS SRV 0 are 1280x960. Once this has been deteced we add Geometry Shader to the D3D11 pipeline. This Geometry Shader will resize the CG. We also need to change the PS samplers to use bilinear filtering and have a black background color. Also we need to configure the stream out state.

We also need to detect when the CG has a Decal on it. These Decals are used by the app to produce variations on the base CG. Since we have transformed the CG, we also need to apply the same transformation to the Decal so that it is rendered in the correct position. These decals are detected by examing the BlendState.

3. GS

To adjust the rendering, a GS interception path has been chosen. This is very convenient to use since this stage is optional, not used by LBEE, and occurs between the VS and PS. This point in the rendering allows us to override the position and texture coordinates of the CG after the VS calculates them and before the Rasterizer/PS use them.

The math in the GS shaders is a little bit tricky and I will only give an overview here. For full detail, see the hlsl code.

LBEE takes a source CG of 1280x960 and renders it into 1280x720 space. This means that only a 720 pixel portion of the 960 vertical range appears on screen. The exact range that gets displayed varies by CG.

The LBEE is somewhat confusing as it uses two different methods to scale the CG. Sometimes it renders 1280x960 geometry with a Y texture coordinate in the [0,1] range and uses the position coordinates to determine what is visible on screen. Other times, it renders only 1280x720 geometry on the screen, and uses the texture coordinates to determine what is visible. The GSs have to account for both approaches.

There are a few CGs where the game uses a small amount of CG scaling to produce a heartbeat effect. This is accounted for the GS code by the "scaleFactor" variable.

After the CgGs has run, we write the min/max posititons to the stream out buffer. These need to be read during the DecalGs so that we can apply the correct transformation to the Decal as well.


4. Acknowledgements

The following projects have helped make this project possible:

https://github.com/TsudaKageyu/minhook
Used to override the D3D11 calls.

https://github.com/DrNseven/D3D11-Wallhack
Used this as reference to find the major numbers to offset into the contextvtable

http://www.hulver.com/scoop/story/2006/2/18/125521/185
Used this to generate the function definitions for D3D11.
