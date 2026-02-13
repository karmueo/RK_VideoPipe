#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nodes/base/vp_node.h"

namespace vp_nodes {
/**
 * @brief YOLO26 预处理节点，直接处理 NV12 帧并输出 BGR/模型输入。
 */
class vp_yolo26_preprocess_node : public vp_node {
private:
    // 目标输入宽度。
    int input_width = 640;
    // 目标输入高度。
    int input_height = 352;
    // 日志限频间隔（帧）。
    int preprocess_debug_log_interval = 300;
    // 帧计数器。
    uint64_t frame_counter = 0;
    // 当前缓存对应的源宽度。
    int cache_src_width = 0;
    // 当前缓存对应的源高度。
    int cache_src_height = 0;
    // 当前缓存对应的目标宽度。
    int cache_dst_width = 0;
    // 当前缓存对应的目标高度。
    int cache_dst_height = 0;
    // NV12->BGR 全尺寸缓存。
    std::vector<uint8_t> cache_bgr_full_data;
    // NV12->RGB 全尺寸缓存（融合失败时回退到两步 RGA 路径使用）。
    std::vector<uint8_t> cache_rgb_full_data;
    // 模型输入 RGB 缩放缓存。
    std::vector<uint8_t> cache_rgb_resize_data;

private:
    /**
     * @brief 读取 YOLO26 配置并初始化预处理参数。
     *
     * @param json_path YOLO26 配置路径。
     */
    void load_preprocess_config(const std::string& json_path);

    /**
     * @brief 根据输入/输出尺寸确保 RGA 缓存可用。
     *
     * @param src_width 输入宽度。
     * @param src_height 输入高度。
     * @return true 缓存可用。
     * @return false 缓存分配失败。
     */
    bool ensure_rga_cache(int src_width, int src_height);

    /**
     * @brief 使用 librga 执行 NV12 预处理。
     *
     * @param src_nv12 输入 NV12 图像（CV_8UC1，高度为 3/2H）。
     * @param dst_rgb_data 输出 RGB 字节缓冲。
     * @param dst_bgr_frame 输出 BGR 图像（供 OSD 节点使用）。
     * @return true 成功。
     * @return false 失败。
     */
    bool preprocess_with_rga(const cv::Mat& src_nv12, std::vector<uint8_t>& dst_rgb_data, cv::Mat& dst_bgr_frame);

protected:
    /**
     * @brief 处理帧元数据并写入 YOLO26 预处理缓存。
     *
     * @param meta 输入帧元数据。
     * @return std::shared_ptr<vp_objects::vp_meta> 处理后的元数据。
     */
    virtual std::shared_ptr<vp_objects::vp_meta> handle_frame_meta(
        std::shared_ptr<vp_objects::vp_frame_meta> meta) override;

    /**
     * @brief 透传控制元数据。
     *
     * @param meta 控制元数据。
     * @return std::shared_ptr<vp_objects::vp_meta> 原样返回。
     */
    virtual std::shared_ptr<vp_objects::vp_meta> handle_control_meta(
        std::shared_ptr<vp_objects::vp_control_meta> meta) override;

public:
    /**
     * @brief 构造 YOLO26 预处理节点。
     *
     * @param node_name 节点名称。
     * @param json_path YOLO26 配置路径。
     */
    vp_yolo26_preprocess_node(std::string node_name, std::string json_path);

    /**
     * @brief 析构函数。
     */
    ~vp_yolo26_preprocess_node();
};
}  // namespace vp_nodes
