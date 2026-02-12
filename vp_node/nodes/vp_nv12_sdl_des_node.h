#pragma once

#include <SDL2/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include "base/vp_des_node.h"

namespace vp_nodes {

/**
 * @brief 查询 NV12 SDL 终端节点是否请求退出（ESC/关闭窗口）。
 * @return true 请求退出；false 未请求。
 */
bool vp_nv12_sdl_des_should_exit();

/**
 * @brief 重置 NV12 SDL 终端节点退出请求标志。
 */
void vp_nv12_sdl_des_reset_exit_flag();

/**
 * @brief 基于 SDL2 的 NV12 显示终端节点。
 */
class vp_nv12_sdl_des_node : public vp_des_node {
private:
    // SDL 视频驱动名称，空字符串表示自动。
    std::string sdl_video_driver;
    // SDL 渲染驱动名称，空字符串表示自动。
    std::string sdl_render_driver;
    // 是否全屏显示。
    bool fullscreen = false;

    // SDL 窗口句柄。
    SDL_Window* sdl_window = nullptr;
    // SDL 渲染器句柄。
    SDL_Renderer* sdl_renderer = nullptr;
    // SDL NV12 纹理句柄。
    SDL_Texture* sdl_texture = nullptr;
    // SDL 是否已初始化。
    bool sdl_inited = false;

    // 当前纹理宽度。
    int texture_width = 0;
    // 当前纹理高度。
    int texture_height = 0;
    // 显示节点输入队列上限，超出后丢弃新帧，避免显示端背压。
    size_t max_in_queue_size = 4;
    // 累计丢帧计数。
    uint64_t dropped_frames = 0;
    // 上次打印丢帧计数快照。
    uint64_t dropped_frames_last_log = 0;
    // 上次打印丢帧日志时间点。
    std::chrono::steady_clock::time_point dropped_log_tp;

private:
    /**
     * @brief 初始化 SDL 资源。
     *
     * @param frame_width 帧宽度。
     * @param frame_height 帧高度。
     * @return true 初始化成功；false 失败。
     */
    bool init_sdl(int frame_width, int frame_height);

    /**
     * @brief 按当前帧尺寸确保 SDL 纹理可用。
     *
     * @param frame_width 帧宽度。
     * @param frame_height 帧高度。
     * @return true 纹理可用；false 失败。
     */
    bool ensure_texture(int frame_width, int frame_height);

    /**
     * @brief 轮询 SDL 事件并更新退出标志。
     */
    void pump_sdl_events();

    /**
     * @brief 释放 SDL 资源。
     */
    void release_sdl();

protected:
    /**
     * @brief 接收上游元数据并在拥塞时执行丢帧保护。
     *
     * @param meta 上游传入的元数据。
     */
    virtual void meta_flow(std::shared_ptr<vp_objects::vp_meta> meta) override;

    /**
     * @brief 处理视频帧元数据并进行 NV12 直显。
     *
     * @param meta 输入帧元数据。
     * @return std::shared_ptr<vp_objects::vp_meta> 始终返回 nullptr。
     */
    virtual std::shared_ptr<vp_objects::vp_meta> handle_frame_meta(std::shared_ptr<vp_objects::vp_frame_meta> meta) override;

    /**
     * @brief 处理控制元数据。
     *
     * @param meta 控制元数据。
     * @return std::shared_ptr<vp_objects::vp_meta> 始终返回 nullptr。
     */
    virtual std::shared_ptr<vp_objects::vp_meta> handle_control_meta(std::shared_ptr<vp_objects::vp_control_meta> meta) override;

public:
    /**
     * @brief 构造 NV12 SDL 显示终端节点。
     *
     * @param node_name 节点名称。
     * @param channel_index 通道索引。
     * @param sdl_video_driver SDL 视频驱动名称（空表示自动）。
     * @param sdl_render_driver SDL 渲染驱动名称（空表示自动）。
     * @param fullscreen 是否全屏显示。
     */
    vp_nv12_sdl_des_node(std::string node_name,
                         int channel_index,
                         std::string sdl_video_driver = "",
                         std::string sdl_render_driver = "",
                         bool fullscreen = false);

    /**
     * @brief 析构并释放资源。
     */
    ~vp_nv12_sdl_des_node();
};

} // namespace vp_nodes
