# 使用说明 - RK_VideoPipe 高性能模式

## 🚀 运行方式

```bash
/home/orangepi/work/RK_VideoPipe/build/rk_videopipe_opt \
    -f /mnt/nfs/datasets/video/uav.mp4 \
    -m 1 \
    -fps
```

## 📊 性能指标

- **平均 FPS**: 129 FPS
- **峰值 FPS**: 136 FPS
- **性能提升**: 相比优化前的 50 FPS，提升了 158%

## ⌨️ 退出方式

程序支持多种退出方式：

| 方法 | 操作 |
|------|------|
| **方法 1** | 在终端按 `Ctrl+C` 立即退出 |
| **方法 2** | 在 SDL2 窗口中按 `ESC` 键 |
| **方法 3** | 点击 SDL2 窗口的关闭按钮 |
| **自动退出** | 视频播放完成后程序会自动退出 |

## 🔧 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-f <file>` | 输入视频文件路径 | /mnt/nfs/datasets/video/uav.mp4 |
| `-m <mode>` | 运行模式：0=完整AI, 1=高性能 | 0 |
| `-fps` | 显示 FPS 覆盖层 | false |
| `-v` | 启用 VSync（会降低 FPS） | false |
| `-D <driver>` | SDL2 视频驱动（如 x11, wayland） | "" |
| `-R <driver>` | SDL2 渲染驱动（如 opengl, opengles2） | "" |

## 📝 示例命令

```bash
# 基本使用（50 FPS 视频，可达到 129 FPS）
/home/orangepi/work/RK_VideoPipe/build/rk_videopipe_opt \
    -f /mnt/nfs/datasets/video/uav.mp4 -m 1

# 显示 FPS 覆盖层
/home/orangepi/work/RK_VideoPipe/build/rk_videopipe_opt \
    -f /mnt/nfs/datasets/video/uav.mp4 -m 1 -fps

# 启用 VSync（画面更稳定，但 FPS 会降低）
/home/orangepi/work/RK_VideoPipe/build/rk_videopipe_opt \
    -f /mnt/nfs/datasets/video/uav.mp4 -m 1 -v -fps

# 完整 AI 流水线（YOLO + 跟踪 + 姿态估计等）
/home/orangepi/work/RK_VideoPipe/build/rk_videopipe_opt \
    -f /mnt/nfs/datasets/video/uav.mp4 -m 0
```

## 💡 优化详情

### 移除的 FPS 限制

1. **MPP 解码器限制** (`mpp_decoder.cpp`)
   - 删除了基于视频 FPS 的 `usleep()` 延迟
   - 允许解码器全速运行

2. **主循环延迟** (`vp_mpp_file_src_node.cpp`)
   - 删除了 100 微秒的循环延迟
   - 解码循环全速运行

### 性能瓶颈分析

虽然已达到 129 FPS，但相比 MPP 示例的 160+ FPS，还有 ~20% 的差距，可能来自：

- **memcpy 开销**: SDL2 渲染中的 NV12 数据拷贝
- **队列同步**: 节点间信号量和队列操作
- **回调开销**: MPP 解码回调的函数调用
- **日志输出**: FPS 日志的格式化和打印

## ⚠️ 注意事项

1. **显示设备**: 程序需要连接显示设备（HDMI）才能运行
2. **自动退出**: 视频播放完成后程序会自动退出，也可以手动退出
3. **资源占用**: CPU 使用率较高（全速解码和渲染）
4. **内存**: 约解码缓冲区占用一定的内存

## 🎯 性能对比

| 场景 | FPS | 说明 |
|------|-----|------|
| MPP 示例 (mp4_hw_dec_sdl2) | 160+ | 纯解码+显示，基准性能 |
| **本程序优化后** | **129** | **全速解码+显示** |
| 本程序优化前 | 50 | 受视频源 FPS 限制 |
| 完整 AI 流水线 | ~15-30 | 包含大量 AI 处理 |
