#pragma once

#include "prelude.h"

#include "jfg_d3d11.h"

#include "field_of_vision_render.h"
#include "sprite_sheet.h"
#include "card_render.h"
#include "debug_draw_world.h"
#include "debug_line_draw.h"
#include "imgui.h"
#include "console.h"

struct Camera
{
	v2  world_center;
	v2  offset;
	f32 zoom;
};

struct Draw
{
	Camera camera;
	Field_Of_Vision_Render fov_render;
	Sprite_Sheet_Renderer renderer;
	Sprite_Sheet_Instances tiles;
	Sprite_Sheet_Instances creatures;
	Sprite_Sheet_Instances water_edges;
	Sprite_Sheet_Instances effects_24;
	Sprite_Sheet_Instances effects_32;
	Sprite_Sheet_Font_Instances boxy_bold;
	Card_Render card_render;
	Debug_Line card_debug_line;
	Debug_Draw_World debug_draw_world;
	IMGUI_Context imgui;
	Console console;
};