#version 450
#extension GL_ARB_bindless_texture : enable
#extension GL_NV_gpu_shader5 : enable
#define SAMPLER_INDEX_NORMAL0 0
#define SAMPLER_INDEX_NORMAL1 1
#define SAMPLER_INDEX_NORMAL2 2
#define SAMPLER_INDEX_NORMAL3 3
#define SAMPLER_INDEX_DIFFUSE0 4
#define SAMPLER_INDEX_DIFFUSE1 5
#define SAMPLER_INDEX_DIFFUSE2 6
#define SAMPLER_INDEX_DIFFUSE3 7
#define SAMPLER_INDEX_SPECULAR0 8
#define SAMPLER_INDEX_SPECULAR1 9
#define SAMPLER_INDEX_SPECULAR2 10
#define SAMPLER_INDEX_SPECULAR3 11
#define SAMPLER_INDEX_HEIGHT0 12
#define SAMPLER_INDEX_WEIGHT 13
#define SAMPLER_INDEX_BAKED_SHADOW 14
#define SAMPLER_INDEX_BAKED_LIGHT 15
#define SAMPLER_INDEX_OPACITY0 16
#define SAMPLER_INDEX_OPACITY1 17
#define SAMPLER_INDEX_LAYERED_DIFFUSE 18
#define SAMPLER_INDEX_ENVIRONMENT_CUBE 19
#define SamplerIndexInShader_TiledLightTiles 20
#define SamplerIndexInShader_TiledLightIndices 21
#define SamplerIndexInShader_Noise3d 24
#define SamplerIndexInShader_CloudShadow 27
#define SamplerIndexInShader_SceneRawDepth 26
#define SamplerIndexInShader_SceneLinearDepth 23
#define SamplerIndexInShader_SceneColor 25
#define SamplerIndexInShader_GBufferNormal 30
#define SamplerIndexInShader_GBufferSpecular 31
#define SamplerIndexInShader_GBufferDiffuse 32
#define SamplerIndexInShader_ScreeenSpaceReflection 33
#define SamplerIndexInShader_Dither 34
#define SamplerIndexInShader_DeformableHeight 22
#define SamplerIndexInShader_Attribue 36
#define SamplerIndexInShader_VolumeFog 35
#define UboIndex_PerScene 0
#define UboIndex_PerCamera 1
#define UboIndex_PerShadow 2
#define UboIndex_PerBatch 4
#define UboIndex_PerMaterial 3
#define PackedColorGainCount 10
#define PackedTexcoordTransformCount 10
#define PackedTangentTransformCount 4
#define MaxInstanceVec4Count 448
#define MaxCharacterCount 32
#define ShaderFlag_NprEye 1
#define ShaderFlag_NprHave2ndMap 2
#define ShaderFlag_NprHair 4
#define ShaderFlag_NprFaceParts 8
#define ShaderFlag_Wind 16
#define ShaderFlag_ModifyByEffect 32
#define ShaderFlag_ReceiveShadow 64
#define ShaderFlag_Water 128
#define ShaderFlag_HaveUserEnvironmentMap 256
#define ExtraShaderFlag_Dissolve 1
#define ExtraShaderFlag_Transparency 2
#define ExtraShaderFlag_UseUniqueShadow 4
#define SHADOW_MAP_COUNT 3
#define LocalShadowMapCapacity 8
#define TILE_COUNT_H 32
#define TILE_COUNT_V 18
#define TILE_COUNT_D 2
#define INV_SCREEN_WIDTH (0.00052083339)
#define INV_SCREEN_HEIGHT (0.00092592601)
#define USE_NSIGHT 0
#define PI 3.1415926535f
#define MIN_ROUGHNESS (0.079999995f)
#define DissolveFadeLength (0.4000000f)
#define ShadowMode_Auto 0
#define ShadowMode_Resolved 1
#define ShadowMode_Unresolved 2
#define ShaderSubType0_Regular 0
#define ShaderSubType0_CompositeVolumeFog 2
#define ShaderSubType0_OutputTextureLod 1
#define ENABLE_TEXTURE_COLOR_CORRECTION 0
#define SHADER_SUB_TYPE0 0
#define ENABLE_PUNCH 0
#define SHADER_LOD 0
#define LIGHTING_TYPE 1
#define HAVE_VERTEX_COLOR 0
#define ENABLE_Z_OFFSET 0
#define ENABLE_DITHER 0
#define USE_TINY_GBUFFER 0
#define USE_CLIP_PLANE 0
#define SHADOW_MODE 0
#if (0 == LIGHTING_TYPE)
	#define HAVE_TANGENT 0
	#define CONSTANT_SHADING 1
#elif (1 == LIGHTING_TYPE)
	#define HAVE_TANGENT 1
	#define CONSTANT_SHADING 0
#else
	#error
#endif
#define USE_OPACITY_AS_COLOR_SCALER 0
#define BLEND_MAP_CAPACITY 4
#define EFFECT_SHADER 0
#define DEPTH_ONLY 0
#define OUTPUT_TINY_GBUFFER 0

