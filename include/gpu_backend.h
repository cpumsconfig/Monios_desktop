#ifndef _GPU_BACKEND_H_
#define _GPU_BACKEND_H_

#include "stdbool.h"

bool gpu_opengl_probe(bool framebuffer_ready);
bool gpu_vulkan_probe(bool framebuffer_ready);
bool gpu_nvidia_probe(bool framebuffer_ready);
bool gpu_nvidia_ready(bool framebuffer_ready, bool nvidia_detected);
bool gpu_igpu_probe(bool framebuffer_ready);

#endif
