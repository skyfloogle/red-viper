#include "GLES2/gl2.h"

#include "vb_dsp.h"
#include "video_hard.h"
#include "v810_mem.h"

GLuint sChar, sFinal, sAffine;

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
        "}\n",

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
        "}\n"
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
        "uniform mediump vec3 uPalette[4];\n"
        "varying mediump vec2 vTexCoord;\n"
        "void main() {\n"
        "   mediump vec4 color = texture2D(sTex, vTexCoord);\n"
        "   gl_FragColor = vec4(mix(mix(mix(uPalette[0], uPalette[1], color.x), uPalette[2], color.y), uPalette[3], color.z), 1.0);\n"
        "}\n"
    );

    sAffine = build_shader(
        "attribute vec4 aParams;\n"
        "attribute vec2 aOffset;\n"
        "varying vec2 vTexCoord;\n"
        "void main() {\n"
        "   gl_Position = vec4(aParams.yx / 256.0 - 1.0, 0.0, 1.0);\n"
        "   vTexCoord = (aParams.zw + aOffset) / 4096.0;\n"
        "}\n",

        "uniform sampler2D sTex;\n"
        "uniform bool uRepeat;\n"
        "uniform highp float uStartMap;\n"
        // (fullwidth, height, modwidth)
        "uniform highp vec3 uWorldSize;\n"
        "uniform bool uMapVisible[8];\n"
        "varying highp vec2 vTexCoord;\n"
        "void main() {\n"
        "   highp vec2 texCoord = vTexCoord;\n"
        "   if (!uRepeat && mod(texCoord, uWorldSize.xy) != texCoord) {\n"
                // TODO: overtile
        "       gl_FragColor = vec4(0.0);\n"
        "       return;\n"
        "   }\n"
        "   texCoord = mod(texCoord, uWorldSize.zy);\n"
        "   mediump float sub_map = floor(texCoord.x) + floor(texCoord.y) * uWorldSize.x;\n"
        "   if (!uMapVisible[int(sub_map)]) {\n"
        "       gl_FragColor = vec4(0.0);\n"
        "       return;\n"
        "   }\n"
        "   gl_FragColor = texture2D(sTex, (mod(texCoord.yx, 1.0) + vec2((uStartMap + sub_map), 0.0)) * vec2(1.0 / 8.0, 1.0));\n"
        "}\n"
    );
}

extern GLuint screenTexHard[2];
void video_hard_render(int drawn_fb);

void video_render(int displayed_fb, bool on_time) {
    update_texture_cache_hard();
    video_hard_render(displayed_fb);

    // we need to have these caches during rendering
    tDSPCACHE.CharCacheInvalid = false;
    memset(tDSPCACHE.BGCacheInvalid, 0, sizeof(tDSPCACHE.BGCacheInvalid));
    memset(tDSPCACHE.CharacterCache, 0, sizeof(tDSPCACHE.CharacterCache));

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(sFinal);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTexHard[displayed_fb]);
    glViewport(0, 0, 384*2, 224*2);
    glScissor(0, 0, 384*2, 224*2);

    glUniform1i(glGetUniformLocation(sFinal, "sTex"), 0);
    
    float colors[4][3] = {
        {0, 0, 0},
        {vb_state->tVIPREG.BRTA / 128.0, 0, 0},
        {vb_state->tVIPREG.BRTB / 128.0, 0, 0},
        {(vb_state->tVIPREG.BRTA + vb_state->tVIPREG.BRTB + vb_state->tVIPREG.BRTC) / 128.0, 0, 0},
    };
    glUniform3fv(glGetUniformLocation(sFinal, "uPalette"), 4, &colors[0][0]);

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
