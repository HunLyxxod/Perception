/********************************************************************
Vireio Perception: Open-Source Stereoscopic 3D Driver
Copyright (C) 2012 Andres Hernandez

File <D3DProxyDevice.cpp> and
Class <D3DProxyDevice> :
Copyright (C) 2012 Andres Hernandez
Modifications Copyright (C) 2013 Chris Drain

Vireio Perception Version History:
v1.0.0 2012 by Andres Hernandez
v1.0.X 2013 by John Hicks, Neil Schneider
v1.1.x 2013 by Primary Coding Author: Chris Drain
Team Support: John Hicks, Phil Larkson, Neil Schneider
v2.0.x 2013 by Denis Reischl, Neil Schneider, Joshua Brown

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
********************************************************************/

#include "D3DProxyDevice.h"
#include "D3D9ProxySurface.h"
#include "StereoViewFactory.h"
#include "MotionTrackerFactory.h"
#include <typeinfo>
#include <assert.h>
#include <comdef.h>
#include <tchar.h>

#ifdef _DEBUG
#include "DxErr.h"
#endif

#pragma comment(lib, "d3dx9.lib")

#define KEY_DOWN(vk_code) ((GetAsyncKeyState(vk_code) & 0x8000) ? 1 : 0)
#define KEY_UP(vk_code) ((GetAsyncKeyState(vk_code) & 0x8000) ? 0 : 1)

#define SMALL_FLOAT 0.001f
#define	SLIGHTLY_LESS_THAN_ONE 0.999f

#define PI 3.141592654
#define RADIANS_TO_DEGREES(rad) ((float) rad * (float) (180.0 / PI))

#define OUTPUT_HRESULT(hr) { _com_error err(hr); LPCTSTR errMsg = err.ErrorMessage(); OutputDebugString(errMsg); }

#define MAX_PIXEL_SHADER_CONST_2_0 32
#define MAX_PIXEL_SHADER_CONST_2_X 32
#define MAX_PIXEL_SHADER_CONST_3_0 224

/**
* Returns the mouse wheel scroll lines.
***/
UINT GetMouseScrollLines()
{
	int nScrollLines = 3;
	HKEY hKey;

	if (RegOpenKeyEx(HKEY_CURRENT_USER,  _T("Control Panel\\Desktop"),
		0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
	{
		TCHAR szData[128];
		DWORD dwKeyDataType;
		DWORD dwDataBufSize = sizeof(szData);

		if (RegQueryValueEx(hKey, _T("WheelScrollLines"), NULL, &dwKeyDataType,
			(LPBYTE) &szData, &dwDataBufSize) == ERROR_SUCCESS)
		{
			nScrollLines = _tcstoul(szData, NULL, 10);
		}

		RegCloseKey(hKey);
	}

	return nScrollLines;
}

/**
* Constructor : creates game handler and sets various states.
***/
D3DProxyDevice::D3DProxyDevice(IDirect3DDevice9* pDevice, BaseDirect3D9* pCreatedBy):BaseDirect3DDevice9(pDevice, pCreatedBy),
	m_activeRenderTargets (1, NULL),
	m_activeTextureStages(),
	m_activeVertexBuffers(),
	m_activeSwapChains()
{
	OutputDebugString("D3D ProxyDev Created\n");

	HMDisplayInfo defaultInfo; // rift info
	m_spShaderViewAdjustment = std::make_shared<ViewAdjustment>(defaultInfo, 1.0f, false);

	m_pGameHandler = new GameHandler();

	// Check the maximum number of supported render targets
	D3DCAPS9 capabilities;
	BaseDirect3DDevice9::GetDeviceCaps(&capabilities);
	DWORD maxRenderTargets = capabilities.NumSimultaneousRTs;
	m_activeRenderTargets.resize(maxRenderTargets, NULL);

	D3DXMatrixIdentity(&m_leftView);
	D3DXMatrixIdentity(&m_rightView);
	D3DXMatrixIdentity(&m_leftProjection);
	D3DXMatrixIdentity(&m_rightProjection);	

	m_currentRenderingSide = vireio::Left;
	m_pCurrentMatViewTransform = &m_spShaderViewAdjustment->LeftAdjustmentMatrix(); 
	m_pCurrentView = &m_leftView;
	m_pCurrentProjection = &m_leftProjection;

	// get pixel shader max constants
	auto major_ps=D3DSHADER_VERSION_MAJOR(capabilities.PixelShaderVersion);
	auto minor_ps=D3DSHADER_VERSION_MINOR(capabilities.PixelShaderVersion);
	DWORD MaxPixelShaderConst = MAX_PIXEL_SHADER_CONST_2_0;
	if ((major_ps>=2) && (minor_ps>0)) MaxPixelShaderConst = MAX_PIXEL_SHADER_CONST_2_X;
	if ((major_ps>=3) && (minor_ps>=0)) MaxPixelShaderConst = MAX_PIXEL_SHADER_CONST_3_0;

	m_spManagedShaderRegisters = std::make_shared<ShaderRegisters>(MaxPixelShaderConst, capabilities.MaxVertexShaderConst, pDevice);

	m_pActiveStereoDepthStencil = NULL;
	m_pActiveIndicies = NULL;
	m_pActivePixelShader = NULL;
	m_pActiveVertexShader = NULL;
	m_pActiveVertexDeclaration = NULL;
	hudFont = NULL;
	m_bActiveViewportIsDefault = true;

	m_bViewTransformSet = false;
	m_bProjectionTransformSet = false;

	m_bInBeginEndStateBlock = false;
	m_pCapturingStateTo = NULL;

	m_isFirstBeginSceneOfFrame = true;

	yaw_mode = 0;
	pitch_mode = 0;
	translation_mode = 0;
	trackingOn = true;

	BRASSA_mode = BRASSA_Modes::INACTIVE;
	borderTopHeight = 0.0f;
	menuVelocity = D3DXVECTOR2(0.0f, 0.0f);
	hud3DDepthMode = HUD_3D_Depth_Modes::HUD_DEFAULT;
	gui3DDepthMode = GUI_3D_Depth_Modes::GUI_DEFAULT;
	oldHudMode = HUD_3D_Depth_Modes::HUD_DEFAULT;
	oldGuiMode = GUI_3D_Depth_Modes::GUI_DEFAULT;
	hud3DDepthPresets[0] = 0.0f;
	hud3DDepthPresets[1] = 0.0f;
	hud3DDepthPresets[2] = 0.0f;
	hud3DDepthPresets[3] = 0.0f;
	hudDistancePresets[0] = 0.5f;
	hudDistancePresets[1] = 0.9f;
	hudDistancePresets[2] = 0.3f;
	hudDistancePresets[3] = 0.0f;
	gui3DDepthPresets[0] = 0.0f;
	gui3DDepthPresets[1] = 0.0f;
	gui3DDepthPresets[2] = 0.0f;
	gui3DDepthPresets[3] = 0.0f;
	guiSquishPresets[0] = 0.6f;
	guiSquishPresets[1] = 0.5f;
	guiSquishPresets[2] = 0.9f;
	guiSquishPresets[3] = 1.0f;
	ChangeHUD3DDepthMode(HUD_3D_Depth_Modes::HUD_DEFAULT);
	ChangeGUI3DDepthMode(GUI_3D_Depth_Modes::GUI_DEFAULT);

	hotkeyCatch = false;
	for (int i = 0; i < 5; i++)
	{
		guiHotkeys[i] = 0;
		hudHotkeys[i] = 0;
	}

#pragma region virtual keys name list
	for (int i = 0; i < 256; i++)
		keyNameList[i] = "-";
	keyNameList[0x01] = "Left mouse button";
	keyNameList[0x02] = "Right mouse button";
	keyNameList[0x03] = "Control-break processing";
	keyNameList[0x04] = "Middle mouse button (three-button mouse)";
	keyNameList[0x08] = "BACKSPACE key";
	keyNameList[0x09] = "TAB key";
	keyNameList[0x0C] = "CLEAR key";
	keyNameList[0x0D] = "ENTER key";
	keyNameList[0x10] = "SHIFT key";
	keyNameList[0x11] = "CTRL key";
	keyNameList[0x12] = "ALT key";
	keyNameList[0x13] = "PAUSE key";
	keyNameList[0x14] = "CAPS LOCK key";
	keyNameList[0x1B] = "ESC key";
	keyNameList[0x20] = "SPACEBAR";
	keyNameList[0x21] = "PAGE UP key";
	keyNameList[0x22] = "PAGE DOWN key";
	keyNameList[0x23] = "END key";
	keyNameList[0x24] = "HOME key";
	keyNameList[0x25] = "LEFT ARROW key";
	keyNameList[0x26] = "UP ARROW key";
	keyNameList[0x27] = "RIGHT ARROW key";
	keyNameList[0x28] = "DOWN ARROW key";
	keyNameList[0x29] = "SELECT key";
	keyNameList[0x2A] = "PRINT key";
	keyNameList[0x2B] = "EXECUTE key";
	keyNameList[0x2C] = "PRINT SCREEN key";
	keyNameList[0x2D] = "INS key";
	keyNameList[0x2E] = "DEL key";
	keyNameList[0x2F] = "HELP key";
	keyNameList[0x30] = "0 key";
	keyNameList[0x31] = "1 key";
	keyNameList[0x32] = "2 key";
	keyNameList[0x33] = "3 key";
	keyNameList[0x34] = "4 key";
	keyNameList[0x35] = "5 key";
	keyNameList[0x36] = "6 key";
	keyNameList[0x37] = "7 key";
	keyNameList[0x38] = "8 key";
	keyNameList[0x39] = "9 key";
	keyNameList[0x41] = "A key";
	keyNameList[0x42] = "B key";
	keyNameList[0x43] = "C key";
	keyNameList[0x44] = "D key";
	keyNameList[0x45] = "E key";
	keyNameList[0x46] = "F key";
	keyNameList[0x47] = "G key";
	keyNameList[0x48] = "H key";
	keyNameList[0x49] = "I key";
	keyNameList[0x4A] = "J key";
	keyNameList[0x4B] = "K key";
	keyNameList[0x4C] = "L key";
	keyNameList[0x4D] = "M key";
	keyNameList[0x4E] = "N key";
	keyNameList[0x4F] = "O key";
	keyNameList[0x50] = "P key";
	keyNameList[0x51] = "Q key";
	keyNameList[0x52] = "R key";
	keyNameList[0x53] = "S key";
	keyNameList[0x54] = "T key";
	keyNameList[0x55] = "U key";
	keyNameList[0x56] = "V key";
	keyNameList[0x57] = "W key";
	keyNameList[0x58] = "X key";
	keyNameList[0x59] = "Y key";
	keyNameList[0x5A] = "Z key";
	keyNameList[0x60] = "Numeric keypad 0 key";
	keyNameList[0x61] = "Numeric keypad 1 key";
	keyNameList[0x62] = "Numeric keypad 2 key";
	keyNameList[0x63] = "Numeric keypad 3 key";
	keyNameList[0x64] = "Numeric keypad 4 key";
	keyNameList[0x65] = "Numeric keypad 5 key";
	keyNameList[0x66] = "Numeric keypad 6 key";
	keyNameList[0x67] = "Numeric keypad 7 key";
	keyNameList[0x68] = "Numeric keypad 8 key";
	keyNameList[0x69] = "Numeric keypad 9 key";
	keyNameList[0x6C] = "Separator key";
	keyNameList[0x6D] = "Subtract key";
	keyNameList[0x6E] = "Decimal key";
	keyNameList[0x6F] = "Divide key";
	keyNameList[0x70] = "F1 key";
	keyNameList[0x71] = "F2 key";
	keyNameList[0x72] = "F3 key";
	keyNameList[0x73] = "F4 key";
	keyNameList[0x74] = "F5 key";
	keyNameList[0x75] = "F6 key";
	keyNameList[0x76] = "F7 key";
	keyNameList[0x77] = "F8 key";
	keyNameList[0x78] = "F9 key";
	keyNameList[0x79] = "F10 key";
	keyNameList[0x7A] = "F11 key";
	keyNameList[0x7B] = "F12 key";
	keyNameList[0x7C] = "F13 key";
	keyNameList[0x7D] = "F14 key";
	keyNameList[0x7E] = "F15 key";
	keyNameList[0x7F] = "F16 key";
	keyNameList[0x80] = "F17 key";
	keyNameList[0x81] = "F18 key";
	keyNameList[0x82] = "F19 key";
	keyNameList[0x83] = "F20 key";
	keyNameList[0x84] = "F21 key";
	keyNameList[0x85] = "F22 key";
	keyNameList[0x86] = "F23 key";
	keyNameList[0x87] = "F24 key";
	keyNameList[0x90] = "NUM LOCK key";
	keyNameList[0x91] = "SCROLL LOCK key";
	keyNameList[0xA0] = "Left SHIFT key";
	keyNameList[0xA1] = "Right SHIFT key";
	keyNameList[0xA2] = "Left CONTROL key";
	keyNameList[0xA3] = "Right CONTROL key";
	keyNameList[0xA4] = "Left MENU key";
	keyNameList[0xA5] = "Right MENU key";
	keyNameList[0xFA] = "Play key";
	keyNameList[0xFB] = "Zoom key";
#pragma endregion

	screenshot = (int)false;
}

/**
* Destructor : calls ReleaseEverything() and releases swap chains.
* @see ReleaseEverything()
***/
D3DProxyDevice::~D3DProxyDevice()
{
	ReleaseEverything();

	m_spShaderViewAdjustment.reset();

	delete m_pGameHandler;
	m_spManagedShaderRegisters.reset();

	// always do this last
	auto it = m_activeSwapChains.begin();
	while (it != m_activeSwapChains.end()) {

		if ((*it) != NULL) {
			(*it)->Release();
			delete (*it);
		}

		it = m_activeSwapChains.erase(it);
	}
}

#define IF_GUID(riid,a,b,c,d,e,f,g,h,i,j,k) if ((riid.Data1==a)&&(riid.Data2==b)&&(riid.Data3==c)&&(riid.Data4[0]==d)&&(riid.Data4[1]==e)&&(riid.Data4[2]==f)&&(riid.Data4[3]==g)&&(riid.Data4[4]==h)&&(riid.Data4[5]==i)&&(riid.Data4[6]==j)&&(riid.Data4[7]==k))
/**
* Catch QueryInterface calls and increment the reference counter if necesarry. 
***/
HRESULT WINAPI D3DProxyDevice::QueryInterface(REFIID riid, LPVOID* ppv)
{
	//DEFINE_GUID(IID_IDirect3DDevice9Ex, 0xb18b10ce, 0x2649, 0x405a, 0x87, 0xf, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a);
	IF_GUID(riid,0xb18b10ce,0x2649,0x405a,0x87,0xf,0x95,0xf7,0x77,0xd4,0x31,0x3a)
	{
		if (ppv==NULL)
			return E_POINTER;

		this->AddRef();
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	return BaseDirect3DDevice9::QueryInterface(riid,ppv);
}

/**
* Currently base TestCooperativeLevel() functionality.
***/
HRESULT WINAPI D3DProxyDevice::TestCooperativeLevel()
{
	HRESULT result = BaseDirect3DDevice9::TestCooperativeLevel();

	if( result == D3DERR_DEVICENOTRESET ) {

		// The calling application will start releasing resources after TestCooperativeLevel returns D3DERR_DEVICENOTRESET.

	}

	return result;
}

/**
* Calls SetCursorProperties() using the actual left surface from the proxy of pCursorBitmap.
***/
HRESULT WINAPI D3DProxyDevice::SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap)
{
	if (!pCursorBitmap)
		return BaseDirect3DDevice9::SetCursorProperties(XHotSpot, YHotSpot, NULL);

	return BaseDirect3DDevice9::SetCursorProperties(XHotSpot, YHotSpot, static_cast<D3D9ProxySurface*>(pCursorBitmap)->getActualLeft());
}

/**
* Creates a proxy (or wrapped) swap chain.
* @param pSwapChain [in, ou] Proxy (wrapped) swap chain to be returned.
***/
HRESULT WINAPI D3DProxyDevice::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters,IDirect3DSwapChain9** pSwapChain)
{
	IDirect3DSwapChain9* pActualSwapChain;
	HRESULT result = BaseDirect3DDevice9::CreateAdditionalSwapChain(pPresentationParameters, &pActualSwapChain);

	if (SUCCEEDED(result)) {
		D3D9ProxySwapChain* wrappedSwapChain = new D3D9ProxySwapChain(pActualSwapChain, this, true);
		*pSwapChain = wrappedSwapChain;
		m_activeSwapChains.push_back(wrappedSwapChain);
	}

	return result;
}

/**
* Provides the swap chain from the intern vector of active proxy (wrapped) swap chains.
* @param pSwapChain [in, out] The proxy (wrapped) swap chain to be returned.
* @see D3D9ProxySwapChain
***/
HRESULT WINAPI D3DProxyDevice::GetSwapChain(UINT iSwapChain,IDirect3DSwapChain9** pSwapChain)
{
	try {
		*pSwapChain = m_activeSwapChains.at(iSwapChain); 
		//Device->GetSwapChain increases ref count on the chain (docs don't say this)
		(*pSwapChain)->AddRef();
	}
	catch (std::out_of_range) {
		OutputDebugString("GetSwapChain: out of range fetching swap chain");
		return D3DERR_INVALIDCALL;
	}

	return D3D_OK;
}

/**
* Calls release functions here and in stereo view class, releases swap chains and restores everything.
* Subclasses which override this method must call through to super method at the end of the subclasses
* implementation.
* @see ReleaseEverything()
* @see StereoView::ReleaseEverything()
* @see OnCreateOrRestore()
***/
HRESULT WINAPI D3DProxyDevice::Reset(D3DPRESENT_PARAMETERS* pPresentationParameters)
{
	if(stereoView)
		stereoView->ReleaseEverything();

	ReleaseEverything();

	m_bInBeginEndStateBlock = false;

	auto it = m_activeSwapChains.begin();
	while (it != m_activeSwapChains.end()) {

		if ((*it) != NULL)
			(*it)->Release();

		delete (*it);

		it = m_activeSwapChains.erase(it);
	}

	HRESULT hr = BaseDirect3DDevice9::Reset(pPresentationParameters);

	// if the device has been successfully reset we need to recreate any resources we created
	if (hr == D3D_OK)  {
		OnCreateOrRestore();
		stereoView->PostReset();
	}
	else {
#ifdef _DEBUG
		char buf[256];
		sprintf_s(buf, "Error: %s error description: %s\n",
			DXGetErrorString(hr), DXGetErrorDescription(hr));

		OutputDebugString(buf);				
#endif
		OutputDebugString("Device reset failed");
	}

	return hr;
}

/**
* Here the chosen stereoviews draw function is called to render to wrapped back buffer.
* All other final screen output is also done here.
***/
HRESULT WINAPI D3DProxyDevice::Present(CONST RECT* pSourceRect,CONST RECT* pDestRect,HWND hDestWindowOverride,CONST RGNDATA* pDirtyRegion)
{
	IDirect3DSurface9* pWrappedBackBuffer;

	try {
		m_activeSwapChains.at(0)->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pWrappedBackBuffer);

		if (stereoView->initialized)
			stereoView->Draw(static_cast<D3D9ProxySurface*>(pWrappedBackBuffer));

		pWrappedBackBuffer->Release();
	}
	catch (std::out_of_range) {
		OutputDebugString("Present: No primary swap chain found. (Present probably called before device has been reset)");
	}

	m_isFirstBeginSceneOfFrame = true; // TODO this can break if device present is followed by present on another swap chain... or not work well anyway

	// BRASSA called here (if not source engine)
	if((stereoView->game_type != D3DProxyDevice::SOURCE_L4D) && (stereoView->game_type != D3DProxyDevice::DATA_GATHERER_SOURCE) && (stereoView->game_type != D3DProxyDevice::ADVANCED_SKYRIM))
	{
		if ((BRASSA_mode>=BRASSA_Modes::MAINMENU) && (BRASSA_mode<BRASSA_Modes::BRASSA_ENUM_RANGE))
			BRASSA();
	}

	// BRASSA menu border velocity updated here
	// Arrow up/down need to be done here !!
	menuVelocity*=0.76f;
	if ((menuVelocity.y<0.5f) && (menuVelocity.y>-0.5f) &&
		(menuVelocity.x<0.5f) && (menuVelocity.x>-0.5f))
		menuVelocity = D3DXVECTOR2(0.0f, 0.0f);
	if (BRASSA_mode != BRASSA_Modes::INACTIVE)
	{
		float fScaleY = ((float)stereoView->viewport.Height / (float)1080.0f);
		if ((KEY_DOWN(VK_UP) || KEY_DOWN(0x49)) && (menuVelocity.y==0.0f))
			menuVelocity.y-=10.0f;
		if ((KEY_DOWN(VK_DOWN) || KEY_DOWN(0x4B)) && (menuVelocity.y==0.0f))
			menuVelocity.y+=10.0f;
		borderTopHeight += menuVelocity.y*fScaleY;
	}
	return BaseDirect3DDevice9::Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

/**
* Calls the backbuffer using the stored active proxy (wrapped) swap chain.
***/
HRESULT WINAPI D3DProxyDevice::GetBackBuffer(UINT iSwapChain,UINT iBackBuffer,D3DBACKBUFFER_TYPE Type,IDirect3DSurface9** ppBackBuffer)
{
	HRESULT result;
	try {
		result = m_activeSwapChains.at(iSwapChain)->GetBackBuffer(iBackBuffer, D3DBACKBUFFER_TYPE_MONO, ppBackBuffer);
		// ref count increase happens in the swapchain GetBackBuffer so we don't add another ref here as we are just passing the value through
	}
	catch (std::out_of_range) {
		OutputDebugString("GetBackBuffer: out of range getting swap chain");
		result = D3DERR_INVALIDCALL;
	}

	return result;
}

/**
* Creates a proxy (or wrapped) texture (D3DProxyTexture).
* Texture to be created only gets both stereo textures if game handler agrees.
* @see D3DProxyTexture
* @see GameHandler::ShouldDuplicateTexture()
***/
HRESULT WINAPI D3DProxyDevice::CreateTexture(UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture9** ppTexture,HANDLE* pSharedHandle)
{
	HRESULT creationResult;
	IDirect3DTexture9* pLeftTexture = NULL;
	IDirect3DTexture9* pRightTexture = NULL;	

	// try and create left
	if (SUCCEEDED(creationResult = BaseDirect3DDevice9::CreateTexture(Width, Height, Levels, Usage, Format, Pool, &pLeftTexture, pSharedHandle))) {

		// Does this Texture need duplicating?
		if (m_pGameHandler->ShouldDuplicateTexture(Width, Height, Levels, Usage, Format, Pool)) {

			if (FAILED(BaseDirect3DDevice9::CreateTexture(Width, Height, Levels, Usage, Format, Pool, &pRightTexture, pSharedHandle))) {
				OutputDebugString("Failed to create right eye texture while attempting to create stereo pair, falling back to mono\n");
				pRightTexture = NULL;
			}
		}
	}
	else {
		OutputDebugString("Failed to create texture\n"); 
	}

	if (SUCCEEDED(creationResult))
		*ppTexture = new D3D9ProxyTexture(pLeftTexture, pRightTexture, this);

	return creationResult;
}

/**
* Creates a a proxy (or wrapped) volume texture (D3D9ProxyVolumeTexture).
* Volumes can't be used as render targets and therefore don't need to be stereo (in DX9).
* @see D3D9ProxyVolumeTexture
***/	
HRESULT WINAPI D3DProxyDevice::CreateVolumeTexture(UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture9** ppVolumeTexture,HANDLE* pSharedHandle)
{
	IDirect3DVolumeTexture9* pActualTexture = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, &pActualTexture, pSharedHandle);

	if (SUCCEEDED(creationResult))
		*ppVolumeTexture = new D3D9ProxyVolumeTexture(pActualTexture, this);

	return creationResult;
}

