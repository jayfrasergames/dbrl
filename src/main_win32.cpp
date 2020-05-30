#include "jfg/prelude.h"
#include "jfg/codepage_437.h"

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include "jfg/jfg_d3d11.h"
#include "jfg/log.h"
#include "jfg/imgui.h"

#include <dxgi1_2.h>

#include <math.h>
#include <stdio.h>

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
		ASSERT(SUCCEEDED(hr));
		D3D11_MESSAGE *message = (D3D11_MESSAGE*)malloc(message_len);
		hr = info_queue->GetMessage(i, message, &message_len);
		ASSERT(SUCCEEDED(hr));
		MessageBox(window, message->pDescription, "DBRL", MB_OK);
		free(message);
	}
}

static u8 running = 1;
static Input input_buffers[2];
static Input *input_front_buffer, *input_back_buffer;
static volatile LONG input_buffer_pointer_lock;

void swap_input_buffers()
{
	while (InterlockedCompareExchange(&input_buffer_pointer_lock, 1, 0)) ;
	Input *tmp = input_front_buffer;
	input_front_buffer = input_back_buffer;
	input_back_buffer = tmp;
	memcpy(input_back_buffer, input_front_buffer, sizeof(*input_front_buffer));
	input_reset(input_back_buffer);
	ASSERT(InterlockedCompareExchange(&input_buffer_pointer_lock, 0, 1));
}

void input_push_event_to_back_buffer(Input_Event input_event)
{
	while (InterlockedCompareExchange(&input_buffer_pointer_lock, 1, 0)) ;
	input_push(input_back_buffer, input_event);
	ASSERT(InterlockedCompareExchange(&input_buffer_pointer_lock, 0, 1));
}

void input_back_buffer_button_down(Input_Button button)
{
	while (InterlockedCompareExchange(&input_buffer_pointer_lock, 1, 0)) ;
	input_button_down(input_back_buffer, button);
	ASSERT(InterlockedCompareExchange(&input_buffer_pointer_lock, 0, 1));
}

void input_back_buffer_button_up(Input_Button button)
{
	while (InterlockedCompareExchange(&input_buffer_pointer_lock, 1, 0)) ;
	input_button_up(input_back_buffer, button);
	ASSERT(InterlockedCompareExchange(&input_buffer_pointer_lock, 0, 1));
}

void get_screen_size(HWND window, v2_u32* screen_size)
{
	RECT rect;
	GetClientRect(window, &rect);
	screen_size->w = rect.right;
	screen_size->h = rect.bottom;
}

static v2_i16 prev_mouse_pos;
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
	case WM_MOUSEWHEEL: {
		i32 delta = GET_WHEEL_DELTA_WPARAM(wparam);
		while (InterlockedCompareExchange(&input_buffer_pointer_lock, 1, 0)) ;
		if (delta > 0) {
			++input_back_buffer->mouse_wheel_delta;
		} else {
			--input_back_buffer->mouse_wheel_delta;
		}
		ASSERT(InterlockedCompareExchange(&input_buffer_pointer_lock, 0, 1));
		return 0;
	}
	case WM_LBUTTONDOWN:
		input_back_buffer_button_down(INPUT_BUTTON_MOUSE_LEFT);
		return 0;
	case WM_MBUTTONDOWN:
		input_back_buffer_button_down(INPUT_BUTTON_MOUSE_MIDDLE);
		return 0;
	case WM_RBUTTONDOWN:
		input_back_buffer_button_down(INPUT_BUTTON_MOUSE_RIGHT);
		return 0;
	case WM_LBUTTONUP:
		input_back_buffer_button_up(INPUT_BUTTON_MOUSE_LEFT);
		return 0;
	case WM_MBUTTONUP:
		input_back_buffer_button_up(INPUT_BUTTON_MOUSE_MIDDLE);
		return 0;
	case WM_RBUTTONUP:
		input_back_buffer_button_up(INPUT_BUTTON_MOUSE_RIGHT);
		return 0;
	}
	return DefWindowProc(window, msg, wparam, lparam);
}

#ifdef DEBUG

#define LIBRARY_NAME "dbrl_d.dll"
#define LIBRARY_TMP_NAME "dbrl_d_tmp.dll"
#define SHARED_OBJECT_WRITTEN_NAME "written_library"
HMODULE game_library;