#define VERTEX_SHADER 0
#define USE_DEFAULT_VS_CONSTANTS 0
#define TESSELLATION_SHADER 0
#define USE_TESSELLATOR 0
#if !defined(DEPTH_ONLY)
#error
#endif//DEPTH_ONLY
#if !defined(SHADOW_MODE)
#error
#endif//SHADOW_MODE
#if VERTEX_SHADER && USE_DEFAULT_VS_CONSTANTS
layout(location = 0) uniform vec4 g_transforms[6 + 4];//[0,2]=world,[3,5]=inv_world(not unscaled),[6,9]=projection*view*world
#define WORLD_TRANSFORM_ROW(row_index) g_transforms[row_index]
#define INV_WORLD_TRANSFORM_ROW(row_index) g_transforms[3 + row_index]
#define PROJ_VIEW_WORLD_ROW(row_index) g_transforms[6 + row_index]
#endif//VERTEX_SHADER && USE_DEFAULT_VS_CONSTANTS
struct Light{
	vec4 m_sphere;
	vec4 m_color;
	vec4 m_packed;//see renderer/impl/tiled_light.h
};
struct ReflectionProxy{//sampling_position=(aabb_min+aabb_max)*0.5f
	vec4 m_aabb_min;//w=blend_distance
	vec4 m_aabb_max;//w=npr_intensity
	uint64_t m_cube_texture_handle;
	uint64_t m_reserved;
};
#if !defined(MIN_ROUGHNESS)
#error
#endif//MIN_ROUGHNESS
layout(std140, binding = UboIndex_PerScene) uniform PerScene{
	vec4 g_parallel_light_colors[2];//xyz=color,w=specular_intensity_in_shadow, [0]=for_npr, [1]=for_pr
	vec4 g_depth_fog_color[2];//xyz=color,w=height
	vec4 g_depth_fog;//x=scale,y=offset,z=unused,w=curve
	vec4 g_npr_tone[6];  // tone3 + identity3
	vec4 g_cloud_shadow_texcoords[2];//[0].xy=offset, z=scale, w=density_offset, [1].x=thicknes_for_pr, [1].y=thicknes_for_npr, [1].z=1.f/render_target_width, [1].w=1.f/render_target_height
	vec4 g_wind_perturbations[2];//[0].xy=offset, [0].zw=direction, [1].x=texcoord_scale, [1].y=perturbation_length, [1].z=fade_begin_depth, [1].w=fade_length
	vec4 g_water_normal_map_fetcher;//xy=scale, zw=offset
	vec4 g_light_modifier;//x=npr_point_light_scale, y=light_slice_depth, z=inv_resolution_scale, w=resolution_scale
	vec4 g_dither_modifier;//x=1.f/(d_far-d_near), y=-d_near/(d_far-d_near), z=far_dither_ratio-1, w=height_dither_min
	vec4 g_border_field; // x=min distance, y=max_distance, z=enable
	vec4 g_character_positions[MaxCharacterCount];
	float g_character_position_count;
	float g_reflection_proxy_count;
	uint64_t g_wind_perturbation_map_handle;
	uint64_t g_water_normal_map_handle;
	uint64_t g_compat_ptr_0_0;
	uint64_t g_compat_ptr_0_1;
	uint64_t g_planar_reflection_map_handle;
	uint64_t g_unique_planar_reflection_map_handle;
	uint64_t g_reserved_per_scene;
	vec4 g_planar_reflection_plane;//減衰スケール済み。planer_intensity=1.f-abs(dot(g_planar_reflection_plane, world_position))
	vec4 g_planar_reflection_tangent_;//w=unused
	vec4 g_planar_reflection_binormal_;//w=unused
	vec4 g_deformable_texcoord;//x=scale, yz=offset, w=attribute_scale
	vec4 g_attribute_texcoord;//xy=scale, zw=offset
	vec4 g_volume_fog_texcoord_from_worlds_in_cmn[3];
	vec4 g_volume_fog_slice_from_linear_depth_in_cmn;//w=unused
};
layout(std430, binding = 30) buffer PtrBuf_0 { Light g_lights[]; };
layout(std430, binding = 31) buffer PtrBuf_1 { ReflectionProxy g_reflection_proxies[]; };
#define SPECULAR_INTENSITY_IN_SHADOW g_parallel_light_colors[0].w
#define g_inverse_render_target_size g_cloud_shadow_texcoords[1].zw
#define g_npr_point_light_scale g_light_modifier.x
#define g_water_normal_uv_scale g_water_normal_map_fetcher.xy
#define g_water_normal_uv_offset g_water_normal_map_fetcher.zw
#define g_light_slice_depth g_light_modifier.y
#define g_inv_resolution_scale g_light_modifier.z
#define g_resolution_scale g_light_modifier.w
#define g_height_dither_min g_dither_modifier.w
#define g_planar_reflection_tangent g_planar_reflection_tangent_.xyz
#define g_planar_reflection_binormal g_planar_reflection_binormal_.xyz
#define g_deform_height_scale g_deformable_texcoord.w
layout(std140, binding = UboIndex_PerCamera) uniform PerCamera{
	vec4 g_projection_view[4];//projection_view
	vec4 g_view_transform[3];//view
	vec4 g_eye;//xyz=pos, w=tan(0.5*fov_y)
	vec4 g_shadow_camera_row2;
	vec4 g_depth_modifiers;//x=(n-f)/(nf), y=1/n, z=(n+f)/(n-f), w=1/f
	vec4 g_depth_modifiers2;//x=n/(f-n), y=near_positive_min_y, z=n, w=1.f/tan(0.5*fov_y)
	vec4 g_camera_traced_light;//xyz=direction, w=intensity
	vec4 g_clip_plane_in_projection;
};
#define g_tan_half_fov_y g_eye.w
#define g_near_positive_min_y g_depth_modifiers2.y
#define g_depth_near_ g_depth_modifiers2.z
#define g_inv_tan_half_fov_y g_depth_modifiers2.w
layout(binding = UboIndex_PerShadow) uniform PerShadow{
	vec4 g_plane_distances;//x=plane1, y=plane2, z=plane3, w=half_blending_distance//plane0=0なのは自明なのでない。
	vec4 g_shadow_fetches[3 * SHADOW_MAP_COUNT];
	vec4 g_parallel_light_dir;//xyz=direction, w=max_glossiness//影の種類により光源向きも変えたいのでPerShadow。
	vec4 g_chara_extra_light_pos;//xyz=position, w=chara_extra_shadow_rate_offset, w<=0であること。
	vec4 g_chara_extra_light_dir;//xyz=direction, w=intensity, (w<0)で副光源なし。
	vec4 g_ao_intensities;//x=for_npr, y=for_stage, z=for_grass, w=output_lod_sampler_index(for debug)
	uint64_t g_screen_shadow_map_handle;
	uvec2 g_cascaded_shadow_map_handle_0;uvec2 g_cascaded_shadow_map_handle_1;uvec2 g_cascaded_shadow_map_handle_2;
	uint64_t g_ssao_map_handle;
	uint64_t g_local_shadow_map_handle;
	vec4 g_local_shadow_fetches[4 * LocalShadowMapCapacity];
	uint64_t g_unique_shadow_map_handle;
	uint64_t g_reserved_per_shadow;
	vec4 g_unique_shadow_fetcher[3];
};
#define g_ao_intensity_for_npr g_ao_intensities.x
#define g_ao_intensity_for_stage g_ao_intensities.y
#define g_ao_intensity_for_grass g_ao_intensities.z
#define OUTPUT_LOD_SAMPLER_INDEX g_ao_intensities.w
#define g_parallel_light_min_glossiness g_parallel_light_dir.w
#define DIFFUSE0_COLOR_GAIN g_color_gains[0].rgb
#define DIFFUSE1_COLOR_GAIN g_color_gains[1].rgb
#define DIFFUSE2_COLOR_GAIN g_color_gains[2].rgb
#define DIFFUSE3_COLOR_GAIN g_color_gains[3].rgb
#define BAKED_LIGHT_COLOR_GAIN g_color_gains[4].rgb
#define LAYERED_DIFFUSE_COLOR_GAIN g_color_gains[5].rgba
#define SPECULAR0_COLOR_GAIN g_color_gains[6].rgba
#define SPECULAR1_COLOR_GAIN g_color_gains[7].rgba
#define SPECULAR2_COLOR_GAIN g_color_gains[8].rgba
#define SPECULAR3_COLOR_GAIN g_color_gains[9].rgba
#define OPACITY0_GAIN g_color_gains[0].a
#define OPACITY1_GAIN g_color_gains[1].a
#define BAKED_SHADOW_GAIN g_color_gains[2].a
#define HEIGHT_SCALE g_color_gains[3].a
#define OVERDRAW_MASK g_npr_anisotropic_param.z
layout(binding = SAMPLER_INDEX_ENVIRONMENT_CUBE) uniform samplerCube g_env_sampler;//ambienet成分はこれをそのまま使う。
layout(binding = SamplerIndexInShader_CloudShadow) uniform sampler2D g_cloud_shadow_sampler;
layout(binding = SamplerIndexInShader_SceneColor) uniform sampler2D g_scene_color_sampler;//hairパスのみ入力される。
#if (0 == DEPTH_ONLY)
layout(binding = SamplerIndexInShader_SceneRawDepth) uniform sampler2D g_scene_raw_depth_sampler;//半透明パスのみ入力される。//hair描画時にはhairのzは書き込まれていない。
layout(binding = SamplerIndexInShader_SceneLinearDepth) uniform sampler2D g_scene_linear_depth_sampler;//hairのzが描画済み。
#endif//DEPTH_ONLY
layout(binding = SamplerIndexInShader_Noise3d) uniform sampler3D g_noise_sampler;
#if USE_TINY_GBUFFER
layout(binding = SamplerIndexInShader_GBufferNormal) uniform sampler2D g_gbuffer_normal_map_sampler;
layout(binding = SamplerIndexInShader_GBufferSpecular) uniform sampler2D g_gbuffer_specular_map_sampler;
layout(binding = SamplerIndexInShader_GBufferDiffuse) uniform sampler2D g_gbuffer_diffuse_map_sampler;
layout(binding = SamplerIndexInShader_ScreeenSpaceReflection) uniform sampler2D g_sslr_map_sampler;
#endif//USE_TINY_GBUFFER
layout(binding = SamplerIndexInShader_Dither) uniform sampler2D g_dither_sampler;
layout(binding = SamplerIndexInShader_DeformableHeight) uniform sampler2D g_deformable_height_sampler;
layout(binding = SamplerIndexInShader_Attribue) uniform sampler2D g_attribute_sampler;
#if (0 == DEPTH_ONLY)
//タイル化されたライト。
struct Tile{
	int m_count;
	int m_index_offset;
};
layout(binding = SamplerIndexInShader_TiledLightTiles) uniform isamplerBuffer g_tiles;
layout(binding = SamplerIndexInShader_TiledLightIndices) uniform isamplerBuffer g_light_indices;
#if VERTEX_SHADER || TESSELLATION_SHADER
float calculate_depth_fog_intensity( vec3 position){
	return clamp(length(g_eye.xyz - position) * g_depth_fog.x + g_depth_fog.y, 0.f, 1.f);
}
#else//VERTEX_SHADER
#if COMPUTE_SHADER
#else//COMPUTE_SHADER
int calculate_tile_index(){
	vec2 p = gl_FragCoord.xy;
	p *= g_inv_resolution_scale;
	p.x *= float(TILE_COUNT_H) * INV_SCREEN_WIDTH;
	p.y *= float(TILE_COUNT_V) * INV_SCREEN_HEIGHT;
	ivec2 ip = ivec2(p);
	int tile_idx = min(int(ip.x + ip.y * TILE_COUNT_H), TILE_COUNT_H * TILE_COUNT_V - 1);
#if (2 == TILE_COUNT_D)
	if(g_light_slice_depth * gl_FragCoord.w < 1.f){//gl_FragCoord.w=1.f/-view_z
		tile_idx += TILE_COUNT_H * TILE_COUNT_V;
	}
#endif//TILE_COUNT_D
	return tile_idx;
}
void fetch_tile(out Tile out_tile){
	int tile_idx = calculate_tile_index();
	ivec2 tile = texelFetch(g_tiles, tile_idx).xy;
	out_tile.m_count = tile.x;
	out_tile.m_index_offset = tile.y;
}
#endif//COMPUTE_SHADER
int calculate_tile_index( vec4 projection){
	if(projection.w < 0.f){//projection.w=-view_z
		return -1;
	}
	vec2 p = projection.xy / projection.w;
	p = clamp(p, vec2(-1.f), vec2(1.f));
	p *= 0.5f;
	p += vec2(0.5f);//[0,1]
	//p *= inv_resolution_scale;//inv_resolution_scaleが必要ならば、引数として渡して、ここで乗算すべし。
	p.x *= float(TILE_COUNT_H);
	p.y *= float(TILE_COUNT_V);
	ivec2 ip = ivec2(p);
	int tile_idx = min(int(ip.x + ip.y * TILE_COUNT_H), TILE_COUNT_H * TILE_COUNT_V - 1);
#if (2 == TILE_COUNT_D)
	if(g_light_slice_depth < projection.w){//projection.w=-view_z
		tile_idx += TILE_COUNT_H * TILE_COUNT_V;
	}
#endif//TILE_COUNT_D
	return tile_idx;
}
void fetch_tile(out Tile out_tile,  vec4 projection){
	int tile_idx = calculate_tile_index(projection);
	if(tile_idx < 0){
		out_tile.m_count = 0;
		out_tile.m_index_offset = 0;
	}else{
		ivec2 tile = texelFetch(g_tiles, tile_idx).xy;
		out_tile.m_count = tile.x;
		out_tile.m_index_offset = tile.y;
	}
}
void fetch_light(
	out Light out_light,
	in  int sequencial_light_index){
	int lgt_idx = texelFetch(g_light_indices, sequencial_light_index).x;
	out_light = g_lights[lgt_idx];
}
#endif//VERTEX_SHADER
#endif//DEPTH_ONLY
// depth & height fog
// 本当はdepthとheightを個別にブレンドした方がいいのだが、
// 輪郭線の濃度決定が複雑になるので深度フォグに対する高さグラデーションという形式をとっている
vec3 calculate_depth_fog_color( in  float position_y ) {
	float fog_curve = clamp( (position_y - g_depth_fog_color[0].w) * g_depth_fog_color[1].w, 0.0f, 1.0f );
	fog_curve = sin( fog_curve * 3.14159265359f * 0.5f ); // linearだとsrgbに戻す時にカーブがかかるので適当なカーブに変換する
	return mix( g_depth_fog_color[0].rgb, g_depth_fog_color[1].rgb, fog_curve );
}
float calculate_depth_fog_ratio( float depth_fog_intensity){
	return 1.f - pow(1.f - depth_fog_intensity, g_depth_fog.w + 1.f);
}
void blend_fog(inout vec3 color,  float depth_fog_intensity,  float position_y){
	vec3 fog_color = calculate_depth_fog_color(position_y);
	color = mix(color, fog_color, calculate_depth_fog_ratio(depth_fog_intensity));
}
// ITU-R BT.601
void to_ycbcr_601( out vec3 ybr, in vec3 rgb ) {
	vec3  _y_coef_601 = vec3(     0.2989,     0.5866,     0.1145 );
	vec3 _cb_coef_601 = vec3( -0.1687747, -0.3312253,        0.5 );
	vec3 _cr_coef_601 = vec3(        0.5, -0.4183426, -0.0816574 );
	ybr.r = dot( rgb, _cr_coef_601 );
	ybr.g = dot( rgb, _y_coef_601 );
	ybr.b = dot( rgb, _cb_coef_601 );
}
void to_rgb_601( out vec3 rgb, in vec3 ybr ) {
	vec3 _red_coef_601 = vec3(    1.4022, 1.0,       0.0 );
	vec3 _grn_coef_601 = vec3( -0.714486, 1.0, -0.345686 );
	vec3 _blu_coef_601 = vec3(       0.0, 1.0,    1.7710 );
	rgb.r = dot( ybr, _red_coef_601 );
	rgb.g = dot( ybr, _grn_coef_601 );
	rgb.b = dot( ybr, _blu_coef_601 );
}
#if (0 == VERTEX_SHADER) && (0 == DEPTH_ONLY) && (0 == TESSELLATION_SHADER) && (0 == COMPUTE_SHADER)
float calculate_local_shadow(in  int shadow_index, in  vec3 world_position){//[0,1]を返す。下限をMIN_SHADOW_INTENSITYでクランプしてから使うこと。
	vec4 wp = vec4(world_position, 1.f);
	const int shadow_index4 = shadow_index * 4;
	vec4 shadow_uv;
	shadow_uv.x = dot(g_local_shadow_fetches[shadow_index4 + 0], wp);
	shadow_uv.y = dot(g_local_shadow_fetches[shadow_index4 + 1], wp);
	shadow_uv.z = dot(g_local_shadow_fetches[shadow_index4 + 2], wp);
	shadow_uv.w = dot(g_local_shadow_fetches[shadow_index4 + 3], wp);
	shadow_uv.xyz /= shadow_uv.w;
	float r = 0.f;
	float offset = 1.f / 1024.f;
	r += texture(sampler2DArrayShadow(unpackUint2x32(g_local_shadow_map_handle)), vec4(shadow_uv.xy, float(shadow_index), shadow_uv.z)).r;
	r += texture(sampler2DArrayShadow(unpackUint2x32(g_local_shadow_map_handle)), vec4(shadow_uv.xy + vec2(-offset, 0.f), float(shadow_index), shadow_uv.z)).r;
	r += texture(sampler2DArrayShadow(unpackUint2x32(g_local_shadow_map_handle)), vec4(shadow_uv.xy + vec2(0.f, -offset), float(shadow_index), shadow_uv.z)).r;
	r += texture(sampler2DArrayShadow(unpackUint2x32(g_local_shadow_map_handle)), vec4(shadow_uv.xy + vec2(-offset, -offset), float(shadow_index), shadow_uv.z)).r;
	return r * 0.25f;
}
float calcualte_self_shadow_split(
	in  int index,
	in  vec4 texcoord,
	in  float texcoord_offset){
	float r = 0.f;
	float offset = texcoord_offset;//2ピクセル幅で良い。sampler2DShadowで2x2は解決済み。
	r += texture(sampler2DShadow((index <= 0 ? g_cascaded_shadow_map_handle_0 : (index == 1 ? g_cascaded_shadow_map_handle_1 : g_cascaded_shadow_map_handle_2))), texcoord.xyz).r;//シングルタップだと厳しい。。
	r += texture(sampler2DShadow((index <= 0 ? g_cascaded_shadow_map_handle_0 : (index == 1 ? g_cascaded_shadow_map_handle_1 : g_cascaded_shadow_map_handle_2))), texcoord.xyz + vec3(-offset, 0.f, 0.f)).r;
	r += texture(sampler2DShadow((index <= 0 ? g_cascaded_shadow_map_handle_0 : (index == 1 ? g_cascaded_shadow_map_handle_1 : g_cascaded_shadow_map_handle_2))), texcoord.xyz + vec3(0.f, -offset, 0.f)).r;
	r += texture(sampler2DShadow((index <= 0 ? g_cascaded_shadow_map_handle_0 : (index == 1 ? g_cascaded_shadow_map_handle_1 : g_cascaded_shadow_map_handle_2))), texcoord.xyz + vec3(-offset, -offset, 0.f)).r;
	return r * 0.25f;
}
float calculate_self_shadow_full_range_from_split(//影で0, 日向で1を返す。
	 float view_distance,
	 vec4 shadow_texcoord0,
	 vec4 shadow_texcoord1,
	 vec4 shadow_texcoord2){
	if(view_distance < g_plane_distances.x){//球での分離のほうが良いけど。
		return calcualte_self_shadow_split(0, shadow_texcoord0, 2.f / 2048.f);
	}else if(view_distance < g_plane_distances.y){
		return calcualte_self_shadow_split(1, shadow_texcoord1, 2.f / 2048.f);
	}else if(view_distance < g_plane_distances.z){
		return calcualte_self_shadow_split(2, shadow_texcoord2, 2.f / 2048.f);
	}
	return 1.f;
}
float calculate_self_shadow_full_range(//影で0, 日向で1を返す。
	 float view_distance,
	 vec4 shadow_texcoord0,
	 vec4 shadow_texcoord1,
	 vec4 shadow_texcoord2,
	 uint extra_shader_flags){
	//スクリーンスペースの影が使えなければ(半透明, 髪越しのポリゴン)、PCF*4にfallback。
	//現在のzが、zバッファに書き込まれている値よりも小さければ、使えない。等しければ使える。
	//const float raw_depth = textureLod(g_scene_raw_depth_sampler, gl_FragCoord.xy * g_inverse_render_target_size, 0.f).r;
	//near=1, far=0
	//処理しているピクセルよりも既存のzが奥ならば(=処理しているピクセルのzが塗られていない。つまり半透明。)、raw_depth<gl_FragCoord.z。
#if (SHADOW_MODE == ShadowMode_Resolved)
	return texture(sampler2D(unpackUint2x32(g_screen_shadow_map_handle)), gl_FragCoord.xy * vec2(INV_SCREEN_WIDTH, INV_SCREEN_HEIGHT), 0).x;
#elif (SHADOW_MODE == ShadowMode_Unresolved)
	return calculate_self_shadow_full_range_from_split(view_distance, shadow_texcoord0, shadow_texcoord1, shadow_texcoord2);
#else//ShadowMode_Auto
	if(0 == (extra_shader_flags & ExtraShaderFlag_Transparency)){
		return texture(sampler2D(unpackUint2x32(g_screen_shadow_map_handle)), gl_FragCoord.xy * vec2(INV_SCREEN_WIDTH, INV_SCREEN_HEIGHT), 0).x;
	}else{
		return calculate_self_shadow_full_range_from_split(view_distance, shadow_texcoord0, shadow_texcoord1, shadow_texcoord2);
	}
#endif//SUPPORT_UNRESOLVED_SHADOW
}
float calculate_self_shadow(//影で0, 日向で1を返す。
	 float view_distance,
	 vec4 shadow_texcoord0,
	 vec4 shadow_texcoord1,
	 vec4 shadow_texcoord2,
	 uint extra_shader_flags){
	float s = calculate_self_shadow_full_range(view_distance, shadow_texcoord0, shadow_texcoord1, shadow_texcoord2, extra_shader_flags);
	return s;
}
float calculate_linear_depth_from_raw_depth( vec2 uv){//hairの書き込まれていないzを使う場合に使用すること。そうでなければg_scene_linear_depth_samplerを使うこと。//奥が正。
	const float raw_depth = textureLod(g_scene_raw_depth_sampler, uv, 0.f).r;
	return -1.f / (g_depth_modifiers.x * raw_depth - g_depth_modifiers.w);
}
float calculate_ssao( float intensity){
	return mix(1.f, textureLod(sampler2D(unpackUint2x32(g_ssao_map_handle)), gl_FragCoord.xy * g_inverse_render_target_size, 0.f).x, intensity);//dummyテクスチャを使うために、texelFetchは使わない。
}
#endif//VERTEX_SHADER
float calculate_cloud_shadow_( vec3 world_position){//0=影,1=日向。
	float s = textureLod(g_cloud_shadow_sampler, g_cloud_shadow_texcoords[0].xy + vec2(world_position.xz - g_parallel_light_dir.xz * world_position.y) * g_cloud_shadow_texcoords[0].z, 0.f).r;//[-1,1]//z=0.0025f
	s += g_cloud_shadow_texcoords[0].w;//大きいほど削れる。
	s = clamp(s, 0.f, 1.f);
	s = pow(s, 2.2f);//linearだと調整しにくい。srgbとして使う。
	return s;
}
float calculate_cloud_shadow( vec3 world_position){
	const float thickness = g_cloud_shadow_texcoords[1].x;
	return calculate_cloud_shadow_(world_position) * thickness + (1.f - thickness);
}
float calculate_cloud_shadow_npr( vec3 world_position){
	const float thickness = g_cloud_shadow_texcoords[1].y;
	return calculate_cloud_shadow_(world_position) * thickness + (1.f - thickness);
}
#if (SHADER_SUB_TYPE0 == ShaderSubType0_OutputTextureLod)
//for debug
vec4 color_from_lod( float lod){
	vec3 pallets[4] = {
		vec3(0.f, 0.f, 1.f),
		vec3(0.f, 1.f, 0.f),
		vec3(1.f, 1.f, 0.f),
        vec3(1.f, 0.f, 0.f),
	};
	int p0 = int(lod);
	if(3 <= p0){
		return vec4(pallets[3], 1.f);
	}else{
		int p1 = p0 + 1;
		float r = lod - p0;
		return vec4(mix(pallets[p0], pallets[p1], r), 1.f);
	}
}
#define OUTPUT_TEXTURE_LOD(sampler_index, texcoord){\
	if(sampler_index == int(OUTPUT_LOD_SAMPLER_INDEX)){\
		float lod = textureQueryLod(sampler2D(unpackUint2x32(_amdshim_map_handle(int(sampler_index)))), texcoord).x;\
		result.rgba = color_from_lod(lod);\
		return;\
	}\
}
#define OUTPUT_TEXTURE_LOD_DIRECTLY(sampler_index, lod){\
	if(sampler_index == int(OUTPUT_LOD_SAMPLER_INDEX)){\
		result.rgba = color_from_lod(lod);\
		return;\
	}\
}
#else//ShaderSubType0_OutputTextureLod
#define OUTPUT_TEXTURE_LOD(sampler_index, texcoord)
#define OUTPUT_TEXTURE_LOD_DIRECTLY(sampler_index, lod)
#endif//ShaderSubType0_OutputTextureLod
float calculate_corrected_reflection_inner(//重みを返す。見つからない場合は0.fを返す。//取り込みAABBの外側への漏れだしを禁止した版。
	out vec3 out_direction,//正規化されていない。
	 vec3 ray_origin,
	 vec3 ray_direction,
	 vec3 aabb_min,
	 vec3 aabb_max,
	 vec3 sampling_position,
	 float blend_distance){//AABBの内側で補間する。
	//aabb内に始点がなければ交差しない。
	if(	any(lessThan(ray_origin, aabb_min)) ||
		any(lessThan(aabb_max, ray_origin))){
		return 0.f;
	}
	vec3 a = (aabb_max - ray_origin) / ray_direction;
	vec3 b = (aabb_min - ray_origin) / ray_direction;
	vec3 t_forward = max(a, b);//起点がAABB内にあれば成分ごとにa, bのいずれかが正で, もうひとつ負である。正を選択。//起点がAABB外であれば、AABBの無限平面との交点のうち最遠方のものが選択されるだけ。
	float t = min(min(t_forward.x, t_forward.y), t_forward.z);//衝突までの最小の時間を計算。
	vec3 p = ray_origin + ray_direction * t;
	out_direction = p - sampling_position;
	out_direction.x *= 1.f / (aabb_max.x - aabb_min.x);//@todo	最適化
	out_direction.y *= 1.f / (aabb_max.y - aabb_min.y);
	out_direction.z *= 1.f / (aabb_max.z - aabb_min.z);
	float w = 1.f;
	if(0.f < blend_distance){//AABBからの距離に応じて重みを決定。
		vec3 a = ray_origin - (aabb_max - vec3(blend_distance));
		vec3 b = (aabb_min + vec3(blend_distance)) - ray_origin;
		vec3 t_min = max(a, b);
		float t_blend = max(max(t_min.x, t_min.y), t_min.z);
		w = 1.f - t_blend / blend_distance;
	}
	return w;
}
vec3 fetch_environment_reflection( vec3 position,  vec3 direction,  float lod,  float npr_intensity,  int shader_flags){
	const uint c = uint(g_reflection_proxy_count);
	if(	(0 == c) ||
		(0 != (ShaderFlag_HaveUserEnvironmentMap & shader_flags))){
		return textureLod(g_env_sampler, direction, lod).rgb;
	}
	float w = 0.f;
	vec3 env = vec3(0.f);
	for(int i = 0; i < c; ++ i){//deferredにしたほうが良いかも。重い。
		vec3 r;
		vec3 aabb_min = g_reflection_proxies[i].m_aabb_min.xyz;
		vec3 aabb_max = g_reflection_proxies[i].m_aabb_max.xyz;
		vec3 sampling_position = (aabb_min + aabb_max) * 0.5f;
		float wt = calculate_corrected_reflection_inner(
			r, position, direction, aabb_min, aabb_max, sampling_position, g_reflection_proxies[i].m_aabb_min.w);
		wt *= mix(1.f, g_reflection_proxies[i].m_aabb_max.w, npr_intensity);
		if(0.f < wt){
			env += textureLod(samplerCube(unpackUint2x32(g_reflection_proxies[i].m_cube_texture_handle)), r, lod).rgb * wt;
			w += wt;
			if(1.f <= w){
				return env / w;//打ち切り。
			}
		}
	}
	env += textureLod(g_env_sampler, direction, lod).rgb * (1.f - w);//不足分をグローバルなマップで補充。
	return env;
}

