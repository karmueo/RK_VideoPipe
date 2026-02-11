#include "vp_mpp_sdl_src_node.h"

#include <chrono>
#include <cstring>
#include <thread>
#include <unistd.h>

#include <opencv2/imgproc.hpp>

#include "vp_utils/logger/vp_logger.h"
#include "vp_utils/vp_utils.h"

namespace vp_nodes {

namespace {
// MPP 输入分片大小，避免超大包一次性提交造成阻塞。
constexpr size_t k_input_chunk_size = 4096;

/**
 * @brief 将 FFmpeg 错误码转换为可读字符串。
 * @param err FFmpeg 错误码。
 * @return std::string 可读错误文本。
 */
std::string ff_err_to_string(int err) {
    // FFmpeg 错误缓冲。
    char err_buf[128] = {0};
    av_strerror(err, err_buf, sizeof(err_buf));
    return std::string(err_buf);
}
} // namespace

uint64_t vp_mpp_sdl_src_node::now_us() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool vp_mpp_sdl_src_node::map_dec_codec(AVCodecID codec_id, MppCodingType& out_coding, const char*& out_bsf_name) {
    switch (codec_id) {
    case AV_CODEC_ID_H264:
        out_coding = MPP_VIDEO_CodingAVC;
        out_bsf_name = "h264_mp4toannexb";
        return true;
    case AV_CODEC_ID_HEVC:
        out_coding = MPP_VIDEO_CodingHEVC;
        out_bsf_name = "hevc_mp4toannexb";
        return true;
    default:
        return false;
    }
}

vp_mpp_sdl_src_node::vp_mpp_sdl_src_node(std::string node_name,
                                         int channel_index,
                                         std::string file_path,
                                         bool cycle,
                                         bool pace_by_src_fps,
                                         bool enable_vsync,
                                         bool disable_overlay,
                                         bool fullscreen,
                                         bool render_to_screen,
                                         bool publish_frame_meta,
                                         std::string sdl_video_driver,
                                         std::string sdl_render_driver)
    : vp_src_node(node_name, channel_index, 1.0f),
      file_path(std::move(file_path)),
      cycle(cycle),
      pace_by_src_fps(pace_by_src_fps),
      enable_vsync(enable_vsync),
      disable_overlay(disable_overlay),
      fullscreen(fullscreen),
      render_to_screen(render_to_screen),
      publish_frame_meta(publish_frame_meta),
      sdl_video_driver(std::move(sdl_video_driver)),
      sdl_render_driver(std::move(sdl_render_driver)) {
    VP_INFO(vp_utils::string_format("[%s] file=%s cycle=%d pace=%d vsync=%d fullscreen=%d render=%d publish_meta=%d",
                                    this->node_name.c_str(),
                                    this->file_path.c_str(),
                                    this->cycle ? 1 : 0,
                                    this->pace_by_src_fps ? 1 : 0,
                                    this->enable_vsync ? 1 : 0,
                                    this->fullscreen ? 1 : 0,
                                    this->render_to_screen ? 1 : 0,
                                    this->publish_frame_meta ? 1 : 0));
    this->initialized();
}

vp_mpp_sdl_src_node::~vp_mpp_sdl_src_node() {
    deinitialized();
}

bool vp_mpp_sdl_src_node::init_demux() {
    // 输入打开返回值。
    int ret = avformat_open_input(&ifmt, file_path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        VP_ERROR(vp_utils::string_format("[%s] avformat_open_input failed: %s",
                                         node_name.c_str(), ff_err_to_string(ret).c_str()));
        return false;
    }

    // 读取流信息返回值。
    ret = avformat_find_stream_info(ifmt, nullptr);
    if (ret < 0) {
        VP_ERROR(vp_utils::string_format("[%s] avformat_find_stream_info failed: %s",
                                         node_name.c_str(), ff_err_to_string(ret).c_str()));
        return false;
    }

    // 视频流索引。
    video_index = av_find_best_stream(ifmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_index < 0) {
        VP_ERROR(vp_utils::string_format("[%s] no video stream found in %s", node_name.c_str(), file_path.c_str()));
        return false;
    }

    // 视频流指针。
    AVStream* video_stream = ifmt->streams[video_index];
    // 首选平均帧率。
    AVRational frame_rate = video_stream->avg_frame_rate;
    if (frame_rate.num <= 0 || frame_rate.den <= 0) {
        frame_rate = video_stream->r_frame_rate;
    }

    if (frame_rate.num > 0 && frame_rate.den > 0) {
        // 源视频 FPS。
        const double fps = av_q2d(frame_rate);
        if (fps > 1.0 && fps < 240.0) {
            frame_interval_us = static_cast<uint64_t>(1000000.0 / fps);
            original_fps = static_cast<int>(fps);
        }
    }
    if (frame_interval_us == 0) {
        frame_interval_us = 20000;  // 默认 50 FPS 节奏。
    }
    if (original_fps <= 0) {
        original_fps = 50;
    }

    original_width = video_stream->codecpar->width;
    original_height = video_stream->codecpar->height;

    // bsf 名称。
    const char* bsf_name = nullptr;
    if (!map_dec_codec(video_stream->codecpar->codec_id, coding, bsf_name)) {
        VP_ERROR(vp_utils::string_format("[%s] unsupported codec, only H264/H265 are supported", node_name.c_str()));
        return false;
    }

    // bsf 描述符。
    const AVBitStreamFilter* bsf = av_bsf_get_by_name(bsf_name);
    if (!bsf) {
        VP_ERROR(vp_utils::string_format("[%s] av_bsf_get_by_name failed: %s", node_name.c_str(), bsf_name));
        return false;
    }

    ret = av_bsf_alloc(bsf, &ibsfc);
    if (ret < 0) {
        VP_ERROR(vp_utils::string_format("[%s] av_bsf_alloc failed: %s",
                                         node_name.c_str(), ff_err_to_string(ret).c_str()));
        return false;
    }

    ret = avcodec_parameters_copy(ibsfc->par_in, video_stream->codecpar);
    if (ret < 0) {
        VP_ERROR(vp_utils::string_format("[%s] avcodec_parameters_copy failed: %s",
                                         node_name.c_str(), ff_err_to_string(ret).c_str()));
        return false;
    }

    ibsfc->time_base_in = video_stream->time_base;
    ret = av_bsf_init(ibsfc);
    if (ret < 0) {
        VP_ERROR(vp_utils::string_format("[%s] av_bsf_init failed: %s",
                                         node_name.c_str(), ff_err_to_string(ret).c_str()));
        return false;
    }

    VP_INFO(vp_utils::string_format("[%s] demux ready w=%d h=%d fps=%d codec=%d",
                                    node_name.c_str(), original_width, original_height, original_fps, coding));
    return true;
}

bool vp_mpp_sdl_src_node::init_decoder() {
    // MPP 返回值。
    MPP_RET ret = mpp_create(&dec_ctx, &dec_mpi);
    if (ret) {
        VP_ERROR(vp_utils::string_format("[%s] mpp_create failed: %d", node_name.c_str(), ret));
        return false;
    }

    ret = mpp_init(dec_ctx, MPP_CTX_DEC, coding);
    if (ret) {
        VP_ERROR(vp_utils::string_format("[%s] mpp_init failed: %d", node_name.c_str(), ret));
        return false;
    }

    // 解码配置对象。
    MppDecCfg cfg = nullptr;
    ret = mpp_dec_cfg_init(&cfg);
    if (ret) {
        VP_ERROR(vp_utils::string_format("[%s] mpp_dec_cfg_init failed: %d", node_name.c_str(), ret));
        return false;
    }

    ret = dec_mpi->control(dec_ctx, MPP_DEC_GET_CFG, cfg);
    if (ret) {
        mpp_dec_cfg_deinit(cfg);
        VP_ERROR(vp_utils::string_format("[%s] MPP_DEC_GET_CFG failed: %d", node_name.c_str(), ret));
        return false;
    }

    ret = mpp_dec_cfg_set_u32(cfg, "base:split_parse", 1);
    if (ret) {
        mpp_dec_cfg_deinit(cfg);
        VP_ERROR(vp_utils::string_format("[%s] mpp_dec_cfg_set_u32 split_parse failed: %d", node_name.c_str(), ret));
        return false;
    }

    ret = dec_mpi->control(dec_ctx, MPP_DEC_SET_CFG, cfg);
    mpp_dec_cfg_deinit(cfg);
    if (ret) {
        VP_ERROR(vp_utils::string_format("[%s] MPP_DEC_SET_CFG failed: %d", node_name.c_str(), ret));
        return false;
    }

    // 强制 NV12 输出格式。
    MppFrameFormat output_format = MPP_FMT_YUV420SP;
    ret = dec_mpi->control(dec_ctx, MPP_DEC_SET_OUTPUT_FORMAT, &output_format);
    if (ret) {
        VP_WARN(vp_utils::string_format("[%s] MPP_DEC_SET_OUTPUT_FORMAT failed: %d", node_name.c_str(), ret));
    }

    ret = mpp_packet_init(&dec_pkt, nullptr, 0);
    if (ret) {
        VP_ERROR(vp_utils::string_format("[%s] mpp_packet_init failed: %d", node_name.c_str(), ret));
        return false;
    }

    return true;
}

bool vp_mpp_sdl_src_node::init_sdl_for_frame(MppFrame frame) {
    width = static_cast<int>(mpp_frame_get_width(frame));
    height = static_cast<int>(mpp_frame_get_height(frame));
    stride_h = static_cast<int>(mpp_frame_get_hor_stride(frame));
    stride_v = static_cast<int>(mpp_frame_get_ver_stride(frame));

    if (!sdl_video_driver.empty()) {
        // 设置 SDL 视频驱动返回值。
        const int set_env_ret = setenv("SDL_VIDEODRIVER", sdl_video_driver.c_str(), 1);
        if (set_env_ret != 0) {
            VP_WARN(vp_utils::string_format("[%s] set SDL_VIDEODRIVER failed", node_name.c_str()));
        }
    }
    if (!sdl_render_driver.empty()) {
        // 对齐 mpp 示例：通过环境变量指定渲染驱动。
        const int set_hint_env_ret = setenv("SDL_HINT_RENDER_DRIVER", sdl_render_driver.c_str(), 1);
        if (set_hint_env_ret != 0) {
            VP_WARN(vp_utils::string_format("[%s] set SDL_HINT_RENDER_DRIVER failed", node_name.c_str()));
        }
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, enable_vsync ? "1" : "0");
#ifdef SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "1");
#endif
#ifdef SDL_HINT_VIDEO_X11_FORCE_EGL
    SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "1");
#endif

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        VP_ERROR(vp_utils::string_format("[%s] SDL_Init failed: %s", node_name.c_str(), SDL_GetError()));
        return false;
    }

    // 窗口标志。
    Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (fullscreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    sdl_window = SDL_CreateWindow(
        "RK_VideoPipe MPP SDL Player",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        window_flags);
    if (!sdl_window) {
        VP_ERROR(vp_utils::string_format("[%s] SDL_CreateWindow failed: %s", node_name.c_str(), SDL_GetError()));
        return false;
    }

    // 渲染器标志。
    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
    if (enable_vsync) {
        renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
    }
    // 指定渲染器索引，默认自动。
    int render_index = -1;
    if (!sdl_render_driver.empty()) {
        const int driver_count = SDL_GetNumRenderDrivers();
        for (int i = 0; i < driver_count; ++i) {
            // 渲染器信息。
            SDL_RendererInfo renderer_info;
            if (SDL_GetRenderDriverInfo(i, &renderer_info) == 0 &&
                renderer_info.name != nullptr &&
                !strcmp(renderer_info.name, sdl_render_driver.c_str())) {
                render_index = i;
                break;
            }
        }
        if (render_index < 0) {
            VP_WARN(vp_utils::string_format("[%s] render driver %s not found, fallback auto",
                                            node_name.c_str(), sdl_render_driver.c_str()));
        }
    }
    sdl_renderer = SDL_CreateRenderer(sdl_window, render_index, renderer_flags);
    if (!sdl_renderer) {
        VP_WARN(vp_utils::string_format("[%s] SDL accelerated renderer failed: %s, fallback",
                                        node_name.c_str(), SDL_GetError()));
        sdl_renderer = SDL_CreateRenderer(sdl_window, -1, enable_vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
        if (!sdl_renderer) {
            VP_ERROR(vp_utils::string_format("[%s] SDL_CreateRenderer failed: %s", node_name.c_str(), SDL_GetError()));
            return false;
        }
    }

    // 当前渲染器信息。
    SDL_RendererInfo active_renderer_info;
    if (SDL_GetRendererInfo(sdl_renderer, &active_renderer_info) == 0) {
        VP_INFO(vp_utils::string_format("[%s] SDL video_driver=%s render_driver=%s flags=0x%x",
                                        node_name.c_str(),
                                        SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "unknown",
                                        active_renderer_info.name ? active_renderer_info.name : "unknown",
                                        active_renderer_info.flags));
    }

    sdl_texture = SDL_CreateTexture(sdl_renderer,
                                    SDL_PIXELFORMAT_NV12,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    width,
                                    height);
    if (!sdl_texture) {
        VP_ERROR(vp_utils::string_format("[%s] SDL_CreateTexture NV12 failed: %s", node_name.c_str(), SDL_GetError()));
        return false;
    }

    sdl_inited = true;
    VP_INFO(vp_utils::string_format("[%s] sdl ready w=%d h=%d stride=%d:%d",
                                    node_name.c_str(), width, height, stride_h, stride_v));
    return true;
}

void vp_mpp_sdl_src_node::pump_sdl_events() {
    // SDL 事件对象。
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            quit = true;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            quit = true;
        }
    }
}

