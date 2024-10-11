#define STRICT
#define D3D11_NO_HELPERS
#define D3D11_VIDEO_NO_HELPERS

#define DEBUG_BUILD 0

#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <dcomp.h>
#include <windows.h>

#define internal	static
#define global		static

#define SC_GRAVE		0x0029
#define SC_NUMPAD_5		0x004C
#define SC_CONTROLLEFT	0x001D

#define GetArrayCount(Array)	(sizeof(Array) / sizeof((Array)[0]))
#define Min(A, B)				((A) < (B) ? (A) : (B))
#define Max(A, B)				((A) > (B) ? (A) : (B))

extern "C" int _fltused = 0;

struct v2
{
	float X;
	float Y;
};

struct vertex
{
	v2 Position;
	v2 Texture;
};

union vertex_constant_buffer
{
	struct
	{
		v2 TextureTransform;
	};
	
	// Must be in multiples of 16
	char Buffer[16];
};

//
// Globals
//

global HRESULT Result;

// @Note These are fallback default values, in case of error 
global int MonitorWidth  = 1920;
global int MonitorHeight = 1080;

global int DisplayWidth  = 200;
global int DisplayHeight = 200;

global D3D11_BOX GlobalCutBox;

global int RenderThreadCBufferVersion = 0;
global int WindowThreadCBufferVersion = 0;
global vertex_constant_buffer CBuffer = {
	1.0f / (1280.0f / 200.0f), 
	1.0f / (1024.0f / 200.0f), 
};

//
// Functions
//

internal size_t GetStringLength(char *String)
{
	size_t Length = 0;
	for (; String[Length] != '\0'; ++Length)
	{
		// Do nothing
	}
	
	return Length;
}

internal void Error(char *ErrorCause)
{
	char ErrorMessage[1024] = "ERROR";
	
	HRESULT StatusCode;
	if (FAILED(Result))
	{
		StatusCode = Result;
	}
	else
	{
		StatusCode = GetLastError();
	}
	
	DWORD Flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
	DWORD ErrorLength = FormatMessageA(Flags, 0, StatusCode, 0, ErrorMessage, sizeof(ErrorMessage), NULL);
	if (ErrorLength == 0)
	{
		// FormatMessageA failed, GetLastError
	}
	
	MessageBoxA(0, ErrorCause, ErrorMessage, 0);
	ExitProcess(1);
}

struct shader_data
{
	void  *Data;
	size_t Size;
};

internal shader_data CompileShader(char *ShaderSource, size_t ShaderSourceSize, char *EntryPoint)
{
	int Flags = 
	(DEBUG_BUILD * D3DCOMPILE_DEBUG) | 
	(DEBUG_BUILD * D3DCOMPILE_OPTIMIZATION_LEVEL3) | (              D3DCOMPILE_ENABLE_STRICTNESS) | (              D3DCOMPILE_PARTIAL_PRECISION);
	
	char *Target;
	if (EntryPoint == "VertexMain")
	{
		Target = "vs_4_0";
	}
	else if (EntryPoint == "PixelMain")
	{
		EntryPoint = "PixelMain";
		Target = "ps_4_0";
	}
	else
	{
		Error("CompileShader: Unknown Shader Target, Vertex/Pixel");
	}
	
	ID3DBlob *OutputBlob;
	ID3DBlob *ErrorBlob;
	Result = D3DCompile(ShaderSource, ShaderSourceSize, NULL, NULL, NULL, EntryPoint, Target, Flags, 0, &OutputBlob, &ErrorBlob);
	if (FAILED(Result))
	{
		char *ErrorMessage		= (char *)ErrorBlob->GetBufferPointer();
		SIZE_T ErrorMessageSize	= ErrorBlob->GetBufferSize();
		
		MessageBoxA(0, EntryPoint, 0, 0);
		Error(ErrorMessage);
	}
	
	shader_data Shader;
	Shader.Data = OutputBlob->GetBufferPointer();
	Shader.Size = OutputBlob->GetBufferSize();
	
	return Shader;
}

