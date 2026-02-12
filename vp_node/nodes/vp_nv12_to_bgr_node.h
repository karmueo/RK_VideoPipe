#pragma once

#include "base/vp_node.h"

namespace vp_nodes {

/**
 * @brief 将 `vp_frame_meta.frame` 从 NV12 转换为 BGR 的中间节点。
 *
 * 该节点用于在“硬解码输出 NV12”与“下游算法节点默认消费 BGR”之间做格式适配，
 * 仅改写 `meta->frame`，不改变目标框、控制信息等业务字段。
 */
class vp_nv12_to_bgr_node : public vp_node {
protected:
    /**
     * @brief 处理视频帧元数据并执行 NV12->BGR 转换。
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
     * @brief 构造 NV12->BGR 适配节点。
     *
     * @param node_name 节点名称。
     */
    explicit vp_nv12_to_bgr_node(std::string node_name);

    /**
     * @brief 析构节点并释放资源。
     */
    ~vp_nv12_to_bgr_node();
};

}  // namespace vp_nodes
