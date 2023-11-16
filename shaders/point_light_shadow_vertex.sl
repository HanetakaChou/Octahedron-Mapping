//
// Copyright (C) YuqiaoZhang(HanetakaChou)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include "point_light_shadow_pipeline_layout.sli"

pal_root_signature(point_light_shadow_root_signature_macro, point_light_shadow_root_signature_name)
pal_vertex_shader_parameter_begin(main)
pal_vertex_shader_parameter_in(pal_float3, in_position, 0) pal_vertex_shader_parameter_split
pal_vertex_shader_parameter_out_position pal_vertex_shader_parameter_split
pal_vertex_shader_parameter_out(pal_float, out_inverse_length_view_space, 0)
pal_vertex_shader_parameter_end(main)
{
    // Model Space
    pal_float3 position_model_space = in_position.xyz;

    // World Space
    pal_float3 position_world_space = (pal_mul(g_model_transform, pal_float4(position_model_space, 1.0))).xyz;

    // Light Information
    pal_float3 point_light_position = g_point_light_position_and_radius.xyz;
    pal_float point_light_radius = g_point_light_position_and_radius.w;

    // View Space
    pal_float3 position_view_space = position_world_space - point_light_position;

    // Sphere Space
    pal_float3 position_sphere_space = normalize(position_view_space);

    // Clip Space
    pal_float4 position_clip_space;

    // NOTE: We should always draw twice, otherwise rasterization may be NOT correct when the same triangle covers different octants.
    // z < 0 -> layer 0
    // z > 0 -> layer 1
    pal_int layer_index = 1;

    // Octahedron Mapping
    pal_float manhattan_norm = abs(position_sphere_space.x) + abs(position_sphere_space.y) + abs(position_sphere_space.z);
    position_clip_space.w = manhattan_norm;

    pal_float2 layer_position_clip_space_xy[2] = pal_array_constructor_begin(pal_float2, 2) 
        (manhattan_norm - pal_abs(position_sphere_space.yx)) * pal_float2((position_sphere_space.x >= 0.0) ? 1.0 : -1.0, (position_sphere_space.y >= 0.0) ? 1.0 : -1.0),
        position_sphere_space.xy 
        pal_array_constructor_end;
    position_clip_space.xy = layer_position_clip_space_xy[layer_index];

    // TODO: Vulkan Viewport Flip y

    // Pseudo Depth (For Cliping and Culling)
    pal_float layer_position_clip_space_z[2] = pal_array_constructor_begin(pal_float, 2) -position_sphere_space.z, position_sphere_space.z pal_array_constructor_end;
    position_clip_space.z = layer_position_clip_space_z[layer_index];

    // Perspective Correct Interpolation
    out_inverse_length_view_space = 1.0 / pal_length(position_view_space);

    pal_position = position_clip_space;
}
