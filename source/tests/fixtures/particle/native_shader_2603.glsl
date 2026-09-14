#version 450
#define COMPOSITE_VOLUME_FOG (0)
#define SamplerIndexInShader_VolumeFog (35)
layout(binding = 5) uniform Scene{
	vec4 g_screen_params;
	vec4 g_volume_fog_texcoord_from_worlds_in_ptc[3];
	vec4 g_volume_fog_slice_from_linear_depth_in_ptc;
};
#define VOLUME_FOG_TEXCOORD_FROM_WORLD_ROW0 g_volume_fog_texcoord_from_worlds_in_ptc[0]
#define VOLUME_FOG_TEXCOORD_FROM_WORLD_ROW1 g_volume_fog_texcoord_from_worlds_in_ptc[1]
#define VOLUME_FOG_TEXCOORD_FROM_WORLD_ROW2 g_volume_fog_texcoord_from_worlds_in_ptc[2]
#define VOLUME_FOG_SLICE_FROM_LINEAR_DEPTH g_volume_fog_slice_from_linear_depth_in_ptc
#define PSConstBitCount_VolumeFogCompositeType (3)
#define VolumeFogCompositeType_Over (1)
#define VolumeFogCompositeType_Additive (2)
#define VolumeFogCompositeType_AdditiveOne (3)
vec3 calculate_volume_fog_texcoord(
	const vec3 world,
	const vec4 volume_texcoord_from_world_row0,
	const vec4 volume_texcoord_from_world_row1,
	const vec4 volume_texcoord_from_world_row2,
	const vec3 volume_slice_from_linear_depth){
	vec3 t;
	t.x = dot(volume_texcoord_from_world_row0, vec4(world, 1.f));
	t.y = dot(volume_texcoord_from_world_row1, vec4(world, 1.f));
	t.z = dot(volume_texcoord_from_world_row2, vec4(world, 1.f));
	t.xy /= t.z;
	t.z = log2(t.z * volume_slice_from_linear_depth.z + volume_slice_from_linear_depth.y) * volume_slice_from_linear_depth.x;
	return t;
}
vec4 composite_volume_fog(const vec4 color_without_fog, const vec4 fog, const uint composite_type){
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
	in const vec4 color_without_fog,
	in const vec3 world_position,
	in const uint composite_type){
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
layout(binding = 0) uniform sampler2D g_scene_linear_depth_sampler;
layout(binding = 1) uniform sampler2D g_color_sampler;
layout(location = 13) uniform vec4 g_fconstants[1];
#define g_draw_flags floatBitsToUint(g_fconstants[0].x)
#define DrawFlag_SoftParticle (1 << 0)
#define DrawFlag_MultiplyScale (1 << 1)
#define g_volume_fog_composite_type (floatBitsToInt(g_fconstants[0].y) & ((1 << PSConstBitCount_VolumeFogCompositeType) - 1))
layout(location = 0) out vec4 result;
in VertexData{
    vec4 color;
    vec4 texcoord;
#if COMPOSITE_VOLUME_FOG
	vec4 world;//w=unused
#endif//COMPOSITE_VOLUME_FOG
}frg;
float calculate_attenuation_by_depth_distance(const vec2 screen_uv, const float src_depth){//0<src_depthが見えているピクセル。
	const float fade_distance = 0.5f;//ソフト効果を適用する深度差の幅
	const float fade_min_depth = 0.f;//ソフト効果がかかり始める描画先深度
	const float fade_max_depth = 50.f;//ソフト効果がかからなくなる描画先深度
	const float dst_depth = textureLod(g_scene_linear_depth_sampler, screen_uv, 0.f).r;
	return clamp(1.f / fade_distance * (dst_depth - src_depth), 0.f, 1.f);//負はあり得るのか?
}
void main(){
#if defined(RENDERING_WITHOUT_TEXTURE)
#if OUTPUT_ONLY_VELOCITY
#else
result = vec4(1.0f,0.f,0.f,g_rendering_without_texture_opaque);
return;
#endif
#endif
	vec4 color = texture(g_color_sampler, frg.texcoord.xy);
    float a = color.a;
	if(a < frg.texcoord.w){
		discard;
	}
	const vec2 screen_uv = gl_FragCoord.xy * g_screen_params.xy;//projection座標から求めたほうが良いかも知れないけど。
	const float view_depth = frg.texcoord.z;
	const uint draw_flags = g_draw_flags;
	if(0 != (draw_flags & DrawFlag_SoftParticle)){
		a *= calculate_attenuation_by_depth_distance(screen_uv, view_depth);
	}
    a *= frg.color.a;
    result.rgb = max(color.rgb * frg.color.rgb, vec3(0.f));//正数を保証。
    result.a = clamp(a, 0.f, 1.f);
#if COMPOSITE_VOLUME_FOG
	result = composite_volume_fog(result, frg.world.xyz, g_volume_fog_composite_type);
#endif//COMPOSITE_VOLUME_FOG
}