//ライティング計算。
#define LightFlag_Spot (1 << 16)
const float s_fresnel_modifier = 0.25f;//デザインさんの要望。
float visibility_implicit(in float nl, in float ne, in float specular_power){
	return 1.f;
}
float visibility_shlick(in float nl, in float ne, in float specular_power){//ジオメトリ項とBRDFの分母を組み合わせたもの。これをvisiblity項と呼ぶ。
	float k = 2.f / sqrt(3.1415926535f * (specular_power + 2.f));
	float ik = 1.f - k;
	return 1.f / ((nl * ik + k) * (ne * ik + k));
}
float d_ggx( float roughness,  float nh){
	float a = roughness * roughness;
	float aa = a * a;
	float t = nh * nh * (aa - 1.f) + 1.f;
	return aa / (PI * t * t);
}
float vis_smith( float roughness,  float nv,  float nl){
	float a = roughness * roughness;
	float aa = a * a;
	return 1.f / ((nv + sqrt(nv * (nv - nv * aa) + aa)) * (nl + sqrt(nl * (nl - nl * aa) + aa)));
}
float vis_smith_approx( float roughness,  float nv,  float nl){
	float a = roughness * roughness;
	float v_v = nv * (nv * (1.f - a) + a);
	float v_l = nl * (nl * (1.f - a) + a);
	return 0.5f / (v_v + v_l + 0.000001f);
	//return 0.5f / (v_v + v_l);//0<aでもnl=0&&nv=0でゼロ除算!
}
vec3 f_schlick( vec3 specular_color,  float vh){
	float t = 1.f - vh;
	float fc = t * t;
	fc *= fc;
	fc *= t;
	return min(50.f * specular_color.g, 1.f) * fc + (1.f - fc) * specular_color;//2%未満は物理的にあり得ないらしい。
}
void calculate_direct_light_ggx(
	out float out_diffuse,
	out vec3 out_specular,
	in  vec3 l,
	in  vec3 specular_color,
	in  vec3 e,
	in  vec3 n,
	in  float ne,//clamp(dot(n, e), 0.f, 1.f);
	in  float roughness,
	in  float min_diffuse_intensity){
	const vec3 v = e;
	const vec3 h = normalize(l + v);
	const float nl = clamp(dot(n, l), 0.f, 1.f);
	const float nv = ne;
	const float nh = clamp(dot(n, h), 0.f, 1.f);
	const float vh = clamp(dot(v, h), 0.f, 1.f);
	const float specular_d = d_ggx(roughness, nh);
	const float specular_vis = vis_smith_approx(roughness, nv, nl);//specular vis(=G/(4.f*nl*nv))
	const vec3 specular_f = f_schlick(specular_color, vh);
	vec3 specular = specular_f * specular_d * specular_vis;//specular_fでspecular_colorを乗算済み。
	out_diffuse = max(nl / PI, min_diffuse_intensity);
	out_specular = specular * nl;
}
void calculate_direct_light_half_lambert_ggx_(
	out float out_diffuse,
	out vec3 out_specular,
	in  vec3 l,
	in  vec3 specular_color,
	in  vec3 e,
	in  vec3 n,
	in  float roughness,
	in  float min_diffuse_intensity,
	in  float lambert_ratio){
	const vec3 v = e;	
	const vec3 h = normalize(l + v);
	const float nl = clamp(dot(n, l), 0.f, 1.f);
	const float nv = clamp(dot(n, v), 0.f, 1.f);
	const float nh = clamp(dot(n, h), 0.f, 1.f);
	const float vh = clamp(dot(v, h), 0.f, 1.f);
	const float specular_d = d_ggx(roughness, nh);
	const float specular_vis = vis_smith_approx(roughness, nv, nl);//specular vis(=G/(4.f*nl*nv))
	const vec3 specular_f = f_schlick(specular_color, vh);
	vec3 specular = specular_f * specular_d * specular_vis;//specular_fでspecular_colorを乗算済み。
	out_diffuse = max((nl * lambert_ratio + (1.f - lambert_ratio)) / PI, min_diffuse_intensity);
	out_specular = specular * nl;
}
float calculate_point_light_falloff( in vec3 distance_vec, in float radius ) {
	//減衰係数を計算。UE4方式。
	vec3 l = distance_vec;
	float ll = dot(l, l);
	float rr = radius * radius;
	float t = ll / rr;
	t *= t;//=(l/r)^4
	t = clamp(1.f - t, 0.f, 1.f);
	float falloff = t * t / (ll + 1.f);
	return falloff;
}
float calculate_sphere_light_falloff( in vec3 distance_vec, in float max_radius, in float min_radius ) {
	// 線形距離の比率を求める
	vec3 l = distance_vec;
	float ll = dot( l, l );
	ll = sqrt(ll);
	float rr = max_radius;
	float t = (ll) / rr;
	// 中心からpeakが1になり、peakからmax radiusまでリニアに0へ収束させる
	float peak =  min_radius / rr;
	t = (1.0 - t);
	t = clamp(t, 0.f, 1.f);
	t /= max(1.0f - peak, 1e-4);
	// UE4と同等の減衰カーブを得るには、(1-(1-t)^4)^2にする
	t = clamp(1.0f - t, 0.f, 1.f);
	t *= t;
	t *= t;
	t = 1.0f - t;
	// 除算後も中心箇所～areaまでが1になるようする
	float falloff =  t * t / (max( (ll*ll ) - (min_radius*min_radius), 0.0f) + 1.f);
	return falloff;
}
float calculate_spot_attenuation(
	in  vec4 light_packed,
	in  vec3 l){
	uint flags = floatBitsToUint(light_packed.x);
	if(0 == (flags & LightFlag_Spot)){
		return 1.f;//点光源。減衰なし。
	}
	//スポットライト。開き角で減衰。
	vec3 cone_direction;
	cone_direction.xy = unpackHalf2x16(floatBitsToUint(light_packed.y));
	vec2 t = unpackHalf2x16(floatBitsToUint(light_packed.z));
	cone_direction.z = t.x;
	const float outer_aperture_cos = t.y;
	float attenuation = -dot(l, cone_direction);//コーンの前方で正。
	if(attenuation < outer_aperture_cos){//コーンの外側。
		attenuation = 0.f;
	}else{
		const float inner_aperture_cos = unpackHalf2x16(floatBitsToUint(light_packed.w)).x;
		if(outer_aperture_cos < inner_aperture_cos){
			attenuation *= min(1.f, (attenuation - outer_aperture_cos) / (inner_aperture_cos - outer_aperture_cos));
		}
	}
	return attenuation;
}
void calculate_direct_sphere_light_ggx(
	out float out_diffuse,
	out vec3 out_specular,
	in  Light light,
	in  vec3 specular_color,
	in  vec3 e,
	in  vec3 n,
	in  float ne,//clamp(dot(n, e), 0.f, 1.f);
	in  vec3 position,
	in  float roughness_,
	in  float min_diffuse_intensity){
	float min_radius = light.m_color.w; // スフィア半径
	float max_radius = light.m_sphere.w; // ライト半径
	vec3 l = light.m_sphere.xyz - position;
	float falloff_sp = calculate_point_light_falloff( l, max_radius ); // スペキュラはタイルからはみ出さない用にするだけ
	float falloff = calculate_sphere_light_falloff( l, max_radius, min_radius );
	const float min_roughness = (floatBitsToUint(light.m_packed.x) >> 24) / 255.f;
	const float roughness = max(roughness_, min_roughness);
	l = normalize(l);
#if 01
	const float spot_attenuation = calculate_spot_attenuation(light.m_packed, l);//sphere lightの計算でlが変更されるので先に。
	falloff *= spot_attenuation;
	falloff_sp *= spot_attenuation;
#endif
#if 01
	float sphere_radius = light.m_color.w;
	if(0.f < sphere_radius){//sphere light
		vec3 t = light.m_sphere.xyz - position;
		vec3 r = -e + 2.f * dot(n, e) * n;
		vec3 center_to_ray = dot(t, r) * r - t;
		vec3 closest_point = light.m_sphere.xyz + center_to_ray * min(sphere_radius / length(center_to_ray), 1.f);//反射ベクトルに最も近い、光源球上の点。
		l = closest_point.xyz - position;
		//エネルギー保存。
		float m = roughness * roughness;
		float s = m / (m + 0.5f * (sphere_radius / length(light.m_sphere.xyz - position)));
		falloff_sp *= s * s;
		l = normalize(l);
	}
#endif
	calculate_direct_light_ggx(out_diffuse, out_specular, l, specular_color, e, n, ne, roughness, min_diffuse_intensity);
	out_diffuse *= falloff;
	out_specular *= falloff_sp;
}
void calculate_direct_point_light_ggx(
	out float out_diffuse,
	out vec3 out_specular,
	in  Light light,
	in  vec3 specular_color,
	in  vec3 e,
	in  vec3 n,
	in  float ne,//clamp(dot(n, e), 0.f, 1.f);
	in  vec3 position,
	in  float roughness,
	in  float min_diffuse_intensity){
	calculate_direct_sphere_light_ggx(out_diffuse, out_specular, light, specular_color, e, n, ne, position, roughness, min_diffuse_intensity);
}
void calculate_direct_point_light_half_lambert_ggx_(
	out float out_diffuse,
	out vec3 out_specular,
	in  Light light,
	in  vec3 specular_color,
	in  vec3 e,
	in  vec3 n,
	in  vec3 position,
	in  float roughness,
	in  float min_diffuse_intensity,
	in  float lambert_ratio){
	float min_radius = light.m_color.w;
	float max_radius = light.m_sphere.w;
	vec3 l = light.m_sphere.xyz - position;
	float falloff_sp = calculate_point_light_falloff(l, max_radius);
	float falloff = calculate_sphere_light_falloff(l, max_radius, min_radius);
	l = normalize(l);
#if 01
	const float spot_attenuation = calculate_spot_attenuation(light.m_packed, l);//sphere lightの計算でlが変更されるので先に。
	falloff *= spot_attenuation;
	falloff_sp *= spot_attenuation;
#endif
	calculate_direct_light_half_lambert_ggx_(out_diffuse, out_specular, l, specular_color, e, n, roughness, min_diffuse_intensity, lambert_ratio);
	out_diffuse *= falloff;
	out_specular *= falloff_sp;
}