/**
* Creates a proxy (or wrapped) cube texture (D3D9ProxyCubeTexture).
* Texture to be created only gets both stereo textures if game handler agrees.
* @see D3D9ProxyCubeTexture
* @see GameHandler::ShouldDuplicateCubeTexture() 
***/
HRESULT WINAPI D3DProxyDevice::CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle)
{
	HRESULT creationResult;
	IDirect3DCubeTexture9* pLeftCubeTexture = NULL;
	IDirect3DCubeTexture9* pRightCubeTexture = NULL;	

	// try and create left
	if (SUCCEEDED(creationResult = BaseDirect3DDevice9::CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, &pLeftCubeTexture, pSharedHandle))) {

		// Does this Texture need duplicating?
		if (m_pGameHandler->ShouldDuplicateCubeTexture(EdgeLength, Levels, Usage, Format, Pool)) {

			if (FAILED(BaseDirect3DDevice9::CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, &pRightCubeTexture, pSharedHandle))) {
				OutputDebugString("Failed to create right eye texture while attempting to create stereo pair, falling back to mono\n");
				pRightCubeTexture = NULL;
			}
		}
	}
	else {
		OutputDebugString("Failed to create texture\n"); 
	}

	if (SUCCEEDED(creationResult))
		*ppCubeTexture = new D3D9ProxyCubeTexture(pLeftCubeTexture, pRightCubeTexture, this);

	return creationResult;
}

/**
* Creates base vertex buffer pointer (BaseDirect3DVertexBuffer9).
* @see BaseDirect3DVertexBuffer9
***/
HRESULT WINAPI D3DProxyDevice::CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle)
{
	IDirect3DVertexBuffer9* pActualBuffer = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateVertexBuffer(Length, Usage, FVF, Pool, &pActualBuffer, pSharedHandle);

	if (SUCCEEDED(creationResult))
		*ppVertexBuffer = new BaseDirect3DVertexBuffer9(pActualBuffer, this);

	return creationResult;
}

/**
* * Creates base index buffer pointer (BaseDirect3DIndexBuffer9).
* @see BaseDirect3DIndexBuffer9
***/
HRESULT WINAPI D3DProxyDevice::CreateIndexBuffer(UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer9** ppIndexBuffer,HANDLE* pSharedHandle)
{
	IDirect3DIndexBuffer9* pActualBuffer = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateIndexBuffer(Length, Usage, Format, Pool, &pActualBuffer, pSharedHandle);

	if (SUCCEEDED(creationResult))
		*ppIndexBuffer = new BaseDirect3DIndexBuffer9(pActualBuffer, this);

	return creationResult;
}

/**
* Calls the public overloaded function.
* The IDirect3DSurface9** ppSurface returned should always be a D3D9ProxySurface. Any class overloading
* this method should ensure that this remains true.
***/
HRESULT WINAPI D3DProxyDevice::CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
												  DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle)
{
	// call public overloaded function
	return CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle, false);
}

/**
* Creates a proxy (or wrapped) depth stencil surface (D3D9ProxySurface).
* Surface to be created only gets both stereo surfaces if game handler agrees.
* @see D3D9ProxySurface
* @see GameHandler::ShouldDuplicateDepthStencilSurface() 
***/
HRESULT WINAPI D3DProxyDevice::CreateDepthStencilSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Discard,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle)
{
	IDirect3DSurface9* pDepthStencilSurfaceLeft = NULL;
	IDirect3DSurface9* pDepthStencilSurfaceRight = NULL;
	HRESULT creationResult;

	// create left/mono
	if (SUCCEEDED(creationResult = BaseDirect3DDevice9::CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, &pDepthStencilSurfaceLeft, pSharedHandle))) {

		// TODO Should we always duplicated Depth stencils? I think yes, but there may be exceptions
		if (m_pGameHandler->ShouldDuplicateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard)) 
		{
			if (FAILED(BaseDirect3DDevice9::CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, &pDepthStencilSurfaceRight, pSharedHandle))) {
				OutputDebugString("Failed to create right eye Depth Stencil Surface while attempting to create stereo pair, falling back to mono\n");
				pDepthStencilSurfaceRight = NULL;
			}
		}
	}
	else {
		OutputDebugString("Failed to create Depth Stencil Surface\n"); 
	}

	if (SUCCEEDED(creationResult))
		*ppSurface = new D3D9ProxySurface(pDepthStencilSurfaceLeft, pDepthStencilSurfaceRight, this, NULL);

	return creationResult;
}

/**
* Copies rectangular subsets of pixels from one proxy (wrapped) surface to another.
* @see D3D9ProxySurface
***/
HRESULT WINAPI D3DProxyDevice::UpdateSurface(IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestinationSurface,CONST POINT* pDestPoint)
{
	if (!pSourceSurface || !pDestinationSurface)
		return D3DERR_INVALIDCALL;

	IDirect3DSurface9* pSourceSurfaceLeft = static_cast<D3D9ProxySurface*>(pSourceSurface)->getActualLeft();
	IDirect3DSurface9* pSourceSurfaceRight = static_cast<D3D9ProxySurface*>(pSourceSurface)->getActualRight();
	IDirect3DSurface9* pDestSurfaceLeft = static_cast<D3D9ProxySurface*>(pDestinationSurface)->getActualLeft();
	IDirect3DSurface9* pDestSurfaceRight = static_cast<D3D9ProxySurface*>(pDestinationSurface)->getActualRight();

	HRESULT result = BaseDirect3DDevice9::UpdateSurface(pSourceSurfaceLeft, pSourceRect, pDestSurfaceLeft, pDestPoint);

	if (SUCCEEDED(result)) {
		if (!pSourceSurfaceRight && pDestSurfaceRight) {
			//OutputDebugString("INFO: UpdateSurface - Source is not stereo, destination is stereo. Copying source to both sides of destination.\n");

			if (FAILED(BaseDirect3DDevice9::UpdateSurface(pSourceSurfaceLeft, pSourceRect, pDestSurfaceRight, pDestPoint))) {
				OutputDebugString("ERROR: UpdateSurface - Failed to copy source left to destination right.\n");
			}
		} 
		else if (pSourceSurfaceRight && !pDestSurfaceRight) {
			//OutputDebugString("INFO: UpdateSurface - Source is stereo, destination is not stereo. Copied Left side only.\n");
		}
		else if (pSourceSurfaceRight && pDestSurfaceRight)	{
			if (FAILED(BaseDirect3DDevice9::UpdateSurface(pSourceSurfaceRight, pSourceRect, pDestSurfaceRight, pDestPoint))) {
				OutputDebugString("ERROR: UpdateSurface - Failed to copy source right to destination right.\n");
			}
		}
	}

	return result;
}

/**
* Calls a helper function to unwrap the textures and calls the super method for both sides.
* The super method updates the dirty portions of a texture.
* @see vireio::UnWrapTexture()
***/
HRESULT WINAPI D3DProxyDevice::UpdateTexture(IDirect3DBaseTexture9* pSourceTexture,IDirect3DBaseTexture9* pDestinationTexture)
{
	if (!pSourceTexture || !pDestinationTexture)
		return D3DERR_INVALIDCALL;


	IDirect3DBaseTexture9* pSourceTextureLeft = NULL;
	IDirect3DBaseTexture9* pSourceTextureRight = NULL;
	IDirect3DBaseTexture9* pDestTextureLeft = NULL;
	IDirect3DBaseTexture9* pDestTextureRight = NULL;

	vireio::UnWrapTexture(pSourceTexture, &pSourceTextureLeft, &pSourceTextureRight);
	vireio::UnWrapTexture(pDestinationTexture, &pDestTextureLeft, &pDestTextureRight);

	HRESULT result = BaseDirect3DDevice9::UpdateTexture(pSourceTextureLeft, pDestTextureLeft);

	if (SUCCEEDED(result)) {
		if (!pSourceTextureRight && pDestTextureRight) {
			//OutputDebugString("INFO: UpdateTexture - Source is not stereo, destination is stereo. Copying source to both sides of destination.\n");

			if (FAILED(BaseDirect3DDevice9::UpdateTexture(pSourceTextureLeft, pDestTextureRight))) {
				OutputDebugString("ERROR: UpdateTexture - Failed to copy source left to destination right.\n");
			}
		} 
		else if (pSourceTextureRight && !pDestTextureRight) {
			//OutputDebugString("INFO: UpdateTexture - Source is stereo, destination is not stereo. Copied Left side only.\n");
		}
		else if (pSourceTextureRight && pDestTextureRight)	{
			if (FAILED(BaseDirect3DDevice9::UpdateTexture(pSourceTextureRight, pDestTextureRight))) {
				OutputDebugString("ERROR: UpdateTexture - Failed to copy source right to destination right.\n");
			}
		}
	}

	return result;
}

/**
* Copies the render-target data from proxy (wrapped) source surface to proxy (wrapped) destination surface.
***/
HRESULT WINAPI D3DProxyDevice::GetRenderTargetData(IDirect3DSurface9* pRenderTarget,IDirect3DSurface9* pDestSurface)
{
	if ((pDestSurface == NULL) || (pRenderTarget == NULL))
		return D3DERR_INVALIDCALL;

	D3D9ProxySurface* pWrappedRenderTarget = static_cast<D3D9ProxySurface*>(pRenderTarget);
	D3D9ProxySurface* pWrappedDest = static_cast<D3D9ProxySurface*>(pDestSurface);

	IDirect3DSurface9* pRenderTargetLeft = pWrappedRenderTarget->getActualLeft();
	IDirect3DSurface9* pRenderTargetRight = pWrappedRenderTarget->getActualRight();
	IDirect3DSurface9* pDestSurfaceLeft = pWrappedDest->getActualLeft();
	IDirect3DSurface9* pDestSurfaceRight = pWrappedDest->getActualRight();

	HRESULT result = BaseDirect3DDevice9::GetRenderTargetData(pRenderTargetLeft, pDestSurfaceLeft);

	if (SUCCEEDED(result)) {
		if (!pRenderTargetRight && pDestSurfaceRight) {
			//OutputDebugString("INFO: GetRenderTargetData - Source is not stereo, destination is stereo. Copying source to both sides of destination.\n");

			if (FAILED(BaseDirect3DDevice9::GetRenderTargetData(pRenderTargetLeft, pDestSurfaceRight))) {
				OutputDebugString("ERROR: GetRenderTargetData - Failed to copy source left to destination right.\n");
			}
		} 
		else if (pRenderTargetRight && !pDestSurfaceRight) {
			//OutputDebugString("INFO: GetRenderTargetData - Source is stereo, destination is not stereo. Copied Left side only.\n");
		}
		else if (pRenderTargetRight && pDestSurfaceRight)	{
			if (FAILED(BaseDirect3DDevice9::GetRenderTargetData(pRenderTargetRight, pDestSurfaceRight))) {
				OutputDebugString("ERROR: GetRenderTargetData - Failed to copy source right to destination right.\n");
			}
		}
	}

	return result;
}

/**
* Gets the front buffer data from the internal stored active proxy (or wrapped) swap chain.
* @see D3D9ProxySwapChain
***/
HRESULT WINAPI D3DProxyDevice::GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface)
{
	HRESULT result;
	try {
		result = m_activeSwapChains.at(iSwapChain)->GetFrontBufferData(pDestSurface);
	}
	catch (std::out_of_range) {
		OutputDebugString("GetFrontBufferData: out of range fetching swap chain");
		result = D3DERR_INVALIDCALL;
	}

	return result;
}

/**
* Copy the contents of the source proxy (wrapped) surface rectangles to the destination proxy (wrapped) surface rectangles.
* @see D3D9ProxySurface
***/
HRESULT WINAPI D3DProxyDevice::StretchRect(IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestSurface,CONST RECT* pDestRect,D3DTEXTUREFILTERTYPE Filter)
{
	if (!pSourceSurface || !pDestSurface)
		return D3DERR_INVALIDCALL;

	D3D9ProxySurface* pWrappedSource = static_cast<D3D9ProxySurface*>(pSourceSurface);
	D3D9ProxySurface* pWrappedDest = static_cast<D3D9ProxySurface*>(pDestSurface);

	IDirect3DSurface9* pSourceSurfaceLeft = pWrappedSource->getActualLeft();
	IDirect3DSurface9* pSourceSurfaceRight = pWrappedSource->getActualRight();
	IDirect3DSurface9* pDestSurfaceLeft = pWrappedDest->getActualLeft();
	IDirect3DSurface9* pDestSurfaceRight = pWrappedDest->getActualRight();

	HRESULT result = BaseDirect3DDevice9::StretchRect(pSourceSurfaceLeft, pSourceRect, pDestSurfaceLeft, pDestRect, Filter);

	if (SUCCEEDED(result)) {
		if (!pSourceSurfaceRight && pDestSurfaceRight) {
			//OutputDebugString("INFO: StretchRect - Source is not stereo, destination is stereo. Copying source to both sides of destination.\n");

			if (FAILED(BaseDirect3DDevice9::StretchRect(pSourceSurfaceLeft, pSourceRect, pDestSurfaceRight, pDestRect, Filter))) {
				OutputDebugString("ERROR: StretchRect - Failed to copy source left to destination right.\n");
			}
		} 
		else if (pSourceSurfaceRight && !pDestSurfaceRight) {
			//OutputDebugString("INFO: StretchRect - Source is stereo, destination is not stereo. Copied Left side only.\n");
		}
		else if (pSourceSurfaceRight && pDestSurfaceRight)	{
			if (FAILED(BaseDirect3DDevice9::StretchRect(pSourceSurfaceRight, pSourceRect, pDestSurfaceRight, pDestRect, Filter))) {
				OutputDebugString("ERROR: StretchRect - Failed to copy source right to destination right.\n");
			}
		}
	}

	return result;
}

/**
* Fills the rectangle for both stereo sides if switchDrawingSide() agrees and sets the render target accordingly.
* @see switchDrawingSide()
***/
HRESULT WINAPI D3DProxyDevice::ColorFill(IDirect3DSurface9* pSurface,CONST RECT* pRect,D3DCOLOR color)
{
	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::ColorFill(pSurface, pRect, color))) {
		if (switchDrawingSide())
			BaseDirect3DDevice9::ColorFill(pSurface, pRect, color);
	}

	return result;
}

/**
* Creates a wrapped mono surface with only one (left) side.
* OffscreenPlainSurfaces doesn't need to be Stereo. 
* They can't be used as render targets and they can't have rendertargets copied to them with stretch
* rect, so don't need to be stereo capable.
* See table at bottom of 
* <http://msdn.microsoft.com/en-us/library/windows/desktop/bb174471%28v=vs.85%29.aspx> 
* for stretch rect restrictions.
**/
HRESULT WINAPI D3DProxyDevice::CreateOffscreenPlainSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DPOOL Pool,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle)
{	
	IDirect3DSurface9* pActualSurface = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateOffscreenPlainSurface(Width, Height, Format, Pool, &pActualSurface, pSharedHandle);

	if (SUCCEEDED(creationResult))
		*ppSurface = new D3D9ProxySurface(pActualSurface, NULL, this, NULL);

	return creationResult;
}

/**
* Updates render target accordingly to current render side.
* Updates proxy collection of stereo render targets to reflect new actual render target.
***/
HRESULT WINAPI D3DProxyDevice::SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget)
{
	D3D9ProxySurface* newRenderTarget = static_cast<D3D9ProxySurface*>(pRenderTarget);

#ifdef _DEBUG
	if (newRenderTarget && !newRenderTarget->getActualLeft() && !newRenderTarget->getActualRight()) {
		OutputDebugString("RenderTarget is not a valid (D3D9ProxySurface) stereo capable surface\n"); 
	}
#endif

	//// Update actual render target ////
	HRESULT result;

	// Removing a render target
	if (newRenderTarget == NULL) {
		if (RenderTargetIndex == 0) {
			// main render target should never be set to NULL
			result = D3DERR_INVALIDCALL; 
		}		
		else {
			result = BaseDirect3DDevice9::SetRenderTarget(RenderTargetIndex, NULL);
		}
	}
	// Setting a render target
	else {
		if (m_currentRenderingSide == vireio::Left) {
			result = BaseDirect3DDevice9::SetRenderTarget(RenderTargetIndex, newRenderTarget->getActualLeft());
		}
		else {
			result = BaseDirect3DDevice9::SetRenderTarget(RenderTargetIndex, newRenderTarget->getActualRight());
		}
	}

	//// update proxy collection of stereo render targets to reflect new actual render target ////
	if (result == D3D_OK) {		
		// changing rendertarget resets viewport to fullsurface
		m_bActiveViewportIsDefault = true;

		// release old render target
		if (m_activeRenderTargets[RenderTargetIndex] != NULL)
			m_activeRenderTargets[RenderTargetIndex]->Release();

		// replace with new render target (may be NULL)
		m_activeRenderTargets[RenderTargetIndex] = newRenderTarget;
		if (m_activeRenderTargets[RenderTargetIndex] != NULL)
			m_activeRenderTargets[RenderTargetIndex]->AddRef();
	}

	return result;
}

/**
* Provides render target from the internally stored active proxy (wrapped) render targets.
***/
HRESULT WINAPI D3DProxyDevice::GetRenderTarget(DWORD RenderTargetIndex,IDirect3DSurface9** ppRenderTarget)
{
	if ((RenderTargetIndex >= m_activeRenderTargets.capacity()) || (RenderTargetIndex < 0)) {
		return D3DERR_INVALIDCALL;
	}

	IDirect3DSurface9* targetToReturn = m_activeRenderTargets[RenderTargetIndex];
	if (!targetToReturn)
		return D3DERR_NOTFOUND;
	else {
		*ppRenderTarget = targetToReturn;
		targetToReturn->AddRef();
		return D3D_OK;
	}
}

