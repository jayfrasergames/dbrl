#include "jfg/prelude.h"
#include "jfg/codepage_437.h"

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include "jfg/jfg_d3d11.h"
#include "jfg/imgui.h"

#include <dxgi1_2.h>

#include "gen/cs_draw_red.data.h"
#include "gen/vs_text.data.h"
#include "gen/ps_text.data.h"

#include <math.h>
#include <stdio.h>
#include <assert.h>

#include "dbrl.h"

void show_debug_messages(HWND window, ID3D11InfoQueue *info_queue)
{
	// u64 num_messages = info_queue->GetNumStoredMessages();
	u64 num_messages = info_queue->GetNumStoredMessagesAllowedByRetrievalFilter();
	char buffer[1024] = {};
	snprintf(buffer, ARRAY_SIZE(buffer), "There are %llu messages", num_messages);
	MessageBox(window, buffer, "DBRL", MB_OK);
	for (u64 i = 0; i < num_messages; ++i) {
		size_t message_len;
		HRESULT hr = info_queue->GetMessage(i, NULL, &message_len);
		assert(SUCCEEDED(hr));
		D3D11_MESSAGE *message = (D3D11_MESSAGE*)malloc(message_len);
		hr = info_queue->GetMessage(i, message, &message_len);
		assert(SUCCEEDED(hr));
		MessageBox(window, message->pDescription, "DBRL", MB_OK);
		free(message);
	}
}

u8 running = 1;

LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
	case WM_DESTROY:
		running = 0;
		PostQuitMessage(0);
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(window, &ps);
		FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW+1));
		EndPaint(window, &ps);
		return 0;
	}
	}
	return DefWindowProc(window, msg, wparam, lparam);
}

#ifdef DEBUG

#define LIBRARY_NAME "dbrl_d.dll"
HMODULE game_library;

#define GAME_FUNCTION(return_type, name, ...) static return_type (*name)(__VA_ARGS__) = NULL;
GAME_FUNCTIONS
#undef GAME_FUNCTION

u8 load_game_functions()
{
	game_library = LoadLibrary(LIBRARY_NAME);
	if (game_library == NULL) {
		return 0;
	}

#define GAME_FUNCTION(return_type, name, ...) \
	name = (return_type (*)(__VA_ARGS__))GetProcAddress(game_library, #name); \
	if (name == NULL) { \
		return 0; \
	}
	GAME_FUNCTIONS
#undef GAME_FUNCTION

	return 1;
}

#else

#include "dbrl.cpp"

u8 load_game_functions()
{
	return 1;
}

#endif

INT WINAPI WinMain(HINSTANCE instance, HINSTANCE prev_instance, PSTR cmd_line, INT cmd_show)
{
	const char window_class_name[] = "dbrl_window_class";

	WNDCLASS window_class = {};

	window_class.lpfnWndProc = window_proc;
	window_class.hInstance = instance;
	window_class.lpszClassName = window_class_name;

	RegisterClass(&window_class);

	HWND window = CreateWindowEx(
		0,
		window_class_name,
		"dbrl",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL,
		NULL,
		instance,
		NULL);
	
	if (window == NULL) {
		return 0;
	}

	if (!load_game_functions()) {
		return 0;
	}

	// TODO - should probably only show window _after_ initialisation of graphics API
	// stuff...
	ShowWindow(window, cmd_show);

	ID3D11Device        *device;
	ID3D11DeviceContext *context;

	D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };

	HRESULT hr = D3D11CreateDevice(
		NULL,
		D3D_DRIVER_TYPE_HARDWARE,
		NULL,
		D3D11_CREATE_DEVICE_DEBUG,
		feature_levels,
		ARRAY_SIZE(feature_levels),
		D3D11_SDK_VERSION,
		&device,
		NULL,
		&context);

	if (FAILED(hr)) {
		return 0;
	}

	ID3D11InfoQueue *info_queue;
	hr = device->QueryInterface(IID_PPV_ARGS(&info_queue));

	if (FAILED(hr)) {
		return 0;
	}

	info_queue->SetMuteDebugOutput(FALSE);

	IDXGIDevice *dxgi_device;
	hr = device->QueryInterface(IID_PPV_ARGS(&dxgi_device));

	if (FAILED(hr)) {
		return 0;
	}

	IDXGIAdapter *dxgi_adapter;
	hr = dxgi_device->GetAdapter(&dxgi_adapter);

	if (FAILED(hr)) {
		return 0;
	}

	IDXGIFactory *dxgi_factory;
	hr = dxgi_adapter->GetParent(IID_PPV_ARGS(&dxgi_factory));

	if (FAILED(hr)) {
		return 0;
	}

	DXGI_SWAP_CHAIN_DESC swap_chain_desc = {};
	swap_chain_desc.BufferDesc.Width = 0;
	swap_chain_desc.BufferDesc.Height = 0;
	swap_chain_desc.BufferDesc.RefreshRate.Numerator = 60;
	swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
	swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swap_chain_desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swap_chain_desc.SampleDesc.Count = 1;
	swap_chain_desc.SampleDesc.Quality = 0;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.BufferCount = 2;
	swap_chain_desc.OutputWindow = window;
	swap_chain_desc.Windowed = TRUE;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	IDXGISwapChain *swap_chain;
	hr = dxgi_factory->CreateSwapChain(device, &swap_chain_desc, &swap_chain);

	if (FAILED(hr)) {
		return 0;
	}

	ID3D11Texture2D *back_buffer;
	hr = swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
	if (FAILED(hr)) {
		return 0;
	}

	D3D11_TEXTURE2D_DESC back_buffer_desc = {};
	back_buffer->GetDesc(&back_buffer_desc);

	D3D11_RENDER_TARGET_VIEW_DESC back_buffer_rtv_desc = {};
	back_buffer_rtv_desc.Format = back_buffer_desc.Format;
	back_buffer_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	back_buffer_rtv_desc.Texture2D.MipSlice = 0;

	ID3D11RenderTargetView *back_buffer_rtv;
	hr = device->CreateRenderTargetView(back_buffer, &back_buffer_rtv_desc, &back_buffer_rtv);
	if (FAILED(hr)) {
		return 0;
	}

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = (f32)back_buffer_desc.Width;
	viewport.Height = (f32)back_buffer_desc.Height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	IMGUI_Context imgui;
	IMGUI_D3D11_Context imgui_d3d11;
	if (!imgui_d3d11_init(&imgui_d3d11, device)) {
		show_debug_messages(window, info_queue);
		return 0;
	}

	for (u32 frame_number = 0; running; ++frame_number) {
		MSG msg = {};
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		DXGI_SWAP_CHAIN_DESC swap_chain_desc;
		hr = swap_chain->GetDesc(&swap_chain_desc);
		if (FAILED(hr)) {
			show_debug_messages(window, info_queue);
			return 0;
		}

		v2_u32 screen_size = {
			swap_chain_desc.BufferDesc.Width,
			swap_chain_desc.BufferDesc.Height
		};

		imgui_begin(&imgui);
		imgui_set_text_cursor(&imgui, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f });
		imgui_text(&imgui, "Hello, world!");

		context->RSSetViewports(1, &viewport);
		f32 clear_color[4] = { 0.2f, 0.4f, 0.2f, 1.0f };
		context->ClearRenderTargetView(back_buffer_rtv, clear_color);

		imgui_d3d11_draw(&imgui, &imgui_d3d11, context, back_buffer_rtv, screen_size);

		swap_chain->Present(1, 0);
	}

	return 0;
}
