#pragma once

#include "prelude.h"
#include "types.h"
#include "render.h"

enum FOV_State
{
	FOV_NEVER_SEEN,
	FOV_PREV_SEEN,
	FOV_VISIBLE,
};

typedef Map_Cache<FOV_State> Field_Of_Vision;

void calculate_fov(Map_Cache_Bool* result, Map_Cache_Bool* visibility_grid, Pos pos);
void update(Field_Of_Vision* fov, Map_Cache_Bool* can_see);

void render(Field_Of_Vision* fov, Render* render);