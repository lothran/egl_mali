#pragma once

#include "errno.h"
#include <fcntl.h>
#include <linux/videodev2.h>
#include <optional>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "glad/egl.h"
#include "glad/gles2.h"
#include <unistd.h>
#include <vector>

struct v4l2_device_info {
  int fd = -1;
  v4l2_format fmt;
  bool mplane_api;
};
v4l2_device_info open_video_device(const char *vdevice, uint32_t in_width,
                                   uint32_t in_height, uint32_t in_fourcc);

struct egl_dma_img {
  EGLImage img = 0;
  GLuint tex = 0;
};
struct v4l2_dma_device_info {
  std::vector<int> dma_bufs;
  std::vector<void *> dma_bufs_maps;
  std::vector<egl_dma_img> egl_imgs;
  int dma_heap_fd = -1;
};

v4l2_dma_device_info init_dma(const v4l2_device_info &dev, int num_bufs,
                              EGLDisplay disp, EGLContext ctx);

struct egl_dma_frame {
  int fd = -1;
  EGLImage img = 0;
  GLuint tex = 0, fb = 0;
  int size_bytes;
  int drm_format;
  int w, h;
};
std::vector<egl_dma_frame> create_egl_frame(const v4l2_device_info &dev,
                                            const v4l2_dma_device_info &dma,
                                            EGLDisplay disp, int num_frames,
                                            int w, int h, int format);