internal LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	LRESULT Result = 0;
	
	switch (Message)
	{
		case WM_NCHITTEST:
		{
			return HTNOWHERE;
		}
		break;
		
		case WM_CLOSE:
		{
			PostQuitMessage(0);
		}
		break;
		
		case WM_ERASEBKGND:
		{
			Result = 1; // @Note Pretend we cleared the background because we don't care
		}
		break;
		
		default:
		{
			Result = DefWindowProcW(Window, Message, WParam, LParam);
		}
		break;
	}
	
	return Result;
}

internal void Direct3DCreateDevice(ID3D11Device **Device, ID3D11DeviceContext **DeviceContext)
{
	int DeviceFlags = 
		D3D11_CREATE_DEVICE_SINGLETHREADED | 
		D3D11_CREATE_DEVICE_BGRA_SUPPORT | 
	(DEBUG_BUILD * D3D11_CREATE_DEVICE_DEBUG);
	
	D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_10_0;
	Result = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, DeviceFlags, &FeatureLevel, 
							   1, D3D11_SDK_VERSION, Device, NULL, DeviceContext);
	
	if (FAILED(Result))
	{
		Error("D3D11CreateDevice");
	}
}

internal DWORD WINAPI RenderThread(LPVOID lpParameter)
{
	HWND Window = (HWND)lpParameter;
	
	//
	// Initialize the Direct3D Device and DeviceContext
	//
	
	ID3D11Device		*Device;
	ID3D11DeviceContext	*DeviceContext;
	
	Direct3DCreateDevice(&Device, &DeviceContext);
	
	//
	// Primitive Topology
	//
	
	DeviceContext->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
	//
	// Shader compilation
	//
	
	char *ShaderSource = 
	R"RAW(
					cbuffer CBuffer { float2 TextureTransform; };
					
					struct VSOutput { float4 pos : SV_POSITION; float2 tex : TEXCOORD0; };
					
					VSOutput VertexMain(uint ID : SV_VERTEXID)
					{
			// Full-screen Triangle from SV_VERTEXID
						VSOutput Output;
						Output.pos.x = (float)(ID / 2) * 4.0f - 1.0f;
						Output.pos.y = (float)(ID % 2) * 4.0f - 1.0f;
						Output.pos.z = 0.0f;
						Output.pos.w = 1.0f;
						
						Output.tex.x = 0.0f + (float)(ID / 2) * 2.0f;
						Output.tex.y = 1.0f - (float)(ID % 2) * 2.0f;
						
						// Draw only the cut region from CopySubresourceRegion
						Output.tex.xy *= TextureTransform.xy;
						
						return Output;
					}
					
					SamplerState Sampler;
					Texture2D    Texture;
					float4 PixelMain(VSOutput Input) : SV_TARGET
					{
						float4 Output = Texture.Sample(Sampler, Input.tex.xy);
						
float Alpha  = 0.1f;
						float Darken = 0.1f;

// Set alpha
						Output.a = Alpha;

// Darken RGB absolute
						Output.rgb = (Output.rgb - Darken) * Alpha;
						
						return Output;
					}
				)RAW";
	
	size_t ShaderSize = GetStringLength(ShaderSource);
	
	shader_data VertexShaderData = CompileShader(ShaderSource, ShaderSize, "VertexMain");
	shader_data PixelShaderData  = CompileShader(ShaderSource, ShaderSize, "PixelMain");
	
	ID3D11VertexShader *VertexShader;
	Result = Device->CreateVertexShader(VertexShaderData.Data, VertexShaderData.Size, NULL, &VertexShader);
	if (FAILED(Result))
	{
		Error("CreateVertexShader");
	}
	
	ID3D11PixelShader *PixelShader;
	Result = Device->CreatePixelShader(PixelShaderData.Data, PixelShaderData.Size, NULL, &PixelShader);
	if (FAILED(Result))
	{
		Error("CreatePixelShader");
	}
	
	DeviceContext->VSSetShader(VertexShader, NULL, 0);
	DeviceContext->PSSetShader(PixelShader,  NULL, 0);
	
	//
	// Constant buffer
	//
	
	D3D11_BUFFER_DESC BufferDescription;
	BufferDescription.ByteWidth           = sizeof(CBuffer);
	BufferDescription.Usage               = D3D11_USAGE_DYNAMIC;
	BufferDescription.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
	BufferDescription.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
	BufferDescription.MiscFlags           = 0;
	BufferDescription.StructureByteStride = 0;
	
	D3D11_SUBRESOURCE_DATA ConstantBufferResource;
	ConstantBufferResource.pSysMem = &CBuffer;
	ConstantBufferResource.SysMemPitch = 0;
	ConstantBufferResource.SysMemSlicePitch = 0;
	
	ID3D11Buffer *ConstantBuffer;
	Result = Device->CreateBuffer(&BufferDescription, &ConstantBufferResource, &ConstantBuffer);
	if (Result != S_OK)
	{
		Error("CreateBuffer");
	}
	
	DeviceContext->VSSetConstantBuffers(0, 1, &ConstantBuffer);
	
	//
	// Texture
	//
	
	D3D11_TEXTURE2D_DESC TextureDesc;
	TextureDesc.Width          = MonitorWidth;
	TextureDesc.Height         = MonitorHeight;
	TextureDesc.MipLevels      = 1;
	TextureDesc.ArraySize      = 1;
	TextureDesc.Format         = DXGI_FORMAT_B8G8R8A8_UNORM;// @Todo Should it be sRGB here
	TextureDesc.SampleDesc     = DXGI_SAMPLE_DESC{ 1, 0 };
	TextureDesc.Usage          = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
	TextureDesc.CPUAccessFlags = 0;
	TextureDesc.MiscFlags      = 0;
	
	ID3D11Texture2D *DisplayTexture;
	Result = Device->CreateTexture2D(&TextureDesc, NULL, &DisplayTexture);
	if (FAILED(Result))
	{
		Error("CreateTexture2D");
	}
	
	D3D11_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDesc;
	ShaderResourceViewDesc.Format                    = DXGI_FORMAT_B8G8R8A8_UNORM;
	ShaderResourceViewDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;// @Todo Should it be sRGB here
	ShaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	ShaderResourceViewDesc.Texture2D.MipLevels       = 1;
	
	ID3D11ShaderResourceView *TextureView;
	Result = Device->CreateShaderResourceView(DisplayTexture, &ShaderResourceViewDesc, &TextureView);
	if (FAILED(Result))
	{
		Error("CreateShaderResourceView");
	}
	
	DeviceContext->PSSetShaderResources(0, 1, &TextureView);
	
	//
	// Texture Sampler
	//
	
	D3D11_SAMPLER_DESC SamplerDesc;
	SamplerDesc.Filter         = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	SamplerDesc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.MipLODBias     = 0.0f;
	SamplerDesc.MaxAnisotropy  = 0; // Texture Anisotropic Filtering (TAF)
	SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SamplerDesc.BorderColor[0] = 0.0f;
	SamplerDesc.BorderColor[1] = 0.0f;
	SamplerDesc.BorderColor[2] = 0.0f;
	SamplerDesc.BorderColor[3] = 0.0f;
	SamplerDesc.MinLOD         = -D3D11_FLOAT32_MAX;
	SamplerDesc.MaxLOD         = D3D11_FLOAT32_MAX;
	
	ID3D11SamplerState *SamplerState;
	Result = Device->CreateSamplerState(&SamplerDesc, &SamplerState);
	if(FAILED(Result))
	{
		Error("CreateSamplerState");
	}
	
	DeviceContext->PSSetSamplers(0, 1, &SamplerState);
	
	//
	// ViewPort
	//
