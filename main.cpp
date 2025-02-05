

#include "glad/egl.h"
#include "glad/gles2.h"
#include "incbin.h"
#include <iostream>
static const EGLint configAttribs[] = {EGL_SURFACE_TYPE,
                                       EGL_PBUFFER_BIT,
                                       EGL_BLUE_SIZE,
                                       8,
                                       EGL_GREEN_SIZE,
                                       8,
                                       EGL_RED_SIZE,
                                       8,
                                       EGL_RENDERABLE_TYPE,
                                       EGL_OPENGL_ES2_BIT,
                                       EGL_NONE};

static const int pbufferWidth = 32;
static const int pbufferHeight = 32;
INCTXT(simple_vert, "shaders/simple.vert");
INCTXT(simple_frag, "shaders/simple.frag");

static const EGLint pbufferAttribs[] = {
    EGL_WIDTH, pbufferWidth, EGL_HEIGHT, pbufferHeight, EGL_NONE,
};
std::string egl_error_string(EGLint error) {
  switch (error) {
  case EGL_SUCCESS:
    return "No error";
  case EGL_NOT_INITIALIZED:
    return "EGL not initialized or failed to initialize";
  case EGL_BAD_ACCESS:
    return "Resource inaccessible";
  case EGL_BAD_ALLOC:
    return "Cannot allocate resources";
  case EGL_BAD_ATTRIBUTE:
    return "Unrecognized attribute or attribute value";
  case EGL_BAD_CONTEXT:
    return "Invalid EGL context";
  case EGL_BAD_CONFIG:
    return "Invalid EGL frame buffer configuration";
  case EGL_BAD_CURRENT_SURFACE:
    return "Current surface is no longer valid";
  case EGL_BAD_DISPLAY:
    return "Invalid EGL display";
  case EGL_BAD_SURFACE:
    return "Invalid surface";
  case EGL_BAD_MATCH:
    return "Inconsistent arguments";
  case EGL_BAD_PARAMETER:
    return "Invalid argument";
  case EGL_BAD_NATIVE_PIXMAP:
    return "Invalid native pixmap";
  case EGL_BAD_NATIVE_WINDOW:
    return "Invalid native window";
  case EGL_CONTEXT_LOST:
    return "Context lost";
  }
  return "Unknown error " + std::to_string(error);
}

void print_gl_error(const char *ctx = "") {
  int err = eglGetError();
  if (err != EGL_SUCCESS) {
    std::cerr << ctx << " failed: " << egl_error_string(err);
  }
}

void init_tex2d(GLuint tex, int w, int h, GLenum fmt) {
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE,
               nullptr);
}

GLuint create_prog(const char *vert, const char *frag) {
  unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vert, NULL);
  glCompileShader(vertexShader);
  // check for shader compile errors
  int success;
  char infoLog[512];
  glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
    std::cerr << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n"
              << infoLog << std::endl;
  }
  // fragment shader
  unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &frag, NULL);
  glCompileShader(fragmentShader);
  // check for shader compile errors
  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
    std::cerr << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n"
              << infoLog << std::endl;
  }
  // link shaders
  unsigned int shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vertexShader);
  glAttachShader(shaderProgram, fragmentShader);
  glLinkProgram(shaderProgram);
  // check for linking errors
  glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
    std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n"
              << infoLog << std::endl;
  }
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
  return shaderProgram;
}

int main() {
  gladLoaderLoadEGL(EGL_NO_DISPLAY);
  EGLDisplay eglDpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);

  EGLint major, minor;
  if (!eglInitialize(eglDpy, &major, &minor)) {
    print_gl_error("eglInitialize");
    return 1;
  }

  gladLoaderLoadEGL(eglDpy);
  std::cout << "EGL extensions: " << eglQueryString(eglDpy, EGL_EXTENSIONS)
            << "\n";

  // 2. Select an appropriate configuration
  EGLint numConfigs;
  EGLConfig eglCfg;
  if (!eglChooseConfig(eglDpy, configAttribs, &eglCfg, 1, &numConfigs)) {
    print_gl_error("eglChooseConfig");
    return 1;
  }

  // 3. Create a surface
  EGLSurface eglSurf = eglCreatePbufferSurface(eglDpy, eglCfg, pbufferAttribs);
  print_gl_error("eglCreatePbufferSurface");

  // 5. Create a context and make it current
  EGLContext eglCtx = eglCreateContext(eglDpy, eglCfg, EGL_NO_CONTEXT, nullptr);

  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    print_gl_error("eglBindAPI");
    return 1;
  }
  if (!eglMakeCurrent(eglDpy, eglSurf, eglSurf, eglCtx)) {
    print_gl_error("eglMakeCurrent");
    return 1;
  }

  std::cout << "API version: " << gladLoaderLoadGLES2() << "\n";
  GLuint simple_shdr = create_prog(gsimple_vertData, gsimple_fragData);
  GLuint tex[2];
  glGenTextures(2, tex);
  GLuint high_tex = tex[0];
  GLuint low_tex = tex[1];
  GLenum low_fmt = GL_RGBA;
  GLenum high_fmt = GL_RGBA;
  int high_w = 1920;
  int high_h = 1536;

  int low_w = 640;
  int low_h = 640;

  init_tex2d(high_tex, high_w, high_h, high_fmt);
  init_tex2d(low_tex, low_w, low_h, low_fmt);
  GLuint fb;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         low_tex, 0);

  while (true) {
    glBindFramebuffer(GL_FRAMEBUFFER, fb);

    eglSwapBuffers(eglDpy, eglSurf);
  }

  // 6. Terminate EGL when finished
  eglTerminate(eglDpy);
  return 0;
}
