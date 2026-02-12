#include "vp_rk_first_yolo26.h"

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
    std::vector<cv::Mat> mats_to_infer;  // 待推理图像容器。

    auto start_time = std::chrono::system_clock::now();  // 开始时间戳。
    vp_primary_infer_node::prepare(frame_meta_with_batch, mats_to_infer);
    const auto prepare_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - start_time);  // prepare 耗时。
    if (mats_to_infer.empty()) {
        return;
    }

    start_time = std::chrono::system_clock::now();
    std::vector<DetectionResult> res;  // 检测结果。
    rk_model->run(mats_to_infer[0], res);
    auto infer_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - start_time);  // infer 耗时。

    auto& frame_meta = frame_meta_with_batch[0];  // 当前帧元数据。
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
