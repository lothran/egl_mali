

#include "glad/egl.h"
#include "glad/gles2.h"
#include "v4l2_device.hpp"
#include <iostream>
#include <ostream>

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>

#include <drm/drm_fourcc.h>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "common.h"
#include "stb_image.h"
#include "stbi_image_write.h"
#include <chrono>
#include <fstream>
#include <streambuf>

static const EGLint configAttribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                       EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                       EGL_NONE};
std::string read_file(const char *path) {
  std::ifstream t(path);
  std::string str((std::istreambuf_iterator<char>(t)),
                  std::istreambuf_iterator<char>());
  return str;
}

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

void init_tex2d(GLuint tex, int w, int h, GLenum fmt, GLenum data_fmt = GL_RGB,
                void *data = 0) {
  GL_CHECK(glBindTexture(GL_TEXTURE_2D, tex));
  GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
  GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
  GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
  GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));

  std::cout << "W: " << w << " " << "H " << h << std::endl;
  int max_size = 9;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
  std::cout << "MAx " << max_size << "\n";
  GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, data_fmt,
                        GL_UNSIGNED_BYTE, data));
}
struct Img {
  int w, h;
  GLenum fmt;
  GLuint tex;
};

static const int pbufferWidth = 640;
static const int pbufferHeight = 640;
static const EGLint pbufferAttribs[] = {
    EGL_WIDTH, pbufferWidth, EGL_HEIGHT, pbufferHeight, EGL_NONE,
};
Img load_img(const char *path) {
  Img img;
  int c;
  void *data = stbi_load(path, &img.w, &img.h, &c, 3);
  if (data == nullptr) {
    std::cerr << "Failed to open: " << path << "\n";
    exit(1);
  }
  glGenTextures(1, &img.tex);
  std::cout << path << " " << img.w << "x" << img.h << "px\n";
  init_tex2d(img.tex, img.w, img.h, GL_RGB, GL_RGB, data);

  stbi_image_free(data);
  return img;
}

GLuint create_prog(const char *vert, const char *frag) {
  auto vert_src = read_file(vert);
  auto frag_src = read_file(frag);
  unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);

  GL_CHECK(int vertexShader = 0;);
  const char *ptr = vert_src.data();
  std::cout << "Compiling " << ptr << "\n";
  GL_CHECK(glShaderSource(vertexShader, 1, &ptr, NULL));
  GL_CHECK(glCompileShader(vertexShader));
  // check for shader compile errors
  int success;
  char infoLog[512];
  GL_CHECK(glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success));
  if (!success) {
    GLint logLength;
    glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &logLength);
    glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n"
              << logLength << "\n"
              << infoLog << std::endl;

    exit(1);
  }
  // fragment shader
  unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  ptr = frag_src.data();
  glShaderSource(fragmentShader, 1, &ptr, NULL);
  glCompileShader(fragmentShader);
  // check for shader compile errors
  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n"
              << infoLog << std::endl;

    exit(1);
  }
  std::cout << "Compiling " << ptr << "\n";
  glShaderSource(vertexShader, 1, &ptr, NULL);
  // link shaders
  unsigned int shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vertexShader);
  glAttachShader(shaderProgram, fragmentShader);
  glLinkProgram(shaderProgram);
  // check for linking errors
  glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n"
              << infoLog << std::endl;
    exit(1);
  }

  std::cout << "Compiling Success \n";
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
  return shaderProgram;
}
static void EGLAPIENTRY LogEGLDebugMessage(EGLenum error, const char *command,
                                           EGLint message_type,
                                           EGLLabelKHR thread_label,
                                           EGLLabelKHR object_label,
                                           const char *message) {
  std::cout << "EGL Error:" << command << "=> " << message;
}
static void on_gl_error(GLenum source, GLenum type, GLuint id, GLenum severity,
                        GLsizei length, const GLchar *message,
                        const void *userParam) {

  printf("%s\n", message);
}

