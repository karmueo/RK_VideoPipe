#pragma once

#include <string>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "nodes/base/vp_src_node.h"

namespace vp_nodes {
    // file source node, read video from local file.
    // example:
    // ../video/test.mp4
    class vp_file_src_node: public vp_src_node {
    private:
        /* data */
        std::string gst_template = "filesrc location=%s ! qtdemux ! h264parse ! %s ! videoconvert ! appsink";
        cv::VideoCapture file_capture;
    protected:
        // re-implemetation
        virtual void handle_run() override;
    public:
        vp_file_src_node(std::string node_name, 
                        int channel_index, 
                        std::string file_path, 
                        float resize_ratio = 1.0, 
                        bool cycle = true,
                        std::string gst_decoder_name = "avdec_h264",
                        int skip_interval = 0,
                        bool throttle_by_source_fps = true,
                        bool deep_copy_frame = true);
        ~vp_file_src_node();

        virtual std::string to_string() override;
        std::string file_path;
        bool cycle;
    
        // set avdec_h264 as the default decoder, we can use hardware decoder instead.
        std::string gst_decoder_name = "avdec_h264";
        // 0 means no skip
        int skip_interval = 0;
        // 是否按源视频 FPS 节奏限速，true 表示实时播放，false 表示尽可能快解码。
        bool throttle_by_source_fps = true;
        // 是否对输入帧做深拷贝，false 可提升吞吐但在少数后端上可能出现帧复用风险。
        bool deep_copy_frame = true;
    };

}