layout(binding = UboIndex_PerMaterial) uniform PerMaterial{
	vec4 g_color_gains[PackedColorGainCount];//{d0+o,d1+bs,d2+height_scale,d3+alpha_ref,bl,ld,s0,s1,s2,s3}
	vec4 g_material_texcoord_transforms[PackedTexcoordTransformCount];//x=m00, y=m01, z=m10, w=m11, [0,3]=mat,[4]=weight,[5]=baked shadow,[6]=baked light,[7]=opacity,[8]=layered diffuse, //マテリアルごとのテクスチャ変換のみ。基底変換にのみ使う。
	vec4 g_tangent_transforms[PackedTangentTransformCount];//t'=x*t+y*b,b'=z*t+w*b, [0,3]=tex
	vec4 g_fresnel_color;//w=MSB|fresnel_intensity(16bits)|fresnel_color_power(16bits)|LSB
	vec4 g_npr_diffuse_param; // x:bias, y:directinaol light occlusion infulence, z:baked shadow mode, w: diffuse softness
	vec4 g_npr_ambient_param; // x:gain
	vec4 g_npr_rim_param; // x:size, y:roll begin, z:roll_end, w:gain
	vec4 g_npr_specular_param; // x:size, y:roll begin, z:roll_end, w:gain
	vec4 g_npr_anisotropic_param; // x:offset, y:normal blend rate, z:overdraw_mask, w:specularmask scale for rimlight
	vec4 g_npr_eye_paramf[2];//[1].z=1.f/z_occlusion_attenuation, [1].w=z_occlusion_attenuation_opacity_min
	ivec4 g_npr_blendfactor[2]; // 0:diffuse normalize, 1: ambient, 2:rim, 3:specular 4:aniso
	ivec4 g_npr_eye_parami;//x=shader_flags
	uvec4 _amdshim_map_handle_pairs[9];
	uvec2 _amdshim_map_handle_last;//SAMPLER_INDEX_ENVIRONMENT_CUBEは含まず。
};
uint64_t _amdshim_map_handle(int i) {
	if (i == 18) return packUint2x32(_amdshim_map_handle_last);
	uvec4 pair_words = _amdshim_map_handle_pairs[i >> 1];
	return packUint2x32(((i & 1) == 0) ? pair_words.xy : pair_words.zw);
}

