#define JFG_HEADER_ONLY

#include "prelude.h"
#include "codepage_437.h"

#include <windows.h>

#include "jfg_dsound.h"

#include "jfg_d3d11.h"
#include "log.h"
#include "imgui.h"

#include <dxgi1_2.h>

#include <math.h>
#include <stdio.h>

#include "dbrl.h"

#include "draw_dx11.h"

u32 win32_try_read_file(char* filename, void* dest, u32 max_size)
{
	HANDLE file_handle = CreateFile(filename,
	                                GENERIC_READ,
	                                FILE_SHARE_READ,
	                                NULL,
	                                OPEN_EXISTING,
	                                FILE_ATTRIBUTE_NORMAL,
	                                NULL);
	if (file_handle == INVALID_HANDLE_VALUE) {
		return 0;
	}
	DWORD file_size = GetFileSize(file_handle, NULL);
	if (file_size > max_size) {
		BOOL close_succeeded = CloseHandle(file_handle);
		ASSERT(close_succeeded);
		return 0;
	}
	DWORD bytes_read = 0;
	BOOL read_succeeded = ReadFile(file_handle, dest, file_size, &bytes_read, NULL);
	ASSERT(read_succeeded);
	ASSERT(bytes_read == file_size);
	BOOL close_succeeded = CloseHandle(file_handle);
	ASSERT(close_succeeded);
	return bytes_read;
}

JFG_Error win32_try_write_file(const char* filename, void* src, size_t size)
{
	HANDLE file_handle = CreateFile(filename,
	                                GENERIC_WRITE,
	                                FILE_SHARE_WRITE,
	                                NULL,
	                                CREATE_ALWAYS,
	                                FILE_ATTRIBUTE_NORMAL,
	                                NULL);
	if (!file_handle) {
		jfg_set_error("Failed to create file \"%s\"", filename);
		return JFG_ERROR;
	}

	JFG_Error error = JFG_SUCCESS;

	DWORD bytes_written = 0;
	BOOL succeeded = WriteFile(file_handle, src, size, &bytes_written, NULL);
	if (!succeeded || bytes_written != size) {
		jfg_set_error("Failed to write file \"%s\"", filename);
		error = JFG_ERROR;
	}

	CloseHandle(file_handle);
	return JFG_SUCCESS;
}

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

struct Start_Thread_Aux_Args
{
	Thread_Function   thread_function;
	void             *thread_args;
	wchar_t           name[256];
};

DWORD __stdcall start_thread_aux(void* uncast_args)
{
	Start_Thread_Aux_Args *args = (Start_Thread_Aux_Args*)uncast_args;

	HRESULT hr = SetThreadDescription(GetCurrentThread(), args->name);
	if (FAILED(hr)) {
		u32 i = 1;
	}

	args->thread_function(args->thread_args);
	free(uncast_args);
	ExitThread(1);
	return 1;
}

void win32_start_thread(Thread_Function thread_function, const char* name, void* thread_args)
{
	Start_Thread_Aux_Args *args = (Start_Thread_Aux_Args*)malloc(sizeof(Start_Thread_Aux_Args));

	mbstowcs(args->name, name, ARRAY_SIZE(args->name));

	args->thread_function = thread_function;
	args->thread_args = thread_args;
	HANDLE thread = CreateThread(
		NULL,
		10 * 1024 * 1024,
		start_thread_aux,
		args,
		0,
		NULL);
}

void win32_sleep(u32 time_in_milliseconds)
{
	Sleep(time_in_milliseconds);
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
	auto input_buffer_pointer_lock_orig_value = InterlockedCompareExchange(&input_buffer_pointer_lock, 0, 1);
	ASSERT(input_buffer_pointer_lock_orig_value);
}

void input_push_event_to_back_buffer(Input_Event input_event)
{
	while (InterlockedCompareExchange(&input_buffer_pointer_lock, 1, 0)) ;
	input_push(input_back_buffer, input_event);
	auto input_buffer_pointer_lock_orig_value = InterlockedCompareExchange(&input_buffer_pointer_lock, 0, 1);
	ASSERT(input_buffer_pointer_lock_orig_value);
}

void input_back_buffer_button_down(Input_Button button)
{
	while (InterlockedCompareExchange(&input_buffer_pointer_lock, 1, 0)) ;
	input_button_down(input_back_buffer, button);
	auto input_buffer_pointer_lock_orig_value = InterlockedCompareExchange(&input_buffer_pointer_lock, 0, 1);
	ASSERT(input_buffer_pointer_lock_orig_value);
}