void vp_mpp_sdl_src_node::log_runtime_fps() {
    if (fps_start_us == 0 || dec_frames == 0) {
        return;
    }

    // 当前时间。
    const uint64_t now = now_us();
    if (fps_last_log_us == 0) {
        fps_last_log_us = now;
        fps_last_log_frames = dec_frames;
        return;
    }

    // 距上次日志时间间隔。
    const uint64_t delta_us = now - fps_last_log_us;
    if (delta_us < 1000000) {
        return;
    }

    // 距上次日志新增帧数。
    const uint32_t delta_frames = dec_frames - fps_last_log_frames;
    // 当前 FPS。
    const double cur_fps = delta_us ? (static_cast<double>(delta_frames) * 1000000.0 / static_cast<double>(delta_us)) : 0.0;
    // 总运行时长。
    const uint64_t total_us = now - fps_start_us;
    // 平均 FPS。
    const double avg_fps = total_us ? (static_cast<double>(dec_frames) * 1000000.0 / static_cast<double>(total_us)) : 0.0;

    VP_INFO(vp_utils::string_format("[%s] current_fps=%.2f avg_fps=%.2f frames=%u",
                                    node_name.c_str(), cur_fps, avg_fps, dec_frames));

    fps_last_log_us = now;
    fps_last_log_frames = dec_frames;
}