/**
* Updates depth stencil accordingly to current render side.
* Updates stored proxy (or wrapped) depth stencil.
***/
HRESULT WINAPI D3DProxyDevice::SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil)
{
	D3D9ProxySurface* pNewDepthStencil = static_cast<D3D9ProxySurface*>(pNewZStencil);

	IDirect3DSurface9* pActualStencilForCurrentSide = NULL;
	if (pNewDepthStencil) {
		if (m_currentRenderingSide == vireio::Left)
			pActualStencilForCurrentSide = pNewDepthStencil->getActualLeft();
		else
			pActualStencilForCurrentSide = pNewDepthStencil->getActualRight();
	}

	// Update actual depth stencil
	HRESULT result = BaseDirect3DDevice9::SetDepthStencilSurface(pActualStencilForCurrentSide);

	// Update stored proxy depth stencil
	if (SUCCEEDED(result)) {
		if (m_pActiveStereoDepthStencil) {
			m_pActiveStereoDepthStencil->Release();
		}

		m_pActiveStereoDepthStencil = pNewDepthStencil;
		if (m_pActiveStereoDepthStencil) {
			m_pActiveStereoDepthStencil->AddRef();
		}
	}

	return result;
}

/**
* Provides the active proxy (wrapped) depth stencil.
***/
HRESULT WINAPI D3DProxyDevice::GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface)
{
	if (!m_pActiveStereoDepthStencil)
		return D3DERR_NOTFOUND;

	*ppZStencilSurface = m_pActiveStereoDepthStencil;
	(*ppZStencilSurface)->AddRef();

	return D3D_OK;
}

/**
* Updates tracker, handles controls if this the first scene of the frame.
* TODO !! handle tracking is currently done here - Do this as late in frame as possible (Present)? 
* Because input for this frame would already have been handled here so injection of any mouse 
* manipulation ?
***/
HRESULT WINAPI D3DProxyDevice::BeginScene()
{
	if (m_isFirstBeginSceneOfFrame) {

		// save screenshot before first clear() is called
		if (screenshot>0)
		{
			if (screenshot==1)
				stereoView->SaveScreen();
			screenshot--;
		}

		// TODO ? move this to Present() ?
		// mouse emulation ?
		HandleControls();
		HandleTracking(); 

		// TODO Doing this now gives very current roll to frame. But should it be done with handle tracking to keep latency similar?
		// How much latency does mouse enulation cause? Probably want direct roll manipulation and mouse emulation to occur with same delay
		// if possible?
		if (trackerInitialized && tracker->isAvailable() && m_spShaderViewAdjustment->RollEnabled()) {
			m_spShaderViewAdjustment->UpdateRoll(tracker->currentRoll);
		}
		m_spShaderViewAdjustment->UpdatePitchYaw(tracker->primaryPitch, tracker->primaryYaw);

		m_spShaderViewAdjustment->ComputeViewTransforms();

		m_isFirstBeginSceneOfFrame = false;
	}

	return BaseDirect3DDevice9::BeginScene();
}

/**
* BRASSA called here for source engine games.
***/
HRESULT WINAPI D3DProxyDevice::EndScene()
{
	if((stereoView->game_type == D3DProxyDevice::SOURCE_L4D) || (stereoView->game_type != D3DProxyDevice::DATA_GATHERER_SOURCE) || (stereoView->game_type == D3DProxyDevice::ADVANCED_SKYRIM))
	{
		if ((BRASSA_mode>=BRASSA_Modes::MAINMENU) && (BRASSA_mode<BRASSA_Modes::BRASSA_ENUM_RANGE))
			BRASSA();
	}
	return BaseDirect3DDevice9::EndScene();
}

/**
* Clears both stereo sides if switchDrawingSide() agrees.
***/
HRESULT WINAPI D3DProxyDevice::Clear(DWORD Count,CONST D3DRECT* pRects,DWORD Flags,D3DCOLOR Color,float Z,DWORD Stencil)
{

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::Clear(Count, pRects, Flags, Color, Z, Stencil))) {
		if (switchDrawingSide()) {

			HRESULT hr;
			if (FAILED(hr = BaseDirect3DDevice9::Clear(Count, pRects, Flags, Color, Z, Stencil))) {

#ifdef _DEBUG
				// TODO !! Deus Ex fails when trying to clear resulting in depth buffer issues.
				char buf[256];
				sprintf_s(buf, "Error: %s error description: %s\n",
					DXGetErrorString(hr), DXGetErrorDescription(hr));

				OutputDebugString(buf);
				OutputDebugString("Clear failed\n");

#endif

			}
		}
	}

	return result;
}

/**
* Catches transform for stored proxy state block accordingly or updates proxy device.
* @see D3D9ProxyStateBlock
***/
HRESULT WINAPI D3DProxyDevice::SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix)
{
	if(State == D3DTS_VIEW)
	{
		D3DXMATRIX tempLeft;
		D3DXMATRIX tempRight;
		D3DXMATRIX* pViewToSet = NULL;
		bool tempIsTransformSet = false;

		if (!pMatrix) {
			D3DXMatrixIdentity(&tempLeft);
			D3DXMatrixIdentity(&tempRight);
		}
		else {

			D3DXMATRIX sourceMatrix(*pMatrix);

			// If the view is set to the identity then we don't need to perform any adjustments
			if (D3DXMatrixIsIdentity(&sourceMatrix)) {

				D3DXMatrixIdentity(&tempLeft);
				D3DXMatrixIdentity(&tempRight);
			}
			else {
				// If the view matrix is modified we need to apply left/right adjustments (for stereo rendering)
				tempLeft = sourceMatrix * m_spShaderViewAdjustment->LeftViewTransform();
				tempRight = sourceMatrix * m_spShaderViewAdjustment->RightViewTransform();

				tempIsTransformSet = true;
			}
		}


		// If capturing state block capture without updating proxy device
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureViewTransform(tempLeft, tempRight);
			if (m_currentRenderingSide == vireio::Left) {
				pViewToSet = &tempLeft;
			}
			else {
				pViewToSet = &tempRight;
			}
		}
		else { // otherwise update proxy device

			m_bViewTransformSet = tempIsTransformSet;
			m_leftView = tempLeft;
			m_rightView = tempRight;

			if (m_currentRenderingSide == vireio::Left) {
				m_pCurrentView = &m_leftView;
			}
			else {
				m_pCurrentView = &m_rightView;
			}

			pViewToSet = m_pCurrentView;
		}

		return BaseDirect3DDevice9::SetTransform(State, pViewToSet);

	}
	else if(State == D3DTS_PROJECTION)
	{

		D3DXMATRIX tempLeft;
		D3DXMATRIX tempRight;
		D3DXMATRIX* pProjectionToSet = NULL;
		bool tempIsTransformSet = false;

		if (!pMatrix) {

			D3DXMatrixIdentity(&tempLeft);
			D3DXMatrixIdentity(&tempRight);
		}
		else {
			D3DXMATRIX sourceMatrix(*pMatrix);

			// If the view is set to the identity then we don't need to perform any adjustments

			if (D3DXMatrixIsIdentity(&sourceMatrix)) {

				D3DXMatrixIdentity(&tempLeft);
				D3DXMatrixIdentity(&tempRight);
			}
			else {


				tempLeft = sourceMatrix * m_spShaderViewAdjustment->LeftShiftProjection();
				tempRight = sourceMatrix * m_spShaderViewAdjustment->RightShiftProjection();

				tempIsTransformSet = true;
			}			
		}

		// If capturing state block capture without updating proxy device
		if (m_pCapturingStateTo) {

			m_pCapturingStateTo->SelectAndCaptureProjectionTransform(tempLeft, tempRight);
			if (m_currentRenderingSide == vireio::Left) {
				pProjectionToSet = &tempLeft;
			}
			else {
				pProjectionToSet = &tempRight;
			}
		}
		else { // otherwise update proxy device

			m_bProjectionTransformSet = tempIsTransformSet;
			m_leftProjection = tempLeft;
			m_rightProjection = tempRight;

			if (m_currentRenderingSide == vireio::Left) {
				m_pCurrentProjection = &m_leftProjection;
			}
			else {
				m_pCurrentProjection = &m_rightProjection;
			}

			pProjectionToSet = m_pCurrentProjection;
		}

		return BaseDirect3DDevice9::SetTransform(State, pProjectionToSet);
	}

	return BaseDirect3DDevice9::SetTransform(State, pMatrix);
}

/**
* Not implemented now - fix in case it needs fixing, calls super method.
***/
HRESULT WINAPI D3DProxyDevice::MultiplyTransform(D3DTRANSFORMSTATETYPE State,CONST D3DMATRIX* pMatrix)
{
	OutputDebugString(__FUNCTION__); 
	OutputDebugString("\n"); 
	OutputDebugString("Not implemented - Fix Me! (if i need fixing)\n"); 

	return BaseDirect3DDevice9::MultiplyTransform(State, pMatrix);
}

/**
* Try and set, if success save viewport.
* Also, it captures the viewport state in stored proxy state block.
* If viewport width and height match primary render target size and zmin is 0 and zmax 1 set 
* m_bActiveViewportIsDefault flag true.
* @see D3D9ProxyStateBlock::SelectAndCaptureState()
* @see m_bActiveViewportIsDefault
***/
HRESULT WINAPI D3DProxyDevice::SetViewport(CONST D3DVIEWPORT9* pViewport)
{	
	HRESULT result = BaseDirect3DDevice9::SetViewport(pViewport);

	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(*pViewport);
		}
		else {
			m_bActiveViewportIsDefault = isViewportDefaultForMainRT(pViewport);
			m_LastViewportSet = *pViewport;
		}
	}

	return result;
}

/**
* Creates proxy state block.
* Also, selects capture type option according to state block type.
* @param ppSB [in, out] The proxy (or wrapped) state block returned.
* @see D3DProxyStateBlock
***/
HRESULT WINAPI D3DProxyDevice::CreateStateBlock(D3DSTATEBLOCKTYPE Type,IDirect3DStateBlock9** ppSB)
{
	IDirect3DStateBlock9* pActualStateBlock = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateStateBlock(Type, &pActualStateBlock);

	if (SUCCEEDED(creationResult)) {

		D3D9ProxyStateBlock::CaptureType capType;

		switch (Type) {
		case D3DSBT_ALL: 
			{
				capType = D3D9ProxyStateBlock::Cap_Type_Full;
				break;
			}

		case D3DSBT_PIXELSTATE: 
			{
				capType = D3D9ProxyStateBlock::Cap_Type_Pixel;
				break;
			}

		case D3DSBT_VERTEXSTATE: 
			{
				capType = D3D9ProxyStateBlock::Cap_Type_Vertex;
				break;
			}

		default:
			{
				capType = D3D9ProxyStateBlock::Cap_Type_Full;
				break;
			}    
		}

		*ppSB = new D3D9ProxyStateBlock(pActualStateBlock, this, capType, m_currentRenderingSide == vireio::Left);
	}

	return creationResult;
}

/**
* Creates and stores proxy state block.
* @see D3DProxyStateBlock
***/
HRESULT WINAPI D3DProxyDevice::BeginStateBlock()
{
	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::BeginStateBlock())) {
		m_bInBeginEndStateBlock = true;
		m_pCapturingStateTo = new D3D9ProxyStateBlock(NULL, this, D3D9ProxyStateBlock::Cap_Type_Selected, m_currentRenderingSide == vireio::Left);
	}

	return result;
}

/**
* Calls both super method and method from stored proxy state block.
* @param [in, out] The returned proxy (or wrapped) state block.
* @see D3D9ProxyStateBlock::EndStateBlock()
***/
HRESULT WINAPI D3DProxyDevice::EndStateBlock(IDirect3DStateBlock9** ppSB)
{
	IDirect3DStateBlock9* pActualStateBlock = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::EndStateBlock(&pActualStateBlock);

	if (SUCCEEDED(creationResult)) {
		m_pCapturingStateTo->EndStateBlock(pActualStateBlock);
		*ppSB = m_pCapturingStateTo;
	}
	else {
		m_pCapturingStateTo->Release();
	}

	m_pCapturingStateTo = NULL;
	m_bInBeginEndStateBlock = false;

	return creationResult;
}

/**
* Provides texture from stored active (mono) texture stages.
* @see D3D9ProxyTexture
***/
HRESULT WINAPI D3DProxyDevice::GetTexture(DWORD Stage,IDirect3DBaseTexture9** ppTexture)
{
	if (m_activeTextureStages.count(Stage) != 1)
		return D3DERR_INVALIDCALL;
	else {
		*ppTexture = m_activeTextureStages[Stage];
		if ((*ppTexture))
			(*ppTexture)->AddRef();
		return D3D_OK;
	}
}

/**
* Calls a helper function to unwrap the textures and calls the super method for both sides.
* Update stored active (mono) texture stages if new texture was successfully set.
*
* @see vireio::UnWrapTexture() 
***/
HRESULT WINAPI D3DProxyDevice::SetTexture(DWORD Stage,IDirect3DBaseTexture9* pTexture)
{
	HRESULT result;
	if (pTexture) {

		IDirect3DBaseTexture9* pActualLeftTexture = NULL;
		IDirect3DBaseTexture9* pActualRightTexture = NULL;

		vireio::UnWrapTexture(pTexture, &pActualLeftTexture, &pActualRightTexture);

		// Try and Update the actual devices textures
		if ((pActualRightTexture == NULL) || (m_currentRenderingSide == vireio::Left)) // use left (mono) if not stereo or one left side
			result = BaseDirect3DDevice9::SetTexture(Stage, pActualLeftTexture);
		else
			result = BaseDirect3DDevice9::SetTexture(Stage, pActualRightTexture);

	}
	else {
		result = BaseDirect3DDevice9::SetTexture(Stage, NULL);
	}

	// Update m_activeTextureStages if new texture was successfully set
	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(Stage, pTexture);
		}
		else {

			// remove existing texture that was active at Stage if there is one
			if (m_activeTextureStages.count(Stage) == 1) { 

				IDirect3DBaseTexture9* pOldTexture = m_activeTextureStages.at(Stage);
				if (pOldTexture)
					pOldTexture->Release();

				m_activeTextureStages.erase(Stage);
			}

			// insert new texture (can be a NULL pointer, this is important for StateBlock tracking)
			if(m_activeTextureStages.insert(std::pair<DWORD, IDirect3DBaseTexture9*>(Stage, pTexture)).second) {
				//success
				if (pTexture)
					pTexture->AddRef();
			}
			else {
				OutputDebugString(__FUNCTION__);
				OutputDebugString("\n");
				OutputDebugString("Unable to store active Texture Stage.\n");
				assert(false);

				//If we get here the state of the texture tracking is fubared and an implosion is imminent.

				result = D3DERR_INVALIDCALL;
			}
		}
	}

	return result;
}

/**
* Applies all dirty shader registers, draws both stereo sides if switchDrawingSide() agrees.
* @see switchDrawingSide()
***/
HRESULT WINAPI D3DProxyDevice::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType,UINT StartVertex,UINT PrimitiveCount)
{
	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount))) {
		if (switchDrawingSide())
			BaseDirect3DDevice9::DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
	}

	return result;
}

/**
* Applies all dirty shader registers, draws both stereo sides if switchDrawingSide() agrees.
* @see switchDrawingSide()
***/
HRESULT WINAPI D3DProxyDevice::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType,INT BaseVertexIndex,UINT MinVertexIndex,UINT NumVertices,UINT startIndex,UINT primCount)
{
	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount))) {
		if (switchDrawingSide()) {
			HRESULT result2 = BaseDirect3DDevice9::DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			if (result != result2)
				OutputDebugString("moop\n");
		}
	}

	return result;
}

/**
* Applies all dirty shader registers, draws both stereo sides if switchDrawingSide() agrees.
* @see switchDrawingSide()
***/
HRESULT WINAPI D3DProxyDevice::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType,UINT PrimitiveCount,CONST void* pVertexStreamZeroData,UINT VertexStreamZeroStride)
{
	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride))) {
		if (switchDrawingSide())
			BaseDirect3DDevice9::DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
	}

	return result;
}

/**
* Applies all dirty shader registers, draws both stereo sides if switchDrawingSide() agrees.
* @see switchDrawingSide()
***/
HRESULT WINAPI D3DProxyDevice::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType,UINT MinVertexIndex,UINT NumVertices,UINT PrimitiveCount,CONST void* pIndexData,D3DFORMAT IndexDataFormat,CONST void* pVertexStreamZeroData,UINT VertexStreamZeroStride)
{
	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride))) {
		if (switchDrawingSide())
			BaseDirect3DDevice9::DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
	}

	return result;
}

/**
* Applies all dirty shader registers, processes vertices.
***/
HRESULT WINAPI D3DProxyDevice::ProcessVertices(UINT SrcStartIndex,UINT DestIndex,UINT VertexCount,IDirect3DVertexBuffer9* pDestBuffer,IDirect3DVertexDeclaration9* pVertexDecl,DWORD Flags)
{
	if (!pDestBuffer)
		return D3DERR_INVALIDCALL;

	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	BaseDirect3DVertexBuffer9* pCastDestBuffer = static_cast<BaseDirect3DVertexBuffer9*>(pDestBuffer);
	BaseDirect3DVertexDeclaration9* pCastVertexDeclaration = NULL;

	HRESULT result;
	if (pVertexDecl) {
		pCastVertexDeclaration = static_cast<BaseDirect3DVertexDeclaration9*>(pVertexDecl);
		result = BaseDirect3DDevice9::ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pCastDestBuffer->getActual(), pCastVertexDeclaration->getActual(), Flags);
	}
	else {
		result = BaseDirect3DDevice9::ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pCastDestBuffer->getActual(), NULL, Flags);
	}

	return result;
}

/**
* Creates base vertex declaration (BaseDirect3DVertexDeclaration9).
***/
HRESULT WINAPI D3DProxyDevice::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements,IDirect3DVertexDeclaration9** ppDecl)
{
	IDirect3DVertexDeclaration9* pActualVertexDeclaration = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateVertexDeclaration(pVertexElements, &pActualVertexDeclaration );

	if (SUCCEEDED(creationResult))
		*ppDecl = new BaseDirect3DVertexDeclaration9(pActualVertexDeclaration, this);

	return creationResult;
}

/**
* Catches vertex declaration in stored proxy state block.
* First, set vertex declaration by base function.
* @see D3D9ProxyStateBlock
***/
HRESULT WINAPI D3DProxyDevice::SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl)
{
	BaseDirect3DVertexDeclaration9* pWrappedVDeclarationData = static_cast<BaseDirect3DVertexDeclaration9*>(pDecl);

	// Update actual Vertex Declaration
	HRESULT result;
	if (pWrappedVDeclarationData)
		result = BaseDirect3DDevice9::SetVertexDeclaration(pWrappedVDeclarationData->getActual());
	else
		result = BaseDirect3DDevice9::SetVertexDeclaration(NULL);

	// Update stored proxy Vertex Declaration
	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(pWrappedVDeclarationData);
		}
		else {

			if (m_pActiveVertexDeclaration) {
				m_pActiveVertexDeclaration->Release();
			}

			m_pActiveVertexDeclaration = pWrappedVDeclarationData;
			if (m_pActiveVertexDeclaration) {
				m_pActiveVertexDeclaration->AddRef();
			}
		}
	}

	return result;
}

/**
* Provides currently stored vertex declaration.
***/
HRESULT WINAPI D3DProxyDevice::GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl)
{
	if (!m_pActiveVertexDeclaration) 
		return D3DERR_INVALIDCALL; // TODO check this is the response if no declaration set

	*ppDecl = m_pActiveVertexDeclaration;

	return D3D_OK;
}

/**
* Creates proxy (wrapped) vertex shader.
* @param ppShader [in, out] The created proxy vertex shader.
* @see D3D9ProxyVertexShader
***/
HRESULT WINAPI D3DProxyDevice::CreateVertexShader(CONST DWORD* pFunction,IDirect3DVertexShader9** ppShader)
{
	IDirect3DVertexShader9* pActualVShader = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateVertexShader(pFunction, &pActualVShader);

	if (SUCCEEDED(creationResult)) {
		*ppShader = new D3D9ProxyVertexShader(pActualVShader, this, m_pGameHandler->GetShaderModificationRepository());
	}

	return creationResult;
}

