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

#include "forward_shading_pipeline_layout.sli"

pal_root_signature(forward_shading_root_signature_macro, forward_shading_root_signature_name)
pal_early_depth_stencil
pal_pixel_shader_parameter_begin(main)
pal_pixel_shader_parameter_in_position pal_pixel_shader_parameter_split
pal_pixel_shader_parameter_in(pal_float3, in_interpolated_position_world_space, 0) pal_pixel_shader_parameter_split
pal_pixel_shader_parameter_in(pal_float2, in_interpolated_uv, 1) pal_pixel_shader_parameter_split
pal_pixel_shader_parameter_out(pal_float4, out_color, 0)
pal_pixel_shader_parameter_end(main)
{
    // Light Information
    pal_float3 point_light_position = g_point_light_position_and_radius.xyz;
    pal_float point_light_radius = g_point_light_position_and_radius.w;

    // View Space
    pal_float3 position_view_space = in_interpolated_position_world_space - point_light_position;

    // Sphere Space
    pal_float3 position_sphere_space = normalize(position_view_space);

    // Clip Space
    pal_float2 position_clip_space_xy;
    pal_float position_clip_space_w;

    // Layer
    // z < 0 -> layer 0
    // z > 0 -> layer 1
    pal_int layer_index = (position_sphere_space.z < 0.0) ? 0 : 1;

    // Octahedron Mapping
    pal_float manhattan_norm = abs(position_sphere_space.x) + abs(position_sphere_space.y) + abs(position_sphere_space.z);
    position_clip_space_w = manhattan_norm;

    pal_float2 layer_position_clip_space_xy[2] = pal_array_constructor_begin(pal_float2, 2) 
        (manhattan_norm - abs(position_sphere_space.yx)) * pal_float2((position_sphere_space.x >= 0.0) ? 1.0 : -1.0, (position_sphere_space.y >= 0.0) ? 1.0 : -1.0), 
        position_sphere_space.xy
        pal_array_constructor_end;
    position_clip_space_xy = layer_position_clip_space_xy[layer_index];

    // NDC Space
    pal_float2 position_ndc_space_xy = position_clip_space_xy / position_clip_space_w;

    // UV
    pal_float2 uv = position_ndc_space_xy * pal_float2(0.5, 0.5) + pal_float2(0.5, 0.5);

    //
    pal_float closest_ratio = pal_sample_2d(g_point_light_shadow_texture[0], g_samplers[0], uv).r + g_point_light_shadow_bias;

    pal_float current_ratio = length(position_view_space) / point_light_radius;

    pal_float shadow = (closest_ratio >= current_ratio) ? 1.0 : -1.0;

    pal_float3 emissive_color = pal_sample_2d(g_material_textures[0], g_samplers[1], in_interpolated_uv).xyz;

    out_color = pal_float4(shadow * emissive_color, 1.0);
}