bool vp_mpp_sdl_src_node::render_frame_nv12(MppFrame frame) {
    // MPP buffer。
    MppBuffer buffer = mpp_frame_get_buffer(frame);
    if (!buffer) {
        VP_WARN(vp_utils::string_format("[%s] frame has no buffer", node_name.c_str()));
        return false;
    }

    // NV12 基地址。
    auto* base = static_cast<RK_U8*>(mpp_buffer_get_ptr(buffer));
    if (!base) {
        VP_WARN(vp_utils::string_format("[%s] mpp buffer ptr is null", node_name.c_str()));
        return false;
    }

    // Y 平面指针。
    const RK_U8* y_plane = base;
    // UV 平面指针。
    const RK_U8* uv_plane = base + static_cast<size_t>(stride_h) * static_cast<size_t>(stride_v);

    // SDL lock 输出像素首地址。
    void* pixels = nullptr;
    // SDL 输出步长。
    int pitch = 0;
    if (SDL_LockTexture(sdl_texture, nullptr, &pixels, &pitch) != 0) {
        VP_WARN(vp_utils::string_format("[%s] SDL_LockTexture failed: %s", node_name.c_str(), SDL_GetError()));
        return false;
    }

    if (!pixels || pitch <= 0) {
        SDL_UnlockTexture(sdl_texture);
        return false;
    }

    // SDL Y 平面目的地址。
    auto* dst_y = static_cast<uint8_t*>(pixels);
    // SDL UV 平面目的地址。
    auto* dst_uv = dst_y + static_cast<size_t>(pitch) * static_cast<size_t>(height);

    for (int row = 0; row < height; ++row) {
        memcpy(dst_y + static_cast<size_t>(row) * static_cast<size_t>(pitch),
               y_plane + static_cast<size_t>(row) * static_cast<size_t>(stride_h),
               static_cast<size_t>(width));
    }

    for (int row = 0; row < height / 2; ++row) {
        memcpy(dst_uv + static_cast<size_t>(row) * static_cast<size_t>(pitch),
               uv_plane + static_cast<size_t>(row) * static_cast<size_t>(stride_h),
               static_cast<size_t>(width));
    }

    SDL_UnlockTexture(sdl_texture);

    if (SDL_RenderCopy(sdl_renderer, sdl_texture, nullptr, nullptr) != 0) {
        VP_WARN(vp_utils::string_format("[%s] SDL_RenderCopy failed: %s", node_name.c_str(), SDL_GetError()));
        return false;
    }

    SDL_RenderPresent(sdl_renderer);
    return true;
}

