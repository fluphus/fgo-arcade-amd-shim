#version 440
#define RANDOM_COUNT (1024)
#extension GL_NV_gpu_shader5 : enable
#extension GL_NV_shader_buffer_load : enable
#extension GL_NV_shader_atomic_float : enable
struct Particle{
	vec4 m_position;//xyz=position, w=life
	vec4 m_velocity;//xyz=velocity, w=unused
	vec4 m_attributes;//x=size, y=alpha, z=noise, w=unsued
};
struct Unused{
	int* m_count;
	uint* m_indices;
};
struct Candidate{
	int* m_count;
	vec4* m_candidates;
	ivec4 m_params;
};
#define g_candidate_capacity m_params.y
#define VoxelResolutionX (32)
#define VoxelResolutionY (8)
#define VoxelResolutionZ (32)
#define SamplerIndexInShader_Noise3d (24)
layout(std140, binding = 0) uniform PerEmitter{
	Particle* g_particles;
	Unused* g_unused0;
	float* g_randoms;
	vec4* g_candidate_cells;
};
layout(location = 0) uniform vec4 g_constants[1];
#define g_unused_ptr packPtr(floatBitsToUint(g_constants[0].xy))
#define g_particle_capacity floatBitsToInt(g_constants[0].z)
void main(){
	gl_Position = vec4(0.f, 0.f, 0.f, -1.f);//culling
	//パーティクルを書き換え。
	Particle p;
	p.m_position = vec4(0.f);
	p.m_velocity = vec4(0.f);
	p.m_attributes = vec4(0.f);
    g_particles[gl_VertexID] = p;
	//未使用列のポインタを書き換え。一回で良いけど。
	const int unused_struct_size_in_u32 = 4;
	const int count_size_in_u32 = 1;
	uint* p32 = (uint*)(g_unused_ptr);
	uint* count_addr = p32 + unused_struct_size_in_u32;
	uint* indices_addr = p32 + unused_struct_size_in_u32 + count_size_in_u32;
	uint64_t* p64 = (uint64_t*)g_unused_ptr;
	p64[0] = (uint64_t)(count_addr);
	p64[1] = (uint64_t)(indices_addr);
	//未使用列の要素を書き換え。
	*count_addr = g_particle_capacity;//一回で良いけど。
	indices_addr[gl_VertexID] = gl_VertexID;
}