#define GAME_FUNCTION(return_type, name, ...) static return_type (*name)(__VA_ARGS__) = NULL;
GAME_FUNCTIONS
#undef GAME_FUNCTION

u8 was_library_written()
{
	u8 result;
	WIN32_FIND_DATA file_data;
	HANDLE handle = FindFirstFile(SHARED_OBJECT_WRITTEN_NAME, &file_data);
	result = handle != INVALID_HANDLE_VALUE;
	if (result) {
		DeleteFile(SHARED_OBJECT_WRITTEN_NAME);
	}
	return result;
}

u8 load_game_functions()
{
	WIN32_FIND_DATA file_data;
	HANDLE handle = FindFirstFile(LIBRARY_TMP_NAME, &file_data);
	if (handle != INVALID_HANDLE_VALUE) {
		if (!DeleteFile(LIBRARY_TMP_NAME)) {
			return 0;
		}
	}

	if (!CopyFile(LIBRARY_NAME, LIBRARY_TMP_NAME, FALSE)) {
		return 0;
	}

	game_library = LoadLibrary(LIBRARY_TMP_NAME);
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

struct Game_Loop_Args
{
	HWND window;
};

DWORD __stdcall game_loop(void *uncast_args)
{
	Game_Loop_Args *args = (Game_Loop_Args*)uncast_args;

	HWND window = args->window;

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
	if (!imgui_d3d11_init(&imgui, device)) {
		show_debug_messages(window, info_queue);
		return 0;
	}

	Memory_Spec program_size = get_program_size();
	Program* program = (Program*)malloc(program_size.size);

	ASSERT(((uintptr_t)program & (program_size.alignment - 1)) == 0);

	v2_u32 screen_size, prev_screen_size;
	get_screen_size(window, &screen_size);
	prev_screen_size = screen_size;

	program_init(program);
	u8 d3d11_init_success = program_d3d11_init(program, device, screen_size);

	v2_u32 back_buffer_size;
	v2_u32 mouse_pos = { 0, 0 }, prev_mouse_pos = { 0, 0 };

	Log d3d11_log = {};

	for (u32 frame_number = 0; running; ++frame_number) {
		if (was_library_written()) {
			if (game_library != NULL) {
				program_d3d11_free(program);
				FreeLibrary(game_library);
			}
			load_game_functions();
			if (game_library != NULL) {
				d3d11_init_success = program_d3d11_init(program, device, screen_size);
			} else {
				d3d11_init_success = 0;
			}
		}

		get_screen_size(window, &screen_size);

		if (screen_size.w != prev_screen_size.w || screen_size.h != prev_screen_size.h) {
			back_buffer_rtv->Release();
			back_buffer->Release();
			hr = swap_chain->ResizeBuffers(2, screen_size.w, screen_size.h,
				DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
			ASSERT(SUCCEEDED(hr));
			hr = swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
			ASSERT(SUCCEEDED(hr));
			back_buffer->GetDesc(&back_buffer_desc);
			back_buffer_rtv_desc.Format = back_buffer_desc.Format;
			hr = device->CreateRenderTargetView(back_buffer, &back_buffer_rtv_desc,
				&back_buffer_rtv);
			prev_screen_size = screen_size;
			back_buffer_size.w = back_buffer_desc.Width;
			back_buffer_size.h = back_buffer_desc.Height;
		}

		swap_input_buffers();

		POINT point;
		if (GetCursorPos(&point) && ScreenToClient(window, &point)) {
			if (point.x < screen_size.x && point.y < screen_size.y) {
				mouse_pos = { (u32)point.x, (u32)point.y };
			} else {
				mouse_pos = prev_mouse_pos;
			}
		} else {
			mouse_pos = prev_mouse_pos;
		}
		input_front_buffer->mouse_pos = mouse_pos;
		input_front_buffer->mouse_delta = {
			(i32)mouse_pos.x - (i32)prev_mouse_pos.x,
			(i32)mouse_pos.y - (i32)prev_mouse_pos.y
		};
		prev_mouse_pos = mouse_pos;

		if (game_library != NULL) {
			process_frame(program, input_front_buffer, screen_size);
		}

		imgui_begin(&imgui);
		imgui_set_text_cursor(&imgui, { 0.9f, 0.9f, 0.1f, 1.0f }, { 0.0f, 0.0f });
		imgui_text(&imgui, "Hello, world!");
		char buffer[1024];
		snprintf(buffer, ARRAY_SIZE(buffer), "Frame: %u", frame_number);
		imgui_text(&imgui, buffer);
		snprintf(buffer, ARRAY_SIZE(buffer), "Screen size: %u x %u",
			screen_size.w, screen_size.h);
		imgui_text(&imgui, buffer);

		viewport.Width = (f32)screen_size.w;
		viewport.Height = (f32)screen_size.h;
		context->RSSetViewports(1, &viewport);
		f32 clear_color[4] = { 0.1f, 0.1f, 0.3f, 1.0f };
		context->ClearRenderTargetView(back_buffer_rtv, clear_color);

		if (d3d11_init_success) {
			if (!program_d3d11_set_screen_size(program, device, screen_size)) {
				imgui_text(&imgui, "Failed to set screen size.");
			}
			render_d3d11(program, context, back_buffer_rtv);
		}

		// render any d3d11 messages we may have
		u64 num_messages = info_queue->GetNumStoredMessagesAllowedByRetrievalFilter();
		if (num_messages) {
			char buffer[1024] = {};
			snprintf(buffer,
			         ARRAY_SIZE(buffer),
			         "Frame %u: There are %llu D3D11 messages",
			         frame_number,
			         num_messages);
			// imgui_text(&imgui, buffer);
			log(&d3d11_log, buffer);
			for (u64 i = 0; i < num_messages; ++i) {
				size_t message_len;
				HRESULT hr = info_queue->GetMessage(i, NULL, &message_len);
				ASSERT(SUCCEEDED(hr));
				D3D11_MESSAGE *message = (D3D11_MESSAGE*)malloc(message_len);
				hr = info_queue->GetMessage(i, message, &message_len);
				ASSERT(SUCCEEDED(hr));
				// imgui_text(&imgui, (char*)message->pDescription);
				log(&d3d11_log, (char*)message->pDescription);
				free(message);
			}
			info_queue->ClearStoredMessages();
		}

		// draw log
		{
			u32 start = 0;
			u32 end = d3d11_log.cur_line;
			if (end > LOG_MAX_LINES) {
				start = end - LOG_MAX_LINES;
			}

			for (u32 i = start; i < end; ++i) {
				imgui_text(&imgui, log_get_line(&d3d11_log, i));
			}
		}

		imgui_d3d11_draw(&imgui, context, back_buffer_rtv, screen_size);

		swap_chain->Present(1, 0);
	}
	return 1;
}

INT WINAPI WinMain(HINSTANCE instance, HINSTANCE prev_instance, PSTR cmd_line, INT cmd_show)
{
	if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) {
		return 0;
	}

	// Set current directory
	{
		char file_name_buffer[1024];
		char directory_buffer[1024];
		DWORD file_name_len = GetModuleFileName(NULL,
		                                        file_name_buffer,
		                                        ARRAY_SIZE(file_name_buffer));
		if (file_name_len) {
			
			DWORD directory_buffer_len = GetFullPathName(file_name_buffer,
			                                            ARRAY_SIZE(directory_buffer),
			                                            directory_buffer,
			                                            NULL);
			if (directory_buffer_len) {
				// XXX
				u32 idx = directory_buffer_len - 1;
				while (directory_buffer[idx] != '\\') {
					--idx;
				}
				directory_buffer[idx] = '\0';
				if (!SetCurrentDirectory(directory_buffer)) {
					return 0;
				}
			}
		}
	}

	const char window_class_name[] = "dbrl_window_class";

	WNDCLASS window_class = {};

	window_class.lpfnWndProc = window_proc;
	window_class.hInstance = instance;
	window_class.lpszClassName = window_class_name;
	window_class.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	window_class.hCursor = LoadCursor(NULL, IDC_ARROW);

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

	Game_Loop_Args game_loop_args = {};
	game_loop_args.window = window;

	input_buffer_pointer_lock = 0;
	input_front_buffer = &input_buffers[0];
	input_back_buffer  = &input_buffers[1];

	HANDLE game_thread = CreateThread(
		NULL,
		10 * 1024 * 1024,
		game_loop,
		&game_loop_args,
		0,
		NULL);

	// windows message loop
	while (running) {
		MSG msg = {};
		while (GetMessage(&msg, NULL, 0, 0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return 0;
}
