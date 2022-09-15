#include "fov.h"

#include "stdafx.h"

static void render_aux(Field_Of_Vision* _fov, Render_Job_Buffer* render_buffer, FOV_State state)
{
	auto &fov = *_fov;
	for (u32 y = 1; y < 255; ++y) {
		for (u32 x = 1; x < 255; ++x) {
			bool tl = fov[Pos(x - 1, y - 1)] >= state;
			bool t  = fov[Pos(    x, y - 1)] >= state;
			bool tr = fov[Pos(x + 1, y - 1)] >= state;
			bool l  = fov[Pos(x - 1,     y)] >= state;
			bool c  = fov[Pos(    x,     y)] >= state;
			bool r  = fov[Pos(x + 1,     y)] >= state;
			bool bl = fov[Pos(x - 1, y + 1)] >= state;
			bool b  = fov[Pos(    x, y + 1)] >= state;
			bool br = fov[Pos(x + 1, y + 1)] >= state;
			u8 mask = 0;
			if (t)  { mask |= 0x01; }
			if (tr) { mask |= 0x02; }
			if (r)  { mask |= 0x04; }
			if (br) { mask |= 0x08; }
			if (b)  { mask |= 0x10; }
			if (bl) { mask |= 0x20; }
			if (l)  { mask |= 0x40; }
			if (tl) { mask |= 0x80; }
			if (c && mask == 0xFF) {
				Field_Of_Vision_Fill_Instance instance = {};
				instance.world_coords = v2((f32)x, (f32)y);
				push_fov_fill(render_buffer, instance);
			} else if (c) {
				Field_Of_Vision_Edge_Instance instance = {};
				instance.world_coords = v2((f32)x, (f32)y);
				instance.sprite_coords = v2((f32)(mask % 16), (f32)(15 - mask / 16));
				push_fov_edge(render_buffer, instance);
			}
		}
	}
}

void render(Field_Of_Vision* fov, Render* render)
{
	auto r = &render->render_job_buffer;

	Field_Of_Vision_Render_Constant_Buffer constants = {};
	constants.grid_size = v2(256.0f, 256.0f);
	constants.sprite_size = v2(24.0f, 24.0f);
	constants.edge_tex_size = 16.0f * v2(24.0f, 24.0f);
	constants.output_val = 1;

	begin(r, RENDER_EVENT_FOV_PRECOMPUTE);

	clear_uint(r, TARGET_TEXTURE_FOV_RENDER);

	begin_fov(r, TARGET_TEXTURE_FOV_RENDER, constants);
	render_aux(fov, r, FOV_PREV_SEEN);

	constants.output_val = 2;
	begin_fov(r, TARGET_TEXTURE_FOV_RENDER, constants);
	render_aux(fov, r, FOV_VISIBLE);

	end(r, RENDER_EVENT_FOV_PRECOMPUTE);
}

void update(Field_Of_Vision* _fov, Map_Cache_Bool* can_see)
{
	auto &fov = *_fov;
	for (u32 y = 0; y < 256; ++y) {
		for (u32 x = 0; x < 256; ++x) {
			Pos p = Pos(x, y);
			u64 can_see_now = can_see->get(p);
			if (can_see_now) {
				fov[p] = FOV_VISIBLE;
			} else if (fov[p] == FOV_VISIBLE) {
				fov[p] = FOV_PREV_SEEN;
			}
		}
	}
}