void input_back_buffer_button_up(Input_Button button)
{
	while (InterlockedCompareExchange(&input_buffer_pointer_lock, 1, 0)) ;
	input_button_up(input_back_buffer, button);
	auto input_buffer_pointer_lock_orig_value = InterlockedCompareExchange(&input_buffer_pointer_lock, 0, 1);
	ASSERT(input_buffer_pointer_lock_orig_value);
}

void input_text_input(char c)
{
	while (InterlockedCompareExchange(&input_buffer_pointer_lock, 1, 0)) ;
	input_back_buffer->text_input.append(c);
	auto input_buffer_pointer_lock_orig_value = InterlockedCompareExchange(&input_buffer_pointer_lock, 0, 1);
	ASSERT(input_buffer_pointer_lock_orig_value);
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
		auto input_buffer_pointer_lock_orig_value = InterlockedCompareExchange(&input_buffer_pointer_lock, 0, 1);
		ASSERT(input_buffer_pointer_lock_orig_value);
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
	case WM_KEYDOWN:
		switch (wparam) {
		case VK_F1: input_back_buffer_button_down(INPUT_BUTTON_F1); break;
		case VK_F2: input_back_buffer_button_down(INPUT_BUTTON_F2); break;
		case VK_F3: input_back_buffer_button_down(INPUT_BUTTON_F3); break;
		case VK_F5: input_back_buffer_button_down(INPUT_BUTTON_F5); break;
		case VK_UP: input_back_buffer_button_down(INPUT_BUTTON_UP); break;
		case VK_DOWN: input_back_buffer_button_down(INPUT_BUTTON_DOWN); break;
		case VK_LEFT: input_back_buffer_button_down(INPUT_BUTTON_LEFT); break;
		case VK_RIGHT: input_back_buffer_button_down(INPUT_BUTTON_RIGHT); break;
		}
		return 0;
	case WM_KEYUP:
		switch (wparam) {
		case VK_F1: input_back_buffer_button_up(INPUT_BUTTON_F1); break;
		case VK_F2: input_back_buffer_button_up(INPUT_BUTTON_F2); break;
		case VK_F3: input_back_buffer_button_up(INPUT_BUTTON_F3); break;
		case VK_F5: input_back_buffer_button_up(INPUT_BUTTON_F5); break;
		case VK_UP: input_back_buffer_button_up(INPUT_BUTTON_UP); break;
		case VK_DOWN: input_back_buffer_button_up(INPUT_BUTTON_DOWN); break;
		case VK_LEFT: input_back_buffer_button_up(INPUT_BUTTON_LEFT); break;
		case VK_RIGHT: input_back_buffer_button_up(INPUT_BUTTON_RIGHT); break;
		}
		return 0;
	case WM_CHAR: {
		if (wparam > 0xFF) {
			break;
		}
		char c = (char)wparam;
		input_text_input(c);
		return 0;
	}

	}
	return DefWindowProc(window, msg, wparam, lparam);
}

u8 was_library_written()
{
	return 0;
}

u8 load_game_functions()
{
	return 1;
}

struct Game_Loop_Args
{
	HWND window;
};

char d3d11_log_buffer[8096] = {};
Draw draw_data = {};
Render renderer = {};

