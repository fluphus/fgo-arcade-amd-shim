/* Historical video-copy function from a4f4ee6, used only by the CPU regression. */
static void mp4_video_copy_cpu_fallback(GLuint src, GLenum src_target,
                                        GLint src_level, GLint src_x,
                                        GLint src_y, GLint src_z, GLuint dst,
                                        GLenum dst_target, GLint dst_level,
                                        GLint dst_x, GLint dst_y, GLint dst_z,
                                        GLsizei width, GLsizei height,
                                        GLsizei depth)
{
    if (!g_mp4_video_copy_cpu_fallback_on || !src || !dst ||
        src == dst || src_target != 0x0DE1 /* GL_TEXTURE_2D */ ||
        dst_target != 0x0DE1 || src_level < 0 || dst_level < 0 ||
        src_x != 0 || src_y != 0 || src_z != 0 || dst_x != 0 ||
        dst_y != 0 || dst_z != 0 || width < 640 || height < 360 ||
        depth != 1 || !idle_video_probe_is_shared_tex(src) ||
        !idle_video_probe_is_video_tex(dst))
        return;

    typedef void (WINAPI *mp4_get_texture_image_t)(GLuint, GLint, GLenum,
                                                    GLenum, GLsizei, void *);
    typedef void (WINAPI *mp4_texture_sub_image_2d_t)(GLuint, GLint, GLint,
                                                      GLint, GLsizei, GLsizei,
                                                      GLenum, GLenum,
                                                      const void *);
    static mp4_get_texture_image_t get_image;
    static mp4_texture_sub_image_2d_t sub_image;
    if (!get_image)
        get_image = (mp4_get_texture_image_t)trace_resolve("glGetTextureImage");
    if (!sub_image)
        sub_image = (mp4_texture_sub_image_2d_t)
            trace_resolve("glTextureSubImage2D");
    if (!get_image || !sub_image) return;

    size_t pixels_size = (size_t)width * (size_t)height * 4u;
    if (pixels_size == 0 || pixels_size > 64u * 1024u * 1024u) return;
    unsigned char *pixels = (unsigned char *)HeapAlloc(
        GetProcessHeap(), 0, pixels_size);
    if (!pixels) return;
    memset(pixels, 0, pixels_size);

    unsigned long long perf_fallback_start = perf_timing_start();
    battle_aux_event(BATTLE_AUX_MP4_COPY, 0, "mp4_copy_before", 0, src, dst);
    GLenum before = real_glGetError ? real_glGetError() : 0;
    get_image(src, src_level, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */,
              (GLsizei)pixels_size, pixels);
    GLenum read_error = real_glGetError ? real_glGetError() : 0;
    if (!read_error)
        sub_image(dst, dst_level, dst_x, dst_y, width, height,
                  0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, pixels);
    GLenum write_error = real_glGetError ? real_glGetError() : 0;
    battle_aux_event(BATTLE_AUX_MP4_COPY, 1, "mp4_copy_after", write_error, src, dst);
    perf_timing_record(&g_perf_copy_fallback_calls,
                       &g_perf_copy_fallback_ticks, perf_fallback_start);

    unsigned long long hash = dataflow_hash_bytes(pixels, pixels_size);
    if (g_mp4_video_copy_cpu_fallback_lines < 128) {
        FILE *f = fopen(
            "C:\\fgo\\_tools\\glshim\\mp4_video_copy_cpu_fallback_v1.log",
            "a");
        if (f) {
            fprintf(f,
                    "pid=%lu frame=%llu src=%u dst=%u size=%dx%d bytes=%llu "
                    "before=0x%x read_error=0x%x write_error=0x%x "
                    "hash=0x%016llx first=%02x,%02x,%02x,%02x\n",
                    (unsigned long)GetCurrentProcessId(),
                    (unsigned long long)g_frame_count, src, dst,
                    (int)width, (int)height,
                    (unsigned long long)pixels_size, (unsigned)before,
                    (unsigned)read_error, (unsigned)write_error, hash,
                    pixels[0], pixels[1], pixels[2], pixels[3]);
            fclose(f);
            g_mp4_video_copy_cpu_fallback_lines++;
        }
    }
    if (!read_error && !write_error)
        g_mp4_video_copy_cpu_fallback_successes++;
    HeapFree(GetProcessHeap(), 0, pixels);
}