bool vp_mpp_sdl_src_node::setup_info_change(MppFrame frame) {
    if (!dec_frm_grp) {
        // 创建内部 buffer group 返回值。
        const MPP_RET group_ret = mpp_buffer_group_get_internal(&dec_frm_grp, MPP_BUFFER_TYPE_ION);
        if (group_ret) {
            VP_ERROR(vp_utils::string_format("[%s] mpp_buffer_group_get_internal failed: %d", node_name.c_str(), group_ret));
            return false;
        }
    }

    // 单帧缓冲需求大小。
    const RK_U32 buf_size = mpp_frame_get_buf_size(frame);
    if (mpp_buffer_group_limit_config(dec_frm_grp, buf_size, 24)) {
        VP_ERROR(vp_utils::string_format("[%s] mpp_buffer_group_limit_config failed", node_name.c_str()));
        return false;
    }
    if (dec_mpi->control(dec_ctx, MPP_DEC_SET_EXT_BUF_GROUP, dec_frm_grp)) {
        VP_ERROR(vp_utils::string_format("[%s] MPP_DEC_SET_EXT_BUF_GROUP failed", node_name.c_str()));
        return false;
    }
    if (dec_mpi->control(dec_ctx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr)) {
        VP_ERROR(vp_utils::string_format("[%s] MPP_DEC_SET_INFO_CHANGE_READY failed", node_name.c_str()));
        return false;
    }

    if (render_to_screen) {
        if (!sdl_inited && !init_sdl_for_frame(frame)) {
            return false;
        }
    }

    return true;
}