/**
* Sets and updates stored proxy vertex shader.
* @see D3D9ProxyVertexShader
***/
HRESULT WINAPI D3DProxyDevice::SetVertexShader(IDirect3DVertexShader9* pShader)
{
	D3D9ProxyVertexShader* pWrappedVShaderData = static_cast<D3D9ProxyVertexShader*>(pShader);

	// Update actual Vertex shader
	HRESULT result;
	if (pWrappedVShaderData)
		result = BaseDirect3DDevice9::SetVertexShader(pWrappedVShaderData->getActual());
	else
		result = BaseDirect3DDevice9::SetVertexShader(NULL);

	// Update stored proxy Vertex shader
	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(pWrappedVShaderData);
		}
		else {
			if (m_pActiveVertexShader) {
				m_pActiveVertexShader->Release();
			}

			m_pActiveVertexShader = pWrappedVShaderData;
			if (m_pActiveVertexShader) {
				m_pActiveVertexShader->AddRef();
			}

			m_spManagedShaderRegisters->ActiveVertexShaderChanged(m_pActiveVertexShader);
		}
	}


	return result;
}

/**
* Returns the stored proxy vertex shader.
***/
HRESULT WINAPI D3DProxyDevice::GetVertexShader(IDirect3DVertexShader9** ppShader)
{
	if (!m_pActiveVertexShader)
		return D3DERR_INVALIDCALL;

	*ppShader = m_pActiveVertexShader;

	return D3D_OK;
}

/**
* Sets shader constants either at stored proxy state block or in managed shader register class.
* @see D3D9ProxyStateBlock
* @see ShaderRegisters
***/
HRESULT WINAPI D3DProxyDevice::SetVertexShaderConstantF(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount)
{
	HRESULT result = D3DERR_INVALIDCALL;

	if (m_pCapturingStateTo) {
		result = m_pCapturingStateTo->SelectAndCaptureStateVSConst(StartRegister, pConstantData, Vector4fCount);
	}
	else { 
		result = m_spManagedShaderRegisters->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
	}

	return result;
}

/**
* Provides constant registers from managed shader register class.
* @see ShaderRegisters
* @see ShaderRegisters::GetVertexShaderConstantF()
***/
HRESULT WINAPI D3DProxyDevice::GetVertexShaderConstantF(UINT StartRegister,float* pData,UINT Vector4fCount)
{
	return m_spManagedShaderRegisters->GetVertexShaderConstantF(StartRegister, pData, Vector4fCount);
}

/**
* Sets stream source and updates stored vertex buffers.
* Also, it calls proxy state block to capture states.
* @see D3D9ProxyStateBlock::SelectAndCaptureState()
***/
HRESULT WINAPI D3DProxyDevice::SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride)
{	
	BaseDirect3DVertexBuffer9* pCastStreamData = static_cast<BaseDirect3DVertexBuffer9*>(pStreamData);
	HRESULT result;
	if (pStreamData) {		
		result = BaseDirect3DDevice9::SetStreamSource(StreamNumber, pCastStreamData->getActual(), OffsetInBytes, Stride);
	}
	else {
		result = BaseDirect3DDevice9::SetStreamSource(StreamNumber, NULL, OffsetInBytes, Stride);
	}


	// Update m_activeVertexBuffers if new vertex buffer was successfully set
	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(StreamNumber, pCastStreamData);
		}
		else {
			// remove existing vertex buffer that was active at StreamNumber if there is one
			if (m_activeVertexBuffers.count(StreamNumber) == 1) { 

				IDirect3DVertexBuffer9* pOldBuffer = m_activeVertexBuffers.at(StreamNumber);
				if (pOldBuffer == pStreamData)
					return result;

				if (pOldBuffer)
					pOldBuffer->Release();

				m_activeVertexBuffers.erase(StreamNumber);
			}

			// insert new vertex buffer
			if(m_activeVertexBuffers.insert(std::pair<UINT, BaseDirect3DVertexBuffer9*>(StreamNumber, pCastStreamData)).second) {
				//success
				if (pStreamData)
					pStreamData->AddRef();
			}
			else {
				OutputDebugString(__FUNCTION__);
				OutputDebugString("\n");
				OutputDebugString("Unable to store active Texture Stage.\n");
				assert(false);

				//If we get here the state of the texture tracking is fubared and an implosion is imminent.

				result = D3DERR_INVALIDCALL;
			}
		}
	}

	return result;
}

/**
* Provides stream data from stored vertex buffers.
* TODO ppStreamData is marked in and out in docs. Potentially it can be a get when the stream hasn't been set before???
* Category of probleme: Worry about it if it breaks.
***/
HRESULT WINAPI D3DProxyDevice::GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData,UINT* pOffsetInBytes,UINT* pStride)
{
	// This whole methods implementation is highly questionable. Not sure exactly how GetStreamSource works
	HRESULT result = D3DERR_INVALIDCALL;

	if (m_activeVertexBuffers.count(StreamNumber) == 1) {

		//IDirect3DVertexBuffer9* pCurrentActual = m_activeVertexBuffers[StreamNumber]->getActual();

		//IDirect3DVertexBuffer9* pActualResultBuffer = NULL;
		//HRESULT result = BaseDirect3DDevice9::GetStreamSource(StreamNumber, &pCurrentActual, pOffsetInBytes, pStride);

		*ppStreamData = m_activeVertexBuffers[StreamNumber];
		if ((*ppStreamData))
			(*ppStreamData)->AddRef();

		result = D3D_OK;
	}
	return result;
}

/**
* Sets indices and calls proxy state block to capture states.
* @see D3D9ProxyStateBlock::SelectAndCaptureState()
***/
HRESULT WINAPI D3DProxyDevice::SetIndices(IDirect3DIndexBuffer9* pIndexData)
{
	BaseDirect3DIndexBuffer9* pWrappedNewIndexData = static_cast<BaseDirect3DIndexBuffer9*>(pIndexData);

	// Update actual index buffer
	HRESULT result;
	if (pWrappedNewIndexData)
		result = BaseDirect3DDevice9::SetIndices(pWrappedNewIndexData->getActual());
	else
		result = BaseDirect3DDevice9::SetIndices(NULL);

	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(pWrappedNewIndexData);
		}
		else {
			// Update stored proxy index buffer
			if (m_pActiveIndicies) {
				m_pActiveIndicies->Release();
			}

			m_pActiveIndicies = pWrappedNewIndexData;
			if (m_pActiveIndicies) {
				m_pActiveIndicies->AddRef();
			}
		}
	}

	return result;
}

/**
* Provides stored indices.
***/
HRESULT WINAPI D3DProxyDevice::GetIndices(IDirect3DIndexBuffer9** ppIndexData)
{
	if (!m_pActiveIndicies)
		return D3DERR_INVALIDCALL;

	*ppIndexData = m_pActiveIndicies;
	m_pActiveIndicies->AddRef();

	return D3D_OK;
}

/**
* Base CreatePixelShader functionality.
***/
HRESULT WINAPI D3DProxyDevice::CreatePixelShader(CONST DWORD* pFunction,IDirect3DPixelShader9** ppShader)
{
	IDirect3DPixelShader9* pActualPShader = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreatePixelShader(pFunction, &pActualPShader);

	if (SUCCEEDED(creationResult)) {
		*ppShader = new D3D9ProxyPixelShader(pActualPShader, this, m_pGameHandler->GetShaderModificationRepository());
	}

	return creationResult;
}

/**
* Sets pixel shader and calls proxy state block to capture states.
* @see D3D9ProxyStateBlock::SelectAndCaptureState()
***/
HRESULT WINAPI D3DProxyDevice::SetPixelShader(IDirect3DPixelShader9* pShader)
{
	D3D9ProxyPixelShader* pWrappedPShaderData = static_cast<D3D9ProxyPixelShader*>(pShader);

	// Update actual pixel shader
	HRESULT result;
	if (pWrappedPShaderData)
		result = BaseDirect3DDevice9::SetPixelShader(pWrappedPShaderData->getActual());
	else
		result = BaseDirect3DDevice9::SetPixelShader(NULL);

	// Update stored proxy pixel shader
	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(pWrappedPShaderData);
		}
		else {

			if (m_pActivePixelShader) {
				m_pActivePixelShader->Release();
			}

			m_pActivePixelShader = pWrappedPShaderData;
			if (m_pActivePixelShader) {
				m_pActivePixelShader->AddRef();
			}

			m_spManagedShaderRegisters->ActivePixelShaderChanged(m_pActivePixelShader);
		}
	}

	return result;
}

/**
* Provides stored pixel shader.
***/
HRESULT WINAPI D3DProxyDevice::GetPixelShader(IDirect3DPixelShader9** ppShader)
{
	if (!m_pActivePixelShader)
		return D3DERR_INVALIDCALL;

	*ppShader = m_pActivePixelShader;

	return D3D_OK;
}

/**
* Sets shader constants either at stored proxy state block or in managed shader register class.
* @see D3D9ProxyStateBlock
* @see ShaderRegisters
***/
HRESULT WINAPI D3DProxyDevice::SetPixelShaderConstantF(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount)
{
	HRESULT result = D3DERR_INVALIDCALL;

	if (m_pCapturingStateTo) {
		result = m_pCapturingStateTo->SelectAndCaptureStatePSConst(StartRegister, pConstantData, Vector4fCount);
	}
	else { 
		result = m_spManagedShaderRegisters->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
	}

	return result;
}

/**
* Provides constant registers from managed shader register class.
* @see ShaderRegisters
* @see ShaderRegisters::GetPixelShaderConstantF()
***/
HRESULT WINAPI D3DProxyDevice::GetPixelShaderConstantF(UINT StartRegister,float* pData,UINT Vector4fCount)
{
	return m_spManagedShaderRegisters->GetPixelShaderConstantF(StartRegister, pData, Vector4fCount);
}

/**
* Applies all dirty registers, draws both stereo sides if switchDrawingSide() agrees.
* @see switchDrawingSide()
***/
HRESULT WINAPI D3DProxyDevice::DrawRectPatch(UINT Handle,CONST float* pNumSegs,CONST D3DRECTPATCH_INFO* pRectPatchInfo)
{
	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::DrawRectPatch(Handle, pNumSegs, pRectPatchInfo))) {
		if (switchDrawingSide())
			BaseDirect3DDevice9::DrawRectPatch(Handle, pNumSegs, pRectPatchInfo);
	}

	return result;
}

/**
* Applies all dirty registers, draws both stereo sides if switchDrawingSide() agrees.
* @see switchDrawingSide() 
***/
HRESULT WINAPI D3DProxyDevice::DrawTriPatch(UINT Handle,CONST float* pNumSegs,CONST D3DTRIPATCH_INFO* pTriPatchInfo)
{
	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::DrawTriPatch(Handle, pNumSegs, pTriPatchInfo))) {
		if (switchDrawingSide())
			BaseDirect3DDevice9::DrawTriPatch(Handle, pNumSegs, pTriPatchInfo);
	}

	return result;
}

/**
* Base CreateQuery functionality.
***/
HRESULT WINAPI D3DProxyDevice::CreateQuery(D3DQUERYTYPE Type,IDirect3DQuery9** ppQuery)
{
	// this seems a weird response to me but it's what the actual device does.
	if (!ppQuery)
		return D3D_OK;

	IDirect3DQuery9* pActualQuery = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateQuery(Type, &pActualQuery);

	if (SUCCEEDED(creationResult)) {
		*ppQuery = new BaseDirect3DQuery9(pActualQuery, this);
	}

	return creationResult;
}

/**
* Creates proxy (wrapped) render target, if swapchain buffer returns StereoBackBuffer, otherwise D3D9ProxySurface.
* Duplicates render target if game handler agrees.
* @see GameHandler::ShouldDuplicateRenderTarget()
* @see StereoBackBuffer
* @see D3D9ProxySurface
***/
HRESULT WINAPI D3DProxyDevice::CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
												  DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle, bool isSwapChainBackBuffer)
{

	IDirect3DSurface9* pLeftRenderTarget = NULL;
	IDirect3DSurface9* pRightRenderTarget = NULL;
	HRESULT creationResult;

	// create left/mono
	if (SUCCEEDED(creationResult = BaseDirect3DDevice9::CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, &pLeftRenderTarget, pSharedHandle))) {

		/* "If Needed" heuristic is the complicated part here.
		Fixed heuristics (based on type, format, size, etc) + game specific overrides + isForcedMono + magic? */
		// TODO Should we duplicate this Render Target? Replace "true" with heuristic
		if (m_pGameHandler->ShouldDuplicateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, isSwapChainBackBuffer))
		{
			if (FAILED(BaseDirect3DDevice9::CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, &pRightRenderTarget, pSharedHandle))) {
				OutputDebugString("Failed to create right eye render target while attempting to create stereo pair, falling back to mono\n");
				pRightRenderTarget = NULL;
			}
		}
	}
	else {
		OutputDebugString("Failed to create render target\n"); 
	}

	if (SUCCEEDED(creationResult)) {
		if (!isSwapChainBackBuffer)
			*ppSurface = new D3D9ProxySurface(pLeftRenderTarget, pRightRenderTarget, this, NULL);
		else
			*ppSurface = new StereoBackBuffer(pLeftRenderTarget, pRightRenderTarget, this);
	}

	return creationResult;
}

/**
* This method must be called on the proxy device before the device is returned to the calling application.
* Inits by game configuration.
* Subclasses which override this method must call through to super method.
* Anything that needs to be done before the device is used by the actual application should happen here.
* @param The game (or engine) specific configuration.
***/
void D3DProxyDevice::Init(ProxyHelper::ProxyConfig& cfg)
{
	OutputDebugString("D3D ProxyDev Init\n");

	config = cfg;

	eyeShutter = 1;
	trackerInitialized = false;

	char buf[64];
	LPCSTR psz = NULL;
	sprintf_s(buf, "type: %d, aspect: %f\n", config.game_type, config.aspect_multiplier);
	psz = buf;
	OutputDebugString(psz);

	m_spShaderViewAdjustment->Load(config);
	m_pGameHandler->Load(config, m_spShaderViewAdjustment);
	stereoView = StereoViewFactory::Get(config, m_spShaderViewAdjustment->HMDInfo());

	// HUD
	for (int i = 0; i < 4; i++)
	{
		hud3DDepthPresets[i] = config.hud3DDepthPresets[i];
		hudDistancePresets[i] = config.hudDistancePresets[i];
		hudHotkeys[i] = config.hudHotkeys[i];
	}
	hudHotkeys[4] = config.hudHotkeys[4];
	ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)config.hud3DDepthMode);

	// GUI
	for (int i = 0; i < 4; i++)
	{
		gui3DDepthPresets[i] = config.gui3DDepthPresets[i];
		guiSquishPresets[i] = config.guiSquishPresets[i];
		guiHotkeys[i] = config.guiHotkeys[i];
	}
	guiHotkeys[4] = config.guiHotkeys[4];
	ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)config.gui3DDepthMode);

	OnCreateOrRestore();
}

/**
* Creates HUD according to viewport height.
***/
void D3DProxyDevice::SetupHUD()
{
	D3DXCreateFont( this, 32, 0, FW_BOLD, 4, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial", &hudFont );
	D3DXCreateSprite(this, &hudMainMenu);
	D3DXCreateSprite(this, &hudTextBox);
}

/**
* Keyboard input handling, BRASSA called here.
***/
void D3DProxyDevice::HandleControls()
{
	// loop through hotkeys
	bool hotkeyPressed = false;
	for (int i = 0; i < 5; i++)
	{
		if ((KEY_DOWN(hudHotkeys[i])) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (i==0)
			{
				HUD_3D_Depth_Modes newMode=(HUD_3D_Depth_Modes)(hud3DDepthMode+1);
				if (newMode>=HUD_3D_Depth_Modes::HUD_ENUM_RANGE)
					newMode=HUD_3D_Depth_Modes::HUD_DEFAULT;
				{
					oldHudMode = hud3DDepthMode;
					ChangeHUD3DDepthMode(newMode);

				}
			}
			else
			{
				if (hud3DDepthMode==(HUD_3D_Depth_Modes)(i-1))
				{
					if (KEY_DOWN(VK_RCONTROL))
					{
						oldHudMode = hud3DDepthMode;
						ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)(i-1));
					}
					else
					{
						ChangeHUD3DDepthMode(oldHudMode);
					}

				}
				else
				{
					oldHudMode = hud3DDepthMode;
					ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)(i-1));
				}
			}
			hotkeyPressed = true;
		}
		if ((KEY_DOWN(guiHotkeys[i])) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (i==0)
			{
				GUI_3D_Depth_Modes newMode=(GUI_3D_Depth_Modes)(gui3DDepthMode+1);
				if (newMode>=GUI_3D_Depth_Modes::GUI_ENUM_RANGE)
					newMode=GUI_3D_Depth_Modes::GUI_DEFAULT;
				{
					oldGuiMode = gui3DDepthMode;
					ChangeGUI3DDepthMode(newMode);
				}
			}
			else
			{
				if (gui3DDepthMode==(GUI_3D_Depth_Modes)(i-1))
				{
					if (KEY_DOWN(VK_RCONTROL))
					{
						oldGuiMode = gui3DDepthMode;
						ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)(i-1));
					}
					else
					{
						ChangeGUI3DDepthMode(oldGuiMode);
					}

				}
				else
				{
					oldGuiMode = gui3DDepthMode;
					ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)(i-1));
				}
			}
			hotkeyPressed=true;
		}
	}
	// avoid double input by using the menu velocity
	if (hotkeyPressed)
		menuVelocity.x+=10.0f;

	// open BRASSA - <CTRL>+<T>
	if(KEY_DOWN(0x54) && KEY_DOWN(VK_CONTROL) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
	{
		if (BRASSA_mode == BRASSA_Modes::INACTIVE)
		{
			borderTopHeight = 0.0f;
			BRASSA_mode = BRASSA_Modes::MAINMENU;
		}
		else
		{
			BRASSA_mode = BRASSA_Modes::INACTIVE;
			ProxyHelper* helper = new ProxyHelper();
			config.roll_multiplier = tracker->multiplierRoll;
			config.yaw_multiplier = tracker->multiplierYaw;
			config.pitch_multiplier = tracker->multiplierPitch;
			config.swap_eyes = stereoView->swapEyes;
			m_spShaderViewAdjustment->Save(config);
			helper->SaveConfig(config);
		}

		menuVelocity.x+=10.0f;
	}

	// open BRASSA - <SHIFT>+<*>
	if(KEY_DOWN(VK_MULTIPLY) && KEY_DOWN(VK_SHIFT) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))		
	{
		if (BRASSA_mode == BRASSA_Modes::INACTIVE)
		{
			borderTopHeight = 0.0f;
			BRASSA_mode = BRASSA_Modes::MAINMENU;
		}
		else
		{
			BRASSA_mode = BRASSA_Modes::INACTIVE;
			ProxyHelper* helper = new ProxyHelper();
			config.roll_multiplier = tracker->multiplierRoll;
			config.yaw_multiplier = tracker->multiplierYaw;
			config.pitch_multiplier = tracker->multiplierPitch;
			config.swap_eyes = stereoView->swapEyes;
			m_spShaderViewAdjustment->Save(config);
			helper->SaveConfig(config);
		}

		menuVelocity.x+=10.0f;
	}
}

/**
* Updates selected motion tracker orientation.
***/
void D3DProxyDevice::HandleTracking()
{
	if(!trackingOn){
		tracker->currentRoll = 0;
		return;
	}
	if(!trackerInitialized)
	{
		OutputDebugString("Try to init Tracker\n");
		tracker = MotionTrackerFactory::Get(config);
		tracker->setMultipliers(config.yaw_multiplier, config.pitch_multiplier, config.roll_multiplier); 
		trackerInitialized = true;
	}

	if(trackerInitialized && tracker->isAvailable())
	{
		tracker->updateOrientation();
	}
}

