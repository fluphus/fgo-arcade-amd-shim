#version 450
#extension GL_ARB_bindless_texture : enable
#extension GL_NV_gpu_shader5 : enable
struct Particle{
	vec4 m_position;//xyz=position, w=elapsed_tick
	vec4 m_velocity;//xyz=velocity, w(uint32_t)=(parent_particle_index<<16)|child_particle_count
	vec4 m_attributes;//x=size, y=duration_tick, z=flags, w=alpha_threshold
	vec4 m_scale;//x=scale, y=(hx=initial_rotate_z_rad, hy=rotate_z_curve_scale), z=(hx=rotate_z, hy=unused), w=(hx=texcoord_scale, hy=texcoord_offset)
	vec4 m_color0;
	vec4 m_color;
};
struct Unused{
	int* m_count;
	uint* m_indices;
};
#define ParticleFlag_Retiring (1 << 0)//子パーティクル回収中。
#define g_particle_size_in_u32 (4 * 6)
#define g_offset_to_velocity_w_in_u32 (4 + 3)
float calculate_packed_rotation_z0(
	 float rotation_z_min_rad,
	 float rotation_z_size_rad,
	 float rotate_z_curve_scale_min,
	 float rotate_z_curve_scale_size,
	 vec2 random,
	 bool binarized_initial_rotate_z){
	float initial_rotate_z_rad = rotation_z_min_rad;
	if(binarized_initial_rotate_z){
		if(random.x < 0.5f){//ランダムで二値に。
			initial_rotate_z_rad += rotation_z_size_rad;
		}
	}else{
		initial_rotate_z_rad += rotation_z_size_rad * random.x;
	}
	const float rotate_z_curve_scale = rotate_z_curve_scale_min + rotate_z_curve_scale_size * random.y;
	uint packed_rotate_z = packHalf2x16(vec2(initial_rotate_z_rad, rotate_z_curve_scale));
	return uintBitsToFloat(packed_rotate_z);
}
#define UVTypeA_Offset 0
#define UVTypeA_Pattern 1
float calculate_packed_texcoord0(
	 vec2 uv_offset_min,
	 vec2 uv_offset_size,
	 vec2 random,//[0,1]
	 uint uv_type_a){
	vec2 offs;
	if(UVTypeA_Offset == uv_type_a){
		offs = uv_offset_size * random + uv_offset_min;
	}else if(UVTypeA_Pattern == uv_type_a){
		offs.x = uv_offset_size.x * random.x;
		offs.y = uv_offset_min.x;
		offs = floor(offs);
	}else{
		offs = vec2(0.f, 0.f);
	}
	return uintBitsToFloat(packHalf2x16(offs));
}
#define RANDOM_COUNT 1024
#define BitIndexInType_BinarizedInitialRotateZ 5
#define BitIndexInType_UVTypeA 6
#define BitMaskInType_BinarizedInitialRotateZ 1
#define BitMaskInType_UVTypeA 3
#define COMPOSITE_VOLUME_FOG 0
#define BLUR_BY_VELOCITY 01
layout(location = 0) uniform vec4 g_constants[13];
#define g_projection_row0 g_constants[0]
#define g_projection_row1 g_constants[1]
#define g_projection_row2 g_constants[2]
#define g_projection_row3 g_constants[3]
#define g_view_row0 g_constants[4]
#define g_view_row1 g_constants[5]
#define g_view_row2 g_constants[6]
#define g_extend_scale_by_velocity g_constants[7].x
#define g_z_offset g_constants[7].y
#define g_depth_modifier g_constants[7].z//(n+f)/(n-f)
#define g_depth_near g_constants[7].w
#define g_draw_flags g_constants[8].x
#define g_uv_pattern_scale_per_tick g_constants[8].y//g_uv_pattern_scale/g_uv_pattern_cycle_tick
#define g_used_pattern_count g_constants[8].z
#define g_pattern_horizontal_count g_constants[8].w
#define g_local_uv_modifiers g_constants[9]
#define g_uv_offset_speed g_constants[10].xy
#define g_uv_pattern_size g_constants[10].zw
#define g_alpha_scale g_constants[11].x
#define g_alpha_offset g_constants[11].y
#define g_camera_fading_scale g_constants[11].z
#define g_camera_fading_offset g_constants[11].w
#define g_disslove_min_y g_constants[12].x
#define g_disslove_inv_length g_constants[12].y
#define DrawFlag_SoftParticle (1 << 0)
#define DrawFlag_MultiplyScale (1 << 1)
#define DrawFlag_Loop (1 << 2)
#define DrawFlag_UVAnimation (1 << 3)
layout(std140, binding = 0) uniform PerEmitter{
	Particle* g_particles;
	Unused* g_unused0;
	Unused* g_unused1;
	float* m_randoms;
};
out VertexData{
	vec4 color;
	vec4 texcoord;//xy=uv, z=depth, w=alpha_threshold
#if COMPOSITE_VOLUME_FOG
	vec4 world;//w=unused
#endif//COMPOSITE_VOLUME_FOG
}result;
vec2 calculate_texcoord_offset(
	 float tick,
	 float duration_tick,
	 vec2 uv_offset0_per_particle,
	 uint draw_flags){
	if(0 == (DrawFlag_UVAnimation & draw_flags)){
		return vec2(0.f, 0.f);
	}else{
		vec2 uv_offset = g_uv_offset_speed * tick + fract(uv_offset0_per_particle + vec2(1.f, 1.f));//pattern切り替えの時は、uv_offset0_per_particleは整数になっている。
		ivec2 i2_uvoffset = ivec2(uv_offset0_per_particle);
		int pattern_index = int(tick * g_uv_pattern_scale_per_tick) + i2_uvoffset.x;
		/*if(0 == (DrawFlag_Loop & draw_flags)){//pattern_indexをクランプ。//only for blend
			int max_pattern_index = int(duration_tick * g_uv_pattern_scale_per_tick);
			pattern_index = min(pattern_index, max_pattern_index);
		}//*/
		pattern_index = pattern_index % int(g_used_pattern_count) + i2_uvoffset.y;
		int hc = int(g_pattern_horizontal_count);
		ivec2 anim_pos = ivec2(pattern_index % hc, pattern_index / hc);
		vec2 r = vec2(anim_pos.x * g_uv_pattern_size.x, (anim_pos.y + 1.f) * g_uv_pattern_size.y);//+1はv反転用に縦方向に1パターンずらすため(左上原点を左下原点に)。
		r += uv_offset;
		r.y *= -1.f;
		r.y += 1.f;
		return r;
	}
}
float calculate_camera_fading( float view_depth){
	return (0.f < g_camera_fading_offset) ? 1.f : clamp(view_depth * g_camera_fading_scale + g_camera_fading_offset, 0.f, 1.f);
}
float calculate_opacity_by_dissolve( float y_in_world){
	return (0.f < abs(g_disslove_inv_length)) ? clamp((y_in_world - g_disslove_min_y) * g_disslove_inv_length, 0.f, 1.f) : 1.f;
}
void main(){
	const uint pidx = gl_VertexID / 6;
	const Particle ptc = g_particles[pidx];
	const uint particle_flags = floatBitsToUint(ptc.m_attributes.z);
	const float elapsed_tick = ptc.m_position.w;
	if(	(ptc.m_attributes.y <= elapsed_tick) ||
		(0 != (ParticleFlag_Retiring & particle_flags))){
		gl_Position = vec4(0.f, 0.f, 0.f, -1.f);//culling
		return;
	}
	const uint vidx = gl_VertexID % 6;
	const float billboard_size = 0.05f;//これがDiPのデフォルトサイズ。//DiPEdit上のパラメータは無視しておく。
	const uint draw_flags = floatBitsToUint(g_draw_flags);
	vec2 pos_offset;
	pos_offset.x = (((vidx >> 1) | ((vidx >> 2) & vidx)) & 1);
	pos_offset.y = (((vidx >> 1) | ~((vidx >> 2) | vidx)) & 1);
	result.texcoord.xy = pos_offset * g_local_uv_modifiers.xy + g_local_uv_modifiers.zw;
	result.texcoord.xy += calculate_texcoord_offset(elapsed_tick, ptc.m_attributes.y, unpackHalf2x16(floatBitsToUint(ptc.m_scale.w)), draw_flags);
	pos_offset = pos_offset * 2.f - vec2(1.f);
	if(0 == (DrawFlag_MultiplyScale & draw_flags)){
		pos_offset *= billboard_size * ptc.m_attributes.x + ptc.m_scale.x;//使うのか?
	}else{
		pos_offset *= billboard_size * ptc.m_attributes.x * ptc.m_scale.x;
	}
	{//回転。
		vec2 packed_rotate_z = unpackHalf2x16(floatBitsToUint(ptc.m_scale.z));//@todo	パフォーマンス調査。
		float angle_rad = packed_rotate_z.x;
		float s = sin(angle_rad);
		float c = cos(angle_rad);
		vec2 r;
		r.x = dot(vec2(c, -s), pos_offset);
		r.y = dot(vec2(s, c), pos_offset);
		pos_offset = r;
	}
#if BLUR_BY_VELOCITY
	vec2 vv;
	vv.x = dot(g_view_row0.xyz, ptc.m_velocity.xyz);
	vv.y = dot(g_view_row1.xyz, ptc.m_velocity.xyz);
	float vvpo = dot(vv, pos_offset);
	if(vvpo < 0.f){
		vv *= g_extend_scale_by_velocity;
		const float ll = dot(vv, vv);
		const float l_max = 0.02f;
		if(l_max * l_max < ll){
			vv *= l_max / sqrt(ll);
		}
		pos_offset -= vv;
    }
#endif//BLUR_BY_VELOCITY
	vec4 p;
	p.xyz = ptc.m_position.xyz;
	p.w = 1.f;
	vec4 v;
	v.x = dot(g_view_row0, p);
	v.y = dot(g_view_row1, p);
	v.z = dot(g_view_row2, p);
	v.w = 1.f;
	v.xy += pos_offset;
	gl_Position.x = dot(g_projection_row0, v);
	gl_Position.y = dot(g_projection_row1, v);
	gl_Position.z = dot(g_projection_row2, v);
	gl_Position.w = dot(g_projection_row3, v);
	float view_depth;
	if((0.f != g_z_offset) && (0.f < gl_Position.w)){//z offset
		const float w = gl_Position.w;
		const float e = 0.001f;
		const float z_offset = min(g_z_offset, w - g_depth_near - e);//nearの手前にまでいかないように。
		gl_Position.xy *= (w - z_offset);
		gl_Position.z += z_offset * g_depth_modifier;
		gl_Position.w -= z_offset;//ココのために、(z_offset<gl_Position.w)でないとダメ。負だと向きが変わってしまう。
		view_depth = gl_Position.w;//wを掛ける前に保存。これが-view_z(=view_depth)。
		gl_Position.zw *= w;//ココのために、(0<gl_Position.w)でないとダメ。負だと向きが変わってしまう。
	}else{
		view_depth = gl_Position.w;//これが-view_z(=view_depth)。
	}
	result.texcoord.z = view_depth;
	result.texcoord.w = ptc.m_attributes.w;
	result.color = ptc.m_color;
#define MODIFY_ALPHA_BY_DEPTH 1
#if MODIFY_ALPHA_BY_DEPTH
	if((g_alpha_scale < 0.f) && (0.f < result.texcoord.z)){
		const float billboard_size_in_world = length(pos_offset);
		result.color.a *= clamp(1.f / result.texcoord.z * billboard_size_in_world * g_alpha_scale + g_alpha_offset, 0.f, 1.f);
	}
#endif//MODIFY_ALPHA_BY_DEPTH
	result.color.a *= calculate_camera_fading(gl_Position.w);
	result.color.a *= calculate_opacity_by_dissolve(p.y);
#if COMPOSITE_VOLUME_FOG
	result.world = vec4(p.xyz, 0.f);
#endif//COMPOSITE_VOLUME_FOG
}