void vp_mpp_sdl_src_node::publish_frame_meta_if_needed(MppFrame frame) {
    if (!publish_frame_meta) {
        return;
    }

    // MPP buffer。
    MppBuffer buffer = mpp_frame_get_buffer(frame);
    if (!buffer) {
        return;
    }

    // NV12 基地址。
    auto* base = static_cast<RK_U8*>(mpp_buffer_get_ptr(buffer));
    if (!base) {
        return;
    }

    // 当前帧宽度。
    const int frame_width = static_cast<int>(mpp_frame_get_width(frame));
    // 当前帧高度。
    const int frame_height = static_cast<int>(mpp_frame_get_height(frame));
    // 当前水平 stride。
    const int frame_stride_h = static_cast<int>(mpp_frame_get_hor_stride(frame));
    // 当前垂直 stride。
    const int frame_stride_v = static_cast<int>(mpp_frame_get_ver_stride(frame));

    if (frame_width <= 0 || frame_height <= 0 || frame_stride_h < frame_width || frame_stride_v < frame_height) {
        return;
    }

    // NV12 原始矩阵（含 stride 区域）。
    cv::Mat nv12_mat(frame_stride_v * 3 / 2, frame_stride_h, CV_8UC1, base);
    // BGR 全尺寸图像（宽度为 stride）。
    cv::Mat bgr_full;
    cv::cvtColor(nv12_mat, bgr_full, cv::COLOR_YUV2BGR_NV12);

    if (bgr_full.cols < frame_width || bgr_full.rows < frame_height) {
        return;
    }

    // 裁剪有效显示区域。
    cv::Mat bgr_roi = bgr_full(cv::Rect(0, 0, frame_width, frame_height));

    // 输出帧（深拷贝，避免底层内存复用导致野指针）。
    cv::Mat output_frame = bgr_roi.clone();
    if (output_frame.empty()) {
        return;
    }

    this->frame_index++;
    // 下游输出 meta。
    auto out_meta = std::make_shared<vp_objects::vp_frame_meta>(
        output_frame,
        this->frame_index,
        this->channel_index,
        frame_width,
        frame_height,
        this->original_fps);

    this->out_queue.push(out_meta);
    if (this->meta_handled_hooker) {
        meta_handled_hooker(node_name, out_queue.size(), out_meta);
    }
    this->out_queue_semaphore.signal();
}