/**
* Creates or restores class setup.
* Subclasses which override this method must call through to super method.
* Do not directly call this method in subclasses.
* This method should be used to re/create any resources that are held by the device proxy and deleted by Reset.
* 
* The only resources used like this are going to be extra resources that are used by the proxy and are not
* part of the actual calling application. 
* 
* Examples in D3DProxyDevice: The Font used in the BRASSA overlay and the stereo buffer.
* 
* Example of something you wouldn't create here:
* Render targets in the m_activeRenderTargets collection. They need to be released to successfully Reset
* the device, but they just wrap IDirect3DSurface9 objects from the underlying application and will be
* re/created by the underlying application.
* 
* This method will be called when the proxy device is initialised with Init (happens before device is
* passed back to actual application by CreateDevice) and after a successful device Reset.
***/
void D3DProxyDevice::OnCreateOrRestore()
{	
	m_currentRenderingSide = vireio::Left;
	m_pCurrentMatViewTransform = &m_spShaderViewAdjustment->LeftAdjustmentMatrix();
	m_pCurrentView = &m_leftView;
	m_pCurrentProjection = &m_leftProjection;

	// Wrap the swap chain
	IDirect3DSwapChain9* pActualPrimarySwapChain;
	if (FAILED(BaseDirect3DDevice9::GetSwapChain(0, &pActualPrimarySwapChain))) {
		OutputDebugString("Failed to fetch swapchain.\n");
		exit(1); 
	}

	assert (m_activeSwapChains.size() == 0);
	m_activeSwapChains.push_back(new D3D9ProxySwapChain(pActualPrimarySwapChain, this, false));
	assert (m_activeSwapChains.size() == 1);

	// Set the primary rendertarget to the first stereo backbuffer
	IDirect3DSurface9* pWrappedBackBuffer;
	m_activeSwapChains[0]->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pWrappedBackBuffer);
	SetRenderTarget(0, pWrappedBackBuffer);
	pWrappedBackBuffer->Release();
	pWrappedBackBuffer = NULL;


	BaseDirect3DDevice9::GetViewport(&m_LastViewportSet);

	// If there is an initial depth stencil
	IDirect3DSurface9* pDepthStencil;
	if (SUCCEEDED(BaseDirect3DDevice9::GetDepthStencilSurface(&pDepthStencil))) { 

		D3DSURFACE_DESC stencilDesc;
		pDepthStencil->GetDesc(&stencilDesc);
		pDepthStencil->Release();

		IDirect3DSurface9* pTemp = NULL;
		CreateDepthStencilSurface(stencilDesc.Width, stencilDesc.Height, stencilDesc.Format, stencilDesc.MultiSampleType, stencilDesc.MultiSampleQuality, false, &pTemp, NULL);
		SetDepthStencilSurface(pTemp);
		pTemp->Release();	
	}


	SetupHUD();

	stereoView->Init(getActual());

	m_spShaderViewAdjustment->UpdateProjectionMatrices((float)stereoView->viewport.Width/(float)stereoView->viewport.Height);
	m_spShaderViewAdjustment->ComputeViewTransforms();
}

/**
* Switches rendering to which ever side is specified by side.
* Use to specify the side that you want to draw to.
* Overriding classes should call the base implementation first and then makes any extra needed changes
* based on the result of the base implementation (if the base class doesn't change side then derived shouldn't 
* change either)
* 
* @return True if change succeeded, false if it fails. The switch will fail if you attempt to setDrawingSide(Right)
* when the current primary active render target (target 0  in m_activeRenderTargets) is not stereo.
* Attempting to switch to a side when that side is already the active side will return true without making any changes.
***/
bool D3DProxyDevice::setDrawingSide(vireio::RenderPosition side)
{
	// Already on the correct eye
	if (side == m_currentRenderingSide) {
		return true;
	}

	// should never try and render for the right eye if there is no render target for the main render targets right side
	if (!m_activeRenderTargets[0]->IsStereo() && (side == vireio::Right)) {
		return false;
	}

	// Everything hasn't changed yet but we set this first so we don't accidentally use the member instead of the local and break
	// things, as I have already managed twice.
	m_currentRenderingSide = side;

	// switch render targets to new side
	bool renderTargetChanged = false;
	HRESULT result;
	D3D9ProxySurface* pCurrentRT;
	for(std::vector<D3D9ProxySurface*>::size_type i = 0; i != m_activeRenderTargets.size(); i++) 
	{
		if ((pCurrentRT = m_activeRenderTargets[i]) != NULL) {

			if (side == vireio::Left) 
				result = BaseDirect3DDevice9::SetRenderTarget(i, pCurrentRT->getActualLeft()); 
			else 
				result = BaseDirect3DDevice9::SetRenderTarget(i, pCurrentRT->getActualRight());

			if (result != D3D_OK) {
				OutputDebugString("Error trying to set one of the Render Targets while switching between active eyes for drawing.\n");
			}
			else {
				renderTargetChanged = true;
			}
		}
	}

	// if a non-fullsurface viewport is active and a rendertarget changed we need to reapply the viewport
	if (renderTargetChanged && !m_bActiveViewportIsDefault) {
		BaseDirect3DDevice9::SetViewport(&m_LastViewportSet);
	}

	// switch depth stencil to new side
	if (m_pActiveStereoDepthStencil != NULL) { 
		if (side == vireio::Left) 
			result = BaseDirect3DDevice9::SetDepthStencilSurface(m_pActiveStereoDepthStencil->getActualLeft()); 
		else 
			result = BaseDirect3DDevice9::SetDepthStencilSurface(m_pActiveStereoDepthStencil->getActualRight());
	}

	// switch textures to new side
	IDirect3DBaseTexture9* pActualLeftTexture = NULL;
	IDirect3DBaseTexture9* pActualRightTexture = NULL;

	for(auto it = m_activeTextureStages.begin(); it != m_activeTextureStages.end(); ++it )
	{
		if (it->second) {
			pActualLeftTexture = NULL;
			pActualRightTexture = NULL;
			vireio::UnWrapTexture(it->second, &pActualLeftTexture, &pActualRightTexture);

			// if stereo texture
			if (pActualRightTexture != NULL) { 
				if (side == vireio::Left) 
					result = BaseDirect3DDevice9::SetTexture(it->first, pActualLeftTexture); 
				else 
					result = BaseDirect3DDevice9::SetTexture(it->first, pActualRightTexture);
			}
			// else the texture is mono and doesn't need changing. It will always be set initially and then won't need changing

			if (result != D3D_OK)
				OutputDebugString("Error trying to set one of the textures while switching between active eyes for drawing.\n");
		}
	}

	// update view transform for new side 
	if (m_bViewTransformSet) {

		if (side == vireio::Left) {
			m_pCurrentView = &m_leftView;
		}
		else {
			m_pCurrentView = &m_rightView;
		}

		BaseDirect3DDevice9::SetTransform(D3DTS_VIEW, m_pCurrentView);
	}

	// update projection transform for new side 
	if (m_bProjectionTransformSet) {

		if (side == vireio::Left) {
			m_pCurrentProjection = &m_leftProjection;
		}
		else {
			m_pCurrentProjection = &m_rightProjection;
		}

		BaseDirect3DDevice9::SetTransform(D3DTS_PROJECTION, m_pCurrentProjection);
	}

	// Updated computed view translation (used by several derived proxies - see: ComputeViewTranslation)
	if (side == vireio::Left) {
		m_pCurrentMatViewTransform = &m_spShaderViewAdjustment->LeftAdjustmentMatrix();
	}
	else {
		m_pCurrentMatViewTransform = &m_spShaderViewAdjustment->RightAdjustmentMatrix();
	}

	// Apply active stereo shader constants
	m_spManagedShaderRegisters->ApplyAllStereoConstants(side);

	return true;
}

/**
* Try and toggle to other drawing side. 
* @return False if changes fails due to the current render target being mono.
***/
bool D3DProxyDevice::switchDrawingSide()
{
	bool switched = false;

	if (m_currentRenderingSide == vireio::Left) {
		switched = setDrawingSide(vireio::Right);
	}
	else if (m_currentRenderingSide == vireio::Right) {
		switched = setDrawingSide(vireio::Left);
	}
	else {
		DebugBreak();
	}

	return switched;
}

/**
* Adds a default shader rule to the game configuration.
* @return True if rule was added, false if rule already present.
***/
bool D3DProxyDevice::addRule(std::string constantName, bool allowPartialNameMatch, UINT startRegIndex, D3DXPARAMETER_CLASS constantType, UINT operationToApply, bool transpose)
{
	return m_pGameHandler->AddRule(m_spShaderViewAdjustment, constantName, allowPartialNameMatch, startRegIndex, constantType, operationToApply, transpose);
}

/**
* Saves current game shader rules (and game configuration).
***/
void D3DProxyDevice::saveShaderRules()
{ 
	m_pGameHandler->Save(config, m_spShaderViewAdjustment);

	ProxyHelper* helper = new ProxyHelper();
	helper->SaveConfig(config);
}

/**
* Simple helper to clear a rectangle using the specified color.
* @param renderPosition Left or Right render target to be used.
* @param rect The rectangle in pixel space to be cleared.
* @param color The direct 3d color to be used.
***/
void D3DProxyDevice::ClearRect(vireio::RenderPosition renderPosition, D3DRECT rect, D3DCOLOR color)
{
	setDrawingSide(renderPosition);
	BaseDirect3DDevice9::Clear(1, &rect, D3DCLEAR_TARGET, color, 0, 0);
}

/**
* Simple helper to clear an empty rectangle or border using the specified color.
* @param renderPosition Left or Right render target to be used.
* @param rect The rectangle in pixel space to be cleared.
* @param color The direct 3d color to be used.
* @param bw The border width.
***/
void D3DProxyDevice::ClearEmptyRect(vireio::RenderPosition renderPosition, D3DRECT rect, D3DCOLOR color, int bw)
{
	// helper rectangle
	D3DRECT rect0 = D3DRECT(rect);

	setDrawingSide(renderPosition);

	rect0.y2 = rect.y1 + bw;
	BaseDirect3DDevice9::Clear(1, &rect0, D3DCLEAR_TARGET, color, 0, 0);

	rect0.y1 = rect.y2 - bw;
	rect0.y2 = rect.y2;
	BaseDirect3DDevice9::Clear(1, &rect0, D3DCLEAR_TARGET, color, 0, 0);

	rect0.y1 = rect.y1;
	rect0.x2 = rect.x1 + bw;
	BaseDirect3DDevice9::Clear(1, &rect0, D3DCLEAR_TARGET, color, 0, 0);

	rect0.x1 = rect.x2 - bw;
	rect0.x2 = rect.x2;
	BaseDirect3DDevice9::Clear(1, &rect0, D3DCLEAR_TARGET, color, 0, 0);
}

/**
* Draws a simple selection control.
* @param renderPosition Left or Right render target to be used.
* @param rect The rectangle in pixel space to be cleared.
* @param color The direct 3d color to be used.
* @param selectionIndex The index of the currently chosen selection.
* @param selectionRange The range of the selection.
***/
void D3DProxyDevice::DrawSelection(vireio::RenderPosition renderPosition, D3DRECT rect, D3DCOLOR color, int selectionIndex, int selectionRange)
{	
	// get width of each selection
	float selectionWidth = (rect.x2-rect.x1) / (float)selectionRange;

	// get secondary color
	D3DXCOLOR color2 = D3DXCOLOR(color);
	FLOAT red = color2.r;
	color2.r = color2.g * 0.7f;
	color2.g = red;

	for (int i = 0; i < selectionRange; i++)
	{
		rect.x2 = rect.x1+(int)selectionWidth;
		if (i==selectionIndex)
			ClearRect(renderPosition, rect, color);
		else
			ClearRect(renderPosition, rect, color2);
		rect.x1+=(int)selectionWidth;
	}
}

/**
* Draws a simple selection control.
* @param renderPosition Left or Right render target to be used.
* @param rect The rectangle in pixel space to be cleared.
* @param color The direct 3d color to be used.
* @param selectionIndex The index of the currently chosen selection.
* @param selectionRange The range of the selection.
***/
void D3DProxyDevice::DrawScrollbar(vireio::RenderPosition renderPosition, D3DRECT rect, D3DCOLOR color, float scroll, int scrollbarSize)
{	
	if (scroll<0.0f) scroll=0.0f;
	if (scroll>1.0f) scroll=1.0f;

	// get width of each selection
	int scrollHeight = rect.y2-rect.y1-scrollbarSize;
	scrollHeight = (int)(scrollHeight*scroll);

	// get secondary color
	D3DXCOLOR color2 = D3DXCOLOR(color);
	FLOAT red = color2.r;
	color2.r = color2.g * 0.7f;
	color2.g = red;

	ClearRect(renderPosition, rect, color2);
	rect.y1 += scrollHeight;
	rect.y2 = rect.y1+scrollbarSize;
	ClearRect(renderPosition, rect, color);
}

/**
* Draws a text with a dark shadow.
* @see DrawText()
***/
void D3DProxyDevice::DrawTextShadowed(ID3DXFont* font, LPD3DXSPRITE sprite, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format, D3DCOLOR color)
{
	lprc->left+=2; lprc->right+=2; lprc->top+=2; lprc->bottom+=2;
	font->DrawText(sprite, lpchText, -1, lprc, format, D3DCOLOR_ARGB(255, 64, 64, 64));
	lprc->left-=2; lprc->right-=2; lprc->top-=2; lprc->bottom-=2;
	font->DrawText(sprite, lpchText, -1, lprc, format, color);
}

/**
* Changes the HUD scale mode - also changes new scale in view adjustment class.
***/
void D3DProxyDevice::ChangeHUD3DDepthMode(HUD_3D_Depth_Modes newMode)
{
	if (newMode >= HUD_3D_Depth_Modes::HUD_ENUM_RANGE)
		return;

	hud3DDepthMode = newMode;

	m_spShaderViewAdjustment->ChangeHUDDistance(hudDistancePresets[(int)newMode]);
	m_spShaderViewAdjustment->ChangeHUD3DDepth(hud3DDepthPresets[(int)newMode]);
}

/**
* Changes the GUI scale mode - also changes new scale in view adjustment class.
***/
void D3DProxyDevice::ChangeGUI3DDepthMode(GUI_3D_Depth_Modes newMode)
{
	if (newMode >= GUI_3D_Depth_Modes::GUI_ENUM_RANGE)
		return;

	gui3DDepthMode = newMode;

	m_spShaderViewAdjustment->ChangeGUISquash(guiSquishPresets[(int)newMode]);
	m_spShaderViewAdjustment->ChangeGUI3DDepth(gui3DDepthPresets[(int)newMode]);


	if (newMode == GUI_3D_Depth_Modes::GUI_FULL)
		m_spShaderViewAdjustment->SetBulletLabyrinthMode(true);
	else
		m_spShaderViewAdjustment->SetBulletLabyrinthMode(false);
}

/**
* 
***/
void D3DProxyDevice::BRASSA()
{
	switch (BRASSA_mode)
	{
	case D3DProxyDevice::MAINMENU:
		BRASSA_MainMenu();
		break;
	case D3DProxyDevice::WORLD_SCALE_CALIBRATION:
		BRASSA_WorldScale();
		break;
	case D3DProxyDevice::CONVERGENCE_ADJUSTMENT:
		BRASSA_Convergence();
		break;
	case D3DProxyDevice::HUD_CALIBRATION:
		BRASSA_HUD();
		break;
	case D3DProxyDevice::GUI_CALIBRATION:
		BRASSA_GUI();
		break;
	case D3DProxyDevice::OVERALL_SETTINGS:
		BRASSA_Settings();
		break;
	}
}