static int dmabuf_sync(int buf_fd, bool start) {
  struct dma_buf_sync sync = {0};

  sync.flags =
      (start ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END) | DMA_BUF_SYNC_RW;

  do {
    if (ioctl(buf_fd, DMA_BUF_IOCTL_SYNC, &sync) == 0)
      return 0;
  } while ((errno == EINTR) || (errno == EAGAIN));

  return -1;
}
int main(int argc, const char **argv) {
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
  assert(eglSurf != EGL_NO_SURFACE);
  print_gl_error("eglCreatePbufferSurface");

  // 5. Create a context and make it current
  //
  EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLContext eglCtx =
      eglCreateContext(eglDpy, eglCfg, EGL_NO_CONTEXT, ctx_attribs);

  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    print_gl_error("eglBindAPI");
    return 1;
  }
  if (!eglMakeCurrent(eglDpy, eglSurf, eglSurf, eglCtx)) {
    print_gl_error("eglMakeCurrent");
    return 1;
  }

  // EGLAttrib controls[] = {
  //     EGL_DEBUG_MSG_CRITICAL_KHR,
  //     EGL_TRUE,
  //     EGL_DEBUG_MSG_ERROR_KHR,
  //     EGL_TRUE,
  //     EGL_DEBUG_MSG_WARN_KHR,
  //     EGL_TRUE,
  //     EGL_DEBUG_MSG_INFO_KHR,
  //     EGL_TRUE,
  //     EGL_NONE,
  // };
  // eglDebugMessageControlKHR(&LogEGLDebugMessage, controls);
  int val;
  eglQueryContext(eglDpy, eglCtx, EGL_CONTEXT_CLIENT_VERSION, &val);
  assert(val == 2);

  std::cout << "API version: " << gladLoaderLoadGLES2() << "\n";
  std::cout << "GLES extensions: " << glGetString(GL_EXTENSIONS) << "\n";
  // During init, enable debug output
  GLuint simple_shdr =
      create_prog("shaders/simple.vert", "shaders/simple.frag");
  std::vector<float> fullscreen_quad = {-1, -1, 1, -1, -1, 1,
                                        -1, 1,  1, -1, 1,  1};
  GLuint fullscreen_quad_buf;
  GL_CHECK(glGenBuffers(1, &fullscreen_quad_buf));
  GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, fullscreen_quad_buf));
  glBufferData(GL_ARRAY_BUFFER, fullscreen_quad.size() * 4,
               fullscreen_quad.data(), GL_STATIC_DRAW);
  glUseProgram(simple_shdr);
  int loc = glGetAttribLocation(simple_shdr, "pos");
  glEnableVertexAttribArray(loc);
  GL_CHECK(glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, 0));

  std::vector<char> buffer(pbufferHeight * pbufferWidth * 4);
  EGLint fence_attrib[] = {EGL_NONE};

  GL_CHECK(glViewport(0, 0, pbufferWidth, pbufferHeight));

  GL_CHECK(glActiveTexture(GL_TEXTURE0));

  auto img = load_img("test.jpg");

  v4l2_device_info v4l2_dev =
      open_video_device(argv[1], 1920, 1536, V4L2_PIX_FMT_NV12);
  v4l2_dma_device_info v4l2_dma_dev = init_dma(v4l2_dev, 3, eglDpy, eglCtx);
  auto out_frames =
      create_egl_frame(v4l2_dev, v4l2_dma_dev, eglDpy, 30, pbufferWidth,
                       pbufferHeight, DRM_FORMAT_RG88);

  for (int i = 0; i < out_frames.size(); i++) {
    // GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    v4l2_buffer buf;
    v4l2_plane planes[VIDEO_MAX_PLANES];
    int buf_index;

    auto t0 = std::chrono::high_resolution_clock::now();
    /* dequeue a buffer */
    memset(&buf, 0, sizeof(buf));
    buf.memory = V4L2_MEMORY_DMABUF;
    if (v4l2_dev.mplane_api) {
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      memset(&planes, 0, sizeof(planes));
      buf.m.planes = planes;
      buf.length = 1;
    } else {
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }
    if (ioctl(v4l2_dev.fd, VIDIOC_DQBUF, &buf)) {
      printf("VIDIOC_DQBUF: %s\n", strerror(errno));
      continue;
    }
    buf_index = buf.index;
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, out_frames[i].fb));

    GL_CHECK(glBindTexture(GL_TEXTURE_EXTERNAL_OES,
                           v4l2_dma_dev.egl_imgs[buf_index].tex));

    GL_CHECK(glDrawArrays(GL_TRIANGLES, 0, 6));

    // GL_CHECK(glReadPixels(0, 0, pbufferWidth, pbufferHeight, GL_RGBA,
    //                       GL_UNSIGNED_BYTE, buffer.data()));
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    eglWaitGL();
    std::cout << "eglSwapBuffers\n";
    eglSwapBuffers(eglDpy, eglSurf);

    /* enqueue a buffer */
    memset(&buf, 0, sizeof(buf));
    buf.index = buf_index;
    buf.memory = V4L2_MEMORY_DMABUF;
    if (v4l2_dev.mplane_api) {
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      memset(&planes, 0, sizeof(planes));
      buf.m.planes = planes;
      buf.length = 1;

      buf.m.planes[0].m.fd = v4l2_dma_dev.dma_bufs[buf_index];

    } else {
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.m.fd = v4l2_dma_dev.dma_bufs[buf_index];
    }

    std::cout << "VIDIOC_QBUF\n";
    if (ioctl(v4l2_dev.fd, VIDIOC_QBUF, &buf)) {
      printf("VIDIOC_QBUF: %s\n", strerror(errno));
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout << "Took: "
              << std::chrono::duration<double>(t1 - t0).count() * 1000
              << "ms\n";
  }

  for (int i = 0; i < out_frames.size(); i++) {
    void *map = mmap(0, out_frames[i].size_bytes, PROT_READ, MAP_SHARED,
                     out_frames[i].fd, 0);

    dmabuf_sync(out_frames[i].fd, true);
    if (out_frames[i].drm_format == DRM_FORMAT_NV12) {

      stbi_write_png(("out" + std::to_string(i) + ".png").c_str(),
                     out_frames[i].w, out_frames[i].h, 1, map,
                     pbufferWidth * 1);
    }
    if (out_frames[i].drm_format == DRM_FORMAT_RGBA8888) {
      stbi_write_png(("out" + std::to_string(i) + ".png").c_str(),
                     out_frames[i].w, out_frames[i].h, 4, map,
                     pbufferWidth * 4);
    }
    if (out_frames[i].drm_format == DRM_FORMAT_RG88) {
      stbi_write_png(("out" + std::to_string(i) + ".png").c_str(),
                     out_frames[i].w, out_frames[i].h, 2, map,
                     pbufferWidth * 2);
    }
  }

  // 6. Terminate EGL when finished
  eglTerminate(eglDpy);
  return 0;
}
