#include "v4l2_device.hpp"
#include "common.h"
#include <cassert>
#include <drm/drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Depending on the configuration method, the name of the device node name
 * of the dmabuf-heap changes. If the CMA area is configured from a device
 * tree node, the heap node is '/dev/dma_heap/linux,cma', otherwise the
 * node is '/dev/dma_heap/reserved'.
 * So let's just try both.
 */
int dmabuf_heap_open() {
  int i;
  static const char *heap_names[] = {"/dev/dma_heap/linux,cma",
                                     "/dev/dma_heap/reserved"};

  for (i = 0; i < 2; i++) {
    int fd = open(heap_names[i], O_RDWR, 0);

    if (fd >= 0)
      return fd;
  }

  return -1;
}

void dmabuf_heap_close(int heap_fd) { close(heap_fd); }

int dmabuf_heap_alloc(int heap_fd, const char *name, size_t size) {
  struct dma_heap_allocation_data alloc = {0};

  alloc.len = size;
  alloc.fd_flags = O_CLOEXEC | O_RDWR;

  if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) < 0)
    return -1;

  if (name)
    ioctl(alloc.fd, DMA_BUF_SET_NAME, name);

  return alloc.fd;
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

int dmabuf_sync_start(int buf_fd) { return dmabuf_sync(buf_fd, true); }
void dump_fmt(const v4l2_format &fmt, bool planes) {
  if (planes) {
    auto p = fmt.fmt.pix_mp;
    char c[5] = {};
    std::cout << "Size: " << p.width << "x" << p.height << "\n";
    memcpy(c, &p.pixelformat, 4);
    std::cout << "FMT: " << c << "\n";
    std::cout << "Planes: " << (int)p.num_planes << "\n";
    for (int i = 0; i < 2; i++) {
      auto pi = p.plane_fmt[i];
      std::cout << i << ":\n";
      std::cout << "Bytes Per Line: " << pi.bytesperline << "\n";
      std::cout << "Size : " << pi.sizeimage << "\n";
    }
  }
}

int dmabuf_sync_stop(int buf_fd) { return dmabuf_sync(buf_fd, false); }
v4l2_device_info open_video_device(const char *vdevice, uint32_t in_width,
                                   uint32_t in_height, uint32_t in_fourcc) {
  v4l2_device_info out;
  struct v4l2_capability caps;
  struct v4l2_format fmt;
  auto mplane_api = &out.mplane_api;
  auto pix_fmt = &out.fmt;
  struct stat st;
  if (stat(vdevice, &st) == -1) {

    printf("stat failed %s : %s", vdevice, strerror(errno));

    return {};
  }

  int fd = open(vdevice, O_RDWR);
  out.fd = fd;
  if (out.fd < 0) {
    printf("Failed to open %s: %s \n", vdevice, strerror(errno));
    return {};
  }

  memset(&caps, 0, sizeof(caps));
  if (ioctl(out.fd, VIDIOC_QUERYCAP, &caps)) {
    printf("VIDIOC_QUERYCAP: %s\n", strerror(errno));
    goto err_cleanup;
  }

  if (caps.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
    printf("Using single-planar API\n");
    *mplane_api = false;
  } else if (caps.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
    printf("Using multi-planar API\n");
    *mplane_api = true;
  } else {
    printf("Devicce does not support video capture\n");
    goto err_cleanup;
  }

  memset(&fmt, 0, sizeof(fmt));
  if (*mplane_api)
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  else
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(fd, VIDIOC_G_FMT, &fmt)) {
    printf("VIDIOC_G_FMT: %s\n", strerror(errno));
    goto err_cleanup;
  }

  if (in_width > 0)
    fmt.fmt.pix.width = in_width;

  if (in_height > 0)
    fmt.fmt.pix.height = in_height;

  if (in_fourcc)
    fmt.fmt.pix.pixelformat = in_fourcc;

  if (ioctl(fd, VIDIOC_S_FMT, &fmt)) {
    printf("VIDIOC_S_FMT: %s\n", strerror(errno));
    goto err_cleanup;
  }

  if (ioctl(fd, VIDIOC_G_FMT, &fmt)) {
    printf("VIDIOC_G_FMT: %s\n", strerror(errno));
    goto err_cleanup;
  }

  out.fmt = fmt;
  dump_fmt(fmt, *mplane_api);
  out.fd = fd;

  return out;