#define g_npr_shadow_param g_tangent_transforms[3]//x=shadow rate, y=planar_reflection_map_intensity, z=color_scale, w=shadow_z_offset
#define g_planar_reflection_map_intensity g_npr_shadow_param.y
#define g_npr_inv_z_occlusion_attenuation g_npr_eye_paramf[1].z
#define g_npr_z_occlusion_attenuation_opacity_min g_npr_eye_paramf[1].w
#define g_alpha_ref g_color_gains[4].w
#define g_shader_flags g_npr_eye_parami.x
#define g_effect_rim_opacity g_npr_rim_param.x
layout(std140, binding = UboIndex_PerBatch) uniform PerBatch{//ココを変えたらshadow_map_impl.h内のシェーダも書き換えること。
	ivec4 g_texcoord_indices[2];//[0].x=mat, y=weight, z=baked shadow, w=baked light, [1].x=opacity0, y=opacity1, z=layered diffuse, w=normal0
	vec4 g_texcoord_transforms[PackedTexcoordTransformCount * 2];//[0].x=m00,y=m01,z=m02,w=unused, [1].x=m10,y=m11,z=m12,w=unused, batch毎のdecorderを乗算済み。並びはPerMaterialと同じ。
};
#define DIFFUSE0_TEXCOORD_INDEX g_texcoord_indices[0].x
#define WEIGHT_TEXCOORD_INDEX g_texcoord_indices[0].y
#define BAKED_SHADOW_TEXCOORD_INDEX g_texcoord_indices[0].z
#define BAKED_LIGHT_TEXCOORD_INDEX g_texcoord_indices[0].w
#define OPACITY0_TEXCOORD_INDEX g_texcoord_indices[1].x
#define OPACITY1_TEXCOORD_INDEX g_texcoord_indices[1].y
#define LAYERED_DIFFUSE_TEXCOORD_INDEX g_texcoord_indices[1].z
#define NORMAL0_TEXCOORD_INDEX g_texcoord_indices[1].w
//[0].x=ambient_scale, y=fog_intensity, zw=min_intensity
//[1].x=color_scale_by_effect, [1].y=dither, [1].z={vanishing_y_max(npr), dither_z_offset(pr)}, [1].w=extra_shader_flags
//[2].xyz=dissolve_color, [2].w=dissolve_noise_texcoord_scale
//[3].xyz=dissolve_protect_sphere_center, [3].w=dissolve_protect_sphere_radius
uniform vec4 g_per_draw[4];
#define AMBIENT_SCALE g_per_draw[0].x
#if !defined(FOG_INTENSITY)
#define FOG_INTENSITY g_per_draw[0].y
#endif//FOG_INTENSITY
#if !defined(MIN_SHADOW_INTENSITY)
#define MIN_SHADOW_INTENSITY g_per_draw[0].z
#endif//MIN_SHADOW_INTENSITY
#define g_min_diffuse_intensity g_per_draw[0].w
#define COLOR_SCALE_BY_EFFECT g_per_draw[1].x
#define g_dither_ratio g_per_draw[1].y
#define g_vanishing_y_max g_per_draw[1].z//dissolve_noise_texcoord_scale乗算済み。
#define g_dither_z_offset g_per_draw[1].z
#define g_extra_shader_flags floatBitsToUint(g_per_draw[1].w)
#define g_dissolve_color g_per_draw[2].xyz
#define g_dissolve_noise_texcoord_scale g_per_draw[2].w
#define g_dissolve_protect_sphere g_per_draw[3].xyzw
void blend_fresnel_color(
	inout vec3 in_out,
	 vec3 e,
	 vec3 normal){//g_fresnel_color.w!=0の時だけコールすること。
	vec2 packed_fresnel = unpackHalf2x16(floatBitsToUint(g_fresnel_color.w));//x=intensity, y=power
	float fresnel = 1.f - dot(e, normal);
	fresnel = pow(fresnel, packed_fresnel.y);
	in_out.rgb = mix(in_out.rgb, g_fresnel_color.xyz, min(packed_fresnel.x * fresnel, 1.f));
}