/**
* BRASSA Main Menu method.
***/
void D3DProxyDevice::BRASSA_MainMenu()
{
	int width = stereoView->viewport.Width;
	int height = stereoView->viewport.Height;
	float menuTop = height*0.32f;
	float menuEntryHeight = height*0.037f;
	UINT menuEntryCount = 8;
	if ((config.game_type == 11) || (config.game_type == 12)) menuEntryCount++;

	RECT rect1;
	rect1.left = 0;
	rect1.right = 1920;
	rect1.top = 0;
	rect1.bottom = 1080;

	float fScaleX = ((float)stereoView->viewport.Width / (float)rect1.right);
	float fScaleY = ((float)stereoView->viewport.Height / (float)rect1.bottom);

	// handle border height
	if (borderTopHeight<menuTop)
	{
		borderTopHeight = menuTop;
		menuVelocity.y=0.0f;
	}
	if (borderTopHeight>(menuTop+(menuEntryHeight*(float)(menuEntryCount-1))))
	{
		borderTopHeight = menuTop+menuEntryHeight*(float)(menuEntryCount-1);
		menuVelocity.y=0.0f;
	}

	// get menu entry id
	float entry = (borderTopHeight-menuTop+(menuEntryHeight/3.0f))/menuEntryHeight;
	UINT entryID = (UINT)entry;
	if (entryID >= menuEntryCount)
		OutputDebugString("Error in BRASSA menu programming !");
	if ((config.game_type != 11) || (config.game_type != 12))
		entryID++;

	/**
	* ESCAPE : Set BRASSA inactive and save the configuration.
	***/
	if (KEY_DOWN(VK_ESCAPE))
	{
		BRASSA_mode = BRASSA_Modes::INACTIVE;
		ProxyHelper* helper = new ProxyHelper();
		config.roll_multiplier = tracker->multiplierRoll;
		config.yaw_multiplier = tracker->multiplierYaw;
		config.pitch_multiplier = tracker->multiplierPitch;
		config.swap_eyes = stereoView->swapEyes;
		m_spShaderViewAdjustment->Save(config);
		helper->SaveConfig(config);
	}

	if ((KEY_DOWN(VK_RETURN)) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
	{
		// world scale
		if (entryID == 1)
		{
			BRASSA_mode = BRASSA_Modes::WORLD_SCALE_CALIBRATION;
			menuVelocity.x+=10.0f;
		}
		// hud calibration
		if (entryID == 2)
		{
			BRASSA_mode = BRASSA_Modes::CONVERGENCE_ADJUSTMENT;
			menuVelocity.x+=10.0f;
		}
		// hud calibration
		if (entryID == 3)
		{
			BRASSA_mode = BRASSA_Modes::HUD_CALIBRATION;
			menuVelocity.x+=10.0f;
		}
		// gui calibration
		if (entryID == 4)
		{
			BRASSA_mode = BRASSA_Modes::GUI_CALIBRATION;
			menuVelocity.x+=10.0f;
		}
		// overall settings
		if (entryID == 7)
		{
			BRASSA_mode = BRASSA_Modes::OVERALL_SETTINGS;
			menuVelocity.x+=10.0f;
		}	
		// back to game
		if (entryID == 8)
		{
			BRASSA_mode = BRASSA_Modes::INACTIVE;
			ProxyHelper* helper = new ProxyHelper();
			config.roll_multiplier = tracker->multiplierRoll;
			config.yaw_multiplier = tracker->multiplierYaw;
			config.pitch_multiplier = tracker->multiplierPitch;
			config.swap_eyes = stereoView->swapEyes;
			m_spShaderViewAdjustment->Save(config);
			helper->SaveConfig(config);
		}
	}

	if (KEY_DOWN(VK_RIGHT) || KEY_DOWN(0x4A))
	{
		// change hud scale 
		if ((entryID == 5) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (hud3DDepthMode > HUD_3D_Depth_Modes::HUD_DEFAULT)
				ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)(hud3DDepthMode-1));
			menuVelocity.x-=10.0f;
		}

		// change gui scale
		if ((entryID == 6) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (gui3DDepthMode > GUI_3D_Depth_Modes::GUI_DEFAULT)
				ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)(gui3DDepthMode-1));
			menuVelocity.x-=10.0f;
		}
	}

	if (KEY_DOWN(VK_RIGHT) || KEY_DOWN(0x4C))
	{
		// change hud scale 
		if ((entryID == 5) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (hud3DDepthMode < HUD_3D_Depth_Modes::HUD_ENUM_RANGE-1)
				ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)(hud3DDepthMode+1));
			menuVelocity.x+=10.0f;
		}

		// change gui scale
		if ((entryID == 6) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (gui3DDepthMode < GUI_3D_Depth_Modes::GUI_ENUM_RANGE-1)
				ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)(gui3DDepthMode+1));
			menuVelocity.x+=10.0f;
		}

	}

	// output menu
	if (hudFont)
	{
		float LeftLensCenterAsPercentage = ((m_spShaderViewAdjustment->HMDInfo().physicalScreenSize.first / 2.0f) - 
			(config.ipd  / 2.0f)) / (m_spShaderViewAdjustment->HMDInfo().physicalScreenSize.first);

		// draw border - total width due to shift correction
		D3DRECT rect;
		rect.x1 = (int)0; rect.x2 = (int)width; rect.y1 = (int)borderTopHeight; rect.y2 = (int)(borderTopHeight+height*0.04f);
		ClearEmptyRect(vireio::RenderPosition::Left, rect, D3DCOLOR_ARGB(255,255,128,128), 2);
		ClearEmptyRect(vireio::RenderPosition::Right, rect, D3DCOLOR_ARGB(255,255,128,128), 2);

		hudMainMenu->Begin(D3DXSPRITE_ALPHABLEND);

		D3DXMATRIX matScale;
		D3DXMatrixScaling(&matScale, fScaleX, fScaleY, 1.0f);
		hudMainMenu->SetTransform(&matScale);

		rect1.left = 550;
		rect1.top = 300;
		DrawTextShadowed(hudFont, hudMainMenu, "Brown Reischl and Schneider Settings Analyzer (B.R.A.S.S.A.).\n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect.x1 = 0; rect.x2 = width; rect.y1 = (int)(335*fScaleY); rect.y2 = (int)(340*fScaleY);
		Clear(1, &rect, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255,255,128,128), 0, 0);

		rect1.top += 50;  rect1.left += 250;
		if ((config.game_type == 11) || (config.game_type == 12))
		{
			DrawTextShadowed(hudFont, hudMainMenu, "Activate BRASSA (Shader Analyzer)\n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
			rect1.top += 40;
		}
		DrawTextShadowed(hudFont, hudMainMenu, "World-Scale Calibration\n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Convergence Adjustment\n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "HUD Calibration\n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "GUI Calibration\n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40; float hudQSHeight = (float)rect1.top * fScaleY;
		DrawTextShadowed(hudFont, hudMainMenu, "HUD Quick Setting : \n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40; float guiQSHeight = (float)rect1.top * fScaleY;
		DrawTextShadowed(hudFont, hudMainMenu, "GUI Quick Setting : \n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Overall Settings\n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Back to Game\n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));

		// draw HUD quick setting rectangles
		rect.x1 = (int)(width*0.49f); rect.x2 = (int)(width*0.53f); rect.y1 = (int)hudQSHeight; rect.y2 = (int)(hudQSHeight+height*0.027f);
		DrawSelection(vireio::RenderPosition::Left, rect, D3DCOLOR_ARGB(255, 128, 196, 128), (int)hud3DDepthMode, (int)HUD_3D_Depth_Modes::HUD_ENUM_RANGE);
		rect.x1 += (int)((LeftLensCenterAsPercentage) * width * 0.5f);
		rect.x2 += (int)((LeftLensCenterAsPercentage) * width * 0.5f);
		DrawSelection(vireio::RenderPosition::Right, rect, D3DCOLOR_ARGB(255, 128, 196, 128), (int)hud3DDepthMode, (int)HUD_3D_Depth_Modes::HUD_ENUM_RANGE);

		// draw GUI quick setting rectangles
		rect.x1 = (int)(width*0.49f); rect.x2 = (int)(width*0.53f); rect.y1 = (int)guiQSHeight; rect.y2 = (int)(guiQSHeight+height*0.027f);
		DrawSelection(vireio::RenderPosition::Left, rect, D3DCOLOR_ARGB(255, 128, 196, 128), (int)gui3DDepthMode, (int)GUI_3D_Depth_Modes::GUI_ENUM_RANGE);
		rect.x1 += (int)((LeftLensCenterAsPercentage) * width * 0.5f);
		rect.x2 += (int)((LeftLensCenterAsPercentage) * width * 0.5f);
		DrawSelection(vireio::RenderPosition::Right, rect, D3DCOLOR_ARGB(255, 128, 196, 128), (int)gui3DDepthMode, (int)GUI_3D_Depth_Modes::GUI_ENUM_RANGE);

		rect1.left = 0;
		rect1.right = 1920;
		rect1.top = 0;
		rect1.bottom = 1080;
		D3DXVECTOR3 vPos( 0.0f, 0.0f, 0.0f);
		hudMainMenu->Draw(NULL, &rect1, NULL, &vPos, D3DCOLOR_ARGB(255, 255, 255, 255));
		hudMainMenu->End();
	}
}

/**
* BRASSA World Scale Calibration.
***/
void D3DProxyDevice::BRASSA_WorldScale()
{
	float seperationChange = 0.05f; 

	/**
	* ESCAPE : Set BRASSA inactive and save the configuration.
	***/
	if (KEY_DOWN(VK_ESCAPE))
	{
		BRASSA_mode = BRASSA_Modes::INACTIVE;
		ProxyHelper* helper = new ProxyHelper();
		config.roll_multiplier = tracker->multiplierRoll;
		config.yaw_multiplier = tracker->multiplierYaw;
		config.pitch_multiplier = tracker->multiplierPitch;
		config.swap_eyes = stereoView->swapEyes;
		m_spShaderViewAdjustment->Save(config);
		helper->SaveConfig(config);
	}

	/**
	* LEFT : Decrease world scale (hold CTRL to lower speed, SHIFT to speed up)
	***/
	if((KEY_DOWN(VK_LEFT) || KEY_DOWN(0x4A)) && (menuVelocity.x == 0.0f))
	{
		if(KEY_DOWN(VK_CONTROL)) {
			seperationChange /= 10.0f;
		}
		else if(KEY_DOWN(VK_SHIFT)) {
			seperationChange *= 10.0f;
		} 

		m_spShaderViewAdjustment->ChangeWorldScale(-seperationChange);
		m_spShaderViewAdjustment->UpdateProjectionMatrices((float)stereoView->viewport.Width/(float)stereoView->viewport.Height);

		menuVelocity.x+=2.0f;
	}

	/**
	* RIGHT : Increase world scale (hold CTRL to lower speed, SHIFT to speed up)
	***/
	if((KEY_DOWN(VK_RIGHT) || KEY_DOWN(0x4C)) && (menuVelocity.x == 0.0f))
	{
		if(KEY_DOWN(VK_CONTROL)) {
			seperationChange /= 10.0f;
		}
		else if(KEY_DOWN(VK_SHIFT))
		{
			seperationChange *= 10.0f;
		}

		m_spShaderViewAdjustment->ChangeWorldScale(seperationChange);
		m_spShaderViewAdjustment->UpdateProjectionMatrices((float)stereoView->viewport.Width/(float)stereoView->viewport.Height);

		menuVelocity.x+=2.0f;
	}

	// handle border height (=scrollbar scroll height)
	if (borderTopHeight<-64.0f)
		borderTopHeight = -64.0f;
	if (borderTopHeight>365.0f)
		borderTopHeight = 365.0f;

	if(hudFont){

		hudMainMenu->Begin(D3DXSPRITE_ALPHABLEND);

		// standard hud size, will be scaled later to actual viewport
		char vcString[1024];
		int width = 1920;
		int height = 1080;

		float fScaleX = ((float)stereoView->viewport.Width / (float)width);
		float fScaleY = ((float)stereoView->viewport.Height / (float)height);

		D3DXMATRIX matScale;
		D3DXMatrixScaling(&matScale, fScaleX, fScaleY, 1.0f);
		hudMainMenu->SetTransform(&matScale);

		// watch HMDInfo::LeftLensCenterAsPercentage() for this formular
		// TODO !! setup HMDInfo::physicalLensSeparation to match the configured IPD (is currently default
		// IPD = 0.064f)
		float LeftLensCenterAsPercentage = ((m_spShaderViewAdjustment->HMDInfo().physicalScreenSize.first / 2.0f) - 
			(/*m_spShaderViewAdjustment->HMDInfo().physicalLensSeparation*/config.ipd  / 2.0f)) / 
			(m_spShaderViewAdjustment->HMDInfo().physicalScreenSize.first);

		// should be the right formular, note to ADD lens center offset :
		// float ScreenCenterAsPercentage = LeftLensCenterAsPercentage + m_spShaderViewAdjustment->HMDInfo().lensXCenterOffset;
		// 0.2f = valuation
		float BlueLineCenterAsPercentage = LeftLensCenterAsPercentage + m_spShaderViewAdjustment->HMDInfo().lensXCenterOffset * 0.2f;

		float horWidth = 0.15f;
		int beg = (int)(stereoView->viewport.Width*(1.0f-horWidth)/2.0) + (int)(BlueLineCenterAsPercentage * stereoView->viewport.Width * 0.25f);
		int end = (int)(stereoView->viewport.Width*(0.5f+(horWidth/2.0f))) + (int)(BlueLineCenterAsPercentage * stereoView->viewport.Width * 0.25f);

		int hashTop = (int)(stereoView->viewport.Height  * 0.48f);
		int hashBottom = (int)(stereoView->viewport.Height  * 0.52f);

		RECT rec2 = {(int)(width*0.27f), (int)(height*0.8f),width,height};
		sprintf_s(vcString, 1024, "Brown Reischl and Schneider Settings Analyzer (B.R.A.S.S.A.).\n");
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec2, 0, D3DCOLOR_ARGB(255,255,255,255));

		// draw right line (using BaseDirect3DDevice9, since otherwise we have two lines)
		D3DRECT rec3 = {(int)(stereoView->viewport.Width/2 + (-BlueLineCenterAsPercentage * stereoView->viewport.Width * 0.25f))-1, 0,
			(int)(stereoView->viewport.Width/2 + (-BlueLineCenterAsPercentage * stereoView->viewport.Width * 0.25f))+1,stereoView->viewport.Height };
		if (!config.swap_eyes)
			ClearRect(vireio::RenderPosition::Right, rec3, D3DCOLOR_ARGB(255,0,0,255));
		else
			ClearRect(vireio::RenderPosition::Left, rec3, D3DCOLOR_ARGB(255,0,0,255));

		// draw left line (using BaseDirect3DDevice9, since otherwise we have two lines)
		D3DRECT rec4 = {(int)(stereoView->viewport.Width/2 + (BlueLineCenterAsPercentage * stereoView->viewport.Width * 0.25f))-1, 0,
			(int)(stereoView->viewport.Width/2 + (BlueLineCenterAsPercentage * stereoView->viewport.Width * 0.25f))+1,stereoView->viewport.Height };
		if (!config.swap_eyes)
			ClearRect(vireio::RenderPosition::Left, rec4, D3DCOLOR_ARGB(255,255,0,0));
		else
			ClearRect(vireio::RenderPosition::Right, rec4, D3DCOLOR_ARGB(255,255,0,0));

		// horizontal line
		D3DRECT rec5 = {beg, (stereoView->viewport.Height /2)-1, end, (stereoView->viewport.Height /2)+1 };
		if (!config.swap_eyes)
			ClearRect(vireio::RenderPosition::Left, rec5, D3DCOLOR_ARGB(255,0,0,255));
		else
			ClearRect(vireio::RenderPosition::Right, rec5, D3DCOLOR_ARGB(255,0,0,255));

		// hash lines
		int hashNum = 10;
		float hashSpace = horWidth*stereoView->viewport.Width / (float)hashNum;
		for(int i=0; i<=hashNum; i++) {
			D3DRECT rec5 = {beg+(int)(i*hashSpace)-1, hashTop, beg+(int)(i*hashSpace)+1, hashBottom};
			if (!config.swap_eyes)
				ClearRect(vireio::RenderPosition::Left, rec5, D3DCOLOR_ARGB(255,255,255,0));
			else
				ClearRect(vireio::RenderPosition::Right, rec5, D3DCOLOR_ARGB(255,255,255,0));
		}

		rec2.left = (int)(width*0.35f);
		rec2.top = (int)(height*0.83f);
		sprintf_s(vcString, 1024, "World-Scale Calibration");
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec2, 0, D3DCOLOR_ARGB(255,255,255,255));

		// output game untis
		// 1 Game Unit = X Feet
		// 1 Game Unit = X Meters
		RECT rec10 = {(int)(width*0.40f), (int)(height*0.57f),width,height};
		DrawTextShadowed(hudFont, hudMainMenu, "<- calibrate using Arrow Keys ->", -1, &rec10, 0, D3DCOLOR_ARGB(255,255,255,255));
		//Column 1:
		//1 Game Unit = X Meters
		//1 Game Unit = X Centimeters
		//1 Game Unit = X Feet
		//1 Game Unit = X Inches
		//Column 2:
		//1 Meter = X Game Units
		//1 Centimeter = X Game Units
		//1 Foot = X Game Units
		//1 Inch = X Game Units
		rec10.top = (int)(height*0.6f); rec10.left = (int)(width*0.28f);
		float meters = 1 / m_spShaderViewAdjustment->WorldScale();
		sprintf_s(vcString,"1 Game Unit = %g Meters", meters);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec10, 0, D3DCOLOR_ARGB(255,255,255,255));
		rec10.top+=35;
		float centimeters = meters * 100.0f;
		sprintf_s(vcString,"1 Game Unit = %g CM", centimeters);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec10, 0, D3DCOLOR_ARGB(255,255,255,255));
		rec10.top+=35;
		float feet = meters * 3.2808399f;
		sprintf_s(vcString,"1 Game Unit = %g Feet", feet);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec10, 0, D3DCOLOR_ARGB(255,255,255,255));
		rec10.top+=35;
		float inches = feet * 12.0f;
		sprintf_s(vcString,"1 Game Unit = %g In.", inches);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec10, 0, D3DCOLOR_ARGB(255,255,255,255));

		RECT rec11 = {(int)(width*0.52f), (int)(height*0.6f),width,height};
		sprintf_s(vcString,"1 Meter      = %g Game Units", m_spShaderViewAdjustment->WorldScale());
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec11, 0, D3DCOLOR_ARGB(255,255,255,255));
		rec11.top+=35;
		float gameUnitsToCentimeter =  m_spShaderViewAdjustment->WorldScale() / 100.0f;
		sprintf_s(vcString,"1 CM         = %g Game Units", gameUnitsToCentimeter);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec11, 0, D3DCOLOR_ARGB(255,255,255,255));
		rec11.top+=35;
		float gameUnitsToFoot = m_spShaderViewAdjustment->WorldScale() / 3.2808399f;
		sprintf_s(vcString,"1 Foot       = %g Game Units", gameUnitsToFoot);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec11, 0, D3DCOLOR_ARGB(255,255,255,255));
		rec11.top+=35;
		float gameUnitsToInches = m_spShaderViewAdjustment->WorldScale() / 39.3700787f;
		sprintf_s(vcString,"1 Inch       = %g Game Units", gameUnitsToInches);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec11, 0, D3DCOLOR_ARGB(255,255,255,255));

		RECT rect1;
		rect1.left = 0;
		rect1.right = 1920;
		rect1.top = 0;
		rect1.bottom = 1080;
		D3DXVECTOR3 vPos( 0.0f, 0.0f, 0.0f);
		hudMainMenu->Draw(NULL, &rect1, NULL, &vPos, D3DCOLOR_ARGB(255, 255, 255, 255));  
		hudMainMenu->End();

		// draw description text box
		hudTextBox->Begin(D3DXSPRITE_ALPHABLEND);
		hudTextBox->SetTransform(&matScale);
		RECT rec8 = {620, (int)(borderTopHeight), 1300, 400};
		sprintf_s(vcString, 1024,
			"In the right eye view, walk up as close as\n"
			"possible to a 90 degree vertical object and\n"
			"align the BLUE vertical line with its edge.\n"
			"Good examples include a wall corner, a table\n"
			"corner, a square post, etc.  While looking at\n"
			"the left image, adjust the World View setting\n"
			"until the same object's edge is on the fourth\n"
			"notch in the >Negative Parallax< section (to\n"
			"the right of the RED line).  If objects go \n"
			"beyond this point, reduce the World Scale \n"
			"further.  Try to keep the BLUE line aligned\n"
			"while changing the World Scale.  Adjust \n"
			"further for comfort and game unit accuracy.\n"
			);
		DrawTextShadowed(hudFont, hudTextBox, vcString, -1, &rec8, 0, D3DCOLOR_ARGB(255,255,255,255));
		hudTextBox->Draw(NULL, &rec8, NULL, &vPos, D3DCOLOR_ARGB(255, 255, 255, 255));

		// draw description box scroll bar
		float scroll = (429.0f-borderTopHeight-64.0f)/429.0f;
		D3DRECT rec9 = {(int)(1300*fScaleX), 0, (int)(1320*fScaleX), (int)(400*fScaleY)};
		rec9.x1 -= (int)(LeftLensCenterAsPercentage * width * 0.25f * fScaleX);
		rec9.x2 -= (int)(LeftLensCenterAsPercentage * width * 0.25f * fScaleX);
		DrawScrollbar(vireio::RenderPosition::Left, rec9, D3DCOLOR_ARGB(255, 128, 196, 128), scroll, (int)(20*fScaleY));
		rec9.x1 += (int)(LeftLensCenterAsPercentage * width * 0.5f * fScaleX);
		rec9.x2 += (int)(LeftLensCenterAsPercentage * width * 0.5f * fScaleX);
		DrawScrollbar(vireio::RenderPosition::Right, rec9, D3DCOLOR_ARGB(255, 128, 196, 128), scroll, (int)(20*fScaleY));

		hudTextBox->End();
	}
}

