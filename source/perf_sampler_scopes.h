/* Close ordinary sampler scopes when the application observes private GL
   bindings, enters another execution path, or changes current-program values
   that do not have a mirrored DSA update in perf_regular_samplers.h. */
#define RS_SCOPE(name, args, call) \
void WINAPI perf_scope_##name args { \
    static void (WINAPI *real) args; \
    perf_rs_close_pending(); \
    if (!real) real=(__typeof__(real))perf_rs_proc(#name); \
    if (real) real call; \
}

RS_SCOPE(glGetIntegerv,(GLenum p,GLint *v),(p,v))
RS_SCOPE(glGetFloatv,(GLenum p,float *v),(p,v))
RS_SCOPE(glGetDoublev,(GLenum p,double *v),(p,v))
RS_SCOPE(glGetBooleanv,(GLenum p,GLboolean *v),(p,v))
RS_SCOPE(glGetInteger64v,(GLenum p,GLint64 *v),(p,v))
RS_SCOPE(glGetIntegeri_v,(GLenum p,GLuint i,GLint *v),(p,i,v))
RS_SCOPE(glGetInteger64i_v,(GLenum p,GLuint i,GLint64 *v),(p,i,v))
RS_SCOPE(glGetBooleani_v,(GLenum p,GLuint i,GLboolean *v),(p,i,v))
RS_SCOPE(glGetFloati_v,(GLenum p,GLuint i,float *v),(p,i,v))
RS_SCOPE(glGetDoublei_v,(GLenum p,GLuint i,double *v),(p,i,v))
RS_SCOPE(glPushAttrib,(GLbitfield mask),(mask))
RS_SCOPE(glBegin,(GLenum mode),(mode))
RS_SCOPE(glCallList,(GLuint id),(id))
RS_SCOPE(glCallLists,(GLsizei n,GLenum type,const void *ids),(n,type,ids))
RS_SCOPE(glNewList,(GLuint id,GLenum mode),(id,mode))
RS_SCOPE(glDrawPixels,(GLsizei w,GLsizei h,GLenum format,GLenum type,const void *p),(w,h,format,type,p))
RS_SCOPE(glBitmap,(GLsizei w,GLsizei h,float x,float y,float dx,float dy,const unsigned char *p),(w,h,x,y,dx,dy,p))
RS_SCOPE(glBeginTransformFeedback,(GLenum mode),(mode))
RS_SCOPE(glEndTransformFeedback,(void),())
RS_SCOPE(glPauseTransformFeedback,(void),())
RS_SCOPE(glResumeTransformFeedback,(void),())
RS_SCOPE(glBindTransformFeedback,(GLenum target,GLuint object),(target,object))
RS_SCOPE(glDeleteTransformFeedbacks,(GLsizei n,const GLuint *objects),(n,objects))
RS_SCOPE(glBeginTransformFeedbackNV,(GLenum mode),(mode))
RS_SCOPE(glEndTransformFeedbackNV,(void),())
RS_SCOPE(glBindTransformFeedbackNV,(GLenum target,GLuint object),(target,object))
RS_SCOPE(glBindProgramPipeline,(GLuint pipeline),(pipeline))
RS_SCOPE(glUseProgramStages,(GLuint pipeline,GLbitfield stages,GLuint program),(pipeline,stages,program))
RS_SCOPE(glActiveShaderProgram,(GLuint pipeline,GLuint program),(pipeline,program))
RS_SCOPE(glValidateProgram,(GLuint program),(program))
RS_SCOPE(glValidateProgramPipeline,(GLuint pipeline),(pipeline))