void calculate_fov(Map_Cache_Bool* fov, Map_Cache_Bool* visibility_grid, Pos vision_pos)
{
	const u8 is_wall            = 1 << 0;
	const u8 bevel_top_left     = 1 << 1;
	const u8 bevel_top_right    = 1 << 2;
	const u8 bevel_bottom_left  = 1 << 3;
	const u8 bevel_bottom_right = 1 << 4;
	Map_Cache<u8> map;
	memset(map.items, 0, sizeof(map.items));

	for (u8 y = 1; y < 255; ++y) {
		for (u8 x = 1; x < 255; ++x) {
			u64 is_opaque = visibility_grid->get(Pos(x, y));
			if (!is_opaque) {
				continue;
			}
			u8 center = is_wall;

			u64 above  = visibility_grid->get(Pos(    x, y - 1));
			u64 left   = visibility_grid->get(Pos(x - 1,     y));
			u64 right  = visibility_grid->get(Pos(x + 1,     y));
			u64 bottom = visibility_grid->get(Pos(    x, y + 1));

			if (!above  && !left)  { center |= bevel_top_left;     }
			if (!above  && !right) { center |= bevel_top_right;    }
			if (!bottom && !left)  { center |= bevel_bottom_left;  }
			if (!bottom && !right) { center |= bevel_bottom_right; }

			map[Pos(x, y)] = center;
		}
	}

	struct Sector
	{
		Rational start;
		Rational end;
	};

	fov->reset();
	fov->set(vision_pos);
	Max_Length_Array<Sector, 1024> sectors_1, sectors_2;
	Max_Length_Array<Sector, 1024> *sectors_front = &sectors_1, *sectors_back = &sectors_2;

	const i32 half_cell_size = 2;
	const i32 cell_margin = 1;
	const i32 cell_size = 2 * half_cell_size;
	const i32 cell_inner_size = cell_size - 2 * cell_margin;
	const Rational wall_see_low  = Rational::cancel(1, 6);
	const Rational wall_see_high = Rational::cancel(5, 6);

	for (u8 octant_id = 0; octant_id < 8; ++octant_id) {
		/*
		octants:

		\ 4  | 0  /
		5 \  |  / 1
		    \|/
		-----+-----
		    /|\
		7 /  |  \ 3
		/ 6  |  2 \
		*/

		sectors_front->reset();
		sectors_back->reset();
		{
			Sector s = {};
			s.start.numerator = 0;
			s.start.denominator = 1;
			s.end.numerator = 1;
			s.end.denominator = 1;
			sectors_front->append(s);
		}

		for (u16 y_iter = 0; ; ++y_iter) {
			auto& old_sectors = *sectors_front;
			auto& new_sectors = *sectors_back;
			new_sectors.reset();

			if (!old_sectors) {
				goto next_octant;
			}

			for (u32 i = 0; i < old_sectors.len; ++i) {
				Sector s = old_sectors[i];

				u16 x_start = ((y_iter * cell_size - cell_size / 2) * s.start.numerator
						+ (s.start.denominator * cell_size / 2))
					      / (s.start.denominator * cell_size);

				u16 x_end = ((y_iter * cell_size + cell_size / 2) * s.end.numerator
					      + (s.end.denominator * cell_size / 2) - 1)
					    / (cell_size * s.end.denominator);

				u8 prev_was_clear = 0;
				for (u16 x_iter = x_start; x_iter <= x_end; ++x_iter) {
					u8 btl, bbr;
					i32 x, y;
					switch (octant_id) {
					case 0:
						btl = bevel_top_left;
						bbr = bevel_bottom_right;
						x = (i32)vision_pos.x + (i32)x_iter;
						y = (i32)vision_pos.y - (i32)y_iter;
						break;
					case 1:
						btl = bevel_bottom_right;
						bbr = bevel_top_left;
						x = (i32)vision_pos.x + (i32)y_iter;
						y = (i32)vision_pos.y - (i32)x_iter;
						break;
					case 2:
						btl = bevel_bottom_left;
						bbr = bevel_top_right;
						x = (i32)vision_pos.x + (i32)x_iter;
						y = (i32)vision_pos.y + (i32)y_iter;
						break;
					case 3:
						btl = bevel_top_right;
						bbr = bevel_bottom_left;
						x = (i32)vision_pos.x + (i32)y_iter;
						y = (i32)vision_pos.y + (i32)x_iter;
						break;
					case 4:
						btl = bevel_top_right;
						bbr = bevel_bottom_left;
						x = (i32)vision_pos.x - (i32)x_iter;
						y = (i32)vision_pos.y - (i32)y_iter;
						break;
					case 5:
						btl = bevel_bottom_left;
						bbr = bevel_top_right;
						x = (i32)vision_pos.x - (i32)y_iter;
						y = (i32)vision_pos.y - (i32)x_iter;
						break;
					case 6:
						btl = bevel_bottom_right;
						bbr = bevel_top_left;
						x = (i32)vision_pos.x - (i32)x_iter;
						y = (i32)vision_pos.y + (i32)y_iter;
						break;
					case 7:
						btl = bevel_top_left;
						bbr = bevel_bottom_right;
						x = (i32)vision_pos.x - (i32)y_iter;
						y = (i32)vision_pos.y + (i32)x_iter;
						break;
					}
					if (!(0 < y && y < 255)) { goto next_octant; }
					if (!(0 < x && x < 255)) { x_end = x_iter; }
					Pos p = Pos((u8)x, (u8)y);
					u8 cell = map[p];
					if (cell & is_wall) {
						u8 horiz_visible = 0;
						Rational horiz_left_intersect = Rational::cancel(
							s.start.numerator * ((i32)y_iter * cell_size
							                      - half_cell_size)
							+ s.start.denominator * (half_cell_size
							                     - (i32)x_iter * cell_size),
							s.start.denominator * cell_size
						);
						Rational horiz_right_intersect = Rational::cancel(
							s.end.numerator * ((i32)y_iter * cell_size
									      - half_cell_size)
							+ s.end.denominator * (half_cell_size
									       - (i32)x_iter * cell_size),
							s.end.denominator * cell_size
						);
						Rational vert_left_intersect = Rational::cancel(
							s.start.denominator * (2 * x_iter - 1)
							+ s.start.numerator * (1 - 2 * y_iter),
							2 * s.start.numerator
						);
						Rational vert_right_intersect = Rational::cancel(
							s.end.denominator * (2 * x_iter - 1)
							+ s.end.numerator * (1 - 2 * y_iter),
							2 * s.end.numerator
						);
						u8 vert_visible = 0;

						if (horiz_left_intersect  <= wall_see_high
						 && horiz_right_intersect >= wall_see_low) {
							fov->set(p);
						} else if (prev_was_clear
							&& vert_left_intersect  >= wall_see_low
							&& vert_right_intersect <= wall_see_high) {
							fov->set(p);
						}

						Rational left_slope, right_slope;
						if (cell & btl) {
							left_slope = Rational::cancel(
								(i32)x_iter * cell_size - cell_size / 2,
								(i32)y_iter * cell_size
							);
						} else {
							left_slope = Rational::cancel(
								(i32)x_iter * cell_size - cell_size / 2,
								(i32)y_iter * cell_size + cell_size / 2
							);
						}
						if (cell & bbr) {
							right_slope = Rational::cancel(
								(i32)x_iter * cell_size + cell_size / 2,
								(i32)y_iter * cell_size
							);
						} else {
							right_slope = Rational::cancel(
								(i32)x_iter * cell_size + cell_size / 2,
								(i32)y_iter * cell_size - cell_size / 2
							);
						}

						if (prev_was_clear) {
							Sector new_sector = s;
							if (left_slope < new_sector.end) {
								new_sector.end = left_slope;
							}
							if (new_sector.end > new_sector.start) {
								new_sectors.append(new_sector);
							}
							prev_was_clear = 0;
						}
						s.start = right_slope;
					} else {
						// Sector s = old_sectors[cur_sector];
						Rational left_slope = Rational::cancel(
							(i32)x_iter * cell_size + cell_margin - cell_size / 2,
							(i32)y_iter * cell_size - cell_margin + cell_size / 2
						);
						Rational right_slope = Rational::cancel(
							(i32)x_iter * cell_size - cell_margin + cell_size / 2,
							(i32)y_iter * cell_size + cell_margin - cell_size / 2
						);
						if (right_slope > s.start && left_slope < s.end) {
							fov->set(p);
						}
						prev_was_clear = 1;
					}
				}
				if (prev_was_clear) {
					if (s.end > s.start) {
						new_sectors.append(s);
					}
				}
			}

			// swap sector buffers
			{
				auto tmp = sectors_front;
				sectors_front = sectors_back;
				sectors_back = tmp;
			}
		}

	next_octant: ;
	}
}
