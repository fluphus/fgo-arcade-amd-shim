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
layout(location = 0) uniform vec4 g_constants[3];
#define g_origin g_constants[0].xyz
#define g_parent_count g_constants[0].w
#define g_velocity g_constants[1].xyz
#define g_force g_constants[1].w
#define g_first_parent_time g_constants[2].x
#define g_first_noise_index int(g_constants[2].y)
#define g_min_life g_constants[2].z
#define g_life_width g_constants[2].w
vec3 calculate_random3(const int random_index){
	vec3 v;
	v.x = m_randoms[(random_index + 0) % RANDOM_COUNT];
	v.y = m_randoms[(random_index + 1) % RANDOM_COUNT];
	v.z = m_randoms[(random_index + 2) % RANDOM_COUNT];
	v *= 2.f;
	v -= vec3(1.f);
	v *= 1.f / (sqrt(dot(v, v)) + 0.000001f);
	return v;
}
void main(){
	gl_Position = vec4(0.f, 0.f, 0.f, -1.f);//culling
	int cnt = atomicAdd(g_unused0->m_count, -1);
	if(cnt <= 0){//戻す。
		atomicAdd(g_unused0->m_count, 1);
	}else{//積む。
		uint pidx = g_unused0->m_indices[cnt - 1];//パーティクルのインデクスを消費。末尾から使う。
		Particle p;
		int ridx = ((gl_VertexID * 135) + g_first_noise_index) % RANDOM_COUNT;
		int parent_idx = int(m_randoms[(ridx + 8) % RANDOM_COUNT] * g_parent_count);
		parent_idx %= int(g_parent_count);
		float time = g_first_parent_time + (1.f / g_parent_count) * parent_idx;
		time += m_randoms[(ridx + 31) % RANDOM_COUNT] * (1.f / g_parent_count);
		time = fract(time);
		p.m_position.xyz = calculate_position(g_origin, g_velocity, g_force, time);
		p.m_position.xyz += calculate_random3(parent_idx + 13) * 1.5f;
		p.m_position.w = 0.f;//elapsed_tick
		p.m_velocity.xyz = vec3(0.f);//@todo	random;
		p.m_velocity.w = m_randoms[(ridx + 7) % RANDOM_COUNT] * g_life_width + g_min_life + 0.0001f;//duration_tick, 0<duration_tickを保証すること。+0.0001fはこれのため。
		p.m_attributes.x = time;
		p.m_attributes.y = 0.f;
		p.m_attributes.z = 0.f;//opacity
		p.m_attributes.w = 0.f;
		g_particles[pidx] = p;
	}
}