#define VolumeFogCompositeType_Over 1
#define VolumeFogCompositeType_Additive 2
#define VolumeFogCompositeType_AdditiveOne 3
#define VOLUME_FOG_TEXCOORD_FROM_WORLD_ROW0 g_volume_fog_texcoord_from_worlds_in_cmn[0]
#define VOLUME_FOG_TEXCOORD_FROM_WORLD_ROW1 g_volume_fog_texcoord_from_worlds_in_cmn[1]
#define VOLUME_FOG_TEXCOORD_FROM_WORLD_ROW2 g_volume_fog_texcoord_from_worlds_in_cmn[2]
#define VOLUME_FOG_SLICE_FROM_LINEAR_DEPTH g_volume_fog_slice_from_linear_depth_in_cmn
#define COMPOSITE_VOLUME_FOG (SHADER_SUB_TYPE0 == ShaderSubType0_CompositeVolumeFog)
#define g_volume_fog_composite_type ((floatBitsToUint(g_per_draw[1].w) >> 29) & ((1 << 3) - 1))
vec3 calculate_volume_fog_texcoord(
	 vec3 world,
	 vec4 volume_texcoord_from_world_row0,
	 vec4 volume_texcoord_from_world_row1,
	 vec4 volume_texcoord_from_world_row2,
	 vec3 volume_slice_from_linear_depth){
	vec3 t;
	t.x = dot(volume_texcoord_from_world_row0, vec4(world, 1.f));
	t.y = dot(volume_texcoord_from_world_row1, vec4(world, 1.f));
	t.z = dot(volume_texcoord_from_world_row2, vec4(world, 1.f));
	t.xy /= t.z;
	t.z = log2(t.z * volume_slice_from_linear_depth.z + volume_slice_from_linear_depth.y) * volume_slice_from_linear_depth.x;
	return t;
}
vec4 composite_volume_fog( vec4 color_without_fog,  vec4 fog,  uint composite_type){
	vec4 r = color_without_fog;
	if(composite_type == VolumeFogCompositeType_Over){
		r.rgb = r.rgb * fog.aaa + fog.rgb;
	}else if(composite_type == VolumeFogCompositeType_Additive){
		r.rgb = r.rgb * r.a * fog.a;
		r.a = 1.f;
	}else if(composite_type == VolumeFogCompositeType_AdditiveOne){
		r.rgb = r.rgb * fog.a;
		r.a = 1.f;
	}else{//other, ここがコールされないように事前に分岐しておくこと。
	}
	return r;
}
#if COMPOSITE_VOLUME_FOG
layout(binding = SamplerIndexInShader_VolumeFog) uniform sampler3D g_volume_fog_sampler;
vec4 composite_volume_fog(
	in  vec4 color_without_fog,
	in  vec3 world_position,
	in  uint composite_type){
	vec4 r = color_without_fog;
	vec3 t = calculate_volume_fog_texcoord(
		world_position,
		VOLUME_FOG_TEXCOORD_FROM_WORLD_ROW0,
		VOLUME_FOG_TEXCOORD_FROM_WORLD_ROW1,
		VOLUME_FOG_TEXCOORD_FROM_WORLD_ROW2,
		VOLUME_FOG_SLICE_FROM_LINEAR_DEPTH.xyz);
	const vec4 fog = textureLod(g_volume_fog_sampler, t, 0.f);
	return composite_volume_fog(r, fog, composite_type);
}
#endif//COMPOSITE_VOLUME_FOG

/*#undef BLEND_MAP_CAPACITY
#define BLEND_MAP_CAPACITY 0
//*/
#if ((0 == ENABLE_PUNCH) && (0 == ENABLE_DITHER)) || USE_TINY_GBUFFER
layout(early_fragment_tests) in;
#endif
//layout (depth_unchanged) out float gl_FragDepth;
#if DEPTH_ONLY
in VertexData{
#if USE_TESSELLATOR
	vec4 position;//xyz_world_position, w=dither_z
#if ENABLE_PUNCH
	vec4 opacity_texcoord;//xy=opacity0, zw=opacity1
#endif//ENABLE_PUNCH
#elif ENABLE_PUNCH
	vec4 position;//xyz_world_position, w=dither_z
	vec4 opacity_texcoord;//xy=opacity0, zw=opacity1
#else//ENABLE_PUNCH
	vec2 dither_z;
#endif//ENABLE_PUNCH
}frg;
#else//DEPTH_ONLY
in VertexData{
	vec4 position;//xyz=world_position, w=dither_z(1.f-snow_amount if USE_TESSELLATOR)
	vec4 opacity_texcoord;//xy=opacity0, zw=opacity1
	vec4 texcoords0;
	vec4 texcoords1;
	vec4 texcoords2;
	vec4 texcoords3;
#if (0 == CONSTANT_SHADING)//CONSTANT_SHADING
	vec3 tangents[4];
	vec3 binormals[4];
	vec3 normal;
#if (SHADOW_MODE != ShadowMode_Resolved)
	vec4 shadow_texcoords[SHADOW_MAP_COUNT];
	float view_distance;
#endif//SHADOW_MODE
#elif EFFECT_SHADER
	vec3 normal;
#endif//CONSTANT_SHADING
	float fog;
#if HAVE_VERTEX_COLOR
	vec4 color;
#endif
}frg;
#endif//DEPTH_ONLY
#if OUTPUT_TINY_GBUFFER
layout(location = 0) out vec4 result_normal;//xyz=world_normal, w=0(non_npr_pixel), !=0(npr_pixel);
layout(location = 1) out vec4 result_specular;//xyz=specular, w=glossiness
layout(location = 2) out vec4 result_diffuse;//xyz=diffuse, w=unused
#elif DEPTH_ONLY
layout(location = 0) out vec4 result;
#else//DEPTH_ONLY
layout(location = 0) out vec4 result;
layout(location = 1) out vec4 result_normal;//w=0(non_npr_pixel), !=0(npr_pixel);
layout(location = 2) out vec4 result_npr_vertex_color;
#endif//DEPTH_ONLY
#if (0 == DEPTH_ONLY)
#if (0 == CONSTANT_SHADING)
vec3 calculate_normal_from_normal_map(in  int normal_map_index, in  vec2 texcoord){//normal_map_index=[0,3]
	vec3 normal_map;
	normal_map.xy = texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_NORMAL0 + normal_map_index)))), texcoord).rg * 2.f - 1.f;
	normal_map.z = sqrt(max(1.f - dot(normal_map.xy, normal_map.xy), 0.f));//|normal_map.xy|が1.fを超え得るようだ。テクスチャフィルタが原因だろう。
	vec3 n;
	n = frg.normal * normal_map.z;
	n += frg.tangents[normal_map_index] * normal_map.x;
	n += frg.binormals[normal_map_index] * normal_map.y;
	return n;
}
#endif
// depth fog
void blend_layered_diffuse(inout vec3 color, in  vec2 texcoord){
	vec4 layered_color = texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_LAYERED_DIFFUSE)))), texcoord);
	layered_color *= LAYERED_DIFFUSE_COLOR_GAIN;
	color = mix(color, layered_color.rgb, layered_color.a);
}
void blend_vertex_color(inout vec3 color){
#if HAVE_VERTEX_COLOR && (0 == USE_TINY_GBUFFER)//gbuffer内に乗算済み。
	color *= frg.color.rgb;
#endif//HAVE_VERTEX_COLOR && (0 == USE_TINY_GBUFFER)
}
void blend_baked_shadow(inout vec3 color, in  vec2 texcoord){
	color.rgb *= texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_BAKED_SHADOW)))), texcoord).r * BAKED_SHADOW_GAIN;
}
void blend_baked_light(inout vec3 color, in  vec2 texcoord){
	color.rgb += texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_BAKED_LIGHT)))), texcoord).rgb * BAKED_LIGHT_COLOR_GAIN;
}
#endif//DEPTH_ONLY
float calculate_opacity(
	in  vec2 texcoord0,
	in  vec2 texcoord1,
	in  vec3 e){
	float opacity = texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_OPACITY0)))), texcoord0).r * OPACITY0_GAIN;
	opacity *= texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_OPACITY1)))), texcoord1).r * OPACITY1_GAIN;
#if (0 == DEPTH_ONLY)
#if HAVE_VERTEX_COLOR
	opacity *= frg.color.a;
#endif//HAVE_VERTEX_COLOR
#if EFFECT_SHADER
	if(g_effect_rim_opacity < 1.f){//フレネル項により透明度を補正。
		float f = dot(frg.normal, e);
		f = min(clamp(gl_FrontFacing ? f : -f, 0.f, 1.f) / (1.f - g_effect_rim_opacity), 1.f);
		f = pow(f, 4.f);
		opacity *= f;
	}