bool vp_mpp_sdl_src_node::process_decoded_frame(MppFrame frame, bool& got_eos) {
    if (mpp_frame_get_info_change(frame)) {
        return setup_info_change(frame);
    }

    // 错误标志。
    const RK_U32 err_info = mpp_frame_get_errinfo(frame);
    // 丢弃标志。
    const RK_U32 discard = mpp_frame_get_discard(frame);
    if (!err_info && !discard) {
        if (fps_start_us == 0) {
            fps_start_us = now_us();
            play_start_us = fps_start_us;
        }

        if (pace_by_src_fps && play_start_us && frame_interval_us) {
            // 当前时间。
            const uint64_t now = now_us();
            // 当前帧目标展示时间。
            const uint64_t target = play_start_us + static_cast<uint64_t>(shown_frames) * frame_interval_us;
            if (target > now) {
                // 需要等待的微秒数。
                const uint64_t wait_us = target - now;
                if (wait_us > 200) {
                    usleep(static_cast<useconds_t>(wait_us));
                }
            }
        }

        dec_frames++;
        log_runtime_fps();

        if (render_to_screen && sdl_inited && !render_frame_nv12(frame)) {
            return false;
        }
        shown_frames++;

        publish_frame_meta_if_needed(frame);
    }

    if (mpp_frame_get_eos(frame)) {
        got_eos = true;
    }

    return true;
}

bool vp_mpp_sdl_src_node::poll_decoder_frames(bool& got_eos) {
    // 超时重试计数。
    int timeout_retry = 20;
    while (alive && !quit) {
        // 取出的解码帧。
        MppFrame frame = nullptr;
        // MPP 返回值。
        const MPP_RET ret = dec_mpi->decode_get_frame(dec_ctx, &frame);

        if (ret == MPP_ERR_TIMEOUT) {
            if (timeout_retry-- > 0) {
                usleep(1000);
                continue;
            }
            return true;
        }
        if (ret != MPP_OK) {
            VP_ERROR(vp_utils::string_format("[%s] decode_get_frame failed: %d", node_name.c_str(), ret));
            return false;
        }
        if (!frame) {
            return true;
        }

        // 当前帧处理结果。
        const bool ok = process_decoded_frame(frame, got_eos);
        mpp_frame_deinit(&frame);
        pump_sdl_events();
        if (!ok) {
            return false;
        }
    }

    return true;
}

bool vp_mpp_sdl_src_node::put_dec_packet_retry(bool& got_eos) {
    // 提交重试次数。
    int retry = 2000;
    while (retry-- > 0 && alive && !quit) {
        // 提交 packet 返回值。
        const MPP_RET ret = dec_mpi->decode_put_packet(dec_ctx, dec_pkt);
        if (ret == MPP_OK) {
            return true;
        }
        if (!poll_decoder_frames(got_eos)) {
            return false;
        }
        usleep(1000);
    }

    VP_ERROR(vp_utils::string_format("[%s] decode_put_packet timeout", node_name.c_str()));
    return false;
}

