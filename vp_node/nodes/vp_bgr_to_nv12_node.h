#pragma once

#include "base/vp_node.h"

namespace vp_nodes {

/**
 * @brief 将 `vp_frame_meta` 中的 BGR 图像转换为 NV12 的中间节点。
 *
 * 优先使用 `osd_frame`（若存在）作为输入，这样可以把检测框/文字叠加结果
 * 转换后交给 NV12 SDL 显示节点输出。
 */
class vp_bgr_to_nv12_node : public vp_node {
protected:
    /**
     * @brief 处理视频帧元数据并执行 BGR->NV12 转换。
     *
     * @param meta 输入帧元数据。
     * @return std::shared_ptr<vp_objects::vp_meta> 转换后的同一份元数据。
     */
    virtual std::shared_ptr<vp_objects::vp_meta> handle_frame_meta(
        std::shared_ptr<vp_objects::vp_frame_meta> meta) override;

    /**
     * @brief 透传控制元数据。
     *
     * @param meta 输入控制元数据。
     * @return std::shared_ptr<vp_objects::vp_meta> 原始控制元数据。
     */
    virtual std::shared_ptr<vp_objects::vp_meta> handle_control_meta(
        std::shared_ptr<vp_objects::vp_control_meta> meta) override;

public:
    /**
     * @brief 构造 BGR->NV12 适配节点。
     *
     * @param node_name 节点名称。
     */
    explicit vp_bgr_to_nv12_node(std::string node_name);

    /**
     * @brief 析构节点并释放资源。
     */
    ~vp_bgr_to_nv12_node();
};

}  // namespace vp_nodes
