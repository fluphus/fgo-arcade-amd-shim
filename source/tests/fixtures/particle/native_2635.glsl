#version 430
#extension GL_NV_gpu_shader5 : enable
#define PI 3.1415926535f
struct Particle{
	vec4 m_position;//xyz=position, w=elapsed_tick
	vec4 m_velocity;//xyz=velocity, w=duration_tick
	vec4 m_attributes;//x=time, y=distance, z=opacity, w=unused
};
struct Unused{
	int* m_count;
	uint* m_indices;
};
vec3 calculate_position(const vec3 origin, const vec3 velocity, const float force, const float time){//time=[0,1]
	vec3 p;
	p = origin + velocity * time;
	p.y += force * time * time * 0.5f;
	return p;
}
vec3 calculate_direction(const vec3 velocity, const float force, const float time){//time=[0,1]
	vec3 d;
	d = velocity;
	d.y += force * time;
	return d;
}
#define RANDOM_COUNT (1024)
layout(std140, binding = 0) uniform PerEmitter{
	Particle* g_particles;
	Unused* g_unused0;
	float* m_randoms;
};
layout(location = 0) uniform vec4 g_constants[11];
#define g_projection_row0 g_constants[0]
#define g_projection_row1 g_constants[1]
#define g_projection_row2 g_constants[2]
#define g_projection_row3 g_constants[3]
#define g_view_world_row0 g_constants[4]
#define g_view_world_row1 g_constants[5]
#define g_view_world_row2 g_constants[6]
#define g_base_size g_constants[7].x
#define g_size_depth_min g_constants[7].y
#define g_size_depth_max g_constants[7].z
#define g_tube_radius g_constants[7].w
#define g_color g_constants[8]
#define g_min_particle_index int(g_constants[9].x)
#define g_focus_sq_intensity g_constants[9].y
#define g_focus_time g_constants[9].z
#define g_focus_extra_size_scale g_constants[9].w
#define g_focus_extra_color_scale g_constants[10].x
out VertexData{
	vec4 color;
	vec4 texcoord;//xy=uv, zw=unused
}result;
void main(){
	const uint pidx = gl_VertexID / 6;
	const Particle p0 = g_particles[pidx];
	const float elapsed_tick = p0.m_position.w;
	const float duration_tick = p0.m_velocity.w;
	gl_Position = vec4(0.f, 0.f, 0.f, -1.f);//culling
	if(	(duration_tick < elapsed_tick) ||
		(int(pidx) < g_min_particle_index)){//密度を保つための間引き。
		return;
	}
	const uint vidx = gl_VertexID % 6;
	vec2 pos_offset;
	pos_offset.x = (((vidx >> 1) | ((vidx >> 2) & vidx)) & 1);
	pos_offset.y = (((vidx >> 1) | ~((vidx >> 2) | vidx)) & 1);
	result.texcoord.xy = pos_offset;
	result.texcoord.zw = vec2(0.f);
	pos_offset = pos_offset * 2.f - vec2(1.f);	
	vec4 p;
	p.xyz = p0.m_position.xyz;
	p.w = 1.f;
	vec4 v;
	v.x = dot(g_view_world_row0, p);
	v.y = dot(g_view_world_row1, p);
	v.z = dot(g_view_world_row2, p);
	float focus_scale = 0.f;
	if(0.f< g_focus_sq_intensity){
		const float time = p0.m_attributes.x;
		float t = time - g_focus_time;
		focus_scale = exp(-t * t * g_focus_sq_intensity);
	}
	float billboard_size = g_base_size * 0.5f;
	billboard_size += focus_scale * g_focus_extra_size_scale;//0.01f
	float d_min = g_size_depth_min;
	float d_max = g_size_depth_max;
	float d = -v.z;
	if(d < d_min){//ビルボードサイズをクランプ。
		billboard_size *= d / d_min;
	}else if(d_max < d){
		billboard_size *= d / d_max;
	}
	pos_offset *= billboard_size;
#if 0//blur by velocity
	vec2 vv;
	vv.x = dot(g_view_world_row0.xyz, p0.m_velocity.xyz);
	vv.y = dot(g_view_world_row1.xyz, p0.m_velocity.xyz);
	float vvpo = dot(vv, pos_offset);
	if(vvpo < 0.f){
#define g_extend_scale_by_velocity (10.5f);
		vv *= g_extend_scale_by_velocity;
		const float ll = dot(vv, vv);
		const float l_max = 0.02f;
		if(l_max * l_max < ll){
			vv *= l_max / sqrt(ll);
		}
		pos_offset -= vv;
    }
#endif
	v.w = 1.f;
	v.xy += pos_offset;
	gl_Position.x = dot(g_projection_row0, v);
	gl_Position.y = dot(g_projection_row1, v);
	gl_Position.z = dot(g_projection_row2, v);
	gl_Position.w = dot(g_projection_row3, v);
	//color
	float max_cd = g_tube_radius;//0.15f;
	float cd = 1.f - p0.m_attributes.y / max_cd;
	cd *= p0.m_attributes.z;
cd = p0.m_attributes.z;
	if(cd < 0.f){
		gl_Position = vec4(0.f, 0.f, 0.f, -1.f);//culling
		return;
	}
	result.color = g_color;
	result.color.a *= cd;
	result.color.rgb *= 1.f + focus_scale * g_focus_extra_color_scale;//4.f
}