bool vp_mpp_sdl_src_node::send_to_decoder(const AVPacket* packet, bool eos, bool& got_eos) {
    // 输入数据首地址。
    RK_U8* data = packet ? packet->data : nullptr;
    // 输入总长度。
    size_t total_size = packet ? static_cast<size_t>(packet->size) : 0;
    // 当前偏移。
    size_t offset = 0;

    if (total_size == 0) {
        mpp_packet_set_data(dec_pkt, nullptr);
        mpp_packet_set_pos(dec_pkt, nullptr);
        mpp_packet_set_size(dec_pkt, 0);
        mpp_packet_set_length(dec_pkt, 0);
        if (eos) {
            mpp_packet_set_eos(dec_pkt);
        } else {
            mpp_packet_clr_eos(dec_pkt);
        }
        return put_dec_packet_retry(got_eos);
    }

    while (offset < total_size && alive && !quit) {
        // 本次提交分片长度。
        size_t chunk = total_size - offset;
        if (chunk > k_input_chunk_size) {
            chunk = k_input_chunk_size;
        }

        mpp_packet_set_data(dec_pkt, data + offset);
        mpp_packet_set_pos(dec_pkt, data + offset);
        mpp_packet_set_size(dec_pkt, chunk);
        mpp_packet_set_length(dec_pkt, chunk);
        if (eos && offset + chunk == total_size) {
            mpp_packet_set_eos(dec_pkt);
        } else {
            mpp_packet_clr_eos(dec_pkt);
        }

        while (mpp_packet_get_length(dec_pkt) > 0 && alive && !quit) {
            if (!put_dec_packet_retry(got_eos)) {
                return false;
            }
        }

        offset += chunk;
    }

    return true;
}

bool vp_mpp_sdl_src_node::run_pipeline_once() {
    // 输入 packet。
    AVPacket* input_packet = av_packet_alloc();
    // 过滤后 packet。
    AVPacket* filtered_packet = av_packet_alloc();
    // 是否拿到 eos。
    bool got_eos = false;

    if (!input_packet || !filtered_packet) {
        if (input_packet) {
            av_packet_free(&input_packet);
        }
        if (filtered_packet) {
            av_packet_free(&filtered_packet);
        }
        VP_ERROR(vp_utils::string_format("[%s] av_packet_alloc failed", node_name.c_str()));
        return false;
    }

    while (alive && !quit && av_read_frame(ifmt, input_packet) >= 0) {
        if (input_packet->stream_index == video_index) {
            // bsf 输入返回值。
            int ret = av_bsf_send_packet(ibsfc, input_packet);
            if (ret < 0) {
                VP_ERROR(vp_utils::string_format("[%s] av_bsf_send_packet failed: %s",
                                                 node_name.c_str(), ff_err_to_string(ret).c_str()));
                av_packet_unref(input_packet);
                av_packet_free(&input_packet);
                av_packet_free(&filtered_packet);
                return false;
            }

            // bsf 输出返回值（初始化为 EAGAIN，避免短路时误判）。
            int receive_ret = AVERROR(EAGAIN);
            while (alive && !quit && (receive_ret = av_bsf_receive_packet(ibsfc, filtered_packet)) >= 0) {
                if (!send_to_decoder(filtered_packet, false, got_eos)) {
                    av_packet_unref(filtered_packet);
                    av_packet_unref(input_packet);
                    av_packet_free(&input_packet);
                    av_packet_free(&filtered_packet);
                    return false;
                }
                if (!poll_decoder_frames(got_eos)) {
                    av_packet_unref(filtered_packet);
                    av_packet_unref(input_packet);
                    av_packet_free(&input_packet);
                    av_packet_free(&filtered_packet);
                    return false;
                }
                av_packet_unref(filtered_packet);
            }

            if (!quit && receive_ret != AVERROR(EAGAIN) && receive_ret != AVERROR_EOF) {
                VP_ERROR(vp_utils::string_format("[%s] av_bsf_receive_packet failed: %s",
                                                 node_name.c_str(), ff_err_to_string(receive_ret).c_str()));
                av_packet_unref(input_packet);
                av_packet_free(&input_packet);
                av_packet_free(&filtered_packet);
                return false;
            }
        }

        av_packet_unref(input_packet);
        pump_sdl_events();
    }

    if (alive && !quit) {
        if (av_bsf_send_packet(ibsfc, nullptr) < 0) {
            av_packet_free(&input_packet);
            av_packet_free(&filtered_packet);
            return false;
        }

        while (alive && !quit) {
            // bsf 输出返回值。
            const int ret = av_bsf_receive_packet(ibsfc, filtered_packet);
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                break;
            }
            if (ret < 0) {
                VP_ERROR(vp_utils::string_format("[%s] av_bsf_receive_packet(flush) failed: %s",
                                                 node_name.c_str(), ff_err_to_string(ret).c_str()));
                av_packet_free(&input_packet);
                av_packet_free(&filtered_packet);
                return false;
            }

            if (!send_to_decoder(filtered_packet, false, got_eos) || !poll_decoder_frames(got_eos)) {
                av_packet_unref(filtered_packet);
                av_packet_free(&input_packet);
                av_packet_free(&filtered_packet);
                return false;
            }
            av_packet_unref(filtered_packet);
        }

        if (!send_to_decoder(nullptr, true, got_eos)) {
            av_packet_free(&input_packet);
            av_packet_free(&filtered_packet);
            return false;
        }

        for (int i = 0; i < 3000 && !got_eos && alive && !quit; ++i) {
            if (!poll_decoder_frames(got_eos)) {
                av_packet_free(&input_packet);
                av_packet_free(&filtered_packet);
                return false;
            }
            usleep(1000);
            pump_sdl_events();
        }
    }

    av_packet_free(&input_packet);
    av_packet_free(&filtered_packet);
    return true;
}

