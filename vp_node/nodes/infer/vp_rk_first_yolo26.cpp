#include "vp_rk_first_yolo26.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>

#include "vp_utils/vp_utils.h"

namespace vp_nodes {
vp_rk_first_yolo26::vp_rk_first_yolo26(std::string node_name, std::string json_path)
    : vp_primary_infer_node(node_name, "") {
    YOLO26Config conf;  // YOLO26 配置。
    const int ret = YOLO26::load_config(json_path, conf);  // 配置加载结果。
    if (ret != 0) {
        throw std::runtime_error(vp_utils::string_format("[%s] load yolo26 config failed! path=%s ret=%d",
                                                         node_name.c_str(),
                                                         json_path.c_str(),
                                                         ret));
    }

    try {
        std::ifstream stream(json_path);  // 配置文件输入流。
        if (stream.is_open()) {
            json j_conf;  // JSON 配置对象。
            stream >> j_conf;
            infer_skip_frames = std::max(0, j_conf.value("infer_skip_frames", 0));
        }
    } catch (const std::exception&) {
        infer_skip_frames = 0;
    }
    infer_period = infer_skip_frames + 1;

    rk_model = std::make_shared<YOLO26>(conf);
    this->initialized();
}

vp_rk_first_yolo26::~vp_rk_first_yolo26() {
    deinitialized();
    rk_model.reset();
}

void vp_rk_first_yolo26::run_infer_combinations(
    const std::vector<std::shared_ptr<vp_objects::vp_frame_meta>>& frame_meta_with_batch) {
    assert(frame_meta_with_batch.size() == 1);
    auto& frame_meta = frame_meta_with_batch[0];  // 当前帧元数据。
    const bool do_infer = (infer_frame_counter % static_cast<uint64_t>(infer_period) == 0);  // 本帧是否执行真实推理。
    ++infer_frame_counter;

    if (!do_infer) {
        for (const auto& cached_target : last_targets_cache) {
            if (cached_target == nullptr) {
                continue;
            }
            auto target = cached_target->clone();  // 克隆缓存目标，避免跨帧共享对象。
            target->frame_index = frame_meta->frame_index;
            target->channel_index = frame_meta->channel_index;
            frame_meta->targets.push_back(target);
        }
        vp_infer_node::infer_combinations_time_cost(static_cast<int>(frame_meta_with_batch.size()), 0, 0, 0, 0);
        return;
    }

    std::vector<cv::Mat> mats_to_infer;  // 待推理图像容器。

    auto start_time = std::chrono::system_clock::now();  // 开始时间戳。
    vp_primary_infer_node::prepare(frame_meta_with_batch, mats_to_infer);
    const auto prepare_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - start_time);  // prepare 耗时。
    if (mats_to_infer.empty()) {
        return;
    }

    start_time = std::chrono::system_clock::now();
    if (!frame_meta->yolo26_input_ready || frame_meta->yolo26_input_rgb_data.empty()) {
        VP_WARN(vp_utils::string_format("[%s] yolo26 input is not ready, drop frame=%d",
                                        node_name.c_str(),
                                        frame_meta->frame_index));
        return;
    }
    const size_t expected_input_bytes =
        static_cast<size_t>(frame_meta->yolo26_input_width) * static_cast<size_t>(frame_meta->yolo26_input_height) * 3U;  // 期望输入字节数。
    if (frame_meta->yolo26_input_rgb_data.size() != expected_input_bytes) {
        VP_WARN(vp_utils::string_format("[%s] yolo26 input bytes mismatch, got=%zu expect=%zu frame=%d",
                                        node_name.c_str(),
                                        frame_meta->yolo26_input_rgb_data.size(),
                                        expected_input_bytes,
                                        frame_meta->frame_index));
        return;
    }

    const int orig_w = frame_meta->original_width > 0 ? frame_meta->original_width : mats_to_infer[0].cols;  // 原始图像宽度。
    const int orig_h = frame_meta->original_height > 0 ? frame_meta->original_height : mats_to_infer[0].rows;  // 原始图像高度。
    std::vector<DetectionResult> res;  // 检测结果。
    rk_model->run(frame_meta->yolo26_input_rgb_data.data(), orig_w, orig_h, res);
    auto infer_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - start_time);  // infer 耗时。

    for (const auto& obj : res) {
        auto target = std::make_shared<vp_objects::vp_frame_target>(obj.box.top,
                                                                     obj.box.left,
                                                                     obj.box.bottom - obj.box.top,
                                                                     obj.box.right - obj.box.left,
                                                                     obj.id,
                                                                     obj.score,
                                                                     frame_meta->frame_index,
                                                                     frame_meta->channel_index,
                                                                     obj.label);
        frame_meta->targets.push_back(target);
    }
    last_targets_cache.clear();
    last_targets_cache.reserve(frame_meta->targets.size());
    for (const auto& target : frame_meta->targets) {
        if (target == nullptr) {
            continue;
        }
        last_targets_cache.push_back(target->clone());
    }

    vp_infer_node::infer_combinations_time_cost(static_cast<int>(mats_to_infer.size()),
                                                static_cast<int>(prepare_time.count()),
                                                0,
                                                static_cast<int>(infer_time.count()),
                                                0);
}

void vp_rk_first_yolo26::postprocess(
    const std::vector<cv::Mat>& raw_outputs,
    const std::vector<std::shared_ptr<vp_objects::vp_frame_meta>>& frame_meta_with_batch) {
    (void)raw_outputs;
    (void)frame_meta_with_batch;
}
}  // namespace vp_nodes