err_cleanup:
  close(fd);
  return {};
}
v4l2_dma_device_info init_dma(const v4l2_device_info &dev, int num_bufs,
                              EGLDisplay disp, EGLContext ctx) {
  v4l2_dma_device_info out;
  assert(dev.fd >= 0);

  std::vector<GLuint> tex(num_bufs);
  out.dma_heap_fd = dmabuf_heap_open();
  if (out.dma_heap_fd < 0) {
    printf("Could not open dmabuf-heap\n");
    return out;
  }
  int type;
  assert(num_bufs > 0);
  auto size_img = dev.fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
  int w = dev.fmt.fmt.pix_mp.width;
  int h = dev.fmt.fmt.pix_mp.height;
  std::cout << "Size " << size_img << std::endl;
  for (int i = 0; i < num_bufs; i++) {
    int fd = dmabuf_heap_alloc(out.dma_heap_fd, NULL,
                               dev.fmt.fmt.pix_mp.plane_fmt[0].sizeimage);
    if (fd < 0) {
      printf("Failed to alloc dmabuf %d\n", i);
      goto err_cleanup;
    }
    out.dma_bufs.push_back(fd);
    // void *map = mmap(0, dev.fmt.fmt.pix_mp.plane_fmt[0].sizeimage,
    //                  PROT_WRITE | PROT_READ, MAP_SHARED, out.dma_bufs[i], 0);
    // if (map == MAP_FAILED) {
    //   printf("Failed to map dmabuf %d\n", i);
    //   goto err_cleanup;
    // }
  }

  v4l2_requestbuffers reqbuf;
  memset(&reqbuf, 0, sizeof(reqbuf));
  reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  ;
  reqbuf.memory = V4L2_MEMORY_DMABUF;
  reqbuf.count = num_bufs;
  if (ioctl(dev.fd, VIDIOC_REQBUFS, &reqbuf) == -1) {
    if (errno == EINVAL)
      printf("Video capturing or DMABUF streaming is not supported\n");
    else
      perror("VIDIOC_REQBUFS");
    goto err_cleanup;
  }
  std::cout << "Count: " << reqbuf.count << std::endl;

  for (int i = 0; i < num_bufs; ++i) {
    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    memset(&buf, 0, sizeof(buf));
    buf.index = i;
    buf.memory = V4L2_MEMORY_DMABUF;
    if (dev.mplane_api) {
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      memset(&planes, 0, sizeof(planes));
      buf.m.planes = planes;
      buf.length = 1;
      buf.m.planes[0].m.fd = out.dma_bufs[i];

    } else {

      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.m.fd = out.dma_bufs[i];
    }
    if (ioctl(dev.fd, VIDIOC_QBUF, &buf)) {
      printf("VIDIOC_QBUF: %s\n", strerror(errno));
      goto err_cleanup;
    }
  }
  glGenTextures(num_bufs, tex.data());
  for (int i = 0; i < num_bufs; i++) {
    EGLint attributes[] = {
        EGL_WIDTH,
        (EGLint)dev.fmt.fmt.pix_mp.width,
        EGL_HEIGHT,
        (EGLint)dev.fmt.fmt.pix_mp.height,
        EGL_LINUX_DRM_FOURCC_EXT,
        DRM_FORMAT_NV12,
        EGL_DMA_BUF_PLANE0_FD_EXT,
        out.dma_bufs[i],
        EGL_DMA_BUF_PLANE0_OFFSET_EXT,
        0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,
        w,
        EGL_DMA_BUF_PLANE1_FD_EXT,
        out.dma_bufs[i],
        EGL_DMA_BUF_PLANE1_OFFSET_EXT,
        w * h,
        EGL_DMA_BUF_PLANE1_PITCH_EXT,
        w / 2,
        EGL_YUV_COLOR_SPACE_HINT_EXT,
        EGL_ITU_REC709_EXT,
        EGL_SAMPLE_RANGE_HINT_EXT,
        EGL_YUV_FULL_RANGE_EXT,
        EGL_NONE,
    };
    EGLImageKHR eglImage = eglCreateImageKHR(
        disp, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes);

    if (eglImage == 0) {
      printf("Failed EGL create image %i \n", eglGetError());
      goto err_cleanup;
    }

    GL_CHECK(glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex[i]));
    GL_CHECK(glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, eglImage));
    out.egl_imgs.push_back(egl_dma_img{eglImage, tex[i]});
  }

  type = dev.mplane_api ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                        : V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(dev.fd, VIDIOC_STREAMON, &type)) {
    printf("VIDIOC_STREAMON: %s\n", strerror(errno));
    goto err_cleanup;
  }
  printf("Started DMA stream\n");
  return out;

