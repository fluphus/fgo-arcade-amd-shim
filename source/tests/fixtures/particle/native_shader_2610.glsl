#version 450
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
	const float rotation_z_min_rad,
	const float rotation_z_size_rad,
	const float rotate_z_curve_scale_min,
	const float rotate_z_curve_scale_size,
	const vec2 random,
	const bool binarized_initial_rotate_z){
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
#define UVTypeA_Offset (0)
#define UVTypeA_Pattern (1)
float calculate_packed_texcoord0(
	const vec2 uv_offset_min,
	const vec2 uv_offset_size,
	const vec2 random,//[0,1]
	const uint uv_type_a){
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
#define RANDOM_COUNT (1024)
#define BitIndexInType_BinarizedInitialRotateZ (5)
#define BitIndexInType_UVTypeA (6)
#define BitMaskInType_BinarizedInitialRotateZ (1)
#define BitMaskInType_UVTypeA (3)
layout(binding = 0) uniform sampler3D g_noise_sampler;
layout(binding = 1) uniform sampler2D g_linear_depth_sampler;
layout(binding = 2) uniform sampler2D g_curve_sampler;
layout(binding = 3) uniform sampler2D g_child_curve_sampler;
#define USE_CURL_NOISE (1)
layout(std140, binding = 0) uniform PerEmitter{
	Particle* g_particles;
	Unused* g_unused0;
	Unused* g_unused1;
	float* m_randoms;
};
#define UpdateFlag_EmitChild (1 << 0)
#define UpdateFlag_CurlNoise (1 << 1)
#define UpdateFlag_Vortex (1 << 2)
#define UpdateFlag_TestCollision (1 << 3)
#define UpdateFlag_AlphaThreshold (1 << 4)
#define UpdateFlag_Scale (1 << 5)
layout(location = 0) uniform vec4 g_constants[23];
#define g_noise_offset g_constants[0]//w=update_flags
#define g_transform_row0 g_constants[1]
#define g_transform_row1 g_constants[2]
#define g_transform_row2 g_constants[3]
#define g_transform_row3 g_constants[4]
#define g_view_row2 g_constants[5]
#define g_view_from_w_div_proj g_constants[6]
#define g_inv_view_row0 g_constants[7]
#define g_inv_view_row1 g_constants[8]
#define g_inv_view_row2 g_constants[9]
#define g_emit_radius_min g_constants[10].x
#define g_emit_radius_thickness g_constants[10].y
#define g_emit_dir_speed g_constants[10].z
#define g_tan_dir_speed g_constants[10].w
#define g_user_speed g_constants[11].xyz
#define g_parent_speed_ratio g_constants[11].w
#define g_speed_size g_constants[12].x
#define g_speed_min g_constants[12].y
#define g_color_anim_style (floatBitsToUint(g_constants[12].z) & 0x3)
#define g_child_life_tick g_constants[12].w
#define g_child_color0_size g_constants[13]
#define g_child_color0_min g_constants[14]
#define g_child_life_tick_size g_constants[15].x
#define g_child_emit_time_start_tick g_constants[15].y
#define g_child_emit_interval_tick g_constants[15].z
#define g_restitution_coef g_constants[15].w
#define g_child_scale_min g_constants[16].x
#define g_child_scale_size g_constants[16].y
#define g_velocity_attenuation_life_ratio g_constants[16].z
#define g_velocity_attenuation_ratio g_constants[16].w
#define g_global_accel g_constants[17].xyz
#define g_curl_velocity_scale g_constants[17].w
#define g_color_scale_min g_constants[18].x
#define g_color_scale_size g_constants[18].y
#define g_delta_tick g_constants[18].z
#define g_child_count_per_emit g_constants[18].w
#define g_rotation_z_min_rad g_constants[19].x
#define g_rotation_z_size_rad g_constants[19].y
#define g_rotate_z_curve_scale_min g_constants[19].z
#define g_rotate_z_curve_scale_size g_constants[19].w
#define g_binarized_initial_rotate_z ((floatBitsToUint(g_constants[12].z) >> BitIndexInType_BinarizedInitialRotateZ) & BitMaskInType_BinarizedInitialRotateZ)
#define g_uv_type_a ((floatBitsToUint(g_constants[12].z) >> BitIndexInType_UVTypeA) & BitMaskInType_UVTypeA)
#define g_uv_offset_min g_constants[20].xy
#define g_uv_offset_size g_constants[20].zw
#define g_curve_texture_inv_height g_constants[21].x
#define g_child_curve_texture_inv_height g_constants[21].y
#define g_child_emitter_scale_inv_duration_tick g_constants[21].z
#define g_child_emitter_scale_normalied_begin_tick g_constants[21].w
#define g_child_fixed_emitter_scale g_constants[22].xyz
#define AnimStyle_Add (1)
#define AnimStyle_Only (2)
#define AnimStyle_Multiple (3)
#define TEXEL_STEP (1.f / 16.f)
#define PI 3.1415926535f
vec3 fetch_noise(in const vec3 position){
	//適当にオフセットして3成分を拾う。
	vec3 n;
	n.x = texture(g_noise_sampler, position).r;
	n.y = texture(g_noise_sampler, position + vec3(133.f, 0.f, 19.f) * TEXEL_STEP).r;
	n.z = texture(g_noise_sampler, position + vec3(91.f, 13.f, 5.f) * TEXEL_STEP).r;
	return n;
}
uint* get_particle_vec_w_ptr(const uint particle_index){
	uint* ptr_u32 = (uint*)(g_particles);
	ptr_u32 += g_particle_size_in_u32 * particle_index;
	ptr_u32 += g_offset_to_velocity_w_in_u32;
	return ptr_u32;
}
vec3 calculate_view_position(const vec2 texcoord, const float linear_depth){
	vec3 v;
	v.xy = (texcoord * 2.f - vec2(1.f)) * linear_depth;
	v.z = -linear_depth;
	v.x = v.x * g_view_from_w_div_proj.x + g_view_from_w_div_proj.y;
	v.y = v.y * g_view_from_w_div_proj.z + g_view_from_w_div_proj.w;
	return v;
}
void emit_child(const vec3 position, const vec3 velocity, const int seed, const int particle_count, const float parent_tick){
	for(int i = 0; i < particle_count; ++ i){
		int cnt = atomicAdd(g_unused1->m_count, -1);
		if(cnt <= 0){//戻す。
			atomicAdd(g_unused1->m_count, 1);
		}else{//積む。
			uint pidx = g_unused1->m_indices[cnt - 1];//パーティクルのインデクスを消費。末尾から使う。
			Particle p1;
			vec3 pos;
			p1.m_position.xyz = position.xyz;
			//球面上の乱数列を用意したほうが良いかも。
			float rad_x = (m_randoms[(seed + 0) % RANDOM_COUNT] * 2.f - 1.f) * 0.5f * PI;
			float rad_y = (m_randoms[(seed + 1) % RANDOM_COUNT] * 2.f - 1.f) * PI;
			float sx = sin(rad_x);
			float cx = cos(rad_x);
			float sy = sin(rad_y);
			float cy = cos(rad_y);
			vec3 lp;
			lp.x = sy * cx;
			lp.y = sx;
			lp.z = cy * cx;
			lp.xyz *= g_emit_radius_thickness * m_randoms[(seed + 2) % RANDOM_COUNT] + g_emit_radius_min; 
			vec3 emitter_scale = vec3(1.f);
			if(0.f < g_child_emitter_scale_inv_duration_tick){
				emitter_scale = textureLod(
					g_child_curve_sampler, vec2(parent_tick * g_child_emitter_scale_inv_duration_tick - g_child_emitter_scale_normalied_begin_tick, 7.5f * g_child_curve_texture_inv_height), 0.f).rgb;
			}else{
				emitter_scale = g_child_fixed_emitter_scale;
			}
			p1.m_position.xyz += lp * emitter_scale;
			p1.m_position.w = 0.f;
			const float scale = g_child_scale_min + g_child_scale_size * m_randoms[(seed + 3) % RANDOM_COUNT];
			const float life = max(g_child_life_tick - g_child_life_tick_size * m_randoms[(seed + 4) % RANDOM_COUNT], 1.f);//1以上を保証。
			//速度を計算。
			vec3 e = normalize(lp);
			vec3 t = vec3(e.z, 0.f, -e.x);//=cross(vec3(0.f, 1.f, 0.f), e)
			vec3 v = e * g_emit_dir_speed + t * g_tan_dir_speed + g_user_speed;
			v *= g_speed_size * m_randoms[(seed + 5) % RANDOM_COUNT] + g_speed_min;
			v += velocity.xyz * g_parent_speed_ratio;//親の速度を追加。
			uint parent_index = gl_VertexID;
			p1.m_velocity = vec4(v, uintBitsToFloat(parent_index << 16));
			atomicAdd(get_particle_vec_w_ptr(gl_VertexID), 1);//親が持つ子カウントを+1。子が0xffff個以下であること。
			p1.m_attributes = vec4(scale, life, 0.f, 1.f / 255.f);
			vec4 r = vec4(
				m_randoms[(seed + 8) % RANDOM_COUNT],
				m_randoms[(seed + 9) % RANDOM_COUNT],
				m_randoms[(seed + 10) % RANDOM_COUNT],
				m_randoms[(seed + 11) % RANDOM_COUNT]);
			float packed_rotate_z0 = calculate_packed_rotation_z0(
				g_rotation_z_min_rad, g_rotation_z_size_rad, g_rotate_z_curve_scale_min, g_rotate_z_curve_scale_size,
				vec2(m_randoms[(seed + 12) % RANDOM_COUNT], m_randoms[(seed + 13) % RANDOM_COUNT]),
				(0 != g_binarized_initial_rotate_z));
			float packed_texcoord0 = calculate_packed_texcoord0(
				g_uv_offset_min,
				g_uv_offset_size,
				vec2(m_randoms[(seed + 14) % RANDOM_COUNT], m_randoms[(seed + 15) % RANDOM_COUNT]),
				g_uv_type_a);
			p1.m_scale = vec4(1.f, packed_rotate_z0, 0.f, packed_texcoord0);
			p1.m_color0 = g_child_color0_size * r + g_child_color0_min;//コレ全体に掛ける係数を乱数化したいらしい。
			p1.m_color = p1.m_color0;
			g_particles[pidx] = p1;
		}
	}
}
void main(){
	gl_Position = vec4(0.f, 0.f, 0.f, -1.f);//culling
	Particle p0 = g_particles[gl_VertexID];
	if(p0.m_attributes.y <= p0.m_position.w){//↓を一度も実行せずに、ここに来ると不味い。寿命がゼロだと、不死になってしまう。
		return;
	}
	const float dt = g_delta_tick;
	const float elapsed_tick = p0.m_position.w;
	const float current_tick = p0.m_position.w + dt;
	const uint particle_flags = floatBitsToUint(p0.m_attributes.z);
	if(	(p0.m_attributes.y <= current_tick) ||//現フレームで死んだ。
		(0 != (ParticleFlag_Retiring & particle_flags))){//既に死んでいるが、子の回収がまだ。
		uint packed_ = floatBitsToUint(p0.m_velocity.w);
		uint child_cnt = packed_ & 0xffff;
		if(0 == child_cnt){//子がすべていなければ消す。
			//親がいれば親から子カウントを減らす。
			uint parent_idx = packed_ >> 16;
			if(0xffff == parent_idx){//1段目。
				//自身のパーティクルのインデクスを回収。
				int idx = atomicAdd(g_unused0->m_count, 1);
				g_unused0->m_indices[idx] = gl_VertexID;
			}else{//2段目。
				atomicAdd(get_particle_vec_w_ptr(parent_idx), -1);//親から子カウントを減らす。
				//自身のパーティクルのインデクスを回収。
				int idx = atomicAdd(g_unused1->m_count, 1);
				g_unused1->m_indices[idx] = gl_VertexID;
			}
			p0.m_position.w = p0.m_attributes.y * 10.f + 100000.f;//死亡。dt=0でもp0.m_attributes.y<p0.m_position.wを保証するように。
		}else{
			//この時、動かないが存在するパーティクルになる。フラグを立てて非表示に。寿命は維持。次回もこのブロックを実行するため。
			p0.m_attributes.z = uintBitsToFloat(particle_flags | ParticleFlag_Retiring);
		}
		g_particles[gl_VertexID] = p0;//life, attributeの書き出し。
		return;
	}
	p0.m_position.w = current_tick;
	const vec3 last_position = p0.m_position.xyz;
	const float life_ratio = current_tick / p0.m_attributes.y;
	p0.m_position.xyz += p0.m_velocity.xyz * dt;
	vec3 acceralation = g_global_accel;
	p0.m_position.xyz += acceralation * dt * dt * 0.5f;
	p0.m_velocity.xyz += acceralation * dt;
	const uint update_flags = floatBitsToUint(g_noise_offset.w);
	if(0 != (UpdateFlag_CurlNoise & update_flags)){
		vec3 np = p0.m_position.xyz * 0.12f + g_noise_offset.xyz;
		vec3 v000 = fetch_noise(np);
		float s = TEXEL_STEP;
		vec3 dx = fetch_noise(np + vec3(s, 0.f, 0.f)) - v000;
		vec3 dy = fetch_noise(np + vec3(0.f, s, 0.f)) - v000;
		vec3 dz = fetch_noise(np + vec3(0.f, 0.f, s)) - v000;
		vec3 v = vec3(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x);//curl
		v *= g_curl_velocity_scale;
		const float tick_per_frame = 50.f;
		v *= dt / (tick_per_frame * tick_per_frame);//[m/frame]でノイズを調整していたので、単位[m/tick]に合わせる。//(1.f/tick_per_frame)*(dt/tick_per_frame)
		float curl_ratio = g_velocity_attenuation_ratio * min(1.f, life_ratio / g_velocity_attenuation_life_ratio);
		p0.m_velocity.xyz = mix(p0.m_velocity.xyz, v, curl_ratio);//寿命が尽きてきたらノイズを支配的に。
	}
#if 1
	if(0 != (UpdateFlag_TestCollision & update_flags)){
		//深度で反射。
		vec4 wpos = vec4(p0.m_position.xyz, 1.f);
		vec3 sp;
		sp.x = dot(g_transform_row0, wpos);
		sp.y = dot(g_transform_row1, wpos);
		sp.z = dot(g_transform_row3, wpos);
		sp.xy *= 1.f / sp.z;
		sp.xy *= 0.5f;
		sp.xy += vec2(0.5f);
		const float dst_depth = textureLod(g_linear_depth_sampler, sp.xy, 0.f).r;
		const float src_depth = -dot(g_view_row2, wpos);
		if(dst_depth < src_depth){
			const float collision_depth_thickness = 0.5f;//コリジョンの厚さ。
			if(src_depth < (dst_depth + collision_depth_thickness)){
				//p0.m_position.xyz -= p0.m_velocity.xyz;//適当。無かったことにする。
				//ワールド法線を求める。
				vec3 v00 = calculate_view_position(sp.xy, dst_depth);
				vec3 v01 = calculate_view_position(sp.xy + vec2(1.f / 1280.f, 0.f), textureLod(g_linear_depth_sampler, sp.xy + vec2(1.f / 1280.f, 0.f), 0.f).r);
				vec3 v10 = calculate_view_position(sp.xy + vec2(0.f, 1.f / 720.f), textureLod(g_linear_depth_sampler, sp.xy + vec2(0.f, 1.f / 720.f), 0.f).r);
				vec3 vn = cross(v10 - v00, v01 - v00);
				vec3 wn;
				wn.x = dot(g_inv_view_row0.xyz, vn);
				wn.y = dot(g_inv_view_row1.xyz, vn);
				wn.z = dot(g_inv_view_row2.xyz, vn);
				wn = normalize(wn);
				//反射ベクトルを求める。
				vec3 r = p0.m_velocity.xyz - 2.f * dot(p0.m_velocity.xyz, wn) * wn;
				const float k = g_restitution_coef;
				p0.m_velocity.xyz = r * k;
				//座標を補正。適当。無かったことに。
				p0.m_position.x = dot(g_inv_view_row0, vec4(v00, 1.f));
				p0.m_position.y = dot(g_inv_view_row1, vec4(v00, 1.f));
				p0.m_position.z = dot(g_inv_view_row2, vec4(v00, 1.f));
				//p0.m_velocity.y *= -k;
			}
		}
	}
#endif
	//色を更新。
	vec4 color = p0.m_color0;
	vec4 anim_color = textureLod(g_curve_sampler, vec2(life_ratio, 4.5f * g_curve_texture_inv_height), 0.f);//linearフィルタでv方向に隣接ピクセルを吸わないようにセンタリング。
	anim_color.rgb = pow(anim_color.rgb, vec3(2.2f));//srgb_to_linear
	uint anim_style = g_color_anim_style;
	if(anim_style == AnimStyle_Add){
		color += anim_color;
	}else if(anim_style == AnimStyle_Only){
		color = anim_color;
	}else if(anim_style == AnimStyle_Multiple){
		color *= anim_color;
	}
	color.rgb *= m_randoms[(gl_VertexID * 135) % RANDOM_COUNT] * g_color_scale_size + g_color_scale_min;
	p0.m_color = color;
	const vec4 anim_scale = textureLod(g_curve_sampler, vec2(life_ratio, 3.5f * g_curve_texture_inv_height), 0.f);
	p0.m_attributes.w = (0 == (UpdateFlag_AlphaThreshold & update_flags)) ? 1.f / 255.f : max(anim_scale.w, 1.f / 255.f);
	p0.m_scale.x = anim_scale.x;
	//回転角を更新。
	const vec4 rot_curve = textureLod(g_curve_sampler, vec2(life_ratio, 2.5f * g_curve_texture_inv_height), 0.f);
	{
		vec2 packed_rotate_z = unpackHalf2x16(floatBitsToUint(p0.m_scale.y));
		const float initial_rotate_z_rad = packed_rotate_z.x;
		const float rotate_z_curve_scale  = packed_rotate_z.y;
		packed_rotate_z = unpackHalf2x16(floatBitsToUint(p0.m_scale.z));
		float rot_z = rot_curve.z * rotate_z_curve_scale + initial_rotate_z_rad;
		packed_rotate_z.x = rot_z;
		p0.m_scale.z = uintBitsToFloat(packHalf2x16(packed_rotate_z));
	}
	g_particles[gl_VertexID] = p0;
	//子パーティクルを生み出す。
	if(0 != (UpdateFlag_EmitChild & update_flags)){
		const int emit_interval_tick = int(g_child_emit_interval_tick);
		if(	(0 < emit_interval_tick) &&
			(g_child_emit_time_start_tick <= elapsed_tick)){
			//発生数をカウント。
			int emit_cnt = 0;
			const int elapsed_tick_from_last_emit_tick = int(elapsed_tick - g_child_emit_time_start_tick) % emit_interval_tick;
			if(0 < dt){
				emit_cnt = int(elapsed_tick_from_last_emit_tick + dt) / emit_interval_tick;
			}
			//if(in_out->m_left_emit_count < emit_cnt){//DiPと挙動を合わせるため。必要?//省略。問題があれば対応。
			//	emit_cnt = in_out->m_left_emit_count;
			//}
			if(0 < emit_cnt){
				const int first_emit_from_elapsed_tick = (emit_interval_tick - elapsed_tick_from_last_emit_tick) % emit_interval_tick;//前フレーム時刻から、現フレームでの最初のエミット時刻までの時間。
				//const float current_from_last_emit_tick = float(int(current_tick - g_child_emit_time_start_tick) % emit_interval_tick);//現フレームでの最後のエミット時刻から、現フレーム時刻までの時間。//elapsed_tickの初期値用。省略。問題があれば対応。
				vec3 v = p0.m_position.xyz - last_position;
				float r = first_emit_from_elapsed_tick / g_delta_tick;//float r = (first_emit_from_elapsed_tick + emit_interval_tick * i) / g_delta_tick;を展開した。
				float dr = emit_interval_tick / g_delta_tick;
				int ptc_cnt = int(g_child_count_per_emit);
				int seed = gl_VertexID * 135;
				int dseed = ptc_cnt * 13;
				vec3 vel = p0.m_velocity.xyz;
				float parent_tick = elapsed_tick;
				float ddt = dt / emit_cnt;
				for(int i = 0; i < emit_cnt; ++ i){
					vec3 pos = last_position + v * r;//位置を補間。速度は無視(必要があれば対応)。
					emit_child(pos, vel, seed, ptc_cnt, parent_tick);
					parent_tick += ddt;
					r += dr;
					seed += dseed;
				}
			}
		}
	}
}