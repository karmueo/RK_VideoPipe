#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <vector>

#include "rkbase.h"
#include "yolo26_post.h"

/**
 * @brief YOLO26 RKNN 推理模型封装。
 */
class YOLO26 : public RKBASE {
public:
    /**
     * @brief 构造 YOLO26 模型。
     * @param config YOLO26 配置。
     */
    explicit YOLO26(const YOLO26Config& config);

    /**
     * @brief 释放模型资源。
     */
    ~YOLO26();

    /**
     * @brief 加载 YOLO26 JSON 配置。
     * @param json_path 配置文件路径。
     * @param conf 输出配置。
     * @return int 0 成功，非 0 失败。
     */
    static int load_config(const std::string& json_path, YOLO26Config& conf);

    /**
     * @brief 单帧推理。
     * @param model_input_rgb 输入预处理后的 RGB 字节缓冲（NHWC uint8）。
     * @param orig_w 原始图宽度。
     * @param orig_h 原始图高度。
     * @param res 输出检测结果。
     */
    void run(const uint8_t* model_input_rgb, int orig_w, int orig_h, std::vector<DetectionResult>& res);

private:
    /**
     * @brief 把 RKNN 输出张量转换为 CHW 格式。
     * @param output RKNN 输出。
     * @param attr 张量属性。
     * @param tensor 输出 head 张量。
     * @return true 转换成功；false 转换失败。
     */
    static bool output_to_chw(const rknn_output& output, const rknn_tensor_attr& attr, Yolo26HeadTensor& tensor);

private:
    YOLO26Config config;  // YOLO26 运行配置。
    std::unique_ptr<Yolo26PostProcessor> postprocessor;  // 后处理对象。
};