err_cleanup:
  for (auto i : out.dma_bufs_maps) {
    munmap(i, dev.fmt.fmt.pix_mp.plane_fmt[0].sizeimage);
  }
  for (auto i : out.dma_bufs) {
    close(i);
  }
  close(out.dma_heap_fd);
  return {};
}

EGLImageKHR create_nv12_drm(int w, int h, int fd, EGLDisplay disp) {
  EGLint attributes[] = {
      EGL_WIDTH,
      w,
      EGL_HEIGHT,
      h,
      EGL_LINUX_DRM_FOURCC_EXT,
      DRM_FORMAT_NV12,
      EGL_DMA_BUF_PLANE0_FD_EXT,
      (EGLint)fd,
      EGL_DMA_BUF_PLANE0_OFFSET_EXT,
      0,
      EGL_DMA_BUF_PLANE0_PITCH_EXT,
      w,
      EGL_DMA_BUF_PLANE1_FD_EXT,
      (EGLint)fd,
      EGL_DMA_BUF_PLANE1_OFFSET_EXT,
      w * h,
      EGL_DMA_BUF_PLANE1_PITCH_EXT,
      w / 2,

      EGL_YUV_COLOR_SPACE_HINT_EXT,
      EGL_ITU_REC709_EXT,
      EGL_SAMPLE_RANGE_HINT_EXT,
      EGL_YUV_FULL_RANGE_EXT,
      EGL_NONE,
  };
  EGLImageKHR eglImage = eglCreateImageKHR(
      disp, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes);
  return eglImage;
}
EGLImageKHR create_rg88_drm(int w, int h, int fd, EGLDisplay disp) {
  EGLint attributes[] = {
      EGL_WIDTH,
      w,
      EGL_HEIGHT,
      h,
      EGL_LINUX_DRM_FOURCC_EXT,
      DRM_FORMAT_RG88,
      EGL_DMA_BUF_PLANE0_FD_EXT,
      (EGLint)fd,
      EGL_DMA_BUF_PLANE0_OFFSET_EXT,
      0,
      EGL_DMA_BUF_PLANE0_PITCH_EXT,
      w * 2,
      EGL_NONE,
  };
  EGLImageKHR eglImage = eglCreateImageKHR(
      disp, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes);
  return eglImage;
}

EGLImageKHR create_rgb888_drm(int w, int h, int fd, EGLDisplay disp) {
  EGLint attributes[] = {
      EGL_WIDTH,
      w,
      EGL_HEIGHT,
      h,
      EGL_LINUX_DRM_FOURCC_EXT,
      DRM_FORMAT_RGB888,
      EGL_DMA_BUF_PLANE0_FD_EXT,
      (EGLint)fd,
      EGL_DMA_BUF_PLANE0_OFFSET_EXT,
      0,
      EGL_DMA_BUF_PLANE0_PITCH_EXT,
      w * 3,
      EGL_NONE,
  };
  EGLImageKHR eglImage = eglCreateImageKHR(
      disp, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes);
  return eglImage;
}
EGLImageKHR create_rgb8888_drm(int w, int h, int fd, EGLDisplay disp) {
  EGLint attributes[] = {
      EGL_WIDTH,
      w,
      EGL_HEIGHT,
      h,
      EGL_LINUX_DRM_FOURCC_EXT,
      DRM_FORMAT_RGBA8888,
      EGL_DMA_BUF_PLANE0_FD_EXT,
      (EGLint)fd,
      EGL_DMA_BUF_PLANE0_OFFSET_EXT,
      0,
      EGL_DMA_BUF_PLANE0_PITCH_EXT,
      w * 4,
      EGL_NONE,
  };
  EGLImageKHR eglImage = eglCreateImageKHR(
      disp, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes);
  return eglImage;
}
EGLImageKHR create_r8_drm(int w, int h, int fd, EGLDisplay disp) {
  EGLint attributes[] = {
      EGL_WIDTH,
      w,
      EGL_HEIGHT,
      h,
      EGL_LINUX_DRM_FOURCC_EXT,
      DRM_FORMAT_R8,
      EGL_DMA_BUF_PLANE0_FD_EXT,
      (EGLint)fd,
      EGL_DMA_BUF_PLANE0_OFFSET_EXT,
      0,
      EGL_DMA_BUF_PLANE0_PITCH_EXT,
      w,
      EGL_NONE,
  };
  EGLImageKHR eglImage = eglCreateImageKHR(
      disp, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes);
  return eglImage;
}

