#include "vp_bgr_to_nv12_node.h"

#include <cstring>

#include <opencv2/imgproc.hpp>

namespace vp_nodes {

vp_bgr_to_nv12_node::vp_bgr_to_nv12_node(std::string node_name) : vp_node(node_name) {
    this->initialized();
}

vp_bgr_to_nv12_node::~vp_bgr_to_nv12_node() {
    deinitialized();
}

std::shared_ptr<vp_objects::vp_meta> vp_bgr_to_nv12_node::handle_frame_meta(
    std::shared_ptr<vp_objects::vp_frame_meta> meta) {
    if (meta == nullptr) {
        return meta;
    }

    // 输入图像（优先使用 OSD 结果）。
    const cv::Mat& input_bgr = meta->osd_frame.empty() ? meta->frame : meta->osd_frame;
    if (input_bgr.empty()) {
        return meta;
    }

    // 输入图像类型。
    const int input_type = input_bgr.type();
    // 输入图像宽度。
    const int input_width = input_bgr.cols;
    // 输入图像高度。
    const int input_height = input_bgr.rows;

    if (input_type != CV_8UC3 || input_width <= 1 || input_height <= 1) {
        VP_WARN(vp_utils::string_format(
            "[%s] invalid bgr frame, keep original. type=%d size=%dx%d",
            node_name.c_str(),
            input_type,
            input_width,
            input_height));
        return meta;
    }

    // NV12 要求偶数宽高，这里做向下对齐裁剪。
    const int even_width = input_width & ~1;
    const int even_height = input_height & ~1;
    if (even_width <= 1 || even_height <= 1) {
        VP_WARN(vp_utils::string_format("[%s] frame too small after even align: %dx%d",
                                        node_name.c_str(),
                                        even_width,
                                        even_height));
        return meta;
    }

    // 偶数尺寸 BGR 输入。
    cv::Mat bgr_even = input_bgr;
    if (even_width != input_width || even_height != input_height) {
        bgr_even = input_bgr(cv::Rect(0, 0, even_width, even_height)).clone();
    }

    // I420 图像缓存。
    cv::Mat i420_frame;
    cv::cvtColor(bgr_even, i420_frame, cv::COLOR_BGR2YUV_I420);
    if (i420_frame.empty()) {
        VP_WARN(vp_utils::string_format("[%s] cvtColor BGR->I420 failed", node_name.c_str()));
        return meta;
    }

    // NV12 输出图像缓存。
    cv::Mat nv12_frame(even_height * 3 / 2, even_width, CV_8UC1);
    if (nv12_frame.empty()) {
        return meta;
    }

    // Y 平面字节数。
    const size_t y_size = static_cast<size_t>(even_width) * static_cast<size_t>(even_height);
    // U/V 平面字节数。
    const size_t uv_size = y_size / 4;
    // I420 基地址。
    const uint8_t* i420_ptr = i420_frame.ptr<uint8_t>(0);
    // NV12 基地址。
    uint8_t* nv12_ptr = nv12_frame.ptr<uint8_t>(0);
    // I420 的 Y 平面。
    const uint8_t* src_y = i420_ptr;
    // I420 的 U 平面。
    const uint8_t* src_u = i420_ptr + y_size;
    // I420 的 V 平面。
    const uint8_t* src_v = i420_ptr + y_size + uv_size;
    // NV12 的 Y 平面。
    uint8_t* dst_y = nv12_ptr;
    // NV12 的 UV 平面。
    uint8_t* dst_uv = nv12_ptr + y_size;

    std::memcpy(dst_y, src_y, y_size);
    for (size_t i = 0; i < uv_size; ++i) {
        dst_uv[2 * i] = src_u[i];
        dst_uv[2 * i + 1] = src_v[i];
    }

    meta->frame = nv12_frame;
    meta->original_width = even_width;
    meta->original_height = even_height;
    return meta;
}

std::shared_ptr<vp_objects::vp_meta> vp_bgr_to_nv12_node::handle_control_meta(
    std::shared_ptr<vp_objects::vp_control_meta> meta) {
    return meta;
}

}  // namespace vp_nodes
