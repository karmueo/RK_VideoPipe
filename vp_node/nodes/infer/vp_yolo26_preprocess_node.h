#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nodes/base/vp_node.h"

namespace vp_nodes {
/**
 * @brief YOLO26 预处理节点，负责把 BGR 帧转换为模型输入 RGB 并缩放。
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

private:
    /**
     * @brief 读取 YOLO26 配置并初始化预处理参数。
     *
     * @param json_path YOLO26 配置路径。
     */
    void load_preprocess_config(const std::string& json_path);

    /**
     * @brief 使用 librga 执行 BGR->RGB 和 resize 预处理。
     *
     * @param src_bgr 输入 BGR 图像。
     * @param dst_rgb_data 输出 RGB 字节缓冲。
     * @return true 成功。
     * @return false 失败。
     */
    bool preprocess_with_rga(const cv::Mat& src_bgr, std::vector<uint8_t>& dst_rgb_data) const;

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
