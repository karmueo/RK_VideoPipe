#include "vp_yolo26_preprocess_node.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "RgaUtils.h"
#include "im2d.h"
#include "im2d_common.h"
#include "nlohmann/json.hpp"
#include "vp_utils/vp_utils.h"

namespace vp_nodes {
namespace {
using json = nlohmann::json;
}

vp_yolo26_preprocess_node::vp_yolo26_preprocess_node(std::string node_name, std::string json_path)
    : vp_node(std::move(node_name)) {
    load_preprocess_config(json_path);
    this->initialized();
}

vp_yolo26_preprocess_node::~vp_yolo26_preprocess_node() {
    deinitialized();
}

void vp_yolo26_preprocess_node::load_preprocess_config(const std::string& json_path) {
    try {
        std::ifstream stream(json_path);  // 配置文件输入流。
        if (!stream.is_open()) {
            VP_WARN(vp_utils::string_format("[%s] open config failed, use default preprocess config. path=%s",
                                            node_name.c_str(),
                                            json_path.c_str()));
            return;
        }

        json j_conf;  // JSON 配置对象。
        stream >> j_conf;
        input_width = std::max(1, j_conf.value("input_width", 640));
        input_height = std::max(1, j_conf.value("input_height", 352));
        preprocess_debug_log_interval = std::max(1, j_conf.value("preprocess_debug_log_interval", 300));
    } catch (const std::exception& e) {
        VP_WARN(vp_utils::string_format("[%s] parse config failed, use default preprocess config. err=%s",
                                        node_name.c_str(),
                                        e.what()));
    }
}

bool vp_yolo26_preprocess_node::ensure_rga_cache(int src_width, int src_height) {
    if (src_width <= 1 || src_height <= 1 || input_width <= 1 || input_height <= 1) {
        return false;
    }

    if (cache_src_width == src_width &&
        cache_src_height == src_height &&
        cache_dst_width == input_width &&
        cache_dst_height == input_height &&
        !cache_bgr_full_data.empty() &&
        !cache_rgb_full_data.empty() &&
        !cache_rgb_resize_data.empty()) {
        return true;
    }

    const size_t src_rgb_bytes = static_cast<size_t>(src_width) * static_cast<size_t>(src_height) * 3U;  // 源尺寸 RGB/BGR 字节数。
    const size_t dst_rgb_bytes = static_cast<size_t>(input_width) * static_cast<size_t>(input_height) * 3U;  // 目标尺寸 RGB 字节数。
    cache_bgr_full_data.assign(src_rgb_bytes, 0U);
    cache_rgb_full_data.assign(src_rgb_bytes, 0U);
    cache_rgb_resize_data.assign(dst_rgb_bytes, 0U);
    if (cache_bgr_full_data.empty() || cache_rgb_full_data.empty() || cache_rgb_resize_data.empty()) {
        return false;
    }

    cache_src_width = src_width;
    cache_src_height = src_height;
    cache_dst_width = input_width;
    cache_dst_height = input_height;
    return true;
}

bool vp_yolo26_preprocess_node::preprocess_with_rga(const cv::Mat& src_nv12,
                                                    std::vector<uint8_t>& dst_rgb_data,
                                                    cv::Mat& dst_bgr_frame) {
    if (src_nv12.empty() || src_nv12.type() != CV_8UC1) {
        return false;
    }

    const int src_width = src_nv12.cols;  // 输入图像宽度。
    const int src_rows = src_nv12.rows;  // 输入总行数（Y+UV）。
    const int src_height = src_rows * 2 / 3;  // 输入图像高度。
    if (src_height <= 1 || src_rows != src_height * 3 / 2 || src_width <= 1) {
        return false;
    }
    const size_t src_nv12_bytes =
        static_cast<size_t>(src_width) * static_cast<size_t>(src_height) * 3U / 2U;  // NV12 输入总字节数。
    if (!ensure_rga_cache(src_width, src_height)) {
        return false;
    }

    std::vector<uint8_t> src_contiguous_data;  // 连续内存输入缓冲。
    const uint8_t* src_ptr = src_nv12.data;  // 输入缓冲地址。
    if (!src_nv12.isContinuous()) {
        src_contiguous_data.resize(src_nv12_bytes);
        for (int row = 0; row < src_rows; ++row) {
            const uint8_t* src_row_ptr = src_nv12.ptr<uint8_t>(row);  // 输入当前行起始地址。
            uint8_t* dst_row_ptr =
                src_contiguous_data.data() + static_cast<size_t>(row) * static_cast<size_t>(src_width);  // 连续缓冲当前行起始地址。
            std::memcpy(dst_row_ptr, src_row_ptr, static_cast<size_t>(src_width));
        }
        src_ptr = src_contiguous_data.data();
    }

    rga_buffer_t src_img = wrapbuffer_virtualaddr(const_cast<uint8_t*>(src_ptr),
                                                  src_width,
                                                  src_height,
                                                  RK_FORMAT_YCbCr_420_SP);  // RGA 输入图描述。
    rga_buffer_t bgr_img = wrapbuffer_virtualaddr(cache_bgr_full_data.data(),
                                                  src_width,
                                                  src_height,
                                                  RK_FORMAT_BGR_888);  // RGA BGR 全尺寸图描述。
    rga_buffer_t rgb_full_img = wrapbuffer_virtualaddr(cache_rgb_full_data.data(),
                                                       src_width,
                                                       src_height,
                                                       RK_FORMAT_RGB_888);  // RGA RGB 全尺寸图描述。
    rga_buffer_t rgb_resize_img = wrapbuffer_virtualaddr(cache_rgb_resize_data.data(),
                                                         input_width,
                                                         input_height,
                                                         RK_FORMAT_RGB_888);  // RGA RGB 缩放图描述。

    IM_STATUS bgr_status =
        imcvtcolor(src_img, bgr_img, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888);  // NV12->BGR 状态。
    bool success = (bgr_status == IM_STATUS_SUCCESS);

    // 融合路径：直接 NV12 -> RGB(目标尺寸)，把颜色转换与缩放合并为一步。
    if (success) {
        IM_STATUS fused_status = improcess(src_img, rgb_resize_img, {}, {}, {}, {}, IM_SYNC);  // 融合处理状态。
        success = (fused_status == IM_STATUS_SUCCESS);
    }

    // RGA 兼容性回退：若融合路径失败，则退回两步 RGA（NV12->RGB，再 RGB resize）。
    if (success) {
        // do nothing
    } else {
        IM_STATUS rgb_status =
            imcvtcolor(src_img, rgb_full_img, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_RGB_888);  // NV12->RGB 状态。
        success = (rgb_status == IM_STATUS_SUCCESS);
        if (success) {
            IM_STATUS resize_status = imresize(rgb_full_img, rgb_resize_img);  // RGB 缩放状态。
            success = (resize_status == IM_STATUS_SUCCESS);
        }
    }

    if (success) {
        dst_bgr_frame = cv::Mat(src_height, src_width, CV_8UC3, cache_bgr_full_data.data()).clone();
        success = !dst_bgr_frame.empty();
    }
    if (!success) {
        dst_rgb_data.clear();
        dst_bgr_frame.release();
        return false;
    }

    dst_rgb_data = cache_rgb_resize_data;
    return true;
}

std::shared_ptr<vp_objects::vp_meta> vp_yolo26_preprocess_node::handle_frame_meta(
    std::shared_ptr<vp_objects::vp_frame_meta> meta) {
    if (meta == nullptr || meta->frame.empty()) {
        return meta;
    }

    std::vector<uint8_t> preprocessed_rgb_data;  // 预处理输出字节缓冲。
    cv::Mat bgr_frame;  // 供后续 OSD 的 BGR 图像。
    const bool ok = preprocess_with_rga(meta->frame, preprocessed_rgb_data, bgr_frame);  // 预处理执行结果。
    meta->yolo26_input_ready = ok;
    if (ok) {
        meta->yolo26_input_rgb_data = std::move(preprocessed_rgb_data);
        meta->yolo26_input_width = input_width;
        meta->yolo26_input_height = input_height;
        meta->frame = bgr_frame;
    } else {
        meta->yolo26_input_rgb_data.clear();
        meta->yolo26_input_width = 0;
        meta->yolo26_input_height = 0;
        VP_WARN(vp_utils::string_format("[%s] preprocess failed for frame_index=%d",
                                        node_name.c_str(),
                                        meta->frame_index));
    }

    ++frame_counter;
    if (frame_counter % static_cast<uint64_t>(preprocess_debug_log_interval) == 0) {
        VP_INFO(vp_utils::string_format("[%s] backend=rga size=%dx%d frame=%llu",
                                        node_name.c_str(),
                                        input_width,
                                        input_height,
                                        static_cast<unsigned long long>(frame_counter)));
    }
    return meta;
}

std::shared_ptr<vp_objects::vp_meta> vp_yolo26_preprocess_node::handle_control_meta(
    std::shared_ptr<vp_objects::vp_control_meta> meta) {
    return meta;
}
}  // namespace vp_nodes
