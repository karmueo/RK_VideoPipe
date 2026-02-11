#pragma once

#include <string>

#include <SDL2/SDL.h>
#include <opencv2/core/core.hpp>

#include "base/vp_src_node.h"

extern "C" {
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
}

#include "rockchip/mpp_buffer.h"
#include "rockchip/rk_mpi.h"
#include "rockchip/rk_mpi_cmd.h"
#include "rockchip/rk_vdec_cfg.h"

namespace vp_nodes {

/**
 * @brief 基于 MPP + SDL 的本地文件源节点。
 *
 * 该节点直接执行 MP4 demux + MPP 硬解 + SDL 渲染，避免 OpenCV VideoCapture/VideoWriter
 * 桥接开销。可选将解码帧转换为 BGR 后下发 frame_meta。
 */
class vp_mpp_sdl_src_node : public vp_src_node {
private:
    // 输入文件路径。
    std::string file_path;
    // 是否循环播放。
    bool cycle = true;
    // 是否按源视频 FPS 节奏显示。
    bool pace_by_src_fps = false;
    // SDL 渲染是否启用 vsync。
    bool enable_vsync = false;
    // 是否禁用 FPS overlay。
    bool disable_overlay = true;
    // 是否全屏显示。
    bool fullscreen = false;
    // 是否渲染到屏幕。
    bool render_to_screen = true;
    // 是否向下游发布 frame_meta。
    bool publish_frame_meta = false;
    // SDL 视频驱动名称，空字符串表示自动选择。
    std::string sdl_video_driver;
    // SDL 渲染驱动名称，空字符串表示自动选择。
    std::string sdl_render_driver;

    // FFmpeg demux 上下文。
    AVFormatContext* ifmt = nullptr;
    // FFmpeg bsf 上下文。
    AVBSFContext* ibsfc = nullptr;
    // 视频流索引。
    int video_index = -1;
    // MPP 编码类型。
    MppCodingType coding = MPP_VIDEO_CodingUnused;

    // MPP 解码上下文。
    MppCtx dec_ctx = nullptr;
    // MPP API 句柄。
    MppApi* dec_mpi = nullptr;
    // MPP 复用 packet。
    MppPacket dec_pkt = nullptr;
    // MPP 输出帧缓冲组。
    MppBufferGroup dec_frm_grp = nullptr;

    // SDL 窗口句柄。
    SDL_Window* sdl_window = nullptr;
    // SDL 渲染器句柄。
    SDL_Renderer* sdl_renderer = nullptr;
    // SDL NV12 纹理句柄。
    SDL_Texture* sdl_texture = nullptr;
    // SDL 是否已初始化。
    bool sdl_inited = false;

    // 当前输出宽度。
    int width = 0;
    // 当前输出高度。
    int height = 0;
    // 水平 stride。
    int stride_h = 0;
    // 垂直 stride。
    int stride_v = 0;

    // 按源帧率节奏播放时的帧间隔(us)。
    uint64_t frame_interval_us = 0;
    // 播放开始时间(us)。
    uint64_t play_start_us = 0;
    // 已显示帧计数。
    uint32_t shown_frames = 0;

    // 解码帧计数。
    uint32_t dec_frames = 0;
    // FPS 统计起始时间(us)。
    uint64_t fps_start_us = 0;
    // 上次打印 FPS 的时间(us)。
    uint64_t fps_last_log_us = 0;
    // 上次打印 FPS 时帧计数。
    uint32_t fps_last_log_frames = 0;

    // UI 是否请求退出。
    bool quit = false;

private:
    /**
     * @brief 获取单调时钟微秒时间戳。
     * @return uint64_t 当前微秒时间。
     */
    static uint64_t now_us();

    /**
     * @brief 把 FFmpeg codec id 映射为 MPP codec 与 bsf 名称。
     * @param codec_id FFmpeg 编码类型。
     * @param out_coding 输出 MPP 编码类型。
     * @param out_bsf_name 输出 bsf 名称。
     * @return true 映射成功；false 不支持。
     */
    static bool map_dec_codec(AVCodecID codec_id, MppCodingType& out_coding, const char*& out_bsf_name);