#endif//EFFECT_SHADER
#endif//DEPTH_ONLY
	return opacity;
}
void discard_by_opacity(in  float opacity){
#if (1 == ENABLE_PUNCH) && (0 == USE_TINY_GBUFFER)
	if(opacity < g_alpha_ref){//punching.
		discard;
	}
#endif
}
#if ENABLE_DITHER
void discard_by_dither_( float dither_z,  float position_y){
	float d = texelFetch(g_dither_sampler, ivec2(int(gl_FragCoord.x) % 4, int(gl_FragCoord.y) % 4), 0).r;
	float dither_ref = clamp(dither_z * g_dither_modifier.x + g_dither_modifier.y, 0.f, 1.f) * g_dither_modifier.z;
	if(g_height_dither_min <= position_y){
		dither_ref += position_y - g_height_dither_min;
	}
	if(d - g_dither_ratio < dither_ref){
		discard;
	}
}
void discard_by_dither(){
#if DEPTH_ONLY && (0 == ENABLE_PUNCH)
	float dither_z = frg.dither_z.x;
	float wy = frg.dither_z.y;
#else//DEPTH_ONLY && (0 == ENABLE_PUNCH)
	float dither_z = frg.position.w;
	float wy = frg.position.y;
#endif//DEPTH_ONLY && (0 == ENABLE_PUNCH)
	discard_by_dither_(dither_z, wy);
}
#else//ENABLE_DITHER
void discard_by_dither(){
}
#endif//ENABLE_DITHER
#if DEPTH_ONLY
void main(){
	discard_by_dither();
#if ENABLE_PUNCH
	const vec3 e = normalize(g_eye.xyz - frg.position.xyz);
	result.a = calculate_opacity(frg.opacity_texcoord.xy, frg.opacity_texcoord.zw, e);
	discard_by_opacity(result.a);
//gl_FragDepth = gl_FragCoord.w;//test
#endif//ENABLE_PUNCH
	result = vec4(1.f);
}
#elif (1 == CONSTANT_SHADING)
void main(){
	discard_by_dither();
	const vec3 e = normalize(g_eye.xyz - frg.position.xyz);
	result.a = calculate_opacity(frg.opacity_texcoord.xy, frg.opacity_texcoord.zw, e);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_OPACITY0, frg.opacity_texcoord.xy);
	discard_by_opacity(result.a);
	result.rgb = texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_DIFFUSE0)))), frg.texcoords0.xy).rgb * DIFFUSE0_COLOR_GAIN;
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_DIFFUSE0, frg.texcoords0.xy);
#if USE_OPACITY_AS_COLOR_SCALER//コレが立っているときは、出力αが透明度にはならない。
	result.rgb /= result.a + 0.004f;
	result.a = 1.f;
#endif//USE_OPACITY_AS_COLOR_SCALER
	blend_layered_diffuse(result.rgb, frg.texcoords3.zw);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_LAYERED_DIFFUSE, frg.texcoords3.zw);
	blend_vertex_color(result.rgb);
	blend_baked_shadow(result.rgb, frg.texcoords2.zw);//skyはdiffuse0のみ。なので、このあたり削るべし。
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_BAKED_SHADOW, frg.texcoords2.zw);
	blend_baked_light(result.rgb, frg.texcoords3.xy);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_BAKED_LIGHT, frg.texcoords3.xy);
	blend_fog(result.rgb, frg.fog, frg.position.y);
	//if(0 != (ShaderFlag_ModifyByEffect & g_shader_flags)){//これどうすっかな//空以外のコンスタントの強度。とかのパラメータをライトに追加するか。//背景にコンスタントのものは置くのか?
		result.rgb *= COLOR_SCALE_BY_EFFECT;
	//}
	result.rgb = max(result.rgb, vec3(0.f));
	result_normal = vec4(0.f, 0.f, 0.f, OVERDRAW_MASK);//OVERDRAW_MASK<=0でoverdrawが描かれる。0<OVERDRAW_MASKでoverdrawが描かれない。
	result_npr_vertex_color = vec4(0.f, 0.f, 0.f, 0.f);
#if COMPOSITE_VOLUME_FOG
	result = composite_volume_fog(result, frg.position.xyz, g_volume_fog_composite_type);
#endif//COMPOSITE_VOLUME_FOG
}
#else//CONSTANT_SHADING
void main(){
	discard_by_dither();
	const vec3 e = normalize(g_eye.xyz - frg.position.xyz);
	float result_a = 0.f;
	result_a = calculate_opacity(frg.opacity_texcoord.xy, frg.opacity_texcoord.zw, e);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_OPACITY0, frg.opacity_texcoord.xy);
#if BORDER_SHADER
	// 位置に応じてフェードさせる
	if ( g_border_field.z > 0.f ) {
		if ( g_character_position_count > 0.f ) {
			vec2 fade = vec2( g_border_field.x, g_border_field.y );
			float alpha = 0.f;
			for ( int i = 0; i < int(g_character_position_count); ++i ) {
				vec2 v = frg.position.xz - g_character_positions[ i ].xz;
				float distance = length( v );
				//float distance = v.x * v.x + v.y * v.y;
				float a = 1.f - max( distance - fade.x, 0.f ) / ( fade.y - fade.x );
				a = clamp( a, 0.f, 1.f );
				a *= a; // mildness
				alpha = max( alpha, a ); // or 1に乗算して最後に1-alpha
			}
			result_a *= alpha;
		} else {
			// 何もないなら表示しない
			discard;
		}
	}
	// sortedge
	{
		const float dest_depth = textureLod( g_scene_linear_depth_sampler, gl_FragCoord.xy * g_inverse_render_target_size, 0.f ).r;
		const float soft_distance_by_depth = 0.5f; // 1unit=1m
		float softness = clamp( ( dest_depth - frg.position.w ) * ( 1.f / soft_distance_by_depth ), 0.f, 1.f );
		result_a *= softness;
	}
#endif
	discard_by_opacity(result_a);
#if (0 == OUTPUT_TINY_GBUFFER)
	result.a = result_a;
#endif//OUTPUT_TINY_GBUFFER
#if USE_TINY_GBUFFER
	vec3 normal = texelFetch(g_gbuffer_normal_map_sampler, ivec2(gl_FragCoord.xy), 0).rgb * 2.f - vec3(127.f / 255.f * 2.f);
	normal = normalize(normal);//量子化誤差により必要。
	vec4 specular_map = texelFetch(g_gbuffer_specular_map_sampler, ivec2(gl_FragCoord.xy), 0);
	vec3 diffuse_color = texelFetch(g_gbuffer_diffuse_map_sampler, ivec2(gl_FragCoord.xy), 0).rgb;
#else//USE_TINY_GBUFFER
	//ウエイトを計算。
#if (0 < BLEND_MAP_CAPACITY)
	//ウエイトはrgbチャネルに格納されている。
	//アルファは次のようにして求める。また成分の合計が1になるように補正する。
	//	if(r + g + b < 1.f){
	//	    a = 1.f - (r + g + b);
	//	}else{
	//	    a = 0.f;
	//	    rgb *= 1.f / (r + g + b);
	//	}
	//質感のインデクス順にa,r,g,bを重みとして使う。
	vec4 weight;
	weight.rgb = texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_WEIGHT)))), frg.texcoords2.xy).rgb;
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_WEIGHT, frg.texcoords2.xy);
#if USE_TESSELLATOR
	const float inv_snow_amount = frg.position.w;
	weight.rgb *= inv_snow_amount;//weight.aが雪。
#endif//USE_TESSELLATOR
#endif//BLEND_MAP_CAPACITY
#if (0 < BLEND_MAP_CAPACITY)
	float t = dot(weight.rgb, vec3(1.f));
	if(t < 1.f){
		weight.a = 1.f - t;
	}else{
		weight.a = 0.f;
		weight.rgb *= 1.f / t;
	}
#endif//BLEND_MAP_CAPACITY
	//calculate normal
	vec3 normal = vec3(0.f);
	normal += calculate_normal_from_normal_map(0, frg.texcoords0.xy);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_NORMAL0, frg.texcoords0.xy);
#if (0 < BLEND_MAP_CAPACITY)
	normal *= weight.a;
	normal += calculate_normal_from_normal_map(1, frg.texcoords0.zw) * weight.r;
	normal += calculate_normal_from_normal_map(2, frg.texcoords1.xy) * weight.g;
	vec3 nb = calculate_normal_from_normal_map(3, frg.texcoords1.zw);//水面とブレンドされるので一旦保存。
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_NORMAL0, frg.texcoords0.xy);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_NORMAL1, frg.texcoords0.zw);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_NORMAL2, frg.texcoords1.xy);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_NORMAL3, frg.texcoords1.zw);
	if(0 != (ShaderFlag_Water & g_shader_flags)){
		vec2 uv = frg.position.xz * g_water_normal_uv_scale + g_water_normal_uv_offset;
		vec3 t;
		t.xz = textureLod(sampler2D(unpackUint2x32(g_water_normal_map_handle)), uv, 0.f).rg * 2.f - vec2(1.f);
		t.y = sqrt(max(0.f, 1.f - dot(t.xz, t.xz)));
		nb = normalize(nb + t * min((1.f - t.y) * 128.f, 1.f));//水面が変化していないところ(法線が+yに近いところ)は、水面の影響を小さく。
	}
	normal += nb * weight.b;
