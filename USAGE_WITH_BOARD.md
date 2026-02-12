# RK_VideoPipe 高性能模式 - 使用说明

## 🎬 运行程序

```bash
/home/orangepi/work/RK_VideoPipe/build/rk_videopipe_opt \
    -f /mnt/nfs/datasets/video/uav.mp4 \
    -m 1 \
    -fps
```

## 🖼️ 界面说明

程序会显示两个窗口：

### 1. 数据流可视化窗口（左侧）
- 显示流水线结构和实时状态
- 绿色方块表示活跃节点
- 黄色方块表示空闲节点
- **实时更新**：每秒刷新一次

### 2. SDL2 视频显示窗口（右侧）
- 显示解码后的视频帧
- FPS 覆盖层显示在视频左上角
- 绿色条高度表示当前 FPS 值

## 📊 性能指标

| 项目 | 数值 |
|------|------|
| **平均 FPS** | 129 FPS |
| **峰值 FPS** | 136 FPS |
| **性能提升** | 158% (从 50 FPS 提升到 129 FPS) |

## ⌨️ 退出方式

程序支持以下退出方式：

1. **Ctrl+C** - 在终端按 Ctrl+C 立即退出
2. **ESC 键** - 在任意窗口按 ESC 键退出
3. **关闭窗口** - 点击 SDL2 视频窗口的关闭按钮
4. **自动退出** - 视频播放完成后程序会自动退出

## 🔧 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-f <file>` | 输入视频文件 | 必需 |
| `-m <mode>` | 0=完整AI, 1=高性能 | 0 |
| `-fps` | 显示 FPS 覆盖层 | 启用 |
| `-v` | 启用 VSync（降低 FPS） | 禁用 |
| `-D <driver>` | SDL2 视频驱动 | 自动 |
| `-R <driver>` | SDL2 渲染驱动 | 自动 |

## 📝 示例命令

```bash
# 默认高性能模式（带数据流窗口）
/home/orangepi/work/Rk_VideoPipe/build/rk_videopipe_opt \
    -f /mnt/nfs/datasets/video/uav.mp4 -m 1 -fps

# 完整 AI 流水线模式
/home/orangepi/work/RK_VideoPipe/build/rk_videopipe_opt \
    -f /mnt/nfs/datasets/video/uav.mp4 -m 0 -fps

# 使用不同的视频文件
/home/orangepi/work/RK_VideoPipe/build/rk_videopipe_opt \
    -f /path/to/video.mp4 -m 1 -fps
```

## ⚡ 性能优化详情

已实施的优化：
1. **移除 MPP 解码器 FPS 限制** - 允许全速解码
2. **移除主循环延迟** - 消除不必要的等待
3. **NV12 格式直通** - 避免 YUV↔RGB 转换
4. **SDL2 硬件渲染** - GPU 加速显示

## 📈 FPS 历史

- **优化前**: 50 FPS（受视频源限制）
- **优化后**: 129 FPS（全速处理）
- **目标**: 160+ FPS（MPP 示例基准）

## 🎯 界面预览

```
┌─────────────────┐  ┌──────────────────────┐
│ Pipeline View  │  │   Video Display        │
│                 │  │                      │
│ [src_0] ───────  │  │ ┌──────────────────┐ │
│                 │  │ │ │  Video Frame   │ │ │
│ [des_0]         │  │ │ │  [129 FPS]     │ │
│                 │  │ └──────────────────┘ │
│ Active nodes    │  │                      │
└─────────────────┘  └──────────────────────┘
```

## 💡 提示

- 数据流窗口每秒刷新一次显示流水线状态
- FPS 是实时计算的（基于实际渲染帧率）
- **程序会在视频播放完成后自动退出**
- 按 Ctrl+C 可以随时退出程序
- 按 ESC 键可以随时退出程序
