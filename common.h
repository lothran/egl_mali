#pragma once
#include "glad/gles2.h"
#include <iostream>

bool CheckOpenGLError(const char *stmt, const char *fname, int line);

#define GL_CHECK(stmt)                                                         \
  [&]() {                                                                      \
    stmt;                                                                      \
    return CheckOpenGLError(#stmt, __FILE__, __LINE__);                        \
  }()
