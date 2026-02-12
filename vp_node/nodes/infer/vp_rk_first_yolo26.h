#pragma once

#include <cstdint>

#include "vp_primary_infer_node.h"
#include "yolo26.h"

namespace vp_nodes {
/**
 * @brief YOLO26 主检测节点（基于 RKNN）。
 */
class vp_rk_first_yolo26 : public vp_primary_infer_node {
private:
    std::shared_ptr<YOLO26> rk_model;  // YOLO26 模型对象。
    int infer_skip_frames = 0;  // 跳帧推理配置，0 表示不跳帧。
    int infer_period = 1;  // 推理周期，等于 infer_skip_frames + 1。
    uint64_t infer_frame_counter = 0;  // 输入帧计数器，用于决定是否执行推理。
    std::vector<std::shared_ptr<vp_objects::vp_frame_target>> last_targets_cache;  // 上一次推理结果缓存。

protected:
    /**
     * @brief 执行整帧推理组合流程。
     * @param frame_meta_with_batch 批次帧元数据。
     */
    virtual void run_infer_combinations(
        const std::vector<std::shared_ptr<vp_objects::vp_frame_meta>>& frame_meta_with_batch) override;

    /**
     * @brief 占位后处理接口（为满足基类纯虚函数）。
     * @param raw_outputs 原始输出。
     * @param frame_meta_with_batch 批次帧元数据。
     */
    virtual void postprocess(const std::vector<cv::Mat>& raw_outputs,
                             const std::vector<std::shared_ptr<vp_objects::vp_frame_meta>>& frame_meta_with_batch) override;

public:
    /**
     * @brief 构造 YOLO26 主检测节点。
     * @param node_name 节点名称。
     * @param json_path 配置文件路径。
     */
    vp_rk_first_yolo26(std::string node_name, std::string json_path);

    /**
     * @brief 析构函数。
     */
    ~vp_rk_first_yolo26();
};
}  // namespace vp_nodes
