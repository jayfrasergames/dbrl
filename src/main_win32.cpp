#include "jfg/prelude.h"
#include "jfg/codepage_437.h"

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include "gen/cs_draw_red.data.h"
#include "gen/vs_text.data.h"
#include "gen/ps_text.data.h"

#include "gpu_data_types.h"

#include <stdio.h>
#include <assert.h>

#define MAX_TEXT_INSTANCES 4096
struct Text_Instance_Buffer
{
	u32 num_instances;
	VS_Text_Instance instances[MAX_TEXT_INSTANCES];
};
Text_Instance_Buffer text_instance_buffer;

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

LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
	case WM_DESTROY:
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

	D3D11_BUFFER_DESC text_instance_buffer_desc = {};
	text_instance_buffer_desc.ByteWidth = sizeof(text_instance_buffer.instances);
	text_instance_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
	text_instance_buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	text_instance_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	text_instance_buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	text_instance_buffer_desc.StructureByteStride = sizeof(VS_Text_Instance);

	ID3D11Buffer *gpu_text_instance_buffer;
	hr = device->CreateBuffer(&text_instance_buffer_desc, NULL, &gpu_text_instance_buffer);
	if (FAILED(hr)) {
		return 0;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC text_instance_buffer_srv_desc = {};
	text_instance_buffer_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
	text_instance_buffer_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	text_instance_buffer_srv_desc.Buffer.ElementOffset = 0;
	text_instance_buffer_srv_desc.Buffer.ElementWidth = MAX_TEXT_INSTANCES;

	ID3D11ShaderResourceView *text_instance_buffer_srv;
	hr = device->CreateShaderResourceView(gpu_text_instance_buffer,
		&text_instance_buffer_srv_desc, &text_instance_buffer_srv);
	if (FAILED(hr)) {
		return 0;
	}

	ID3D11VertexShader *vs_text;
	hr = device->CreateVertexShader(VS_TEXT, ARRAY_SIZE(VS_TEXT), NULL, &vs_text);
	if (FAILED(hr)) {
		return 0;
	}

	ID3D11PixelShader *ps_text;
	hr = device->CreatePixelShader(PS_TEXT, ARRAY_SIZE(PS_TEXT), NULL, &ps_text);
	if (FAILED(hr)) {
		return 0;
	}

	D3D11_BUFFER_DESC text_constant_buffer_desc = {};
	text_constant_buffer_desc.ByteWidth = sizeof(CB_Text);
	text_constant_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
	text_constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	text_constant_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	text_constant_buffer_desc.MiscFlags = 0;
	text_constant_buffer_desc.StructureByteStride = sizeof(CB_Text);

	ID3D11Buffer *gpu_text_constant_buffer;
	hr = device->CreateBuffer(&text_constant_buffer_desc, NULL, &gpu_text_constant_buffer);
	if (FAILED(hr)) {
		show_debug_messages(window, info_queue);
		return 0;
	}

	D3D11_RASTERIZER_DESC rasterizer_desc = {};
	rasterizer_desc.FillMode = D3D11_FILL_SOLID;
	rasterizer_desc.CullMode = D3D11_CULL_NONE;

	ID3D11RasterizerState *rasterizer_state;
	hr = device->CreateRasterizerState(&rasterizer_desc, &rasterizer_state);
	if (FAILED(hr)) {
		show_debug_messages(window, info_queue);
		return 0;
	}

	D3D11_TEXTURE2D_DESC text_texture_desc = {};
	text_texture_desc.Width = TEXTURE_CODEPAGE_437.width;
	text_texture_desc.Height = TEXTURE_CODEPAGE_437.height;
	text_texture_desc.MipLevels = 1;
	text_texture_desc.ArraySize = 1;
	text_texture_desc.Format = DXGI_FORMAT_R8_UNORM;
	text_texture_desc.SampleDesc.Count = 1;
	text_texture_desc.SampleDesc.Quality = 0;
	text_texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
	text_texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	text_texture_desc.CPUAccessFlags = 0;
	text_texture_desc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA text_texture_data_desc = {};
	text_texture_data_desc.pSysMem = TEXTURE_CODEPAGE_437.data;
	text_texture_data_desc.SysMemPitch = TEXTURE_CODEPAGE_437.width * sizeof(TEXTURE_CODEPAGE_437.data[0]);
	text_texture_data_desc.SysMemSlicePitch = 0;

	ID3D11Texture2D *text_texture;
	hr = device->CreateTexture2D(&text_texture_desc, &text_texture_data_desc, &text_texture);
	if (FAILED(hr)) {
		show_debug_messages(window, info_queue);
		return 0;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC text_texture_srv_desc = {};
	text_texture_srv_desc.Format = DXGI_FORMAT_R8_UNORM;
	text_texture_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	text_texture_srv_desc.Texture2D.MostDetailedMip = 0;
	text_texture_srv_desc.Texture2D.MipLevels = 1;

	ID3D11ShaderResourceView *text_texture_srv;
	hr = device->CreateShaderResourceView(text_texture, &text_texture_srv_desc, &text_texture_srv);
	if (FAILED(hr)) {
		show_debug_messages(window, info_queue);
		return 0;
	}


	text_instance_buffer.num_instances = 3;
	text_instance_buffer.instances[0].glyph = 'A';
	text_instance_buffer.instances[0].pos = { 0.0f, 0.0f };
	text_instance_buffer.instances[0].color = { 1.0f, 0.0f, 0.0f, 1.0f };

	text_instance_buffer.instances[1].glyph = 'B';
	text_instance_buffer.instances[1].pos = { 1.0f, 0.0f };
	text_instance_buffer.instances[1].color = { 0.0f, 1.0f, 0.0f, 1.0f };

	text_instance_buffer.instances[2].glyph = 'C';
	text_instance_buffer.instances[2].pos = { 0.0f, 1.0f };
	text_instance_buffer.instances[2].color = { 0.0f, 0.0f, 1.0f, 1.0f };

	MSG msg = {};
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		DXGI_SWAP_CHAIN_DESC swap_chain_desc;
		hr = swap_chain->GetDesc(&swap_chain_desc);
		if (FAILED(hr)) {
			show_debug_messages(window, info_queue);
			return 0;
		}

		CB_Text text_constant_buffer = {};
		text_constant_buffer.screen_size.w = (f32)swap_chain_desc.BufferDesc.Width;
		text_constant_buffer.screen_size.h = (f32)swap_chain_desc.BufferDesc.Height;
		text_constant_buffer.glyph_size.w = (f32)TEXTURE_CODEPAGE_437.glyph_width;
		text_constant_buffer.glyph_size.h = (f32)TEXTURE_CODEPAGE_437.glyph_height;
		text_constant_buffer.tex_size.w = (f32)TEXTURE_CODEPAGE_437.width;
		text_constant_buffer.tex_size.h = (f32)TEXTURE_CODEPAGE_437.height;
		text_constant_buffer.zoom = 5.0f;

		context->RSSetViewports(1, &viewport);
		f32 clear_color[4] = { 0.2f, 0.4f, 0.2f, 1.0f };
		context->ClearRenderTargetView(back_buffer_rtv, clear_color);

		D3D11_MAPPED_SUBRESOURCE mapped_buffer = {};

		hr = context->Map(gpu_text_instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
			&mapped_buffer);
		assert(SUCCEEDED(hr));
		memcpy(mapped_buffer.pData, text_instance_buffer.instances,
			text_instance_buffer.num_instances * sizeof(VS_Text_Instance));
		context->Unmap(gpu_text_instance_buffer, 0);

		hr = context->Map(gpu_text_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
			&mapped_buffer);
		assert(SUCCEEDED(hr));
		memcpy(mapped_buffer.pData, &text_constant_buffer,
			sizeof(text_constant_buffer));
		context->Unmap(gpu_text_constant_buffer, 0);


		context->IASetInputLayout(NULL);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		context->VSSetConstantBuffers(0, 1, &gpu_text_constant_buffer);
		context->VSSetShaderResources(0, 1, &text_instance_buffer_srv);
		context->VSSetShader(vs_text, NULL, 0);
		context->PSSetConstantBuffers(0, 1, &gpu_text_constant_buffer);
		context->PSSetShaderResources(0, 1, &text_texture_srv);
		context->PSSetShader(ps_text, NULL, 0);

		context->RSSetState(rasterizer_state);
		context->OMSetRenderTargets(1, &back_buffer_rtv, NULL);

		context->DrawInstanced(6, text_instance_buffer.num_instances, 0, 0);

		swap_chain->Present(1, 0);
	}

	return 0;
}
