# MPP+SDL2 高性能优化实现计划

## 问题分析

当前 `rk_videopipe` 的性能瓶颈主要在以下几个方面：

### 1. 当前架构的问题

**vp_file_src_node.cpp** (行 46, 70):
- 使用 GStreamer 管道: `filesrc -> qtdemux -> h264parse -> mppvideodec -> videoconvert -> appsink`
- `videoconvert` 进行 YUV->RGB/BGR 格式转换（CPU 密集操作）
- cv::Mat 创建和克隆操作（内存复制开销）

**vp_screen_des_node.cpp** (行 16, 35-38):
- 使用 GStreamer appsrc 管道显示
- ximagesink 可能不是最高效的显示方式
- 多次格式转换和内存拷贝

**完整流水线开销** (main.cc):
- YOLO 推理 -> ByteTrack -> RTMPose -> 分类 -> OSD -> 消息代理 -> 屏幕显示
- 每个节点的队列同步开销
- cv::Mat 在多个节点间复制和克隆

### 2. MPP 示例的优势 (mp4_hw_dec_sdl2.cpp)

- **直接 MPP 解码** (行 204-249): 不经过 GStreamer 中间层
- **NV12 输出格式** (行 240): 直接输出 SDL2 可用的格式，无需转换
- **SDL2 硬件加速渲染** (行 297-339): 直接纹理上传
- **分块发送数据包** (行 663-684): 4KB chunk 发送到解码器
- **重试机制** (行 627-643): 超时处理和轮询解码帧

## 实现方案

### 阶段 1: 扩展 vp_frame_meta 支持硬件缓冲区

**文件**: `vp_node/objects/vp_frame_meta.h`

添加新的成员变量：
```cpp
// NV12 格式原始数据（来自 MPP 解码器）
bool is_nv12 = false;
int fd = -1;              // DMA buffer fd
void* nv12_data = nullptr;  // 虚拟地址
size_t data_size = 0;
int stride_h = 0;           // 水平 stride
int stride_v = 0;           // 垂直 stride
```

### 阶段 2: 创建 MPP 硬件解码源节点

**文件**: `vp_node/nodes/vp_mpp_file_src_node.h` 和 `.cpp`

主要功能：
1. **FFmpeg demuxer**: 使用 `avformat_open_input`, `av_read_frame` 读取 H.264/H.265 数据包
2. **MPP 硬件解码器**:
   - `mpp_create` 创建解码上下文
   - `mpp_init` 初始化解码器
   - `decode_put_packet` 发送数据
   - `decode_get_frame` 获取解码帧
3. **NV12 输出**: 设置 `MPP_DEC_SET_OUTPUT_FORMAT` 为 `MPP_FMT_YUV420SP`
4. **分块发送**: 4KB chunk 发送数据包（参考 mp4_hw_dec_sdl2.cpp:663）
5. **信息变化处理**: `MPP_DEC_SET_EXT_BUF_GROUP` 和 `MPP_DEC_SET_INFO_CHANGE_READY`

GStreamer 管道对比:
```
当前: filesrc -> qtdemux -> h264parse -> mppvideodec -> videoconvert -> appsink -> cv::Mat
优化: avformat -> av_read_frame -> MPP (NV12) -> vp_frame_meta
```

### 阶段 3: 创建 SDL2 硬件渲染目标节点

**文件**: `vp_node/nodes/vp_sdl2_des_node.h` 和 `.cpp`

主要功能：
1. **SDL2 初始化**:
   - `SDL_Init` (SDL_INIT_VIDEO)
   - `SDL_CreateWindow` 创建窗口
   - `SDL_CreateRenderer` 创建加速渲染器
   - `SDL_CreateTexture` 创建 NV12 纹理

2. **直接纹理上传**:
   ```cpp
   SDL_LockTexture(texture, NULL, &pixels, &pitch);
   // 直接从 MPP buffer 复制 Y 和 UV 分量
   memcpy(dst_y, y_src, width);
   memcpy(dst_uv, uv_src, width);
   SDL_UnlockTexture(texture);
   SDL_RenderCopy(renderer, texture, NULL, NULL);
   SDL_RenderPresent(renderer);
   ```

3. **FPS 显示**: 绘制 7 段数码管风格的 FPS 覆盖层（可选）

4. **事件处理**: ESC 和窗口关闭事件

### 阶段 4: 构建系统集成

**文件**: `cmake/common.cmake`

添加 SDL2 支持：
```cmake
# SDL2
find_package(SDL2 REQUIRED)
if(SDL2_FOUND)
    include_directories(${SDL2_INCLUDE_DIRS})
    list(APPEND COMMON_LIBS ${SDL2_LIBRARIES})
endif()
```

### 阶段 5: 优化 main.cc 配置

**文件**: `main.cc`

提供两种模式：
```cpp
// 模式 1: 完整 AI 流水线（当前）
// 源 -> YOLO -> 跟踪 -> Pose -> 分类 -> OSD -> 显示

// 模式 2: 高性能解码+显示（新增）
// MPP源 -> SDL2显示（纯解码和显示，达到最高 FPS）
```

添加命令行参数或编译时宏来选择模式。

## 性能预期

基于 MPP 示例达到 170 FPS，优化后的预期：

| 场景 | 当前 FPS | 预期 FPS | 提升 |
|-------|---------|-----------|------|
| 纯解码+显示 | ~30-60 | 150-170 | 3-5x |
| 完整 AI 流水线 | ~15-30 | 20-40 | 1.5-2x |

完整 AI 流水线的提升来自于：
- 消除 videoconvert 格式转换
- 减少 cv::Mat 复制
- 更高效的源和目标节点

## 实现文件清单

### 新增文件
1. `vp_node/nodes/vp_mpp_file_src_node.h` - MPP 源节点头文件
2. `vp_node/nodes/vp_mpp_file_src_node.cpp` - MPP 源节点实现
3. `vp_node/nodes/vp_sdl2_des_node.h` - SDL2 目标节点头文件
4. `vp_node/nodes/vp_sdl2_des_node.cpp` - SDL2 目标节点实现

### 修改文件
1. `vp_node/objects/vp_frame_meta.h` - 添加 NV12/DMA buffer 支持
2. `vp_node/objects/vp_frame_meta.cpp` - 更新构造函数和拷贝构造函数
3. `cmake/common.cmake` - 添加 SDL2 依赖
4. `main.cc` - 添加高性能模式配置

## 关键优化点总结

1. **零拷贝传输**: MPP 解码输出 -> SDL2 纹理（最小内存复制）
2. **硬件加速**: MPP 解码 + SDL2 GPU 渲染
3. **格式优化**: NV12 直通，避免 YUV<->RGB 转换
4. **减少中间层**: 绕过 GStreamer，直接使用 MPP API
5. **批量处理**: 参考示例的分块发送和轮询机制

## 测试计划

1. 编译新的 rk_videopipe
2. 测试纯解码+显示模式，对比 `./mpp/build/example/mp4_hw_dec_sdl2 -i /mnt/nfs/datasets/video/uav.mp4`
3. 测试完整 AI 流水线模式的性能提升
4. 验证帧率稳定性和资源使用

## 风险和注意事项

1. **格式兼容性**: NV12 格式需要所有中间节点支持
2. **DMA buffer 生命周期**: MPP buffer 必须在 SDL2 使用前保持有效
3. **线程安全**: MPP 解码在源节点线程，SDL2 渲染在目标节点线程
4. **回退机制**: 如果 SDL2 初始化失败，回退到 GStreamer 显示
