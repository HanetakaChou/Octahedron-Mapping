#ifndef _CUBE_H_
#define _CUBE_H_ 1

#include <stdint.h>

static constexpr uint32_t const g_cube_vertex_count = 6U * 6U;

extern float const g_cube_vertex_postion[3U * g_cube_vertex_count];

extern float const g_cube_vertex_uv[2U * g_cube_vertex_count];

#endif