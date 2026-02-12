#include "yolo26_post.h"

#include <algorithm>
#include <cmath>

Yolo26PostProcessor::Yolo26PostProcessor(const YOLO26Config& config) : config(config) {}

float Yolo26PostProcessor::sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

float Yolo26PostProcessor::box_iou(const Detection& a, const Detection& b) {
    const float xx1 = std::max(a.x1, b.x1);  // 相交区域左上角 x。
    const float yy1 = std::max(a.y1, b.y1);  // 相交区域左上角 y。
    const float xx2 = std::min(a.x2, b.x2);  // 相交区域右下角 x。
    const float yy2 = std::min(a.y2, b.y2);  // 相交区域右下角 y。

    const float iw = std::max(0.0f, xx2 - xx1);  // 相交宽度。
    const float ih = std::max(0.0f, yy2 - yy1);  // 相交高度。
    const float inter = iw * ih;  // 相交面积。

    const float area_a = std::max(0.0f, a.x2 - a.x1) * std::max(0.0f, a.y2 - a.y1);  // A 面积。
    const float area_b = std::max(0.0f, b.x2 - b.x1) * std::max(0.0f, b.y2 - b.y1);  // B 面积。
    const float uni = area_a + area_b - inter;  // 并集面积。
    if (uni <= 0.0f) {
        return 0.0f;
    }
    return inter / uni;
}

std::vector<Yolo26PostProcessor::Detection> Yolo26PostProcessor::nms_per_class(const std::vector<Detection>& boxes, float nms_thres) {
    std::map<int, std::vector<Detection>> cls_buckets;  // 按类别分桶。
    for (const auto& box : boxes) {
        cls_buckets[box.cls_id].push_back(box);
    }

    std::vector<Detection> kept;  // 保留结果。
    for (auto& pair : cls_buckets) {
        auto& cls_boxes = pair.second;  // 当前类别检测框。
        std::sort(cls_boxes.begin(), cls_boxes.end(), [](const Detection& a, const Detection& b) {
            return a.score > b.score;
        });

        while (!cls_boxes.empty()) {
            const Detection best = cls_boxes.front();  // 当前最高分框。
            kept.push_back(best);
            cls_boxes.erase(cls_boxes.begin());

            std::vector<Detection> remain;  // NMS 后剩余框。
            remain.reserve(cls_boxes.size());
            for (const auto& box : cls_boxes) {
                if (box_iou(best, box) < nms_thres) {
                    remain.push_back(box);
                }
            }
            cls_boxes.swap(remain);
        }
    }

    return kept;
}

int Yolo26PostProcessor::run(const std::vector<Yolo26HeadTensor>& heads,
                             int orig_w,
                             int orig_h,
                             float ratio_w,
                             float ratio_h,
                             std::vector<DetectionResult>& results) const {
    std::vector<Detection> boxes;  // 解码后的候选框。
    for (const auto& head : heads) {
        if (head.feat_h <= 0 || head.feat_w <= 0 || head.num_cls <= 0) {
            continue;
        }
        const int hw = head.feat_h * head.feat_w;  // 特征图像素总数。
        if (head.reg.size() != static_cast<size_t>(4 * hw) || head.cls.size() != static_cast<size_t>(head.num_cls * hw)) {
            continue;
        }

        const float stride_h = static_cast<float>(config.input_height) / static_cast<float>(head.feat_h);  // 高方向 stride。
        const float stride_w = static_cast<float>(config.input_width) / static_cast<float>(head.feat_w);  // 宽方向 stride。
        if (std::fabs(stride_h - stride_w) > 1e-6f) {
            continue;
        }
        const float stride = stride_h;  // 当前分支 stride。

        for (int h = 0; h < head.feat_h; ++h) {
            for (int w = 0; w < head.feat_w; ++w) {
                const int base_idx = h * head.feat_w + w;  // 当前栅格线性索引。
                int best_cls = -1;  // 最佳类别 ID。
                float best_score = -1.0f;  // 最佳类别分数。
                for (int c = 0; c < head.num_cls; ++c) {
                    const float score = sigmoid(head.cls[c * hw + base_idx]);
                    if (score > best_score) {
                        best_score = score;
                        best_cls = c;
                    }
                }

                if (best_cls < 0 || best_score < config.conf_threshold) {
                    continue;
                }

                const float grid_x = static_cast<float>(w) + 0.5f;  // 栅格中心 x。
                const float grid_y = static_cast<float>(h) + 0.5f;  // 栅格中心 y。
                float x1 = (grid_x - head.reg[0 * hw + base_idx]) * stride;  // 输入尺度 x1。
                float y1 = (grid_y - head.reg[1 * hw + base_idx]) * stride;  // 输入尺度 y1。
                float x2 = (grid_x + head.reg[2 * hw + base_idx]) * stride;  // 输入尺度 x2。
                float y2 = (grid_y + head.reg[3 * hw + base_idx]) * stride;  // 输入尺度 y2。

                x1 /= ratio_w;
                y1 /= ratio_h;
                x2 /= ratio_w;
                y2 /= ratio_h;

                x1 = std::max(0.0f, std::min(static_cast<float>(orig_w), x1));
                y1 = std::max(0.0f, std::min(static_cast<float>(orig_h), y1));
                x2 = std::max(0.0f, std::min(static_cast<float>(orig_w), x2));
                y2 = std::max(0.0f, std::min(static_cast<float>(orig_h), y2));
                boxes.push_back({x1, y1, x2, y2, best_score, best_cls});
            }
        }
    }

    const std::vector<Detection> kept = nms_per_class(boxes, config.nms_threshold);  // NMS 后保留框。
    results.clear();
    results.reserve(kept.size());
    for (const auto& det : kept) {
        DetectionResult result;  // 统一输出检测结构。
        result.id = det.cls_id;
        result.score = det.score;
        if (det.cls_id >= 0 && det.cls_id < static_cast<int>(config.labels.size())) {
            result.label = config.labels[det.cls_id];
        } else {
            result.label = "cls_" + std::to_string(det.cls_id);
        }
        result.box.top = static_cast<int>(det.x1);
        result.box.left = static_cast<int>(det.y1);
        result.box.bottom = static_cast<int>(det.x2);
        result.box.right = static_cast<int>(det.y2);
        results.push_back(result);
    }

    return static_cast<int>(results.size());
}

