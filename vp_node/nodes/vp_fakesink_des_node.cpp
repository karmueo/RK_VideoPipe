#include "vp_fakesink_des_node.h"

#include "vp_utils/logger/vp_logger.h"
#include "vp_utils/vp_utils.h"

namespace vp_nodes {

vp_fakesink_des_node::vp_fakesink_des_node(std::string node_name,
                                           int channel_index,
                                           bool osd,
                                           std::string gst_encoder_name)
    : vp_des_node(node_name, channel_index),
      gst_encoder_name(std::move(gst_encoder_name)),
      osd(osd) {
    this->gst_pipeline = vp_utils::string_format(this->gst_template, this->gst_encoder_name.c_str());
    VP_INFO(vp_utils::string_format("[%s] [%s]", this->node_name.c_str(), this->gst_pipeline.c_str()));
    this->initialized();
}

vp_fakesink_des_node::~vp_fakesink_des_node() {
    if (sink_writer.isOpened()) {
        sink_writer.release();
    }
    deinitialized();
}

std::shared_ptr<vp_objects::vp_meta>
vp_fakesink_des_node::handle_frame_meta(std::shared_ptr<vp_objects::vp_frame_meta> meta) {
    // 待编码帧。
    cv::Mat encode_frame = (osd && !meta->osd_frame.empty()) ? meta->osd_frame : meta->frame;
    if (encode_frame.empty()) {
        return vp_des_node::handle_frame_meta(meta);
    }

    if (!sink_writer.isOpened()) {
        // 输出帧率（兜底 25fps）。
        int output_fps = meta->fps > 0 ? meta->fps : 25;
        const bool opened = sink_writer.open(
            gst_pipeline,
            cv::CAP_GSTREAMER,
            0,
            output_fps,
            {encode_frame.cols, encode_frame.rows});
        if (!opened) {
            VP_ERROR(vp_utils::string_format("[%s] open gst writer failed: %s", node_name.c_str(), gst_pipeline.c_str()));
            return vp_des_node::handle_frame_meta(meta);
        }

        fps_start_tp = std::chrono::steady_clock::now();
        fps_last_log_tp = fps_start_tp;
        encoded_frames = 0;
    }

    sink_writer.write(encode_frame);
    encoded_frames++;

    // 当前时间点。
    const auto now_tp = std::chrono::steady_clock::now();
    const auto log_delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_tp - fps_last_log_tp).count();
    if (log_delta_ms >= 1000) {
        // 总耗时毫秒。
        const auto total_delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_tp - fps_start_tp).count();
        // 平均 FPS。
        const double avg_fps = total_delta_ms > 0 ? (encoded_frames * 1000.0 / static_cast<double>(total_delta_ms)) : 0.0;
        VP_INFO(vp_utils::string_format("[%s] encode_to_fakesink fps=%.2f frames=%llu",
                                        node_name.c_str(),
                                        avg_fps,
                                        static_cast<unsigned long long>(encoded_frames)));
        fps_last_log_tp = now_tp;
    }

    return vp_des_node::handle_frame_meta(meta);
}

std::shared_ptr<vp_objects::vp_meta>
vp_fakesink_des_node::handle_control_meta(std::shared_ptr<vp_objects::vp_control_meta> meta) {
    return vp_des_node::handle_control_meta(meta);
}

} // namespace vp_nodes