// DWORD __stdcall game_loop(void *uncast_args)
void game_loop(void *uncast_args)
{
	Game_Loop_Args *args = (Game_Loop_Args*)uncast_args;

	HWND window = args->window;

	IMGUI_Context imgui;
	/*
	if (!imgui_d3d11_init(&imgui, device)) {
		show_debug_messages(window, info_queue);
		return 0;
	}
	*/

	Memory_Spec program_size = get_program_size();
	Program* program = (Program*)malloc(program_size.size);

	if (init(&renderer)) {
		MessageBox(window, jfg_get_error(), "DBRL", MB_OK);
		return;
	}

	Log d3d11_log = {};
	init(&d3d11_log, d3d11_log_buffer, ARRAY_SIZE(d3d11_log_buffer));
	d3d11_log.grid_size = v2_u32(80, 40);

	ASSERT(((uintptr_t)program & (program_size.alignment - 1)) == 0);

	v2_u32 screen_size, prev_screen_size;
	get_screen_size(window, &screen_size);
	prev_screen_size = screen_size;

	Platform_Functions platform_functions = {};
	platform_functions.start_thread = win32_start_thread;
	platform_functions.sleep = win32_sleep;
	platform_functions.try_read_file = win32_try_read_file;
	platform_functions.try_write_file = win32_try_write_file;

	start_thread = win32_start_thread;
	sleep = win32_sleep;
	try_read_file = win32_try_read_file;
	try_write_file = win32_try_write_file;

	program_init(program, &draw_data, &renderer, platform_functions);
	// u8 d3d11_init_success = program_d3d11_init(program, device, screen_size);

	DX11_Renderer dx11_renderer = {};	
	// TODO -- should probably post a quit message along with exiting!!!
	if (init(&dx11_renderer, &renderer, &d3d11_log, &draw_data, window) != JFG_SUCCESS) {
		MessageBox(window, d3d11_log.buffer, "DBRL", MB_OK);
		return;
	}

	load_textures(&renderer, &platform_functions);
	reload_textures(&dx11_renderer, &renderer);

	v2_u32 mouse_pos = { 0, 0 }, prev_mouse_pos = { 0, 0 };

	if (dsound_try_load() != JFG_SUCCESS) {
		MessageBox(window, jfg_get_error(), "DBRL", MB_OK);
		return;
	}

	// TODO -- enumerate devices/give a choice of sound devices
	IDirectSound *dsound = NULL;
	HRESULT hr = DirectSoundCreate(NULL, &dsound, NULL);
	if (FAILED(hr)) {
		return;
	}

	hr = dsound->SetCooperativeLevel(window, DSSCL_PRIORITY);
	if (FAILED(hr)) {
		// XXX -- amusing this is called "d3d11 log"
		log(&d3d11_log, "Failed to set direct sound priority");
	}

	// create sound buffer
	IDirectSoundBuffer *ds_primary_buffer = NULL;
	{
		DSBUFFERDESC desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DSBCAPS_PRIMARYBUFFER; // | DSBCAPS_GLOBALFOCUS;

		hr = dsound->CreateSoundBuffer(&desc, &ds_primary_buffer, NULL);
	}
	if (FAILED(hr)) {
		return;
	}

	WAVEFORMATEX waveformat = {};
	waveformat.wFormatTag = WAVE_FORMAT_PCM;
	waveformat.nChannels = 2;
	waveformat.nSamplesPerSec = 44100;
	waveformat.wBitsPerSample = 16;
	waveformat.nBlockAlign = waveformat.nChannels * waveformat.wBitsPerSample / 8;
	waveformat.nAvgBytesPerSec = waveformat.nSamplesPerSec * waveformat.nBlockAlign;
	waveformat.cbSize = 0;

	hr = ds_primary_buffer->SetFormat(&waveformat);
	if (FAILED(hr)) {
		log(&d3d11_log, "Failed to set direct sound primary buffer format.");
	}

	program_dsound_init(program, dsound);

	bool draw_debug_info = false;
	for (u32 frame_number = 1; running; ++frame_number) {

		get_screen_size(window, &screen_size);
		if (screen_size != prev_screen_size) {
			auto succeeded = set_screen_size(&dx11_renderer, screen_size);
			ASSERT(succeeded);
			prev_screen_size = screen_size;
		}

		swap_input_buffers();

		if (input_get_num_up_transitions(input_front_buffer, INPUT_BUTTON_F5)) {
			draw_debug_info = !draw_debug_info;
		}
		if (input_get_num_up_transitions(input_front_buffer, INPUT_BUTTON_F3)) {
			write_to_file(&d3d11_log, "d3d11_log.txt");
		}

		POINT point;
		if (GetCursorPos(&point) && ScreenToClient(window, &point)) {
			if (point.x < (i32)screen_size.x && point.y < (i32)screen_size.y) {
				mouse_pos = { (u32)point.x, (u32)point.y };
			} else {
				mouse_pos = prev_mouse_pos;
			}
		} else {
			mouse_pos = prev_mouse_pos;
		}
		input_front_buffer->mouse_pos = mouse_pos;
		input_front_buffer->mouse_delta = (v2_i32)mouse_pos - (v2_i32)prev_mouse_pos;
		prev_mouse_pos = mouse_pos;

		begin_frame(&dx11_renderer, &renderer);

		process_frame(program, input_front_buffer, screen_size);

		program_dsound_play(program);

		bool new_errors = check_errors(&dx11_renderer, frame_number);
		draw_debug_info |= new_errors;
		if (draw_debug_info) {
			imgui_begin(&imgui, input_front_buffer);
			imgui_set_text_cursor(&imgui, { 0.9f, 0.9f, 0.1f, 1.0f }, { 0.0f, 0.0f });
			imgui_text(&imgui, "Frame: %u", frame_number);
			imgui_text(&imgui, "Screen size: %u x %u", screen_size.w, screen_size.h);
			render(&imgui, &renderer);
			render(&d3d11_log, &renderer);
		}

		draw(&dx11_renderer, &draw_data, &renderer);
	}
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

	// XXX -- use this to force the old DLL to be deleted
	was_library_written();
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

	/*
	HANDLE game_thread = CreateThread(
		NULL,
		10 * 1024 * 1024,
		game_loop,
		&game_loop_args,
		0,
		NULL);
	*/
	win32_start_thread(game_loop, "game_loop", &game_loop_args);

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