/**
* BRASSA Convergence Adjustment.
***/
void D3DProxyDevice::BRASSA_Convergence()
{
	float convergenceChange = 0.05f;

	/**
	* ESCAPE : Set BRASSA inactive and save the configuration.
	***/
	if (KEY_DOWN(VK_ESCAPE))
	{
		BRASSA_mode = BRASSA_Modes::INACTIVE;
		ProxyHelper* helper = new ProxyHelper();
		config.roll_multiplier = tracker->multiplierRoll;
		config.yaw_multiplier = tracker->multiplierYaw;
		config.pitch_multiplier = tracker->multiplierPitch;
		config.swap_eyes = stereoView->swapEyes;
		m_spShaderViewAdjustment->Save(config);
		helper->SaveConfig(config);
	}

	/**
	* LEFT : Decrease convergence (hold CTRL to lower speed, SHIFT to speed up)
	***/
	if((KEY_DOWN(VK_LEFT) || KEY_DOWN(0x4A)) && (menuVelocity.x == 0.0f))
	{
		if(KEY_DOWN(VK_CONTROL)) {
			convergenceChange /= 10.0f;
		}
		else if(KEY_DOWN(VK_SHIFT)) {
			convergenceChange *= 10.0f;
		} 

		m_spShaderViewAdjustment->ChangeConvergence(-convergenceChange);
		m_spShaderViewAdjustment->UpdateProjectionMatrices((float)stereoView->viewport.Width/(float)stereoView->viewport.Height);

		menuVelocity.x+=2.0f;
	}

	/**
	* RIGHT : Increase convergence (hold CTRL to lower speed, SHIFT to speed up)
	***/
	if((KEY_DOWN(VK_RIGHT) || KEY_DOWN(0x4C)) && (menuVelocity.x == 0.0f))
	{
		if(KEY_DOWN(VK_CONTROL)) {
			convergenceChange /= 10.0f;
		}
		else if(KEY_DOWN(VK_SHIFT))
		{
			convergenceChange *= 10.0f;
		}

		m_spShaderViewAdjustment->ChangeConvergence(convergenceChange);
		m_spShaderViewAdjustment->UpdateProjectionMatrices((float)stereoView->viewport.Width/(float)stereoView->viewport.Height);

		menuVelocity.x+=2.0f;
	}

	// handle border height (=scrollbar scroll height)
	if (borderTopHeight<-64.0f)
		borderTopHeight = -64.0f;
	if (borderTopHeight>365.0f)
		borderTopHeight = 365.0f;

	if(hudFont){

		hudMainMenu->Begin(D3DXSPRITE_ALPHABLEND);

		// standard hud size, will be scaled later to actual viewport
		char vcString[1024];
		int width = 1920;
		int height = 1080;

		float fScaleX = ((float)stereoView->viewport.Width / (float)width);
		float fScaleY = ((float)stereoView->viewport.Height / (float)height);

		D3DXMATRIX matScale;
		D3DXMatrixScaling(&matScale, fScaleX, fScaleY, 1.0f);
		hudMainMenu->SetTransform(&matScale);

		// watch HMDInfo::LeftLensCenterAsPercentage() for this formular
		// TODO !! setup HMDInfo::physicalLensSeparation to match the configured IPD (is currently default
		// IPD = 0.064f)
		float LeftLensCenterAsPercentage = ((m_spShaderViewAdjustment->HMDInfo().physicalScreenSize.first / 2.0f) - 
			(/*m_spShaderViewAdjustment->HMDInfo().physicalLensSeparation*/config.ipd  / 2.0f)) / 
			(m_spShaderViewAdjustment->HMDInfo().physicalScreenSize.first);

		// should be the right formular, note to ADD lens center offset :
		// float ScreenCenterAsPercentage = LeftLensCenterAsPercentage + m_spShaderViewAdjustment->HMDInfo().lensXCenterOffset;
		// 0.2f = valuation
		float BlueLineCenterAsPercentage = LeftLensCenterAsPercentage + m_spShaderViewAdjustment->HMDInfo().lensXCenterOffset * 0.2f;

		float horWidth = 0.15f;
		int beg = (int)(stereoView->viewport.Width*(1.0f-horWidth)/2.0) + (int)(BlueLineCenterAsPercentage * stereoView->viewport.Width * 0.25f);
		int end = (int)(stereoView->viewport.Width*(0.5f+(horWidth/2.0f))) + (int)(BlueLineCenterAsPercentage * stereoView->viewport.Width * 0.25f);

		int hashTop = (int)(stereoView->viewport.Height  * 0.48f);
		int hashBottom = (int)(stereoView->viewport.Height  * 0.52f);

		RECT rec2 = {(int)(width*0.27f), (int)(height*0.8f),width,height};
		sprintf_s(vcString, 1024, "Brown Reischl and Schneider Settings Analyzer (B.R.A.S.S.A.).\n");
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec2, 0, D3DCOLOR_ARGB(255,255,255,255));

		// draw right line (using BaseDirect3DDevice9, since otherwise we have two lines)
		D3DRECT rec3 = {(int)(stereoView->viewport.Width/2 + (-BlueLineCenterAsPercentage * stereoView->viewport.Width * 0.25f))-1, 0,
			(int)(stereoView->viewport.Width/2 + (-BlueLineCenterAsPercentage * stereoView->viewport.Width * 0.25f))+1,stereoView->viewport.Height };
		if (!config.swap_eyes)
			ClearRect(vireio::RenderPosition::Right, rec3, D3DCOLOR_ARGB(255,0,0,255));
		else
			ClearRect(vireio::RenderPosition::Left, rec3, D3DCOLOR_ARGB(255,0,0,255));

		// draw left line (using BaseDirect3DDevice9, since otherwise we have two lines)
		D3DRECT rec4 = {(int)(stereoView->viewport.Width/2 + (BlueLineCenterAsPercentage * stereoView->viewport.Width * 0.25f))-1, 0,
			(int)(stereoView->viewport.Width/2 + (BlueLineCenterAsPercentage * stereoView->viewport.Width * 0.25f))+1,stereoView->viewport.Height };
		if (!config.swap_eyes)
			ClearRect(vireio::RenderPosition::Left, rec4, D3DCOLOR_ARGB(255,0,0,255));
		else
			ClearRect(vireio::RenderPosition::Right, rec4, D3DCOLOR_ARGB(255,0,0,255));

		// horizontal line
		D3DRECT rec5 = {beg, (stereoView->viewport.Height /2)-1, end, (stereoView->viewport.Height /2)+1 };
		if (!config.swap_eyes)
			ClearRect(vireio::RenderPosition::Left, rec5, D3DCOLOR_ARGB(255,0,0,255));
		else
			ClearRect(vireio::RenderPosition::Right, rec5, D3DCOLOR_ARGB(255,0,0,255));

		// hash lines
		int hashNum = 10;
		float hashSpace = horWidth*stereoView->viewport.Width / (float)hashNum;
		for(int i=0; i<=hashNum; i++) {
			D3DRECT rec5 = {beg+(int)(i*hashSpace)-1, hashTop, beg+(int)(i*hashSpace)+1, hashBottom};
			if (!config.swap_eyes)
				ClearRect(vireio::RenderPosition::Left, rec5, D3DCOLOR_ARGB(255,255,255,0));
			else
				ClearRect(vireio::RenderPosition::Right, rec5, D3DCOLOR_ARGB(255,255,255,0));
		}

		rec2.left = (int)(width*0.35f);
		rec2.top = (int)(height*0.83f);
		sprintf_s(vcString, 1024, "Convergence Adjustment");
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec2, 0, D3DCOLOR_ARGB(255,255,255,255));

		// output convergence
		RECT rec10 = {(int)(width*0.40f), (int)(height*0.57f),width,height};
		DrawTextShadowed(hudFont, hudMainMenu, "<- calibrate using Arrow Keys ->", -1, &rec10, 0, D3DCOLOR_ARGB(255,255,255,255));
		// Convergence Screen = X Meters = X Feet
		rec10.top = (int)(height*0.6f); rec10.left = (int)(width*0.385f);
		float meters = m_spShaderViewAdjustment->Convergence();
		sprintf_s(vcString,"Convergence Screen = %g Meters", meters);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec10, 0, D3DCOLOR_ARGB(255,255,255,255));
		rec10.top+=35;
		float centimeters = meters * 100.0f;
		sprintf_s(vcString,"Convergence Screen = %g CM", centimeters);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec10, 0, D3DCOLOR_ARGB(255,255,255,255));
		rec10.top+=35;
		float feet = meters * 3.2808399f;
		sprintf_s(vcString,"Convergence Screen = %g Feet", feet);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec10, 0, D3DCOLOR_ARGB(255,255,255,255));
		rec10.top+=35;
		float inches = feet * 12.0f;
		sprintf_s(vcString,"Convergence Screen = %g Inches", inches);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rec10, 0, D3DCOLOR_ARGB(255,255,255,255));

		RECT rect1;
		rect1.left = 0;
		rect1.right = 1920;
		rect1.top = 0;
		rect1.bottom = 1080;
		D3DXVECTOR3 vPos( 0.0f, 0.0f, 0.0f);
		hudMainMenu->Draw(NULL, &rect1, NULL, &vPos, D3DCOLOR_ARGB(255, 255, 255, 255));  
		hudMainMenu->End();

		// draw description text box
		hudTextBox->Begin(D3DXSPRITE_ALPHABLEND);
		hudTextBox->SetTransform(&matScale);
		RECT rec8 = {620, (int)(borderTopHeight), 1300, 400};
		sprintf_s(vcString, 1024,
			"Note that the Convergence Screens distance\n"
			"is measured in physical meters and should\n"
			"only be adjusted to match Your personal\n"
			"depth cognition after You calibrated the\n"
			"World Scale accordingly.\n"
			"In the right eye view, walk up as close as\n"
			"possible to a 90 degree vertical object and\n"
			"align the BLUE vertical line with its edge.\n"
			"Good examples include a wall corner, a table\n"
			"corner, a square post, etc.\n"
			);
		DrawTextShadowed(hudFont, hudTextBox, vcString, -1, &rec8, 0, D3DCOLOR_ARGB(255,255,255,255));
		hudTextBox->Draw(NULL, &rec8, NULL, &vPos, D3DCOLOR_ARGB(255, 255, 255, 255));

		// draw description box scroll bar
		float scroll = (429.0f-borderTopHeight-64.0f)/429.0f;
		D3DRECT rec9 = {(int)(1300*fScaleX), 0, (int)(1320*fScaleX), (int)(400*fScaleY)};
		rec9.x1 -= (int)(LeftLensCenterAsPercentage * width * 0.25f * fScaleX);
		rec9.x2 -= (int)(LeftLensCenterAsPercentage * width * 0.25f * fScaleX);
		DrawScrollbar(vireio::RenderPosition::Left, rec9, D3DCOLOR_ARGB(255, 128, 196, 128), scroll, (int)(20*fScaleY));
		rec9.x1 += (int)(LeftLensCenterAsPercentage * width * 0.5f * fScaleX);
		rec9.x2 += (int)(LeftLensCenterAsPercentage * width * 0.5f * fScaleX);
		DrawScrollbar(vireio::RenderPosition::Right, rec9, D3DCOLOR_ARGB(255, 128, 196, 128), scroll, (int)(20*fScaleY));

		hudTextBox->End();
	}
}