#endif//BLEND_MAP_CAPACITY
	normal = normalize(normal);
	//get specular color and glossiness
	vec4 specular_map = vec4(0.f);//rgb=color,a=glossiness
	specular_map += texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_SPECULAR0)))), frg.texcoords0.xy).rgba * SPECULAR0_COLOR_GAIN;
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_SPECULAR0, frg.texcoords0.xy);
#if (0 < BLEND_MAP_CAPACITY)
	specular_map *= weight.a;
	specular_map += texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_SPECULAR1)))), frg.texcoords0.zw).rgba * SPECULAR1_COLOR_GAIN * weight.r;
	specular_map += texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_SPECULAR2)))), frg.texcoords1.xy).rgba * SPECULAR2_COLOR_GAIN * weight.g;
	specular_map += texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_SPECULAR3)))), frg.texcoords1.zw).rgba * SPECULAR3_COLOR_GAIN * weight.b;
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_SPECULAR0, frg.texcoords0.xy);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_SPECULAR1, frg.texcoords0.zw);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_SPECULAR2, frg.texcoords1.xy);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_SPECULAR3, frg.texcoords1.zw);
#endif//BLEND_MAP_CAPACITY
	//get diffuse color
	vec3 diffuse_color = vec3(0.f);
	diffuse_color += texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_DIFFUSE0)))), frg.texcoords0.xy).rgb * DIFFUSE0_COLOR_GAIN;
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_DIFFUSE0, frg.texcoords0.xy);
#if (0 < BLEND_MAP_CAPACITY)
	diffuse_color *= weight.a;
	diffuse_color += texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_DIFFUSE1)))), frg.texcoords0.zw).rgb * DIFFUSE1_COLOR_GAIN * weight.r;
	diffuse_color += texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_DIFFUSE2)))), frg.texcoords1.xy).rgb * DIFFUSE2_COLOR_GAIN * weight.g;
	diffuse_color += texture(sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_DIFFUSE3)))), frg.texcoords1.zw).rgb * DIFFUSE3_COLOR_GAIN * weight.b;
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_DIFFUSE0, frg.texcoords0.xy);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_DIFFUSE1, frg.texcoords0.zw);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_DIFFUSE2, frg.texcoords1.xy);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_DIFFUSE3, frg.texcoords1.zw);
#endif//BLEND_MAP_CAPACITY
	blend_layered_diffuse(diffuse_color.rgb, frg.texcoords3.zw);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_LAYERED_DIFFUSE, frg.texcoords3.zw);
	blend_vertex_color(diffuse_color);
#endif//USE_TINY_GBUFFER
	const vec3 n = normal;
	vec3 specular_color = specular_map.rgb;
	blend_vertex_color(specular_color);
	float glossiness = specular_map.a;
	const float no_clamped_glossiness = glossiness;
#if (0 == BORDER_SHADER)//borderは反射率が1を超える謎物質であることを許容。
#if OUTPUT_TINY_GBUFFER
	//gbufferには上限1でクランプしたglossinessが書きだされる。
#else//OUTPUT_TINY_GBUFFER
	glossiness = clamp(glossiness, 0.f, 1.f - MIN_ROUGHNESS);
#endif//OUTPUT_TINY_GBUFFER
	specular_color = min(specular_color, vec3(1.f));
#endif//BORDER_SHADER
#if ENABLE_DITHER
	//glossiness = 0.f;//specularエイリアシング回避のため。
	glossiness *= max(0.f, 1.f - g_dither_ratio * 2.f);//specularエイリアシング回避のため。g_dither_ratio=0.5fで0になるように。
#endif//ENABLE_DITHER
#if OUTPUT_TINY_GBUFFER
	result_normal = vec4(normal * 0.5f + vec3(0.5f), 0.f);
	result_normal.rgb *= 254.f / 255.f;
	result_specular = vec4(specular_color, glossiness);
	result_diffuse = vec4(diffuse_color, 0.f);
#else//OUTPUT_TINY_GBUFFER
	//ライティング計算。
#if (0 == BORDER_SHADER)//borderは反射率が1を超える謎物質であることを許容。
	diffuse_color = min(diffuse_color, vec3(1.f));
#endif//BORDER_SHADER
	vec3 result_diffuse = vec3(0.f);
	vec3 result_specular = vec3(0.f);
	//平行光源。
	float shadow = 1.f;
	if(0 != (g_shader_flags & ShaderFlag_ReceiveShadow)){
#if (SHADOW_MODE == ShadowMode_Resolved)
		shadow = texture(sampler2D(unpackUint2x32(g_screen_shadow_map_handle)), gl_FragCoord.xy * vec2(INV_SCREEN_WIDTH, INV_SCREEN_HEIGHT), 0).x;
#else//SHADOW_MODE
		shadow = calculate_self_shadow_full_range(
			frg.view_distance,
			frg.shadow_texcoords[0],
			frg.shadow_texcoords[1],
			frg.shadow_texcoords[2],
			g_extra_shader_flags);
#endif//SHADOW_MODE
		shadow *= calculate_cloud_shadow(frg.position.xyz);//minよりも乗算する方が自然に見える。それぞれが二値でなく、遮蔽率とみなせば乗算でよかろう。
		shadow = max(shadow, MIN_SHADOW_INTENSITY);
	}
	float shadowed_diffuse = shadow;
	float roughness = 1.f - min(glossiness, g_parallel_light_min_glossiness);
	const float ne = clamp(dot(n, e), 0.f, 1.f);
	{
		//直接項の拡散成分、鏡面成分を計算。
		float d;
		vec3 s;
		calculate_direct_light_ggx(d, s, g_parallel_light_dir.xyz, specular_color, e, n, ne, roughness, g_min_diffuse_intensity);
		vec3 c = g_parallel_light_colors[1].rgb;
		c *= shadow;
		result_diffuse += c * d;
		result_specular += c * s;
	}
#if BORDER_SHADER
	// 点光源の影響を受けない
#else // BORDER_SHADER
	//点光源。//これはstage_light_scale_by_effectでは減衰させない。
	Tile tile;
	fetch_tile(tile);
	int ln = tile.m_count;
	int idx = tile.m_index_offset;
	roughness = 1.f - glossiness;
	for(int i = 0; i < ln; ++ i){//このあたり、整理対象。
		Light lgt;
		fetch_light(lgt, idx);
		float local_shadow = 1.f;
		int shadow_idx = floatBitsToInt(lgt.m_packed.x) & 0xf;
		if(shadow_idx < LocalShadowMapCapacity){
			local_shadow = calculate_local_shadow(shadow_idx, frg.position.xyz);
			if(local_shadow < MIN_SHADOW_INTENSITY){
				++ idx;
				continue;//完全に影の中。光源処理をスキップ。
			}
		}
		float d;
		vec3 s;
		calculate_direct_point_light_ggx(d, s, lgt, specular_color, e, n, ne, frg.position.xyz, roughness, g_min_diffuse_intensity);
		vec3 c = lgt.m_color.rgb;
		c *= local_shadow;
		result_diffuse += c * d;
		result_specular += c * s;
		++ idx;
	}
#endif // BORDER_SHADER
	result.rgb = diffuse_color * result_diffuse;
	result.rgb += result_specular;//負数が来ることがあるようだ。
	//間接光の鏡面成分(要は環境マップ。)を計算。
	vec3 fresnel_indirect = specular_color + (vec3(1.f) - specular_color) * pow(max(0.f, 1.f - dot(n, e)), 5.f) / (4.f - 3.f * glossiness) * glossiness * s_fresnel_modifier;
	vec3 r = reflect(-e, n);
	vec3 reflection_intensity = fresnel_indirect * min(shadowed_diffuse + SPECULAR_INTENSITY_IN_SHADOW, 1.f);
#if USE_TINY_GBUFFER
#if 1
	//uvへのg_resolution_scaleの乗算は不要。
	//viewportのサイズにg_resolution_scaleが掛けられているので、gl_FragCoord自体がg_resolution_scaleで乗算されているのと等しい。
	vec4 sslr = textureLod(g_sslr_map_sampler, gl_FragCoord.xy * g_inverse_render_target_size, 0.f).rgba;
#else
	vec4 sslr = texelFetch(g_sslr_map_sampler, ivec2(gl_FragCoord.xy) / 2, 0).rgba;
#endif
	sslr.rgb *= 3.f;//decode
	result.rgb += sslr.rgb * sslr.a * reflection_intensity;
	reflection_intensity *= (1.f - sslr.a);//cubeは残り分。
#endif//USE_TINY_GBUFFER
	if(0 != g_planar_reflection_map_handle){//planar reflection
		vec4 planer_ref = textureLod(sampler2D(unpackUint2x32(g_planar_reflection_map_handle)), gl_FragCoord.xy * g_inverse_render_target_size, 6.f * (1.f - no_clamped_glossiness));
		float possibility = max(0.f, 1.f - abs(dot(g_planar_reflection_plane, vec4(frg.position.xyz, 1.f))));
		possibility *= min(planer_ref.a, 1.f);
		result.rgb += planer_ref.rgb * reflection_intensity * possibility;
		reflection_intensity *= (1.f - possibility);
	}
	result.rgb += fetch_environment_reflection(frg.position.xyz, r, (1.f - no_clamped_glossiness) * 6.f, 0.f, g_shader_flags).rgb * reflection_intensity;//マップのフェチ時にはクランプしないglossiness値を使う。
	//間接光の拡散成分を計算。
	float ambient_scale = AMBIENT_SCALE;
	result.rgb += textureLod(g_env_sampler, n, 6.f).rgb * ambient_scale * diffuse_color;
	//焼き成分をブレンド。
	blend_baked_shadow(result.rgb, frg.texcoords2.zw);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_BAKED_SHADOW, frg.texcoords2.zw);
	blend_baked_light(result.rgb, frg.texcoords3.xy);
	OUTPUT_TEXTURE_LOD(SAMPLER_INDEX_BAKED_LIGHT, frg.texcoords3.xy);
	result.rgb *= calculate_ssao(g_ao_intensity_for_stage);
	blend_fog(result.rgb, frg.fog, frg.position.y);
	if(0.f != g_fresnel_color.w){//無効な時は全ビット0になっている。
		blend_fresnel_color(result.rgb, e, frg.normal.xyz);//頂点法線を使う。
	}
	result.rgb = max(result.rgb, vec3(0.f));
	result_normal = vec4(0.f, 0.f, 0.f, OVERDRAW_MASK);//OVERDRAW_MASK<=0でoverdrawが描かれる。0<OVERDRAW_MASKでoverdrawが描かれない。
	result_npr_vertex_color = vec4(0.f, 0.f, 0.f, 0.f);///
#if COMPOSITE_VOLUME_FOG
	result = composite_volume_fog(result, frg.position.xyz, g_volume_fog_composite_type);
#endif//COMPOSITE_VOLUME_FOG
#endif//OUTPUT_TINY_GBUFFER
}
#endif//CONSTANT_SHADING


