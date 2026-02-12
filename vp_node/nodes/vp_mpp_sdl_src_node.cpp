#include "vp_mpp_sdl_src_node.h"

#include <chrono>
#include <cstring>
#include <thread>
#include <unistd.h>

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
                                         bool pace_by_src_fps)
    : vp_src_node(node_name, channel_index, 1.0f),
      file_path(std::move(file_path)),
      cycle(cycle),
      pace_by_src_fps(pace_by_src_fps) {
    VP_INFO(vp_utils::string_format("[%s] file=%s cycle=%d pace=%d decode_only=1 nv12_output=1",
                                    this->node_name.c_str(),
                                    this->file_path.c_str(),
                                    this->cycle ? 1 : 0,
                                    this->pace_by_src_fps ? 1 : 0));
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

bool vp_mpp_sdl_src_node::setup_info_change(MppFrame frame) {
    width = static_cast<int>(mpp_frame_get_width(frame));
    height = static_cast<int>(mpp_frame_get_height(frame));
    stride_h = static_cast<int>(mpp_frame_get_hor_stride(frame));
    stride_v = static_cast<int>(mpp_frame_get_ver_stride(frame));

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

    return true;
}

void vp_mpp_sdl_src_node::publish_nv12_frame_meta(MppFrame frame) {
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

    // NV12 输出帧（按有效宽高组织，不带 stride padding）。
    cv::Mat output_nv12(frame_height * 3 / 2, frame_width, CV_8UC1);
    if (output_nv12.empty()) {
        return;
    }
    // Y 平面指针。
    const RK_U8* y_plane = base;
    // UV 平面指针。
    const RK_U8* uv_plane = base + static_cast<size_t>(frame_stride_h) * static_cast<size_t>(frame_stride_v);
    // 目标 Y 平面指针。
    RK_U8* dst_y = output_nv12.ptr<RK_U8>(0);
    // 目标 UV 平面指针。
    RK_U8* dst_uv = output_nv12.ptr<RK_U8>(frame_height);

    for (int row = 0; row < frame_height; ++row) {
        memcpy(dst_y + static_cast<size_t>(row) * static_cast<size_t>(frame_width),
               y_plane + static_cast<size_t>(row) * static_cast<size_t>(frame_stride_h),
               static_cast<size_t>(frame_width));
    }
    for (int row = 0; row < frame_height / 2; ++row) {
        memcpy(dst_uv + static_cast<size_t>(row) * static_cast<size_t>(frame_width),
               uv_plane + static_cast<size_t>(row) * static_cast<size_t>(frame_stride_h),
               static_cast<size_t>(frame_width));
    }

    this->frame_index++;
    // 下游输出 meta。
    auto out_meta = std::make_shared<vp_objects::vp_frame_meta>(
        output_nv12,
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
        shown_frames++;
        publish_nv12_frame_meta(frame);
    }

    if (mpp_frame_get_eos(frame)) {
        got_eos = true;
    }

    return true;
}

bool vp_mpp_sdl_src_node::poll_decoder_frames(bool& got_eos) {
    // 超时重试计数。
    int timeout_retry = 20;
    while (alive) {
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
        if (!ok) {
            return false;
        }
    }

    return true;
}

bool vp_mpp_sdl_src_node::put_dec_packet_retry(bool& got_eos) {
    // 提交重试次数。
    int retry = 2000;
    while (retry-- > 0 && alive) {
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

    // 主动退出时不视为错误，避免 Ctrl+C 产生误导性 timeout 日志。
    if (!alive) {
        return false;
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

    while (offset < total_size && alive) {
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

        while (mpp_packet_get_length(dec_pkt) > 0 && alive) {
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

    while (alive && av_read_frame(ifmt, input_packet) >= 0) {
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
            while (alive && (receive_ret = av_bsf_receive_packet(ibsfc, filtered_packet)) >= 0) {
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

            if (alive && receive_ret != AVERROR(EAGAIN) && receive_ret != AVERROR_EOF) {
                VP_ERROR(vp_utils::string_format("[%s] av_bsf_receive_packet failed: %s",
                                                 node_name.c_str(), ff_err_to_string(receive_ret).c_str()));
                av_packet_unref(input_packet);
                av_packet_free(&input_packet);
                av_packet_free(&filtered_packet);
                return false;
            }
        }

        av_packet_unref(input_packet);
    }

    if (alive) {
        if (av_bsf_send_packet(ibsfc, nullptr) < 0) {
            av_packet_free(&input_packet);
            av_packet_free(&filtered_packet);
            return false;
        }

        while (alive) {
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

        for (int i = 0; i < 3000 && !got_eos && alive; ++i) {
            if (!poll_decoder_frames(got_eos)) {
                av_packet_free(&input_packet);
                av_packet_free(&filtered_packet);
                return false;
            }
            usleep(1000);
        }
    }

    av_packet_free(&input_packet);
    av_packet_free(&filtered_packet);
    return true;
}

void vp_mpp_sdl_src_node::cleanup() {
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

        if (!alive) {
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
