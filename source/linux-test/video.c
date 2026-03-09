#include "GLES2/gl2.h"

#include "vb_dsp.h"
#include "video_hard.h"

GLuint sChar, sFinal;

int eye_count = 2;

GLuint build_shader(const char *vertex_source, const char *fragment_source) {
    GLint compiled, infoLen;

    GLuint vshader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vshader, 1, &vertex_source, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        puts("Vertex shader failed to compile!");
        glGetShaderiv(vshader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            char *infoLog = malloc(infoLen);
            glGetShaderInfoLog(vshader, infoLen, NULL, infoLog);
            puts(infoLog);
            free(infoLog);
        }
    }
    
    GLuint fshader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fshader, 1, &fragment_source, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        puts("Fragment shader failed to compile!");
        glGetShaderiv(fshader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            char *infoLog = malloc(infoLen);
            glGetShaderInfoLog(fshader, infoLen, NULL, infoLog);
            puts(infoLog);
            free(infoLog);
        }
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vshader);
    glAttachShader(program, fshader);
    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        puts("Shader program failed to link!");
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            char *infoLog = malloc(infoLen);
            glGetProgramInfoLog(program, infoLen, NULL, infoLog);
            puts(infoLog);
            free(infoLog);
        }
    }

    return program;
}

void video_hard_init(void);

void video_init(void) {
    video_hard_init();
    sChar = build_shader(
        "uniform vec2 uOffset;\n"
        "uniform mat3 uPalette[8];\n"
        "attribute vec2 aPosition;\n"
        "attribute vec4 aParams;\n"
        "varying vec3 vParams;\n"
        "varying mat3 vPalette;\n"
        "void main() {\n"
        "   vParams = aParams.xyw;\n"
        "   vPalette = uPalette[int(aParams.z)];\n"
        "   vec2 realPos = aPosition * (1.0/256.0) - (1.0 - 4.0/256.0) + uOffset;\n"
        "   gl_Position = vec4(realPos.y, realPos.x, 0.0, 1.0);\n"
        "   gl_PointSize = 8.0;\n"
        "}",

        "uniform sampler2D sTex;\n"
        "varying mediump vec3 vParams;\n"
        "varying mediump mat3 vPalette;\n"
        "void main() {\n"\
        "   mediump vec2 tileCoord = gl_PointCoord.yx;\n"
        "   if (vParams.z >= 2.0) tileCoord.x = 1.0 - tileCoord.x;\n"
        "   if (vParams.z == 1.0 || vParams.z == 3.0) tileCoord.y = 1.0 - tileCoord.y;\n"
        "   mediump vec2 realCoord = (vParams.xy + vec2(1.0 - tileCoord.x, tileCoord.y)) / vec2(256.0 / 8.0, 512.0 / 8.0);\n"
        "   lowp vec4 color = texture2D(sTex, realCoord);\n"
        "   gl_FragColor = vec4(vPalette * color.xyz, color.w);\n"
        "}"
    );

    sFinal = build_shader(
        "attribute vec4 aPosition;\n"
        "attribute vec2 aTexCoord;\n"
        "varying vec2 vTexCoord;\n"
        "void main() {\n"
        "   gl_Position = aPosition;\n"
        "   vTexCoord = aTexCoord;\n"
        "}\n",

        "uniform sampler2D sTex;\n"
        "varying mediump vec2 vTexCoord;\n"
        "void main() {\n"
        "   gl_FragColor = texture2D(sTex, vTexCoord);\n"
        "}\n"
    );
}

extern GLuint screenTexHard[2];
void video_hard_render(int drawn_fb);

void video_render(int displayed_fb, bool on_time) {
    update_texture_cache_hard();
    video_hard_render(displayed_fb);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(sFinal);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTexHard[displayed_fb]);
    glViewport(0, 0, 384*2, 224*2);
    glScissor(0, 0, 384*2, 224*2);

    glUniform1i(glGetUniformLocation(sFinal, "sTex"), 0);

    GLfloat vPositions[] = {
        -1, -1,
        1, -1,
        -1, 1,
        1, 1};
    glVertexAttribPointer(glGetAttribLocation(sFinal, "aPosition"), 2, GL_FLOAT, GL_FALSE, 0, vPositions);
    glEnableVertexAttribArray(glGetAttribLocation(sFinal, "aPosition"));
    GLfloat vTexCoords[] = {
        224.0/512.0, 0,
        224.0/512.0, 384.0/512.0,
        0, 0,
        0, 384.0/512.0};
    glVertexAttribPointer(glGetAttribLocation(sFinal, "aTexCoord"), 2, GL_FLOAT, GL_FALSE, 0, vTexCoords);
    glEnableVertexAttribArray(glGetAttribLocation(sFinal, "aTexCoord"));
    gpu_set_opaque(true);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gpu_set_opaque(false);
}