    /**
     * @brief 初始化 demux 与 bsf。
     * @return true 初始化成功；false 失败。
     */
    bool init_demux();

    /**
     * @brief 初始化 MPP 解码器。
     * @return true 初始化成功；false 失败。
     */
    bool init_decoder();

    /**
     * @brief 基于首帧信息初始化 SDL 渲染资源。
     * @param frame MPP 输出帧。
     * @return true 初始化成功；false 失败。
     */
    bool init_sdl_for_frame(MppFrame frame);

    /**
     * @brief 处理 SDL 事件。
     */
    void pump_sdl_events();

    /**
     * @brief 打印运行时 FPS。
     */
    void log_runtime_fps();

    /**
     * @brief 把 NV12 帧渲染到 SDL 窗口。
     * @param frame MPP 输出帧。
     * @return true 渲染成功；false 失败。
     */
    bool render_frame_nv12(MppFrame frame);

    /**
     * @brief 在 info change 阶段配置解码缓冲并初始化 SDL。
     * @param frame MPP 输出帧。
     * @return true 配置成功；false 失败。
     */
    bool setup_info_change(MppFrame frame);

    /**
     * @brief 把 NV12 帧转换为 BGR 并可选缩放，输出 frame_meta。
     * @param frame MPP 输出帧。
     */
    void publish_frame_meta_if_needed(MppFrame frame);

    /**
     * @brief 处理单帧 decode 输出。
     * @param frame MPP 输出帧。
     * @param got_eos 是否收到 EOS。
     * @return true 处理成功；false 失败。
     */
    bool process_decoded_frame(MppFrame frame, bool& got_eos);

    /**
     * @brief 拉取并处理解码器输出帧。
     * @param got_eos 是否收到 EOS。
     * @return true 成功；false 失败。
     */
    bool poll_decoder_frames(bool& got_eos);

    /**
     * @brief 重试向解码器提交 packet。
     * @param got_eos 是否收到 EOS。
     * @return true 成功；false 失败。
     */
    bool put_dec_packet_retry(bool& got_eos);

    /**
     * @brief 把码流 packet 发送到解码器。
     * @param packet 输入 packet，可为空表示 flush。
     * @param eos 当前 packet 是否为 EOS。
     * @param got_eos 是否收到 EOS。
     * @return true 成功；false 失败。
     */
    bool send_to_decoder(const AVPacket* packet, bool eos, bool& got_eos);

    /**
     * @brief 执行一次完整 demux+decode+render 流程。
     * @return true 成功；false 失败。
     */
    bool run_pipeline_once();

    /**
     * @brief 释放 FFmpeg/MPP/SDL 资源。
     */
    void cleanup();

protected:
    /**
     * @brief 源节点主循环。
     */
    virtual void handle_run() override;

public:
    /**
     * @brief 构造 MPP+SDL 源节点。
     * @param node_name 节点名称。
     * @param channel_index 通道索引。
     * @param file_path 输入 MP4 路径。
     * @param cycle 是否循环播放。
     * @param pace_by_src_fps 是否按源 FPS 节奏播放。
     * @param enable_vsync SDL 是否启用 vsync。
     * @param disable_overlay 是否关闭 overlay。
     * @param fullscreen 是否全屏显示。
     * @param render_to_screen 是否渲染到屏幕。
     * @param publish_frame_meta 是否向下游发布 frame_meta。
     * @param sdl_video_driver SDL 视频驱动名称（空表示自动）。
     * @param sdl_render_driver SDL 渲染驱动名称（空表示自动）。
     */
    vp_mpp_sdl_src_node(std::string node_name,
                        int channel_index,
                        std::string file_path,
                        bool cycle = true,
                        bool pace_by_src_fps = false,
                        bool enable_vsync = false,
                        bool disable_overlay = true,
                        bool fullscreen = false,
                        bool render_to_screen = true,
                        bool publish_frame_meta = false,
                        std::string sdl_video_driver = "",
                        std::string sdl_render_driver = "");

    /**
     * @brief 析构并释放资源。
     */
    ~vp_mpp_sdl_src_node();

    /**
     * @brief 返回节点描述字符串。
     * @return std::string 输入文件路径。
     */
    virtual std::string to_string() override;
};

} // namespace vp_nodes