#if 0
	D3D11_VIEWPORT Viewport;
	Viewport.TopLeftX = 0;
	Viewport.TopLeftY = 0;
	Viewport.Width    = DisplayWidth;
	Viewport.Height   = DisplayHeight;
	Viewport.MinDepth = 0.0f;
	Viewport.MaxDepth = 1.0f;
	
	DeviceContext->RSSetViewports(1, &Viewport);
#endif
	//
	// SwapChain
	//
	
	IDXGIDevice *DXGIDevice;
	Result = Device->QueryInterface(__uuidof(IDXGIDevice), (void **)&DXGIDevice);
	if (FAILED(Result))
	{
		Error("QueryInterface(IDXGIDevice)");
	}
	
	IDXGIAdapter *Adapter;
	Result = DXGIDevice->GetAdapter(&Adapter);
	if (FAILED(Result))
	{
		Error("GetAdapter");
	}
	
	IDXGIOutput *Output;
	Result = Adapter->EnumOutputs(0, &Output);
	if (FAILED(Result))
	{
		Error("EnumOutputs");
	}
	
	IDXGIOutput1 *Output1;
	Result = Output->QueryInterface(__uuidof(IDXGIOutput1), (void **)&Output1);
	if (FAILED(Result))
	{
		Error("QueryInterface(IDXGIOutput1)");
	}
	
	IDXGIFactory2 *Factory;
	Adapter->GetParent(__uuidof(IDXGIFactory2), (void **)&Factory);
	
	DXGI_SWAP_CHAIN_DESC1 SwapChainDesc;
	SwapChainDesc.Width        = MonitorWidth;
	SwapChainDesc.Height       = MonitorHeight;
	SwapChainDesc.Format       = DXGI_FORMAT_B8G8R8A8_UNORM;
	SwapChainDesc.Stereo       = false;
	SwapChainDesc.SampleDesc   = DXGI_SAMPLE_DESC{ 1, 0 };
	SwapChainDesc.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapChainDesc.BufferCount  = 2;
	SwapChainDesc.Scaling      = DXGI_SCALING_STRETCH;
	SwapChainDesc.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	SwapChainDesc.AlphaMode    = DXGI_ALPHA_MODE_PREMULTIPLIED;
	SwapChainDesc.Flags        = 0;
	
	IDXGISwapChain1 *SwapChain;
	Result = Factory->CreateSwapChainForComposition(Device, &SwapChainDesc, NULL, &SwapChain);
	if (FAILED(Result))
	{
		Error("CreateSwapChain");
	}
	
	//
	// Render Target View
	//
	
	ID3D11Texture2D *BackBuffer;
	Result = SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&BackBuffer);
	if (FAILED(Result))
	{
		Error("GetBuffer(BackBuffer)");
	}
	
	D3D11_RENDER_TARGET_VIEW_DESC RenderTargetViewDesc;
	RenderTargetViewDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	RenderTargetViewDesc.ViewDimension      = D3D11_RTV_DIMENSION_TEXTURE2D;
	RenderTargetViewDesc.Texture2D.MipSlice = 0;
	
	ID3D11RenderTargetView *RenderTargetView;
	Result = Device->CreateRenderTargetView(BackBuffer, &RenderTargetViewDesc, &RenderTargetView);
	if (FAILED(Result))
	{
		Error("CreateRenderTargetView");
	}
	
	//
	// Direct Composition
	//
	
	IDCompositionDevice *CompositionDevice;
	Result = DCompositionCreateDevice(DXGIDevice, __uuidof(CompositionDevice), (void **)&CompositionDevice);
	if (FAILED(Result))
	{
		Error("DCompositionCreateDevice");
	}
	
	IDCompositionTarget *CompositionTarget;
	Result = CompositionDevice->CreateTargetForHwnd(Window, true, &CompositionTarget);
	if (FAILED(Result))
	{
		Error("CreateTargetForHwnd");
	}
	
	IDCompositionVisual *CompositionVisual;
	Result = CompositionDevice->CreateVisual(&CompositionVisual);
	if (FAILED(Result))
	{
		Error("CreateVisual");
	}
	
	Result = CompositionVisual->SetContent((IUnknown *)SwapChain);
	if (FAILED(Result))
	{
		Error("SetContent");
	}
	
	Result = CompositionTarget->SetRoot(CompositionVisual);
	if (FAILED(Result))
	{
		Error("SetRoot");
	}
	
	Result = CompositionDevice->Commit();
	if (FAILED(Result))
	{
		Error("Commit");
	}
	
	// @Note Creation is handled per-frame because it's not reliable and it can be destoyed at any time so we need to recreate it
	IDXGIOutputDuplication *OutputDuplication = NULL;
	
	//
	// Render loop
	//
	
	int ViewportWidth  = 0;
	int ViewportHeight = 0;
	
	for (;;)
	{
		//
		// Update the viewport
		//
		
		if ((ViewportWidth  != DisplayWidth) || 
			(ViewportHeight != DisplayHeight))
		{
			ViewportWidth  = DisplayWidth;
			ViewportHeight = DisplayHeight;
			
			D3D11_VIEWPORT Viewport;
			Viewport.TopLeftX = 0;
			Viewport.TopLeftY = 0;
			Viewport.Width    = DisplayWidth;
			Viewport.Height   = DisplayHeight;
			Viewport.MinDepth = 0.0f;
			Viewport.MaxDepth = 1.0f;
			
			DeviceContext->RSSetViewports(1, &Viewport);
		}
		
		//
		// Update the constant buffer
		//
		
		if (RenderThreadCBufferVersion != WindowThreadCBufferVersion)
		{
			RenderThreadCBufferVersion = WindowThreadCBufferVersion;
			
			DeviceContext->UpdateSubresource(ConstantBuffer, 0, NULL, &ConstantBufferResource, 0, 0);
		}
		
		//
		// Capture the minimap
		//
		
		IDXGIResource *DesktopResource;
		DXGI_OUTDUPL_FRAME_INFO FrameInfo;
		UINT Timeout = 100;
		
		if (OutputDuplication == NULL)
		{
			Result = Output1->DuplicateOutput(Device, &OutputDuplication);
			if (FAILED(Result))
			{
				if (Result == E_ACCESSDENIED)
				{
					Sleep(100);
					continue;
				}
				else
				{
					Error("DuplicateOutput");
				}
			}
		}
		
		Result = OutputDuplication->AcquireNextFrame(INFINITE, &FrameInfo, &DesktopResource);
		if (FAILED(Result))
		{
			if (Result == DXGI_ERROR_ACCESS_LOST)
			{
				OutputDuplication->Release();
				OutputDuplication = NULL;
				continue;
			}
			else
			{
				Error("AcquireNextFrame");
			}
		}
		
		ID3D11Texture2D *DesktopTexture;
		Result = DesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&DesktopTexture);
		if (FAILED(Result))
		{
			Error("QueryInterface(ID3D11Texture2D)");
		}
		
		D3D11_BOX CurrentCutBox = GlobalCutBox;
		
		DeviceContext->CopySubresourceRegion(DisplayTexture, 0, 
											 0, 0, 0, 
											 DesktopTexture, 0, 
											 &CurrentCutBox);
		
		//
		// Relesae the captured frame
		//
		
		DesktopTexture->Release();
		DesktopResource->Release();
		
		Result = OutputDuplication->ReleaseFrame();
		if (FAILED(Result))
		{
			if (Result == DXGI_ERROR_ACCESS_LOST)
			{
				OutputDuplication->Release();
				OutputDuplication = NULL;
				continue;
			}
			else
			{
				Error("ReleaseFrame");
			}
		}
		
		//
		// Render the overlay
		//
		
		DeviceContext->OMSetRenderTargets(1, &RenderTargetView, NULL);
		
		float ClearColour[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		DeviceContext->ClearRenderTargetView(RenderTargetView, ClearColour);
		
		UINT VertexCount = 3;
		UINT StartVertex = 0;
		DeviceContext->Draw(VertexCount, StartVertex);
		
		// @Note For DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
		// 0 - Cancel the remaining time on the previously
		//     presented frame and discard this frame if a newer frame is queued.
		int SyncInterval = 0;
		int Flags = 0;
		Result = SwapChain->Present(SyncInterval, Flags);
		if (FAILED(Result))
		{
			if ((Result == DXGI_ERROR_DEVICE_RESET)   || 
				(Result == DXGI_ERROR_DEVICE_REMOVED))
			{
				Device->Release();
				DeviceContext->Release();
				
				Direct3DCreateDevice(&Device, &DeviceContext);
			}
			else
			{
				Error("Present");
			}
		}
	}
	
	return 0;
}

