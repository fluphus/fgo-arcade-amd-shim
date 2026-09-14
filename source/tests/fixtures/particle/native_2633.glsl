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
layout(location = 0) uniform vec4 g_constants[4];
layout(binding = 0) uniform sampler3D g_noise_sampler;
#define g_origin g_constants[0].xyz
#define g_force_scale g_constants[0].w
#define g_velocity g_constants[1].xyz
#define g_force g_constants[1].w
#define g_attenuation g_constants[2].x
#define g_max_distance g_constants[2].y
#define g_curl_intensity g_constants[2].z
#define g_noise_origin g_constants[3].xyz
void main(){
	gl_Position = vec4(0.f, 0.f, 0.f, -1.f);//culling
	Particle p0 = g_particles[gl_VertexID];
	float elapsed_tick = p0.m_position.w;
	const float duration_tick = p0.m_velocity.w;
	if(duration_tick < elapsed_tick){
		return;
	}
	const float dt = 1.f;
	//const float dt = 0.5f;
	//const float dt = 0.25f;
	const float dtdt = dt * dt;
	elapsed_tick += dt;
	p0.m_position.w = elapsed_tick;
	if(duration_tick < elapsed_tick){//現フレームで死んだ。
		//自身のパーティクルのインデクスを回収。
		int idx = atomicAdd(g_unused0->m_count, 1);
		g_unused0->m_indices[idx] = gl_VertexID;
		g_particles[gl_VertexID] = p0;//elapsed_tickの書き出し。
		return;
	}
	//目標点を計算。
	float time = p0.m_attributes.x;
	vec3 target = calculate_position(g_origin, g_velocity, g_force, time);
//	time += dt * 0.0025f;//動かさない。
	float curl_intensity_offset = 0.f;
	float force_scale = g_force_scale;
	const float fade_in_duration = 8.f;
	if(1.f < time){//@todo	消滅させるべし
		time = 1.f;
		force_scale = 0.f;
		curl_intensity_offset = 0.01f;
		elapsed_tick += dt * 20.f;//急速に老化させる。
		elapsed_tick = min(elapsed_tick, duration_tick - 0.1f);//完全には死なせない。
	}else if(elapsed_tick < fade_in_duration){
		curl_intensity_offset = 0.05f * (1.f - (elapsed_tick / fade_in_duration));
	}
	p0.m_attributes.x = time;
	//力, 速度, 座標を計算。
	vec3 f;
	f = target - p0.m_position.xyz;
	f *= force_scale * 0.1f;
//f*=0.f;
	f += textureLod(g_noise_sampler, p0.m_position.xyz * 0.05f + g_noise_origin, 0.f).rgb * (g_curl_intensity + curl_intensity_offset) * 50.f;//test
	f *= dtdt;
//	f += p0.m_velocity.xyz * -g_attenuation*10;//空気抵抗。
	p0.m_velocity.xyz += f;
	p0.m_position.xyz += p0.m_velocity.xyz * dt;
//p0.m_position.xyz=target;
	{//targetからの距離を拘束。
		vec3 t = target - p0.m_position.xyz;
		float tt = dot(t, t);
		float r = g_max_distance * 0.5f;// * 4;
		float last_distance = 0.025f;
		if(1.f - last_distance < time){
			float p = time * (-1.f / last_distance) + 1.f / last_distance;//終点付近は距離を強く拘束。
			p = 1.f - p;
			p *= p;
			p = 1.f - p;
			r *= p;
		}
		float rr = r * r;
		if(rr < tt){
			p0.m_position.xyz = target - t * sqrt(rr / tt);//差分を速度に計上したほうが良い?
		}
	}
	{//ターゲットへの距離に応じて色付け。
		//ターゲットを線分とみなす。曲線との距離のほうが良いけど。。
		vec3 rd = calculate_direction(g_velocity, g_force, time);
		float dd = dot(rd, rd);
		if(0.f < dd){
			vec3 q;
			q = p0.m_position.xyz - target;
			float x = dot(q, rd) / sqrt(dd);
			float tt = dot(q, q);
			float ll = tt - x * x;
			if(0.f < ll){
				p0.m_attributes.y = sqrt(ll);
			}
		}
	}
	const float fade_duration = 15.f;
	const float life = duration_tick - elapsed_tick;
	const float opacity = min(1.f, min(elapsed_tick, life) / fade_duration);//出現, 消滅付近で透過しやすく。
	p0.m_attributes.z = opacity;
	g_particles[gl_VertexID] = p0;
}