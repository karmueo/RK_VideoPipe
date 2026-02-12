

#pragma once

#include <opencv2/core/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <chrono>
#include "base/vp_des_node.h"

namespace vp_nodes {
    /**
     * @brief 查询 screen_des 是否请求退出（由 OpenCV 窗口 ESC/关闭触发）。
     * @return true 请求退出；false 未请求。
     */
    bool vp_screen_des_should_exit();

    /**
     * @brief 重置 screen_des 退出请求标志。
     */
    void vp_screen_des_reset_exit_flag();

    // screen des node, display video on local window.
    class vp_screen_des_node: public vp_des_node
    {
    private:
        /* data */
        std::string gst_template_normal = "appsrc ! videoconvert ! videoscale ! textoverlay text=%s halignment=left valignment=top font-desc='Sans,16' shaded-background=true ! timeoverlay halignment=right valignment=top font-desc='Sans,16' shaded-background=true ! queue ! fpsdisplaysink video-sink=%s sync=false";
        std::string gst_template_fast = "appsrc ! queue leaky=downstream max-size-buffers=2 ! videoconvert ! queue leaky=downstream max-size-buffers=2 ! fpsdisplaysink text-overlay=false video-sink=%s sync=false";
        // 降级使用的最简显示管线（直接走 sink）。
        std::string gst_template_fallback = "appsrc ! videoconvert ! videoscale ! queue ! %s";
        // 实际使用的 GStreamer 管线字符串。
        std::string gst_template;
        // 实际打开成功的 GStreamer 管线字符串。
        std::string opened_gst_template;
        cv::VideoWriter screen_writer;
        // 是否已经打印过 open 失败日志，避免刷屏。
        bool open_failed_logged = false;
        // 是否使用 OpenCV imshow 显示。
        bool use_opencv_window = false;
        // OpenCV 窗口是否已初始化。
        bool opencv_window_inited = false;
        // OpenCV 窗口是否已请求退出。
        bool opencv_exit_requested = false;
        // OpenCV 显示限帧（0 表示不限速）。
        int opencv_display_fps_limit = 30;
        // 上次 OpenCV 实际显示时间点。
        std::chrono::steady_clock::time_point opencv_last_display_tp = std::chrono::steady_clock::now();
    protected:
        // re-implementation, return nullptr.
        virtual std::shared_ptr<vp_objects::vp_meta> handle_frame_meta(std::shared_ptr<vp_objects::vp_frame_meta> meta) override; 
        // re-implementation, return nullptr.
        virtual std::shared_ptr<vp_objects::vp_meta> handle_control_meta(std::shared_ptr<vp_objects::vp_control_meta> meta) override;
    public:
        vp_screen_des_node(std::string node_name, 
                            int channel_index, 
                            bool osd = true,
                            vp_objects::vp_size display_w_h = {},
                            bool fast_mode = false,
                            std::string video_sink = "ximagesink");
        ~vp_screen_des_node();

        // for osd frame
        bool osd;
        // display size
        vp_objects::vp_size display_w_h;
        // 是否开启快速显示模式（关闭 overlay，减少显示链路开销）。
        bool fast_mode = false;
        // 显示 sink 名称，默认 ximagesink，可按平台改为 xvimagesink/kmssink。
        std::string video_sink = "ximagesink";
    };
}