void vp_mpp_sdl_src_node::cleanup() {
    if (sdl_texture) {
        SDL_DestroyTexture(sdl_texture);
        sdl_texture = nullptr;
    }
    if (sdl_renderer) {
        SDL_DestroyRenderer(sdl_renderer);
        sdl_renderer = nullptr;
    }
    if (sdl_window) {
        SDL_DestroyWindow(sdl_window);
        sdl_window = nullptr;
    }
    if (sdl_inited) {
        SDL_Quit();
        sdl_inited = false;
    }

    if (dec_frm_grp) {
        mpp_buffer_group_put(dec_frm_grp);
        dec_frm_grp = nullptr;
    }
    if (dec_pkt) {
        mpp_packet_deinit(&dec_pkt);
        dec_pkt = nullptr;
    }
    if (dec_ctx) {
        mpp_destroy(dec_ctx);
        dec_ctx = nullptr;
        dec_mpi = nullptr;
    }

    if (ibsfc) {
        av_bsf_free(&ibsfc);
        ibsfc = nullptr;
    }
    if (ifmt) {
        avformat_close_input(&ifmt);
        ifmt = nullptr;
    }

    width = 0;
    height = 0;
    stride_h = 0;
    stride_v = 0;
}

void vp_mpp_sdl_src_node::handle_run() {
    while (alive) {
        gate.knock();
        if (!alive) {
            break;
        }

        quit = false;
        dec_frames = 0;
        shown_frames = 0;
        fps_start_us = 0;
        fps_last_log_us = 0;
        fps_last_log_frames = 0;
        play_start_us = 0;

        if (!init_demux() || !init_decoder()) {
            cleanup();
            VP_ERROR(vp_utils::string_format("[%s] init failed, retry in 1s", node_name.c_str()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        vp_stream_info stream_info{channel_index, original_fps, original_width, original_height, to_string()};
        invoke_stream_info_hooker(node_name, stream_info);

        const bool ok = run_pipeline_once();
        const uint64_t elapsed_us = (fps_start_us && dec_frames > 0) ? (now_us() - fps_start_us) : 0;
        const double avg_fps = elapsed_us ? (static_cast<double>(dec_frames) * 1000000.0 / static_cast<double>(elapsed_us)) : 0.0;
        VP_INFO(vp_utils::string_format("[%s] run done ok=%d frames=%u avg_fps=%.2f",
                                        node_name.c_str(), ok ? 1 : 0, dec_frames, avg_fps));

        cleanup();

        if (!alive || quit) {
            break;
        }
        if (!cycle) {
            break;
        }
    }

    this->out_queue.push(nullptr);
    this->out_queue_semaphore.signal();
}

std::string vp_mpp_sdl_src_node::to_string() {
    return file_path;
}

} // namespace vp_nodes
