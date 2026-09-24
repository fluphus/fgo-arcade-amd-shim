#version 430
#extension GL_NV_shader_buffer_load : enable
#define TILE_X_COUNT 32
#define TILE_Y_COUNT 18
#define TILE_Z_COUNT 2
#define TILE_WIDTH (60.000000)
#define TILE_HEIGHT (60.000000)
#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define MAX_LIGHT_COUNT_IN_TILE 48
#define USE_PER_TILE_MIN_DEPTH 1
#define LightFlag_Spot 65536
#define USE_MINMAX_FILTER 1
#define USE_DEPTH_RANGE 1
struct Tile{//全メンバintであること。TBO経由で読み込むため。
	int m_count;
	int m_index_offset;
};
layout(std430, binding = 0) writeonly buffer TileSet{
	Tile m_tiles[];
}g_tile;
layout(std430, binding = 1) writeonly buffer LightIndices{
	int m_light_indices[];
}g_light_indices;
struct Light{
	vec4 m_sphere;//xyz=position,w=radius
	vec4 m_color;
	vec4 m_packed;
};layout(std430, binding = 36) buffer LightData { Light g_light_data[]; };

layout(location = 0) uniform vec4 g_constants[11];
#define g_eye_direction g_constants[0].xyz
#define g_light_count g_constants[0].w
#define g_eye_position g_constants[1].xyz
#define g_slice_ratio g_constants[1].w
#define g_frustum_position0 g_constants[2].xyz
#define g_frustum_position1 g_constants[3].xyz
#define g_frustum_position2 g_constants[4].xyz
#define g_frustum_position3 g_constants[5].xyz
#define g_frustum_position4 g_constants[6].xyz
#define g_frustum_position5 g_constants[7].xyz
#define g_frustum_position6 g_constants[8].xyz
#define g_frustum_position7 g_constants[9].xyz
#define g_lights_addr packPtr(uvec2(floatBitsToUint(g_constants[10].x), floatBitsToUint(g_constants[10].y)))
#define g_linear_z_params g_constants[10].zw
#define g_depth_near g_constants[2].w
#define g_depth_range g_constants[3].w
#define g_side_plane_coefficient g_constants[4].w
#define g_resolution_scale g_constants[5].w
layout(binding = 0) uniform sampler2D g_opaque_only_raw_depth_sampler;
layout(binding = 1) uniform sampler2D g_full_raw_depth_sampler;
void calculate_sub_frustum(
	out vec3 out_vertices[8],
	in  uint x,
	in  uint y){
	float sx = 1.f / TILE_X_COUNT;
	float sy = 1.f / TILE_Y_COUNT;
	vec3 r = g_frustum_position2 - g_frustum_position0;
	vec3 u = g_frustum_position1 - g_frustum_position0;
	r *= sx;
	u *= sy;
	out_vertices[0] = g_frustum_position0 + r * x + u * y;
	out_vertices[2] = out_vertices[0] + r;
	out_vertices[1] = out_vertices[0] + u;
	out_vertices[3] = out_vertices[2] + u;
	r = g_frustum_position6 - g_frustum_position4;
	u = g_frustum_position5 - g_frustum_position4;
	r *= sx;
	u *= sy;
	out_vertices[4] = g_frustum_position4 + r * x + u * y;
	out_vertices[6] = out_vertices[4] + r;
	out_vertices[5] = out_vertices[4] + u;
	out_vertices[7] = out_vertices[6] + u;
}
#define PLANE_COUNT 10
void calculate_planes(
	out vec4 out_planes[PLANE_COUNT],
	in  vec3 vertices[8]){
	//法線は内向き。
	vec3 ed = g_eye_direction.xyz;
	out_planes[0] = vec4(ed.x, ed.y, ed.z, -dot(ed, vertices[0]));
	out_planes[1] = vec4(-ed.x, -ed.y, -ed.z, dot(ed, vertices[4]));
	vec3 a, b;
	a = vertices[7] - vertices[3];
	b = vertices[1] - vertices[3];
	vec3 n;
	n = cross(b, a);
	n = normalize(n);
	n *= g_side_plane_coefficient;
	out_planes[2] = vec4(n.x, n.y, n.z, -dot(n, vertices[3]));
	b = vertices[2] - vertices[3];
	n = cross(a, b);
	n = normalize(n);
	n *= g_side_plane_coefficient;
	out_planes[3] = vec4(n.x, n.y, n.z, -dot(n, vertices[3]));
	a = vertices[4] - vertices[0];
	b = vertices[2] - vertices[0];
	n = cross(b, a);
	n = normalize(n);
	n *= g_side_plane_coefficient;
	out_planes[4] = vec4(n.x, n.y, n.z, -dot(n, vertices[0]));
	b = vertices[1] - vertices[0];
	n = cross(a, b);
	n = normalize(n);
	n *= g_side_plane_coefficient;
	out_planes[5] = vec4(n.x, n.y, n.z, -dot(n, vertices[0]));
	//縁。
	n = normalize(out_planes[2].xyz + out_planes[5].xyz);
	out_planes[6] = vec4(n.x, n.y, n.z, -dot(n, vertices[1]));
	n = normalize(out_planes[2].xyz + out_planes[3].xyz);
	out_planes[7] = vec4(n.x, n.y, n.z, -dot(n, vertices[3]));
	n = normalize(out_planes[3].xyz + out_planes[4].xyz);
	out_planes[8] = vec4(n.x, n.y, n.z, -dot(n, vertices[2]));
	n = normalize(out_planes[4].xyz + out_planes[5].xyz);
	out_planes[9] = vec4(n.x, n.y, n.z, -dot(n, vertices[0]));
}
bool is_intersected(
    in  vec4 sphere,
    in  vec4 planes[PLANE_COUNT]){
    for(int i = 0; i < PLANE_COUNT; ++ i){
        if((dot(planes[i].xyz, sphere.xyz) + planes[i].w + sphere.w) < 0.f){
            return false;
        }
    }
    return true;
}
float calculate_linear_depth( float raw_depth){
	return 1.f / (raw_depth * g_linear_z_params.x + g_linear_z_params.y);//forward g_linear_z_params.x=(n-f)/(nf), y=1/n, or reverse g_linear_z_params.x=(f-n)/(nf), y=1/f
}
void main(){
	const uint tile_idx = gl_VertexID;
	const uint tile_x_idx = gl_VertexID % TILE_X_COUNT;
	const uint tile_y_idx = (gl_VertexID % (TILE_X_COUNT * TILE_Y_COUNT)) / TILE_X_COUNT;
	const uint tile_z_idx = gl_VertexID / (TILE_X_COUNT * TILE_Y_COUNT);
	vec3 v[8];
	calculate_sub_frustum(v, tile_x_idx, tile_y_idx);
#if (1 < TILE_Z_COUNT)
//	float zp = g_slice_exponential;//分割z面をどの程度近くに寄せるか。near,farと分割面から逆算すること。近景ほど細かく割る。
	float sz = 1.f / TILE_Z_COUNT;
//	const float slice_near_ratio = pow((tile_z_idx * sz), zp);
//	const float slice_far_ratio = pow(((tile_z_idx + 1) * sz), zp);
	float slice_near_ratio = 0.f;
	float slice_far_ratio = 0.f;
	if(0 == tile_z_idx){
		slice_near_ratio = 0.f;
		slice_far_ratio = g_slice_ratio;
	}else{
		slice_near_ratio = g_slice_ratio;
		slice_far_ratio = 1.f;
	}
#endif
#if USE_DEPTH_RANGE
	//min, maxを計算。
	const int cell_width = int(TILE_WIDTH);
	const int cell_height = int(TILE_HEIGHT);
	vec2 offset = vec2(1.f / SCREEN_WIDTH, 1.f / SCREEN_HEIGHT);
	vec2 uv;//1080が32で割り切れないのでtexelFetchが使えない。
	uv.x = float(tile_x_idx * cell_width) * offset.x;
	uv.y = float(tile_y_idx * cell_height) * offset.y;
	uv *= g_resolution_scale;
	offset *= g_resolution_scale;
	float df = 10000.f;
	float dn = 0.f;
#if USE_MINMAX_FILTER
	//ループをまとめないこと。まとめるとアクセスパターン的に不利になる。
	offset *= 2.f;
	vec2 t = uv;
	float max_uv = g_resolution_scale - 1.f / SCREEN_HEIGHT;//1ピクセル分内側に。
#if USE_PER_TILE_MIN_DEPTH
	for(int i = 0; i <= cell_height / 2; ++ i){
		t.x = uv.x;
		for(int j = 0; j <= cell_width / 2; ++ j){
			dn = max(dn, textureLod(g_full_raw_depth_sampler, t, 0.f).r);//半透明のために、nearは別深度マップを使う。
			t.x = min(t.x + offset.x, max_uv);
		}
		t.y = min(t.y + offset.y, max_uv);
	}
#endif//USE_PER_TILE_MIN_DEPTH
	t = uv;
	for(int i = 0; i <= cell_height / 2; ++ i){
		t.x = uv.x;
		for(int j = 0; j <= cell_width / 2; ++ j){
			df = min(df, textureLod(g_opaque_only_raw_depth_sampler, t, 0.f).r);
			t.x = min(t.x + offset.x, max_uv);
		}
		t.y = min(t.y + offset.y, max_uv);
	}
#else//USE_MINMAX_FILTER
	vec2 t = uv;
	for(int i = 0; i <= cell_height; ++ i){
		t.x = uv.x;
		for(int j = 0; j <= cell_width; ++ j){
			float d = textureLod(g_full_raw_depth_sampler, t, 0.f).r;//半透明のために、nearは別深度マップを使う。
			if(dn < d){//最も近いところでd=1.0。reverse zだから。
				dn = d;
			}
			t.x += offset.x;
		}
		t.y += offset.y;
	}
	t = uv;
	for(int i = 0; i <= cell_height; ++ i){
		t.x = uv.x;
		for(int j = 0; j <= cell_width; ++ j){
			float d = textureLod(g_opaque_only_raw_depth_sampler, t, 0.f).r;
			if(d < df){//最も遠いところでd=0.0。reverse zだから。
				df = d;
			}
			t.x += offset.x;
		}
		t.y += offset.y;
	}
#endif//USE_MINMAX_FILTER
	df = calculate_linear_depth(df);
#if USE_PER_TILE_MIN_DEPTH
	dn = calculate_linear_depth(dn);
#else//USE_PER_TILE_MIN_DEPTH
	dn = g_depth_near;//カメラのnearを使う。ボリュームフォグ用。
#endif//USE_PER_TILE_MIN_DEPTH
#if (1 < TILE_Z_COUNT)
	const float slice_near_depth = g_depth_near + g_depth_range * slice_near_ratio;//ここはカメラごとの値でなく、サブ視錐台ごとの方がカリング効率が良いだろうが、タイルごとに分割深度の記録が必要になる。
	const float slice_far_depth = g_depth_near + g_depth_range * slice_far_ratio;
	if((df < slice_near_depth) || (slice_far_depth < dn)){//視錐台の体積0。
		g_tile.m_tiles[tile_idx].m_count = 0;
		return;
	}
	df = min(df, slice_far_depth);
	dn = max(dn, slice_near_depth);
#endif//(1 < TILE_Z_COUNT)
	//視錐台を切り落とす。
	for(int i = 0; i < 4; ++ i){
		vec3 d = v[i] - g_eye_position;
		float l = dot(d, g_eye_direction);
		v[i] = d * (dn / l) + g_eye_position;
	}
	for(int i = 4; i < 8; ++ i){
		vec3 d = v[i] - g_eye_position;
		float l = dot(d, g_eye_direction);
		v[i] = d * (df / l) + g_eye_position;
	}
#elif (1 < TILE_Z_COUNT)
	vec3 ed = v[4] - v[0];
	v[4] = v[0] + ed * slice_far_ratio;
	v[0] = v[0] + ed * slice_near_ratio;
	ed = v[5] - v[1];
	v[5] = v[1] + ed * slice_far_ratio;
	v[1] = v[1] + ed * slice_near_ratio;
	ed = v[6] - v[2];
	v[6] = v[2] + ed * slice_far_ratio;
	v[2] = v[2] + ed * slice_near_ratio;
	ed = v[7] - v[3];
	v[7] = v[3] + ed * slice_far_ratio;
	v[3] = v[3] + ed * slice_near_ratio;
#endif//USE_DEPTH_RANGE
	vec4 p[PLANE_COUNT];
	calculate_planes(p, v);
	int light_count = int(g_light_count);
	int idx = int(tile_idx) * MAX_LIGHT_COUNT_IN_TILE;
	g_tile.m_tiles[tile_idx].m_index_offset = idx;
	int c = 0;
	
	for(int i = 0; i < light_count; ++ i){
#if 01
		if(is_intersected(g_light_data[i].m_sphere, p)){
			bool visible = true;
			vec4 light_packed = g_light_data[i].m_packed;
			if(0 != (floatBitsToUint(light_packed.x) & LightFlag_Spot)){//スポットライトは追加のカリング。
				visible = false;
				vec3 direction;
				direction.xy = unpackHalf2x16(floatBitsToUint(light_packed.y));
				vec2 t = unpackHalf2x16(floatBitsToUint(light_packed.z));
				direction.z = t.x;
				vec3 origin = g_light_data[i].m_sphere.xyz;
				for(int j = 0; j < 8; ++ j){//全ての頂点が背面側にあれば棄却。
					if(0.f < dot(direction, v[j] - origin)){//一頂点でも前方にあれば合格。
						visible = true;
						break;
					}
				}
			}
			if(visible){
				g_light_indices.m_light_indices[idx] = i;
				++ idx;
				++ c;
				if(c == MAX_LIGHT_COUNT_IN_TILE){
					break;
				}
			}
		}
#else
		if(is_intersected(g_light_data[i].m_sphere, p)){
			g_light_indices.m_light_indices[idx] = i;
			++ idx;
			++ c;
			if(c == MAX_LIGHT_COUNT_IN_TILE){
				break;
			}
		}
#endif
	}
	g_tile.m_tiles[tile_idx].m_count = c;
}