/**
* BRASSA HUD Calibration.
***/
void D3DProxyDevice::BRASSA_HUD()
{
	int width = stereoView->viewport.Width;
	int height = stereoView->viewport.Height;
	float menuTop = height*0.32f;
	float menuEntryHeight = height*0.037f;
	UINT menuEntryCount = 12;

	RECT rect1;
	rect1.left = 0;
	rect1.right = 1920;
	rect1.top = 0;
	rect1.bottom = 1080;

	float fScaleX = ((float)stereoView->viewport.Width / (float)rect1.right);
	float fScaleY = ((float)stereoView->viewport.Height / (float)rect1.bottom);

	// handle border height
	if (borderTopHeight<menuTop)
	{
		borderTopHeight = menuTop;
		menuVelocity.y=0.0f;
	}
	if (borderTopHeight>(menuTop+(menuEntryHeight*(float)(menuEntryCount-1))))
	{
		borderTopHeight = menuTop+menuEntryHeight*(float)(menuEntryCount-1);
		menuVelocity.y=0.0f;
	}

	// get menu entry id
	float entry = (borderTopHeight-menuTop+(menuEntryHeight/3.0f))/menuEntryHeight;
	UINT entryID = (UINT)entry;
	if (entryID >= menuEntryCount)
		OutputDebugString("Error in BRASSA menu programming !");

	if ((hotkeyCatch) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
	{
		for (int i = 0; i < 256; i++)
			if (KEY_DOWN(i) && (keyNameList[i]!="-"))
			{
				hotkeyCatch = false;
				int index = entryID-3;
				if ((index >=0) && (index <=4))
					hudHotkeys[index] = i;
			}
	}
	else
	{
		if (KEY_DOWN(VK_ESCAPE))
			BRASSA_mode = BRASSA_Modes::INACTIVE;

		if ((KEY_DOWN(VK_RETURN)) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if ((entryID >= 3) && (entryID <= 7) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				hotkeyCatch = true;
				menuVelocity.x+=10;
			}
			// save
			if (entryID == 8)
			{
				ProxyHelper* helper = new ProxyHelper();
				config.hud3DDepthMode = (int)hud3DDepthMode;
				for (int i = 0; i < 4; i++)
				{
					config.hud3DDepthPresets[i] = hud3DDepthPresets[i];
					config.hudDistancePresets[i] = hudDistancePresets[i];
					config.hudHotkeys[i] = hudHotkeys[i];
				}
				config.hudHotkeys[4] = hudHotkeys[4];
				helper->SaveHUDConfig(config);
			}
			// reload
			if (entryID == 9)
			{
				ProxyHelper* helper = new ProxyHelper();
				helper->LoadHUDConfig(config);
				for (int i = 0; i < 4; i++)
				{
					hud3DDepthPresets[i] = config.hud3DDepthPresets[i];
					hudDistancePresets[i] = config.hudDistancePresets[i];
					hudHotkeys[i] = config.hudHotkeys[i];
				}
				hudHotkeys[4] = config.hudHotkeys[4];
				ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)config.hud3DDepthMode);
			}
			// back to main menu
			if (entryID == 10)
			{
				BRASSA_mode = BRASSA_Modes::MAINMENU;
				menuVelocity.x+=10.0f;
			}
			// back to game
			if (entryID == 11)
				BRASSA_mode = BRASSA_Modes::INACTIVE;
		}

		if (KEY_DOWN(VK_BACK))
		{
			if ((entryID >= 3) && (entryID <= 7) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				int index = entryID-3;
				if ((index >=0) && (index <=4))
					hudHotkeys[index] = 0;
				menuVelocity.x+=10;
			}
		}

		if (KEY_DOWN(VK_RIGHT) || KEY_DOWN(0x4A))
		{
			if ((entryID == 0) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				if (hud3DDepthMode > HUD_3D_Depth_Modes::HUD_DEFAULT)
					ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)(hud3DDepthMode-1));
				menuVelocity.x-=10.0f;
			}

			if ((entryID == 1) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				hudDistancePresets[(int)hud3DDepthMode]-=0.01f;
				ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)hud3DDepthMode);
				menuVelocity.x-=2.0f;
			}

			if ((entryID == 2) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				hud3DDepthPresets[(int)hud3DDepthMode]-=0.002f;
				ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)hud3DDepthMode);
				menuVelocity.x-=2.0f;
			}
		}

		if (KEY_DOWN(VK_RIGHT) || KEY_DOWN(0x4C))
		{
			// change hud scale
			if ((entryID == 0) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				if (hud3DDepthMode < HUD_3D_Depth_Modes::HUD_ENUM_RANGE-1)
					ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)(hud3DDepthMode+1));
				menuVelocity.x+=10.0f;
			}

			if ((entryID == 1) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				hudDistancePresets[(int)hud3DDepthMode]+=0.01f;
				ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)hud3DDepthMode);
				menuVelocity.x+=2.0f;
			}

			if ((entryID == 2) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				hud3DDepthPresets[(int)hud3DDepthMode]+=0.002f;
				ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)hud3DDepthMode);
				menuVelocity.x+=2.0f;
			}
		}
	}
	// output menu
	if (hudFont)
	{
		float LeftLensCenterAsPercentage = ((m_spShaderViewAdjustment->HMDInfo().physicalScreenSize.first / 2.0f) - 
			(config.ipd  / 2.0f)) / (m_spShaderViewAdjustment->HMDInfo().physicalScreenSize.first);

		// draw border - total width due to shift correction
		D3DRECT rect;
		rect.x1 = (int)0; rect.x2 = (int)width; rect.y1 = (int)borderTopHeight; rect.y2 = (int)(borderTopHeight+height*0.04f);
		ClearEmptyRect(vireio::RenderPosition::Left, rect, D3DCOLOR_ARGB(255,255,128,128), 2);
		ClearEmptyRect(vireio::RenderPosition::Right, rect, D3DCOLOR_ARGB(255,255,128,128), 2);

		hudMainMenu->Begin(D3DXSPRITE_ALPHABLEND);

		D3DXMATRIX matScale;
		D3DXMatrixScaling(&matScale, fScaleX, fScaleY, 1.0f);
		hudMainMenu->SetTransform(&matScale);

		rect1.left = 550;
		rect1.top = 300;
		DrawTextShadowed(hudFont, hudMainMenu, "Brown Reischl and Schneider Settings Analyzer (B.R.A.S.S.A.).\n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect.x1 = 0; rect.x2 = width; rect.y1 = (int)(335*fScaleY); rect.y2 = (int)(340*fScaleY);
		Clear(1, &rect, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255,255,128,128), 0, 0);

		rect1.top += 50;  rect1.left += 250; float hudQSHeight = (float)rect1.top * fScaleY;
		switch (hud3DDepthMode)
		{
		case D3DProxyDevice::HUD_DEFAULT:
			DrawTextShadowed(hudFont, hudMainMenu, "HUD : Default", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
			break;
		case D3DProxyDevice::HUD_SMALL:
			DrawTextShadowed(hudFont, hudMainMenu, "HUD : Small", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
			break;
		case D3DProxyDevice::HUD_LARGE:
			DrawTextShadowed(hudFont, hudMainMenu, "HUD : Large", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
			break;
		case D3DProxyDevice::HUD_FULL:
			DrawTextShadowed(hudFont, hudMainMenu, "HUD : Full", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
			break;
		default:
			break;
		}
		rect1.top += 40;
		char vcString[128];
		sprintf_s(vcString,"HUD Distance : %g", hudDistancePresets[(int)hud3DDepthMode]);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"HUD's 3D Depth : %g", hud3DDepthPresets[(int)hud3DDepthMode]);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Hotkey >Switch< : ", hud3DDepthPresets[(int)hud3DDepthMode]);
		std::string stdString = std::string(vcString);
		stdString.append(keyNameList[hudHotkeys[0]]);
		if ((hotkeyCatch) && (entryID==3))
			stdString = "Press the desired key.";
		DrawTextShadowed(hudFont, hudMainMenu, (LPCSTR)stdString.c_str(), -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Hotkey >Default< : ", hud3DDepthPresets[(int)hud3DDepthMode]);
		stdString = std::string(vcString);
		stdString.append(keyNameList[hudHotkeys[1]]);
		if ((hotkeyCatch) && (entryID==4))
			stdString = "Press the desired key.";
		DrawTextShadowed(hudFont, hudMainMenu, (LPCSTR)stdString.c_str(), -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Hotkey >Small< : ", hud3DDepthPresets[(int)hud3DDepthMode]);
		stdString = std::string(vcString);
		stdString.append(keyNameList[hudHotkeys[2]]);
		if ((hotkeyCatch) && (entryID==5))
			stdString = "Press the desired key.";
		DrawTextShadowed(hudFont, hudMainMenu, (LPCSTR)stdString.c_str(), -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Hotkey >Large< : ", hud3DDepthPresets[(int)hud3DDepthMode]);
		stdString = std::string(vcString);
		stdString.append(keyNameList[hudHotkeys[3]]);
		if ((hotkeyCatch) && (entryID==6))
			stdString = "Press the desired key.";
		DrawTextShadowed(hudFont, hudMainMenu, (LPCSTR)stdString.c_str(), -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Hotkey >Full< : ", hud3DDepthPresets[(int)hud3DDepthMode]);
		stdString = std::string(vcString);
		stdString.append(keyNameList[hudHotkeys[4]]);
		if ((hotkeyCatch) && (entryID==7))
			stdString = "Press the desired key.";
		DrawTextShadowed(hudFont, hudMainMenu, (LPCSTR)stdString.c_str(), -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Save current configuration", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Reload configuration", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Back to BRASSA Menu", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Back to Game", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));

		// draw HUD quick setting rectangles
		rect.x1 = (int)(width*0.49f); rect.x2 = (int)(width*0.53f); rect.y1 = (int)hudQSHeight; rect.y2 = (int)(hudQSHeight+height*0.027f);
		DrawSelection(vireio::RenderPosition::Left, rect, D3DCOLOR_ARGB(255, 128, 196, 128), (int)hud3DDepthMode, (int)HUD_3D_Depth_Modes::HUD_ENUM_RANGE);
		rect.x1 += (int)((LeftLensCenterAsPercentage) * width * 0.5f);
		rect.x2 += (int)((LeftLensCenterAsPercentage) * width * 0.5f);
		DrawSelection(vireio::RenderPosition::Right, rect, D3DCOLOR_ARGB(255, 128, 196, 128), (int)hud3DDepthMode, (int)HUD_3D_Depth_Modes::HUD_ENUM_RANGE);

		rect1.left = 0;
		rect1.right = 1920;
		rect1.top = 0;
		rect1.bottom = 1080;
		D3DXVECTOR3 vPos( 0.0f, 0.0f, 0.0f);
		hudMainMenu->Draw(NULL, &rect1, NULL, &vPos, D3DCOLOR_ARGB(255, 255, 255, 255));
		hudMainMenu->End();
	}
}

/**
* BRASSA GUI Calibration.
***/
void D3DProxyDevice::BRASSA_GUI()
{
	int width = stereoView->viewport.Width;
	int height = stereoView->viewport.Height;
	float menuTop = height*0.32f;
	float menuEntryHeight = height*0.037f;
	UINT menuEntryCount = 12;

	RECT rect1;
	rect1.left = 0;
	rect1.right = 1920;
	rect1.top = 0;
	rect1.bottom = 1080;

	float fScaleX = ((float)stereoView->viewport.Width / (float)rect1.right);
	float fScaleY = ((float)stereoView->viewport.Height / (float)rect1.bottom);

	// handle border height
	if (borderTopHeight<menuTop)
	{
		borderTopHeight = menuTop;
		menuVelocity.y=0.0f;
	}
	if (borderTopHeight>(menuTop+(menuEntryHeight*(float)(menuEntryCount-1))))
	{
		borderTopHeight = menuTop+menuEntryHeight*(float)(menuEntryCount-1);
		menuVelocity.y=0.0f;
	}

	// get menu entry id
	float entry = (borderTopHeight-menuTop+(menuEntryHeight/3.0f))/menuEntryHeight;
	UINT entryID = (UINT)entry;
	if (entryID >= menuEntryCount)
		OutputDebugString("Error in BRASSA menu programming !");

	if ((hotkeyCatch) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
	{
		for (int i = 0; i < 256; i++)
			if (KEY_DOWN(i) && (keyNameList[i]!="-"))
			{
				hotkeyCatch = false;
				int index = entryID-3;
				if ((index >=0) && (index <=4))
					guiHotkeys[index] = i;
			}
	}
	else
	{
		if (KEY_DOWN(VK_ESCAPE))
			BRASSA_mode = BRASSA_Modes::INACTIVE;

		if ((KEY_DOWN(VK_RETURN)) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if ((entryID >= 3) && (entryID <= 7) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				hotkeyCatch = true;
				menuVelocity.x+=10;
			}
			// save
			if (entryID == 8)
			{
				ProxyHelper* helper = new ProxyHelper();
				config.gui3DDepthMode = (int)gui3DDepthMode;
				for (int i = 0; i < 4; i++)
				{
					config.gui3DDepthPresets[i] = gui3DDepthPresets[i];
					config.guiSquishPresets[i] = guiSquishPresets[i];
					config.guiHotkeys[i] = guiHotkeys[i];
				}
				config.guiHotkeys[4] = guiHotkeys[4];
				helper->SaveGUIConfig(config);
			}
			// reload
			if (entryID == 9)
			{
				ProxyHelper* helper = new ProxyHelper();
				helper->LoadGUIConfig(config);
				for (int i = 0; i < 4; i++)
				{
					gui3DDepthPresets[i] = config.gui3DDepthPresets[i];
					guiSquishPresets[i] = config.guiSquishPresets[i];
					guiHotkeys[i] = config.guiHotkeys[i];
				}
				guiHotkeys[4] = config.guiHotkeys[4];
				ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)config.gui3DDepthMode);
			}
			// back to main menu
			if (entryID == 10)
			{
				BRASSA_mode = BRASSA_Modes::MAINMENU;
				menuVelocity.x+=10.0f;
			}
			// back to game
			if (entryID == 11)
				BRASSA_mode = BRASSA_Modes::INACTIVE;
		}

		if (KEY_DOWN(VK_BACK))
		{
			if ((entryID >= 3) && (entryID <= 7) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				int index = entryID-3;
				if ((index >=0) && (index <=4))
					guiHotkeys[index] = 0;
				menuVelocity.x+=10;
			}
		}

		if (KEY_DOWN(VK_RIGHT) || KEY_DOWN(0x4A))
		{
			if ((entryID == 0) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				if (gui3DDepthMode > GUI_3D_Depth_Modes::GUI_DEFAULT)
					ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)(gui3DDepthMode-1));
				menuVelocity.x-=10.0f;
			}

			if ((entryID == 1) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				guiSquishPresets[(int)gui3DDepthMode]-=0.01f;
				ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)gui3DDepthMode);
				menuVelocity.x-=2.0f;
			}

			if ((entryID == 2) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				gui3DDepthPresets[(int)gui3DDepthMode]-=0.002f;
				ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)gui3DDepthMode);
				menuVelocity.x-=2.0f;
			}
		}

		if (KEY_DOWN(VK_RIGHT) || KEY_DOWN(0x4C))
		{
			// change gui scale
			if ((entryID == 0) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				if (gui3DDepthMode < GUI_3D_Depth_Modes::GUI_ENUM_RANGE-1)
					ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)(gui3DDepthMode+1));
				menuVelocity.x+=10.0f;
			}

			if ((entryID == 1) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				guiSquishPresets[(int)gui3DDepthMode]+=0.01f;
				ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)gui3DDepthMode);
				menuVelocity.x+=2.0f;
			}

			if ((entryID == 2) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
			{
				gui3DDepthPresets[(int)gui3DDepthMode]+=0.002f;
				ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)gui3DDepthMode);
				menuVelocity.x+=2.0f;
			}
		}
	}
	// output menu
	if (hudFont)
	{
		float LeftLensCenterAsPercentage = ((m_spShaderViewAdjustment->HMDInfo().physicalScreenSize.first / 2.0f) - 
			(config.ipd  / 2.0f)) / (m_spShaderViewAdjustment->HMDInfo().physicalScreenSize.first);

		// draw border - total width due to shift correction
		D3DRECT rect;
		rect.x1 = (int)0; rect.x2 = (int)width; rect.y1 = (int)borderTopHeight; rect.y2 = (int)(borderTopHeight+height*0.04f);
		ClearEmptyRect(vireio::RenderPosition::Left, rect, D3DCOLOR_ARGB(255,255,128,128), 2);
		ClearEmptyRect(vireio::RenderPosition::Right, rect, D3DCOLOR_ARGB(255,255,128,128), 2);

		hudMainMenu->Begin(D3DXSPRITE_ALPHABLEND);

		D3DXMATRIX matScale;
		D3DXMatrixScaling(&matScale, fScaleX, fScaleY, 1.0f);
		hudMainMenu->SetTransform(&matScale);

		rect1.left = 550;
		rect1.top = 300;
		DrawTextShadowed(hudFont, hudMainMenu, "Brown Reischl and Schneider Settings Analyzer (B.R.A.S.S.A.).\n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect.x1 = 0; rect.x2 = width; rect.y1 = (int)(335*fScaleY); rect.y2 = (int)(340*fScaleY);
		Clear(1, &rect, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255,255,128,128), 0, 0);

		rect1.top += 50;  rect1.left += 250; float guiQSHeight = (float)rect1.top * fScaleY;
		switch (gui3DDepthMode)
		{
		case D3DProxyDevice::GUI_DEFAULT:
			DrawTextShadowed(hudFont, hudMainMenu, "GUI : Default", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
			break;
		case D3DProxyDevice::GUI_SMALL:
			DrawTextShadowed(hudFont, hudMainMenu, "GUI : Small", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
			break;
		case D3DProxyDevice::GUI_LARGE:
			DrawTextShadowed(hudFont, hudMainMenu, "GUI : Large", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
			break;
		case D3DProxyDevice::GUI_FULL:
			DrawTextShadowed(hudFont, hudMainMenu, "GUI : Full", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
			break;
		default:
			break;
		}
		rect1.top += 40;
		char vcString[128];
		sprintf_s(vcString,"GUI Size : %g", guiSquishPresets[(int)gui3DDepthMode]);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"GUI's 3D Depth : %g", gui3DDepthPresets[(int)gui3DDepthMode]);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Hotkey >Switch< : ", hud3DDepthPresets[(int)hud3DDepthMode]);
		std::string stdString = std::string(vcString);
		stdString.append(keyNameList[guiHotkeys[0]]);
		if ((hotkeyCatch) && (entryID==3))
			stdString = "Press the desired key.";
		DrawTextShadowed(hudFont, hudMainMenu, (LPCSTR)stdString.c_str(), -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Hotkey >Default< : ", hud3DDepthPresets[(int)hud3DDepthMode]);
		stdString = std::string(vcString);
		stdString.append(keyNameList[guiHotkeys[1]]);
		if ((hotkeyCatch) && (entryID==4))
			stdString = "Press the desired key.";
		DrawTextShadowed(hudFont, hudMainMenu, (LPCSTR)stdString.c_str(), -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Hotkey >Small< : ", hud3DDepthPresets[(int)hud3DDepthMode]);
		stdString = std::string(vcString);
		stdString.append(keyNameList[guiHotkeys[2]]);
		if ((hotkeyCatch) && (entryID==5))
			stdString = "Press the desired key.";
		DrawTextShadowed(hudFont, hudMainMenu, (LPCSTR)stdString.c_str(), -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Hotkey >Large< : ", hud3DDepthPresets[(int)hud3DDepthMode]);
		stdString = std::string(vcString);
		stdString.append(keyNameList[guiHotkeys[3]]);
		if ((hotkeyCatch) && (entryID==6))
			stdString = "Press the desired key.";
		DrawTextShadowed(hudFont, hudMainMenu, (LPCSTR)stdString.c_str(), -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Hotkey >Full< : ", hud3DDepthPresets[(int)hud3DDepthMode]);
		stdString = std::string(vcString);
		stdString.append(keyNameList[guiHotkeys[4]]);
		if ((hotkeyCatch) && (entryID==7))
			stdString = "Press the desired key.";
		DrawTextShadowed(hudFont, hudMainMenu, (LPCSTR)stdString.c_str(), -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Save current configuration", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Reload configuration", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Back to BRASSA Menu", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Back to Game", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));

		// draw GUI quick setting rectangles
		rect.x1 = (int)(width*0.49f); rect.x2 = (int)(width*0.53f); rect.y1 = (int)guiQSHeight; rect.y2 = (int)(guiQSHeight+height*0.027f);
		DrawSelection(vireio::RenderPosition::Left, rect, D3DCOLOR_ARGB(255, 128, 196, 128), (int)gui3DDepthMode, (int)GUI_3D_Depth_Modes::GUI_ENUM_RANGE);
		rect.x1 += (int)((LeftLensCenterAsPercentage) * width * 0.5f);
		rect.x2 += (int)((LeftLensCenterAsPercentage) * width * 0.5f);
		DrawSelection(vireio::RenderPosition::Right, rect, D3DCOLOR_ARGB(255, 128, 196, 128), (int)gui3DDepthMode, (int)GUI_3D_Depth_Modes::GUI_ENUM_RANGE);

		rect1.left = 0;
		rect1.right = 1920;
		rect1.top = 0;
		rect1.bottom = 1080;
		D3DXVECTOR3 vPos( 0.0f, 0.0f, 0.0f);
		hudMainMenu->Draw(NULL, &rect1, NULL, &vPos, D3DCOLOR_ARGB(255, 255, 255, 255));
		hudMainMenu->End();
	}
}

/**
* BRASSA Settings.
***/
void D3DProxyDevice::BRASSA_Settings()
{
	int width = stereoView->viewport.Width;
	int height = stereoView->viewport.Height;
	float menuTop = height*0.32f;
	float menuEntryHeight = height*0.037f;
	UINT menuEntryCount = 9;

	RECT rect1;
	rect1.left = 0;
	rect1.right = 1920;
	rect1.top = 0;
	rect1.bottom = 1080;

	float fScaleX = ((float)stereoView->viewport.Width / (float)rect1.right);
	float fScaleY = ((float)stereoView->viewport.Height / (float)rect1.bottom);

	// handle border height
	if (borderTopHeight<menuTop)
	{
		borderTopHeight = menuTop;
		menuVelocity.y=0.0f;
	}
	if (borderTopHeight>(menuTop+(menuEntryHeight*(float)(menuEntryCount-1))))
	{
		borderTopHeight = menuTop+menuEntryHeight*(float)(menuEntryCount-1);
		menuVelocity.y=0.0f;
	}

	// get menu entry id
	float entry = (borderTopHeight-menuTop+(menuEntryHeight/3.0f))/menuEntryHeight;
	UINT entryID = (UINT)entry;
	if (entryID >= menuEntryCount)
		OutputDebugString("Error in BRASSA menu programming !");

	/**
	* ESCAPE : Set BRASSA inactive and save the configuration.
	***/
	if (KEY_DOWN(VK_ESCAPE))
	{
		BRASSA_mode = BRASSA_Modes::INACTIVE;
		ProxyHelper* helper = new ProxyHelper();
		config.roll_multiplier = tracker->multiplierRoll;
		config.yaw_multiplier = tracker->multiplierYaw;
		config.pitch_multiplier = tracker->multiplierPitch;
		config.swap_eyes = stereoView->swapEyes;
		m_spShaderViewAdjustment->Save(config);
		helper->SaveConfig(config);
	}

	if ((KEY_DOWN(VK_RETURN)) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
	{
		// swap eyes
		if (entryID == 0)
		{
			stereoView->swapEyes = !stereoView->swapEyes;
			menuVelocity.x += 10.0f;
		}
		// screenshot
		if (entryID == 2)
		{
			// render 3 frames to get screenshots without BRASSA
			screenshot = 3;
			BRASSA_mode = BRASSA_Modes::INACTIVE;
		}
		// reset multipliers
		if (entryID == 6)
		{
			tracker->multiplierYaw = 25.0f;
			tracker->multiplierPitch = 25.0f;
			tracker->multiplierRoll = 1.0f;
			menuVelocity.x += 10.0f;
		}
		// back to main menu
		if (entryID == 7)
		{
			BRASSA_mode = BRASSA_Modes::MAINMENU;
			menuVelocity.x+=10.0f;
		}
		// back to game
		if (entryID == 8)
		{
			BRASSA_mode = BRASSA_Modes::INACTIVE;
			ProxyHelper* helper = new ProxyHelper();
			config.roll_multiplier = tracker->multiplierRoll;
			config.yaw_multiplier = tracker->multiplierYaw;
			config.pitch_multiplier = tracker->multiplierPitch;
			config.swap_eyes = stereoView->swapEyes;
			m_spShaderViewAdjustment->Save(config);
			helper->SaveConfig(config);
		}
	}

	if (KEY_DOWN(VK_BACK) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
	{
		// distortion
		if (entryID == 1)
		{
			this->stereoView->DistortionScale = 0.0f;
			this->stereoView->PostReset();
			menuVelocity.x += 2.0f;
		}
	}

	if ((KEY_DOWN(VK_RIGHT) || KEY_DOWN(0x4A)) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
	{
		// swap eyes
		if (entryID == 0)
		{
			stereoView->swapEyes = false;
			menuVelocity.x-=10.0f;
		}
		// distortion
		if (entryID == 1)
		{
			this->stereoView->DistortionScale -= 0.01f;
			this->stereoView->PostReset();
			menuVelocity.x -= 2.0f;
		}
		// yaw multiplier
		if (entryID == 3)
		{
			tracker->multiplierYaw -= 0.5f;
			menuVelocity.x -= 2.0f;
		}
		// pitch multiplier
		if (entryID == 4)
		{
			tracker->multiplierPitch -= 0.5f;
			menuVelocity.x -= 2.0f;
		}
		// roll multiplier
		if (entryID == 5)
		{
			tracker->multiplierRoll -= 0.05f;
			menuVelocity.x -= 2.0f;
		}
	}

	if ((KEY_DOWN(VK_RIGHT) || KEY_DOWN(0x4C)) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
	{
		// swap eyes
		if (entryID == 0)
		{
			stereoView->swapEyes = true;
			menuVelocity.x-=10.0f;
		}
		// distortion
		if (entryID == 1)
		{
			this->stereoView->DistortionScale += 0.01f;
			this->stereoView->PostReset();
			menuVelocity.x += 2.0f;
		}
		// yaw multiplier
		if (entryID == 3)
		{
			tracker->multiplierYaw += 0.5f;
			menuVelocity.x += 2.0f;
		}
		// pitch multiplier
		if (entryID == 4)
		{
			tracker->multiplierPitch += 0.5f;
			menuVelocity.x += 2.0f;
		}
		// roll multiplier
		if (entryID == 5)
		{
			tracker->multiplierRoll += 0.05f;
			menuVelocity.x += 2.0f;
		}
	}

	// output menu
	if (hudFont)
	{
		float LeftLensCenterAsPercentage = ((m_spShaderViewAdjustment->HMDInfo().physicalScreenSize.first / 2.0f) - 
			(config.ipd  / 2.0f)) / (m_spShaderViewAdjustment->HMDInfo().physicalScreenSize.first);

		// draw border - total width due to shift correction
		D3DRECT rect;
		rect.x1 = (int)0; rect.x2 = (int)width; rect.y1 = (int)borderTopHeight; rect.y2 = (int)(borderTopHeight+height*0.04f);
		ClearEmptyRect(vireio::RenderPosition::Left, rect, D3DCOLOR_ARGB(255,255,128,128), 2);
		ClearEmptyRect(vireio::RenderPosition::Right, rect, D3DCOLOR_ARGB(255,255,128,128), 2);

		hudMainMenu->Begin(D3DXSPRITE_ALPHABLEND);

		D3DXMATRIX matScale;
		D3DXMatrixScaling(&matScale, fScaleX, fScaleY, 1.0f);
		hudMainMenu->SetTransform(&matScale);

		rect1.left = 550;
		rect1.top = 300;
		DrawTextShadowed(hudFont, hudMainMenu, "Brown Reischl and Schneider Settings Analyzer (B.R.A.S.S.A.).\n", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect.x1 = 0; rect.x2 = width; rect.y1 = (int)(335*fScaleY); rect.y2 = (int)(340*fScaleY);
		Clear(1, &rect, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255,255,128,128), 0, 0);

		rect1.top += 50;  rect1.left += 250; float guiQSHeight = (float)rect1.top * fScaleY;
		switch (stereoView->swapEyes)
		{
		case true:
			DrawTextShadowed(hudFont, hudMainMenu, "Swap Eyes : true", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
			break;
		case false:
			DrawTextShadowed(hudFont, hudMainMenu, "Swap Eyes : false", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
			break;
		}
		rect1.top += 40;
		char vcString[128];
		sprintf_s(vcString,"Distortion Scale : %g", this->stereoView->DistortionScale);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Stereo Screenshots", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Yaw multiplier : %g", tracker->multiplierYaw);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Pitch multiplier : %g", tracker->multiplierPitch);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		sprintf_s(vcString,"Roll multiplier : %g", tracker->multiplierRoll);
		DrawTextShadowed(hudFont, hudMainMenu, vcString, -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Reset multipliers", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Back to BRASSA Menu", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));
		rect1.top += 40;
		DrawTextShadowed(hudFont, hudMainMenu, "Back to Game", -1, &rect1, 0, D3DCOLOR_ARGB(255, 255, 255, 255));

		rect1.left = 0;
		rect1.right = 1920;
		rect1.top = 0;
		rect1.bottom = 1080;
		D3DXVECTOR3 vPos( 0.0f, 0.0f, 0.0f);
		hudMainMenu->Draw(NULL, &rect1, NULL, &vPos, D3DCOLOR_ARGB(255, 255, 255, 255));
		hudMainMenu->End();
	}

}

/**
* Releases HUD font, shader registers, render targets, texture stages, vertex buffers, depth stencils, indices, shaders, declarations.
***/
void D3DProxyDevice::ReleaseEverything()
{
	// Fonts and any other D3DX interfaces should be released first.
	// They frequently hold stateblocks which are holding further references to other resources.
	if(hudFont) {
		hudFont->Release();
		hudFont = NULL;
	}


	m_spManagedShaderRegisters->ReleaseResources();

	if (m_pCapturingStateTo) {
		m_pCapturingStateTo->Release();
		m_pCapturingStateTo = NULL;
	}

	// one of these will still have a count of 1 until the backbuffer is released
	for(std::vector<D3D9ProxySurface*>::size_type i = 0; i != m_activeRenderTargets.size(); i++) 
	{
		if (m_activeRenderTargets[i] != NULL) {
			m_activeRenderTargets[i]->Release();
			m_activeRenderTargets[i] = NULL;
		}
	} 


	auto it = m_activeTextureStages.begin();
	while (it != m_activeTextureStages.end()) {
		if (it->second)
			it->second->Release();

		it = m_activeTextureStages.erase(it);
	}


	auto itVB = m_activeVertexBuffers.begin();
	while (itVB != m_activeVertexBuffers.end()) {
		if (itVB->second)
			itVB->second->Release();

		itVB = m_activeVertexBuffers.erase(itVB);
	}




	if (m_pActiveStereoDepthStencil) {
		m_pActiveStereoDepthStencil->Release();
		m_pActiveStereoDepthStencil = NULL;
	}

	if (m_pActiveIndicies) {
		m_pActiveIndicies->Release();
		m_pActiveIndicies = NULL;
	}

	if (m_pActivePixelShader) {
		m_pActivePixelShader->Release();
		m_pActivePixelShader = NULL;
	}

	if (m_pActiveVertexShader) {
		m_pActiveVertexShader->Release();
		m_pActiveVertexShader = NULL;
	}

	if (m_pActiveVertexDeclaration) {
		m_pActiveVertexDeclaration->Release();
		m_pActiveVertexDeclaration = NULL;
	}
}

/**
* Comparison made against active primary render target.
*
***/
bool D3DProxyDevice::isViewportDefaultForMainRT(CONST D3DVIEWPORT9* pViewport)
{
	D3D9ProxySurface* pPrimaryRenderTarget = m_activeRenderTargets[0];
	D3DSURFACE_DESC pRTDesc;
	pPrimaryRenderTarget->GetDesc(&pRTDesc);

	return  ((pViewport->Height == pRTDesc.Height) && (pViewport->Width == pRTDesc.Width) &&
		(pViewport->MinZ <= SMALL_FLOAT) && (pViewport->MaxZ >= SLIGHTLY_LESS_THAN_ONE));
}

/**
* Stores and sets view transform calling SetTransform() accordingly to current render side.
* @param pLeftMatrix The left view matrix.
* @param pRightMatrix The right view matrix.
* @param apply True to apply calling SetTransform()
* @see BaseDirect3DDevice9::SetTransform()
***/
HRESULT D3DProxyDevice::SetStereoViewTransform(D3DXMATRIX pLeftMatrix, D3DXMATRIX pRightMatrix, bool apply)
{
	if (D3DXMatrixIsIdentity(&pLeftMatrix) && D3DXMatrixIsIdentity(&pRightMatrix)) {
		m_bViewTransformSet = false;
	}
	else {
		m_bViewTransformSet = true;
	}

	m_leftView = pLeftMatrix;
	m_rightView = pRightMatrix;

	if (m_currentRenderingSide == vireio::Left) {
		m_pCurrentView = &m_leftView;
	}
	else {
		m_pCurrentView = &m_rightView;
	}

	if (apply)
		return BaseDirect3DDevice9::SetTransform(D3DTS_VIEW, m_pCurrentView);
	else
		return D3D_OK;
}

/**
* Stores and sets projection transform calling SetTransform() accordingly to current render side.
* @param pLeftMatrix The left view matrix.
* @param pRightMatrix The right view matrix.
* @param apply True to apply calling SetTransform()
* @see BaseDirect3DDevice9::SetTransform()
***/
HRESULT D3DProxyDevice::SetStereoProjectionTransform(D3DXMATRIX pLeftMatrix, D3DXMATRIX pRightMatrix, bool apply)
{
	if (D3DXMatrixIsIdentity(&pLeftMatrix) && D3DXMatrixIsIdentity(&pRightMatrix)) {
		m_bProjectionTransformSet = false;
	}
	else {
		m_bProjectionTransformSet = true;
	}

	m_leftProjection = pLeftMatrix;
	m_rightProjection = pRightMatrix;

	if (m_currentRenderingSide == vireio::Left) {
		m_pCurrentProjection = &m_leftProjection;
	}
	else {
		m_pCurrentProjection = &m_rightProjection;
	}

	if (apply)
		return BaseDirect3DDevice9::SetTransform(D3DTS_PROJECTION, m_pCurrentProjection);
	else
		return D3D_OK;
}