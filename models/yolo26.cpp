#include "yolo26.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <utility>

YOLO26::YOLO26(const YOLO26Config& config) : RKBASE(config.model_path), config(config) {
    // 使用模型真实输入尺寸，避免配置尺寸与模型不一致导致越界访问。
    this->config.input_width = model_width;
    this->config.input_height = model_height;
    this->postprocessor = std::make_unique<Yolo26PostProcessor>(this->config);
}

YOLO26::~YOLO26() = default;

int YOLO26::load_config(const std::string& json_path, YOLO26Config& conf) {
    std::ifstream stream(json_path);  // 配置文件输入流。
    if (!stream.is_open()) {
        return -1;
    }

    json j_conf;  // JSON 配置对象。
    stream >> j_conf;

    conf.model_path = j_conf.value("model_path", "");
    conf.conf_threshold = j_conf.value("conf_threshold", 0.5f);
    conf.nms_threshold = j_conf.value("nms_threshold", 0.45f);
    conf.input_width = j_conf.value("input_width", 640);
    conf.input_height = j_conf.value("input_height", 352);
    conf.core_mask = j_conf.value("core_mask", 0);
    conf.type = ModelType::YOLO26;

    conf.labels.clear();
    if (j_conf.contains("labels") && j_conf["labels"].is_array()) {
        for (const auto& item : j_conf["labels"]) {
            conf.labels.push_back(item.template get<std::string>());
        }
    }
    if (conf.labels.empty()) {
        conf.labels = {"bird", "uav"};
    }

    conf.alarm_labels.clear();
    if (j_conf.contains("alarm_labels") && j_conf["alarm_labels"].is_array()) {
        for (const auto& item : j_conf["alarm_labels"]) {
            conf.alarm_labels.push_back(item.template get<std::string>());
        }
    }
    if (conf.alarm_labels.empty()) {
        conf.alarm_labels = conf.labels;
    }

    if (conf.model_path.empty() || conf.input_width <= 0 || conf.input_height <= 0) {
        return -2;
    }
    return 0;
}

bool YOLO26::output_to_chw(const rknn_output& output, const rknn_tensor_attr& attr, Yolo26HeadTensor& tensor) {
    if (output.buf == nullptr) {
        return false;
    }
    const float* src = reinterpret_cast<const float*>(output.buf);  // 输出浮点缓冲区。
    if (src == nullptr) {
        return false;
    }

    if (attr.n_dims == 4) {
        if (attr.fmt == RKNN_TENSOR_NCHW) {
            tensor.num_cls = attr.dims[1];
            tensor.feat_h = attr.dims[2];
            tensor.feat_w = attr.dims[3];
            const size_t total = static_cast<size_t>(tensor.num_cls) * tensor.feat_h * tensor.feat_w;  // 总元素数。
            tensor.cls.assign(src, src + total);
            return true;
        }

        tensor.feat_h = attr.dims[1];
        tensor.feat_w = attr.dims[2];
        tensor.num_cls = attr.dims[3];
        const size_t total = static_cast<size_t>(tensor.num_cls) * tensor.feat_h * tensor.feat_w;  // 总元素数。
        tensor.cls.resize(total);
        for (int y = 0; y < tensor.feat_h; ++y) {
            for (int x = 0; x < tensor.feat_w; ++x) {
                for (int c = 0; c < tensor.num_cls; ++c) {
                    const size_t nhwci = static_cast<size_t>(y) * tensor.feat_w * tensor.num_cls + static_cast<size_t>(x) * tensor.num_cls + c;  // NHWC 索引。
                    const size_t chwi = static_cast<size_t>(c) * tensor.feat_h * tensor.feat_w + static_cast<size_t>(y) * tensor.feat_w + x;  // CHW 索引。
                    tensor.cls[chwi] = src[nhwci];
                }
            }
        }
        return true;
    }

    if (attr.n_dims == 3) {
        tensor.num_cls = attr.dims[0];
        tensor.feat_h = attr.dims[1];
        tensor.feat_w = attr.dims[2];
        const size_t total = static_cast<size_t>(tensor.num_cls) * tensor.feat_h * tensor.feat_w;  // 总元素数。
        tensor.cls.assign(src, src + total);
        return true;
    }

    return false;
}

void YOLO26::run(const uint8_t* model_input_rgb, int orig_w, int orig_h, std::vector<DetectionResult>& res) {
    res.clear();
    if (model_input_rgb == nullptr || orig_w <= 0 || orig_h <= 0) {
        return;
    }

    const float ratio_w = static_cast<float>(config.input_width) / static_cast<float>(orig_w);  // 宽方向缩放比。
    const float ratio_h = static_cast<float>(config.input_height) / static_cast<float>(orig_h);  // 高方向缩放比。

    inputs[0].buf = const_cast<uint8_t*>(model_input_rgb);
    ret = rknn_inputs_set(ctx, io_num.n_input, inputs);
    if (ret < 0) {
        return;
    }

    std::vector<rknn_output> outputs(io_num.n_output);  // RKNN 输出容器。
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        outputs[i].index = i;
        outputs[i].want_float = 1;
        outputs[i].is_prealloc = 0;
    }

    ret = rknn_run(ctx, nullptr);
    if (ret < 0) {
        return;
    }
    ret = rknn_outputs_get(ctx, io_num.n_output, outputs.data(), nullptr);
    if (ret < 0) {
        return;
    }

    struct PartialHead {
        int feat_h = 0;  // 特征图高度。
        int feat_w = 0;  // 特征图宽度。
        int cls_c = 0;  // 分类通道数。
        std::vector<float> reg;  // 回归数据。
        std::vector<float> cls;  // 分类数据。
    };

    std::map<std::pair<int, int>, PartialHead> head_map;  // 以 (H,W) 聚合分支输出。
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        Yolo26HeadTensor tensor;  // 临时张量。
        if (!output_to_chw(outputs[i], output_attrs[i], tensor)) {
            continue;
        }

        auto key = std::make_pair(tensor.feat_h, tensor.feat_w);  // 当前 head 键。
        auto& partial = head_map[key];
        partial.feat_h = tensor.feat_h;
        partial.feat_w = tensor.feat_w;
        if (tensor.num_cls == 4) {
            partial.reg = std::move(tensor.cls);
        } else {
            partial.cls = std::move(tensor.cls);
            partial.cls_c = tensor.num_cls;
        }
    }

    std::vector<Yolo26HeadTensor> heads;  // 最终 head 集合。
    heads.reserve(head_map.size());
    for (auto& pair : head_map) {
        auto& partial = pair.second;
        if (partial.reg.empty() || partial.cls.empty() || partial.cls_c <= 0) {
            continue;
        }
        Yolo26HeadTensor head;  // 待解码 head。
        head.feat_h = partial.feat_h;
        head.feat_w = partial.feat_w;
        head.num_cls = partial.cls_c;
        head.reg = std::move(partial.reg);
        head.cls = std::move(partial.cls);
        heads.push_back(std::move(head));
    }
    std::sort(heads.begin(), heads.end(), [](const Yolo26HeadTensor& a, const Yolo26HeadTensor& b) {
        return a.feat_h > b.feat_h;
    });

    postprocessor->run(heads, orig_w, orig_h, ratio_w, ratio_h, res);
    rknn_outputs_release(ctx, io_num.n_output, outputs.data());
}
