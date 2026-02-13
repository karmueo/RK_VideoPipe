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

bool vp_yolo26_preprocess_node::preprocess_with_rga(const cv::Mat& src_bgr, std::vector<uint8_t>& dst_rgb_data) const {
    if (src_bgr.empty() || src_bgr.type() != CV_8UC3) {
        return false;
    }

    const int src_width = src_bgr.cols;  // 输入图像宽度。
    const int src_height = src_bgr.rows;  // 输入图像高度。
    const size_t src_bytes = static_cast<size_t>(src_width) * static_cast<size_t>(src_height) * 3U;  // 输入总字节数。

    std::vector<uint8_t> src_contiguous_data;  // 连续内存输入缓冲。
    const uint8_t* src_ptr = src_bgr.data;  // 输入缓冲地址。
    if (!src_bgr.isContinuous()) {
        src_contiguous_data.resize(src_bytes);
        for (int row = 0; row < src_height; ++row) {
            const uint8_t* src_row_ptr = src_bgr.ptr<uint8_t>(row);  // 输入当前行起始地址。
            uint8_t* dst_row_ptr = src_contiguous_data.data() + static_cast<size_t>(row) * static_cast<size_t>(src_width) * 3U;  // 连续缓冲当前行起始地址。
            std::memcpy(dst_row_ptr, src_row_ptr, static_cast<size_t>(src_width) * 3U);
        }
        src_ptr = src_contiguous_data.data();
    }

    std::vector<uint8_t> rgb_same_size_data(src_bytes);  // 与输入同尺寸的 RGB 中间缓冲。
    const size_t dst_bytes = static_cast<size_t>(input_width) * static_cast<size_t>(input_height) * 3U;  // 输出总字节数。
    dst_rgb_data.assign(dst_bytes, 0U);
    if (rgb_same_size_data.empty() || dst_rgb_data.empty()) {
        return false;
    }

    rga_buffer_handle_t src_handle = 0;  // RGA 输入句柄。
    rga_buffer_handle_t rgb_handle = 0;  // RGA 中间图句柄。
    rga_buffer_handle_t dst_handle = 0;  // RGA 输出句柄。

    src_handle = importbuffer_virtualaddr(const_cast<uint8_t*>(src_ptr), src_bytes);
    rgb_handle = importbuffer_virtualaddr(rgb_same_size_data.data(), src_bytes);
    dst_handle = importbuffer_virtualaddr(dst_rgb_data.data(), dst_bytes);
    if (src_handle == 0 || rgb_handle == 0 || dst_handle == 0) {
        if (src_handle > 0) {
            releasebuffer_handle(src_handle);
        }
        if (rgb_handle > 0) {
            releasebuffer_handle(rgb_handle);
        }
        if (dst_handle > 0) {
            releasebuffer_handle(dst_handle);
        }
        return false;
    }

    rga_buffer_t src_img =
        wrapbuffer_handle(src_handle, src_width, src_height, RK_FORMAT_BGR_888);  // RGA 输入图描述。
    rga_buffer_t rgb_img =
        wrapbuffer_handle(rgb_handle, src_width, src_height, RK_FORMAT_RGB_888);  // RGA 中间图描述。
    rga_buffer_t dst_img =
        wrapbuffer_handle(dst_handle, input_width, input_height, RK_FORMAT_RGB_888);  // RGA 输出图描述。

    IM_STATUS color_status =
        imcvtcolor(src_img, rgb_img, RK_FORMAT_BGR_888, RK_FORMAT_RGB_888);  // RGA 颜色转换状态。
    bool success = (color_status == IM_STATUS_SUCCESS);
    if (success) {
        IM_STATUS resize_status = imresize(rgb_img, dst_img);  // RGA 缩放状态。
        success = (resize_status == IM_STATUS_SUCCESS);
    }

    releasebuffer_handle(src_handle);
    releasebuffer_handle(rgb_handle);
    releasebuffer_handle(dst_handle);
    if (!success) {
        dst_rgb_data.clear();
    }
    return success;
}

std::shared_ptr<vp_objects::vp_meta> vp_yolo26_preprocess_node::handle_frame_meta(
    std::shared_ptr<vp_objects::vp_frame_meta> meta) {
    if (meta == nullptr || meta->frame.empty()) {
        return meta;
    }

    std::vector<uint8_t> preprocessed_rgb_data;  // 预处理输出字节缓冲。
    const bool ok = preprocess_with_rga(meta->frame, preprocessed_rgb_data);  // 预处理执行结果。
    meta->yolo26_input_ready = ok;
    if (ok) {
        meta->yolo26_input_rgb_data = std::move(preprocessed_rgb_data);
        meta->yolo26_input_width = input_width;
        meta->yolo26_input_height = input_height;
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