#define RS_UNIFORMS(suffix,type) \
RS_SCOPE(glUniform1##suffix,(GLint l,type a),(l,a)) \
RS_SCOPE(glUniform2##suffix,(GLint l,type a,type b),(l,a,b)) \
RS_SCOPE(glUniform3##suffix,(GLint l,type a,type b,type c),(l,a,b,c)) \
RS_SCOPE(glUniform4##suffix,(GLint l,type a,type b,type c,type d),(l,a,b,c,d)) \
RS_SCOPE(glUniform1##suffix##v,(GLint l,GLsizei n,const type *v),(l,n,v)) \
RS_SCOPE(glUniform2##suffix##v,(GLint l,GLsizei n,const type *v),(l,n,v)) \
RS_SCOPE(glUniform3##suffix##v,(GLint l,GLsizei n,const type *v),(l,n,v)) \
RS_SCOPE(glUniform4##suffix##v,(GLint l,GLsizei n,const type *v),(l,n,v))
RS_UNIFORMS(f,float)
RS_UNIFORMS(i,GLint)
RS_UNIFORMS(ui,GLuint)
RS_UNIFORMS(d,double)
#define RS_MAT(size,suffix,type) \
RS_SCOPE(glUniformMatrix##size##suffix,(GLint l,GLsizei n,GLboolean t,const type *v),(l,n,t,v))
#define RS_MATS(suffix,type) \
RS_MAT(2,suffix,type) RS_MAT(3,suffix,type) RS_MAT(4,suffix,type) \
RS_MAT(2x3,suffix,type) RS_MAT(3x2,suffix,type) RS_MAT(2x4,suffix,type) \
RS_MAT(4x2,suffix,type) RS_MAT(3x4,suffix,type) RS_MAT(4x3,suffix,type)
RS_MATS(fv,float)
RS_MATS(dv,double)

static PROC perf_rs_scope_wrapper(const char *name)
{
    size_t length=strlen(name);
    if (length>3 && length<96 && !strcmp(name+length-3,"ARB") &&
        !strncmp(name,"glUniform",9)) {
        char core[96]; memcpy(core,name,length-3); core[length-3]=0;
        return perf_rs_scope_wrapper(core);
    }
#define RS_SELECT(api) if (!strcmp(name,#api)) return (PROC)perf_scope_##api
    RS_SELECT(glGetIntegerv); RS_SELECT(glGetFloatv); RS_SELECT(glGetDoublev);
    RS_SELECT(glGetBooleanv); RS_SELECT(glGetInteger64v); RS_SELECT(glGetIntegeri_v);
    RS_SELECT(glGetInteger64i_v); RS_SELECT(glGetBooleani_v); RS_SELECT(glGetFloati_v);
    RS_SELECT(glGetDoublei_v); RS_SELECT(glPushAttrib); RS_SELECT(glBegin);
    RS_SELECT(glCallList); RS_SELECT(glCallLists); RS_SELECT(glNewList);
    RS_SELECT(glDrawPixels); RS_SELECT(glBitmap); RS_SELECT(glBeginTransformFeedback);
    RS_SELECT(glEndTransformFeedback); RS_SELECT(glPauseTransformFeedback);
    RS_SELECT(glResumeTransformFeedback); RS_SELECT(glBindTransformFeedback);
    RS_SELECT(glDeleteTransformFeedbacks); RS_SELECT(glBeginTransformFeedbackNV);
    RS_SELECT(glEndTransformFeedbackNV); RS_SELECT(glBindTransformFeedbackNV);
    RS_SELECT(glBindProgramPipeline); RS_SELECT(glUseProgramStages);
    RS_SELECT(glActiveShaderProgram); RS_SELECT(glValidateProgram); RS_SELECT(glValidateProgramPipeline);
    /* The existing mirrored vec4/int sampler setters take precedence. */
    if (!strcmp(name,"glUniform4f") || !strcmp(name,"glUniform4fv") ||
        !strcmp(name,"glUniform1i") || !strcmp(name,"glUniform1iv")) return NULL;
#define RS_SELECT_U(s) RS_SELECT(glUniform1##s); RS_SELECT(glUniform2##s); \
    RS_SELECT(glUniform3##s); RS_SELECT(glUniform4##s); RS_SELECT(glUniform1##s##v); \
    RS_SELECT(glUniform2##s##v); RS_SELECT(glUniform3##s##v); RS_SELECT(glUniform4##s##v)
    RS_SELECT_U(f); RS_SELECT_U(i); RS_SELECT_U(ui); RS_SELECT_U(d);
#define RS_SELECT_M(n,s) RS_SELECT(glUniformMatrix##n##s)
#define RS_SELECT_MS(s) RS_SELECT_M(2,s); RS_SELECT_M(3,s); RS_SELECT_M(4,s); \
    RS_SELECT_M(2x3,s); RS_SELECT_M(3x2,s); RS_SELECT_M(2x4,s); \
    RS_SELECT_M(4x2,s); RS_SELECT_M(3x4,s); RS_SELECT_M(4x3,s)
    RS_SELECT_MS(fv); RS_SELECT_MS(dv);
#undef RS_SELECT_MS
#undef RS_SELECT_M
#undef RS_SELECT_U
#undef RS_SELECT
    return NULL;
}
#undef RS_MATS
#undef RS_MAT
#undef RS_UNIFORMS
#undef RS_SCOPE