void WINAPI WinMainCRTStartup()
{
	//
	// Monitor info
	//
	
	HMONITOR Monitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
	
	MONITORINFO MonitorInfo;
	MonitorInfo.cbSize = sizeof(MonitorInfo);
	
	if (GetMonitorInfoW(Monitor, &MonitorInfo) != 0)
	{
		RECT *Rect = &MonitorInfo.rcMonitor;
		MonitorWidth  = Rect->right  - Rect->left;
		MonitorHeight = Rect->bottom - Rect->top;
	}
	
	//
	// Default cut area
	//
	
	GlobalCutBox.left   = MonitorWidth  - DisplayWidth;
	GlobalCutBox.top    = MonitorHeight - DisplayHeight;
	GlobalCutBox.right  = MonitorWidth;
	GlobalCutBox.bottom = MonitorHeight;
	GlobalCutBox.front  = 0;
	GlobalCutBox.back   = 1;
	
	//
	// Window creation
	//
	
	WNDCLASSEXW WindowClassDesc;
	WindowClassDesc.cbSize			= sizeof(WindowClassDesc);
	WindowClassDesc.style			= CS_HREDRAW | CS_VREDRAW;
	WindowClassDesc.lpfnWndProc		= WindowProc;
	WindowClassDesc.cbClsExtra		= 0;
	WindowClassDesc.cbWndExtra		= 0;
	WindowClassDesc.hInstance		= GetModuleHandleW(NULL);
	WindowClassDesc.hIcon			= NULL;
	WindowClassDesc.hCursor			= (HCURSOR)LoadImageW(NULL, (LPCWSTR)IDC_ARROW, IMAGE_CURSOR, 0, 0, LR_SHARED);
	WindowClassDesc.hbrBackground	= NULL;
	WindowClassDesc.lpszMenuName	= NULL;
	WindowClassDesc.lpszClassName	= L"LoLBigMapClass";
	WindowClassDesc.hIconSm			= NULL;
	
	ATOM WindowAtom = RegisterClassExW(&WindowClassDesc);
	if (WindowAtom == 0)
	{
		Error("RegisterClassW");
	}
	
	DWORD ExtendedFlags = WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_APPWINDOW;
	ExtendedFlags |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE; // Click-through
	
	int WindowX = (MonitorWidth  - DisplayWidth)  / 2;
	int WindowY = (MonitorHeight - DisplayHeight) / 2;
	
	HWND Window = CreateWindowExW(ExtendedFlags, WindowClassDesc.lpszClassName, L"BigMap", 
								  WS_POPUP | WS_VISIBLE, WindowX, WindowY, DisplayWidth, DisplayHeight, 
								  NULL, NULL, WindowClassDesc.hInstance, NULL);
	
	if (Window == NULL)
	{
		Error("CreateWindowExW");
	}
	
	//
	// Raw Input
	//
	
	RAWINPUTDEVICE Device;
	Device.usUsagePage = 1; // Generic
	Device.usUsage     = 6; // Keyboard
	Device.dwFlags     = RIDEV_INPUTSINK | RIDEV_NOLEGACY;
	Device.hwndTarget  = Window;
	
	if (RegisterRawInputDevices(&Device, 1, sizeof(Device)) == false)
	{
		Error("RegisterRawInputDevices");
	}
	
	//
	// Main Thread
	//
	
	DWORD RenderThreadID;
	HANDLE RenderThreadHandle = CreateThread(NULL, 0, RenderThread, (LPVOID)Window, 0, &RenderThreadID);
	if (RenderThreadHandle == NULL)
	{
		Error("CreateThread(RenderThread)");
	}
	
	//
	// Keyboard state
	//
	
	size_t ControlIsDown = false;
	
	size_t DragKeyScanCode	= SC_NUMPAD_5;
	size_t DragKeyIsDown	= false;
	
	//
	// Drag state
	//
	
	size_t FirstDragPointIsValid = false;
	POINT DragStartPoint	= {};
	POINT DragEndPoint		= {};
	
	//
	// Window Message Loop
	//
	
	for (;;)
	{
		MSG Message;
		BOOL MessageLoopResult = GetMessageW(&Message, NULL, 0, 0);
		if (MessageLoopResult > 0)
		{
			switch (Message.message)
			{
				case WM_INPUT:
				{
					HRAWINPUT RawInput = (HRAWINPUT)Message.lParam;
					char DataBuffer[4 * 1024];
					UINT DataSize = sizeof(DataBuffer);
					
					if (GetRawInputData(RawInput, RID_INPUT, 
										&DataBuffer, &DataSize, sizeof(RAWINPUTHEADER)) == -1)
					{
						Error("GetRawInputData");
					}
					
					RAWINPUT *Data = (RAWINPUT *)DataBuffer;
					if (Data->header.dwType == RIM_TYPEKEYBOARD)
					{
						size_t Flags	= Data->data.keyboard.Flags;
						size_t ScanCode	= Data->data.keyboard.MakeCode;
						size_t IsDown	= ((Flags & RI_KEY_BREAK) == 0);
						
						if (ScanCode == SC_CONTROLLEFT)
						{
							ControlIsDown = IsDown;
						}
						else if (ScanCode == DragKeyScanCode)
						{
							size_t DragKeyWasDown = DragKeyIsDown;
							DragKeyIsDown = IsDown;
							
							if (DragKeyIsDown && !DragKeyWasDown)
							{
								if (GetCursorPos(&DragStartPoint) != 0)
								{
									// Success
									FirstDragPointIsValid = true;
								}
								else
								{
									// Failure
									FirstDragPointIsValid = false;
								}
							}
							
							if (!DragKeyIsDown && DragKeyWasDown)
							{
								if ((GetCursorPos(&DragEndPoint) != 0) &&
									(FirstDragPointIsValid == TRUE))
								{
									//
									// New cut area to capture
									//
									
									D3D11_BOX DragCutBox;
									DragCutBox.left		= Min(DragStartPoint.x, DragEndPoint.x);
									DragCutBox.top		= Min(DragStartPoint.y, DragEndPoint.y);
									DragCutBox.right	= Max(DragStartPoint.x, DragEndPoint.x);
									DragCutBox.bottom	= Max(DragStartPoint.y, DragEndPoint.y);
									DragCutBox.front	= 0;
									DragCutBox.back     = 1;
									
									int CutBoxWidth  = DragCutBox.right	 - DragCutBox.left;
									int CutBoxHeight = DragCutBox.bottom - DragCutBox.top;
									
									//
									// New window dimensions
									//
									
									if (!ControlIsDown)
									{
										// CHANGE THE CAPTURE REGION
										GlobalCutBox = DragCutBox;
										
										v2 *Transform = &CBuffer.TextureTransform;
										Transform->X = 1.0f / (1280.0f / CutBoxWidth);
										Transform->Y = 1.0f / (1024.0f / CutBoxHeight);
										
										// @Todo There should be fence here :P
										
										// Tell the render thread to update the constant buffer
										WindowThreadCBufferVersion++;
									}
									else
									{
										// CHANGE THE WINDOW REGION
										WindowX	= DragCutBox.left;
										WindowY	= DragCutBox.top;
										DisplayWidth	= CutBoxWidth;
										DisplayHeight	= CutBoxHeight;
										
										SetWindowPos(Window, NULL, WindowX, WindowY, DisplayWidth, DisplayHeight, SWP_NOCOPYBITS);
									}
								}
								
								FirstDragPointIsValid = false;
							}
						}
					}
				}
				break;
				
				default:
				{
					TranslateMessage(&Message);
					DispatchMessageW(&Message);
				}
				break;
			};
		}
		else
		{
			if (MessageLoopResult == -1)
			{
				Error("GetMessage");
			}
			else if (MessageLoopResult == 0)
			{
				ExitProcess(0);
			}
		}
	}
}
