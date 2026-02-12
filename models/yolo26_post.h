#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "config.h"

/**
 * @brief YOLO26 单尺度分支张量描述。
 */
struct Yolo26HeadTensor {
    int feat_h = 0;  // 特征图高度。
    int feat_w = 0;  // 特征图宽度。
    int num_cls = 0;  // 类别数。
    std::vector<float> reg;  // 回归分支数据，布局为 [4, H, W]。
    std::vector<float> cls;  // 分类分支数据，布局为 [C, H, W]。
};

/**
 * @brief YOLO26 后处理器，负责解码与 NMS。
 */
class Yolo26PostProcessor {
public:
    /**
     * @brief 构造后处理器。
     * @param config YOLO26 配置。
     */
    explicit Yolo26PostProcessor(const YOLO26Config& config);

    /**
     * @brief 执行后处理。
     * @param heads 各尺度 head 数据。
     * @param orig_w 原图宽度。
     * @param orig_h 原图高度。
     * @param ratio_w 宽度缩放比例。
     * @param ratio_h 高度缩放比例。
     * @param results 输出检测框结果。
     * @return int 有效检测框数量。
     */
    int run(const std::vector<Yolo26HeadTensor>& heads,
            int orig_w,
            int orig_h,
            float ratio_w,
            float ratio_h,
            std::vector<DetectionResult>& results) const;

private:
    /**
     * @brief 内部检测框结构。
     */
    struct Detection {
        float x1 = 0.0f;  // 左上角 x。
        float y1 = 0.0f;  // 左上角 y。
        float x2 = 0.0f;  // 右下角 x。
        float y2 = 0.0f;  // 右下角 y。
        float score = 0.0f;  // 置信度。
        int cls_id = -1;  // 类别 ID。
    };

    /**
     * @brief 计算 Sigmoid。
     * @param x 输入值。
     * @return float 输出值。
     */
    static float sigmoid(float x);

    /**
     * @brief 计算 IoU。
     * @param a 检测框 A。
     * @param b 检测框 B。
     * @return float IoU 值。
     */
    static float box_iou(const Detection& a, const Detection& b);

    /**
     * @brief 按类别执行 NMS。
     * @param boxes 输入检测框。
     * @param nms_thres NMS 阈值。
     * @return std::vector<Detection> 保留检测框。
     */
    static std::vector<Detection> nms_per_class(const std::vector<Detection>& boxes, float nms_thres);

    YOLO26Config config;  // 后处理配置。
};

