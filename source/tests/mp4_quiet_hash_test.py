"""Replay the exact old/new CPU video fallback with deterministic GL transfers.

Checks pixel/API parity and diagnostic-only hashing, then times quiet copies.
This is a CPU regression/benchmark, not a decoder or in-game FPS test.
"""
from pathlib import Path
import argparse
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', help='Optional Git revision instead of the bundled previous copy function')
args = parser.parse_args()
out = root / 'build/mp4-quiet-hash'
out.mkdir(parents=True, exist_ok=True)
before = (subprocess.check_output(['git', 'show', f'{args.baseline}:source/shim.c'], cwd=root.parent).decode()
          if args.baseline else (root / 'tests/fixtures/video/cpu_copy_before_fix.c').read_text())
after = (root / 'shim.c').read_text()


def function(source):
    source = source.replace('\r\n', '\n')
    marker = source.find('/* AMD accepts the DX-interop')
    start = source.index('static void mp4_video_copy_cpu_fallback(', max(marker, 0))
    end = source.index('\n}\n', start) + 3
    return source[start:end]


prefix = r'''
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
typedef unsigned GLuint, GLenum;
typedef int GLint, GLsizei;
enum { BATTLE_AUX_MP4_COPY = 1 };
static int g_mp4_video_copy_cpu_fallback_on=1, g_telemetry_quiet=1;
static unsigned long g_mp4_video_copy_cpu_fallback_lines, g_mp4_video_copy_cpu_fallback_successes;
static unsigned long long g_frame_count=1, g_perf_copy_fallback_calls, g_perf_copy_fallback_ticks;
static unsigned char *source_pixels, *dest_pixels;
static size_t bytes_count;
static int expected_w, expected_h, hash_calls, error_index, read_fails, log_available;
static int destination_recorded=1;
static unsigned trace_code;
static unsigned long long perf_timing_start(void) { return 0; }
static void perf_timing_record(void *a,void *b,unsigned long long c) { (void)a;(void)b;(void)c; }
static void battle_aux_event(int a,int b,const char *c,int d,GLuint e,GLuint f) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; }
static int idle_video_probe_is_shared_tex(GLuint t) { return t==99; }
static int idle_video_probe_is_video_tex(GLuint t) { return t==118 && destination_recorded; }
static GLenum WINAPI get_error(void) { trace_code=trace_code*10+1;return read_fails && error_index++==1?0x502:0; }
static GLenum (WINAPI *real_glGetError)(void)=get_error;
static void WINAPI get_image(GLuint t,GLint l,GLenum f,GLenum ty,GLsizei n,void *p) {
    assert(t==99 && l==0 && f==0x1908 && ty==0x1401 && n==(GLsizei)bytes_count);
    trace_code=trace_code*10+2;memcpy(p,source_pixels,bytes_count);
}
static void WINAPI sub_image(GLuint t,GLint l,GLint x,GLint y,GLsizei w,GLsizei h,GLenum f,GLenum ty,const void *p) {
    assert(t==118 && l==0 && x==0 && y==0 && w==expected_w && h==expected_h && f==0x1908 && ty==0x1401);
    trace_code=trace_code*10+3;memcpy(dest_pixels,p,bytes_count);
}
static PROC trace_resolve(const char *s) {
    if(!strcmp(s,"glGetTextureImage"))return (PROC)get_image;
    assert(!strcmp(s,"glTextureSubImage2D"));return (PROC)sub_image;
}
static unsigned long long dataflow_hash_bytes(const void *p,size_t n) {
    ++hash_calls;unsigned long long h=1469598103934665603ULL;
    for(size_t i=0;i<n;++i){h^=((const unsigned char *)p)[i];h*=1099511628211ULL;}return h;
}
static FILE *log_open(const char *p,const char *m) { (void)p;(void)m;return !g_telemetry_quiet && log_available?tmpfile():NULL; }
#define fopen log_open
'''
suffix = r'''
static void copy_frame(int version) {
    if(version) fallback_new(99,0xde1,0,0,0,0,118,0xde1,0,0,0,0,expected_w,expected_h,1);
    else fallback_old(99,0xde1,0,0,0,0,118,0xde1,0,0,0,0,expected_w,expected_h,1);
}
static void setup(int w,int h) {
    free(source_pixels);free(dest_pixels);expected_w=w;expected_h=h;bytes_count=(size_t)w*h*4;
    source_pixels=malloc(bytes_count);dest_pixels=malloc(bytes_count);assert(source_pixels && dest_pixels);
    for(size_t i=0;i<bytes_count;++i)source_pixels[i]=(unsigned char)(i*37+(i>>12));
}
static void check(int scenario) {
    unsigned old_trace=0;unsigned long old_success=0;
    for(int v=0;v<2;++v) {
        memset(dest_pixels,0xa7,bytes_count);hash_calls=0;error_index=0;trace_code=0;
        g_telemetry_quiet=scenario==0 || scenario==4;
        log_available=scenario!=3;read_fails=scenario==4;
        g_mp4_video_copy_cpu_fallback_lines=scenario==2?128:0;
        g_mp4_video_copy_cpu_fallback_successes=0;copy_frame(v);
        assert(hash_calls==(!v || scenario==1));
        if(read_fails){for(size_t i=0;i<bytes_count;++i)assert(dest_pixels[i]==0xa7);}
        else assert(!memcmp(source_pixels,dest_pixels,bytes_count));
        if(v){assert(trace_code==old_trace);assert(g_mp4_video_copy_cpu_fallback_successes==old_success);}
        else{old_trace=trace_code;old_success=g_mp4_video_copy_cpu_fallback_successes;}
    }
}
static double bench(int v) {
    LARGE_INTEGER f,a,b;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&a);
    for(int i=0;i<12;++i)copy_frame(v);
    QueryPerformanceCounter(&b);return (b.QuadPart-a.QuadPart)*1000.0/f.QuadPart/12;
}
static void check_missing_destination(void) {
    destination_recorded=0;g_telemetry_quiet=1;read_fails=0;trace_code=0;hash_calls=0;
    memset(dest_pixels,0xa7,bytes_count);
    copy_frame(0);assert(trace_code==0 && hash_calls==0);
    for(size_t i=0;i<bytes_count;++i)assert(dest_pixels[i]==0xa7);
    copy_frame(1);assert(trace_code==12131 && !memcmp(source_pixels,dest_pixels,bytes_count));
    assert(hash_calls==0);destination_recorded=1;
}
static int compare(const void *a,const void *b) {double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
int main(void) {
    const int sizes[][2]={{1280,720},{1920,1080},{3840,2160}};
    for(unsigned s=0;s<3;++s) {
        setup(sizes[s][0],sizes[s][1]);for(int c=0;c<5;++c)check(c);check_missing_destination();
        g_telemetry_quiet=1;read_fails=0;log_available=0;
        double old[5],next[5];
        for(int k=0;k<5;++k){if(k%2){next[k]=bench(1);old[k]=bench(0);}else{old[k]=bench(0);next[k]=bench(1);}}
        qsort(old,5,sizeof(double),compare);qsort(next,5,sizeof(double),compare);
        printf("%dx%d: pixel/API parity PASS; quiet/limit/no-file/read-error PASS; missing destination tag: old skips pixels, new copies PASS; median CPU copy old=%.3f ms new=%.3f ms saved=%.3f ms\n",expected_w,expected_h,old[2],next[2],old[2]-next[2]);
    }
    free(source_pixels);free(dest_pixels);return 0;
}
'''
cfile = out / 'mp4_quiet_hash_fixture.c'
cfile.write_text(prefix + function(before).replace('mp4_video_copy_cpu_fallback(', 'fallback_old(')
                 + function(after).replace('mp4_video_copy_cpu_fallback(', 'fallback_new(') + suffix)
exe = out / 'mp4_quiet_hash_fixture.exe'
subprocess.run(['x86_64-w64-mingw32-gcc.exe', '-O2', str(cfile), '-o', str(exe)], check=True)
result = subprocess.check_output([str(exe)], text=True)
(out / 'mp4-quiet-hash-test.txt').write_text(result)
print(result, end='')
