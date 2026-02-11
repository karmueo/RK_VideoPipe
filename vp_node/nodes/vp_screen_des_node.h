

#pragma once

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include "base/vp_des_node.h"

namespace vp_nodes {
    // screen des node, display video on local window.
    class vp_screen_des_node: public vp_des_node
    {
    private:
        /* data */
        std::string gst_template_normal = "appsrc ! videoconvert ! videoscale ! textoverlay text=%s halignment=left valignment=top font-desc='Sans,16' shaded-background=true ! timeoverlay halignment=right valignment=top font-desc='Sans,16' shaded-background=true ! queue ! fpsdisplaysink video-sink=%s sync=false";
        std::string gst_template_fast = "appsrc ! queue leaky=downstream max-size-buffers=2 ! videoconvert ! queue leaky=downstream max-size-buffers=2 ! fpsdisplaysink text-overlay=false video-sink=%s sync=false";
        // 实际使用的 GStreamer 管线字符串。
        std::string gst_template;
        cv::VideoWriter screen_writer;
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