std::vector<egl_dma_frame> create_egl_frame(const v4l2_device_info &dev,
                                            const v4l2_dma_device_info &dma,
                                            EGLDisplay disp, int num_frames,
                                            int w, int h, int format) {
  std::vector<egl_dma_frame> out;
  assert(dev.fd >= 0);
  assert(dma.dma_heap_fd >= 0);
  int size_img = w * h;
  switch (format) {
  case DRM_FORMAT_NV12:
    size_img = w * h + (w * h / 2);
    break;
  case DRM_FORMAT_RG88:
    size_img = w * h * 2;
    break;
  case DRM_FORMAT_R8:
    size_img = w * h;
    break;
  case DRM_FORMAT_RGB888:
    size_img = w * h * 3;
    break;
  case DRM_FORMAT_RGBA8888:
    size_img = w * h * 4;
    break;
  default:
    printf("unsupported format");
    return {};
  }

  for (int i = 0; i < num_frames; i++) {
    egl_dma_frame frame = {};
    frame.w = w;
    frame.drm_format = format;
    frame.h = h;
    frame.size_bytes = size_img;
    frame.fd = dmabuf_heap_alloc(dma.dma_heap_fd, NULL, size_img);
    if (frame.fb < 0) {
      printf("DMA ALLOC: %s\n", strerror(errno));
      goto err_cleanup;
    }

    switch (format) {
    case DRM_FORMAT_NV12:
      frame.img = create_nv12_drm(w, h, frame.fd, disp);
      break;
    case DRM_FORMAT_RG88:
      frame.img = create_rg88_drm(w, h, frame.fd, disp);
      break;
    case DRM_FORMAT_RGB888:
      frame.img = create_rgb888_drm(w, h, frame.fd, disp);
      break;
    case DRM_FORMAT_R8:
      frame.img = create_r8_drm(w, h, frame.fd, disp);
      break;
    case DRM_FORMAT_RGBA8888:
      frame.img = create_rgb8888_drm(w, h, frame.fd, disp);
      break;
    default:
      goto err_cleanup;
    }
    if (frame.img == 0) {
      printf("Failed EGL create image %i \n", eglGetError());
      goto err_cleanup;
    }
#if 0

    glActiveTexture(GL_TEXTURE0);
    GL_CHECK(glGenTextures(1, &frame.tex));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, frame.tex));
    GL_CHECK(glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, frame.img));
    GL_CHECK(glGenFramebuffers(1, &frame.fb));
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, frame.fb));
    GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                    GL_TEXTURE_2D, frame.tex, 0));
#else
    if (!GL_CHECK(glGenRenderbuffers(1, &frame.tex))) {
      goto err_cleanup;
    }
    if (!GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, frame.tex))) {
      goto err_cleanup;
    }
    if (!GL_CHECK(glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER,
                                                         frame.img))) {
      goto err_cleanup;
    }
    GL_CHECK(glGenFramebuffers(1, &frame.fb));
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, frame.fb));
    if (!GL_CHECK(glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                            GL_COLOR_ATTACHMENT0,
                                            GL_RENDERBUFFER, frame.tex))) {
      goto err_cleanup;
    }

#endif
    out.push_back(frame);
  }
  return out;
err_cleanup:
  // TODO
  return {};
}
