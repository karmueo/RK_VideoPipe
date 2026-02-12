#include "vp_nv12_to_bgr_node.h"

#include <opencv2/imgproc.hpp>

namespace vp_nodes {

vp_nv12_to_bgr_node::vp_nv12_to_bgr_node(std::string node_name) : vp_node(node_name) {
    this->initialized();
}

vp_nv12_to_bgr_node::~vp_nv12_to_bgr_node() {
    deinitialized();
}

std::shared_ptr<vp_objects::vp_meta> vp_nv12_to_bgr_node::handle_frame_meta(
    std::shared_ptr<vp_objects::vp_frame_meta> meta) {
    if (meta == nullptr || meta->frame.empty()) {
        return meta;
    }

    // 输入帧类型。
    const int input_type = meta->frame.type();
    // 输入帧宽度。
    const int frame_width = meta->frame.cols;
    // 输入帧总行数（Y+UV）。
    const int frame_rows = meta->frame.rows;
    // 推导得到的图像高度。
    const int frame_height = frame_rows * 2 / 3;

    // NV12 基础合法性判断。
    const bool valid_nv12_type = (input_type == CV_8UC1);
    const bool valid_nv12_size =
        (frame_width > 0 && frame_rows > 0 && frame_rows == frame_height * 3 / 2);
    if (!valid_nv12_type || !valid_nv12_size) {
        VP_WARN(vp_utils::string_format(
            "[%s] invalid nv12 frame, keep original. type=%d size=%dx%d",
            node_name.c_str(),
            input_type,
            frame_width,
            frame_rows));
        return meta;
    }

    // NV12 转换后的 BGR 图像。
    cv::Mat bgr_frame;
    cv::cvtColor(meta->frame, bgr_frame, cv::COLOR_YUV2BGR_NV12);
    if (bgr_frame.empty()) {
        VP_WARN(vp_utils::string_format("[%s] cvtColor NV12->BGR failed, keep original frame",
                                        node_name.c_str()));
        return meta;
    }

    meta->frame = bgr_frame;
    return meta;
}

std::shared_ptr<vp_objects::vp_meta> vp_nv12_to_bgr_node::handle_control_meta(
    std::shared_ptr<vp_objects::vp_control_meta> meta) {
    return meta;
}

}  // namespace vp_nodes
