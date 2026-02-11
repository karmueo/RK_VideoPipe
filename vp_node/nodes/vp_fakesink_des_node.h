#pragma once

#include <chrono>
#include <memory>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <string>

#include "nodes/base/vp_des_node.h"
#include "objects/vp_control_meta.h"
#include "objects/vp_frame_meta.h"

namespace vp_nodes {

/**
 * @brief 编码并输出到 fakesink 的目标节点。
 *
 * 使用 OpenCV VideoWriter + GStreamer 管线，将输入帧编码后送入 fakesink，
 * 便于评估“解码+编码”链路吞吐，不受显示端影响。
 */
class vp_fakesink_des_node : public vp_des_node {
private:
    // GStreamer 管线模板。
    std::string gst_template = "appsrc ! videoconvert ! %s ! h264parse ! fakesink sync=false";
    // 实际使用的 GStreamer 管线。
    std::string gst_pipeline;
    // OpenCV 输出器。
    cv::VideoWriter sink_writer;

    // 编码器名称。
    std::string gst_encoder_name = "mpph264enc";
    // 是否优先使用 OSD 帧。
    bool osd = true;

    // 已编码帧计数。
    uint64_t encoded_frames = 0;
    // FPS 统计起始时间。
    std::chrono::steady_clock::time_point fps_start_tp;
    // 上次打印 FPS 时间。
    std::chrono::steady_clock::time_point fps_last_log_tp;

protected:
    /**
     * @brief 处理输入视频帧并推送到编码+fakesink 管线。
     * @param meta 输入帧元数据。
     * @return std::shared_ptr<vp_objects::vp_meta> 始终返回 nullptr。
     */
    virtual std::shared_ptr<vp_objects::vp_meta> handle_frame_meta(std::shared_ptr<vp_objects::vp_frame_meta> meta) override;

    /**
     * @brief 处理控制元数据。
     * @param meta 控制元数据。
     * @return std::shared_ptr<vp_objects::vp_meta> 始终返回 nullptr。
     */
    virtual std::shared_ptr<vp_objects::vp_meta> handle_control_meta(std::shared_ptr<vp_objects::vp_control_meta> meta) override;

public:
    /**
     * @brief 构造编码到 fakesink 的目标节点。
     * @param node_name 节点名称。
     * @param channel_index 通道索引。
     * @param osd 是否优先使用 OSD 帧。
     * @param gst_encoder_name GStreamer 编码器名称。
     */
    vp_fakesink_des_node(std::string node_name,
                         int channel_index,
                         bool osd = true,
                         std::string gst_encoder_name = "mpph264enc");

    /**
     * @brief 析构并释放资源。
     */
    ~vp_fakesink_des_node();
};

} // namespace vp_nodes
