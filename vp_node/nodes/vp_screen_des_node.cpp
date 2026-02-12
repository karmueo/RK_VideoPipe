
#include "vp_screen_des_node.h"
#include "vp_utils/vp_utils.h"
#include <atomic>
#include <vector>

namespace vp_nodes {
    // screen_des 的全局退出请求标志。
    static std::atomic<bool> g_screen_des_exit_requested{false};

    bool vp_screen_des_should_exit() {
        return g_screen_des_exit_requested.load();
    }

    void vp_screen_des_reset_exit_flag() {
        g_screen_des_exit_requested.store(false);
    }

    vp_screen_des_node::vp_screen_des_node(std::string node_name, 
                                            int channel_index, 
                                            bool osd,
                                            vp_objects::vp_size display_w_h,
                                            bool fast_mode,
                                            std::string video_sink):
                                            vp_des_node(node_name, channel_index),
                                            osd(osd),
                                            display_w_h(display_w_h),
                                            fast_mode(fast_mode),
                                            video_sink(video_sink) {
        if (this->video_sink == "opencv" || this->video_sink == "imshow") {
            use_opencv_window = true;
        }
        if (this->fast_mode) {
            this->gst_template = vp_utils::string_format(this->gst_template_fast, this->video_sink.c_str());
        }
        else {
            this->gst_template = vp_utils::string_format(this->gst_template_normal, node_name.c_str(), this->video_sink.c_str());
        }
        VP_INFO(vp_utils::string_format("[%s] [%s]", node_name.c_str(), gst_template.c_str()));
        this->initialized();
    }
    
    vp_screen_des_node::~vp_screen_des_node() {
        // 注意：不在析构中主动 destroyWindow，避免在退出竞态阶段触发高 GUI 后端崩溃。
        opencv_window_inited = false;
        opencv_exit_requested = false;
        deinitialized();
    }

    // re-implementation, return nullptr.
    std::shared_ptr<vp_objects::vp_meta> 
        vp_screen_des_node::handle_frame_meta(std::shared_ptr<vp_objects::vp_frame_meta> meta) {
            VP_DEBUG(vp_utils::string_format("[%s] received frame meta, channel_index=>%d, frame_index=>%d", node_name.c_str(), meta->channel_index, meta->frame_index));
            
            cv::Mat resize_frame;
            if (this->display_w_h.width != 0 && this->display_w_h.height != 0) {                 
                cv::resize((osd && !meta->osd_frame.empty()) ? meta->osd_frame : meta->frame, resize_frame, cv::Size(display_w_h.width, display_w_h.height));
            }
            else {
                resize_frame = (osd && !meta->osd_frame.empty()) ? meta->osd_frame : meta->frame;
            }

            if (use_opencv_window) {
                if (opencv_exit_requested) {
                    return vp_des_node::handle_frame_meta(meta);
                }
                if (!opencv_window_inited) {
                    cv::namedWindow(node_name, cv::WINDOW_NORMAL);
                    opencv_window_inited = true;
                    VP_INFO(vp_utils::string_format("[%s] open opencv window success.", node_name.c_str()));
                }

                // OpenCV 显示限帧：超过阈值的帧直接跳过显示，避免显示端拖慢整条处理链路。
                if (opencv_display_fps_limit > 0) {
                    const auto now_tp = std::chrono::steady_clock::now();
                    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_tp - opencv_last_display_tp).count();
                    const int min_interval_ms = 1000 / opencv_display_fps_limit;
                    if (elapsed_ms < min_interval_ms) {
                        return vp_des_node::handle_frame_meta(meta);
                    }
                    opencv_last_display_tp = now_tp;
                }

                cv::imshow(node_name, resize_frame);
                const int key = cv::waitKey(1) & 0xFF;  // 当前按键。
                if (key == 27) {
                    VP_INFO(vp_utils::string_format("[%s] ESC detected in opencv window, request exit.", node_name.c_str()));
                    opencv_exit_requested = true;
                    g_screen_des_exit_requested.store(true);
                    return vp_des_node::handle_frame_meta(meta);
                }
                return vp_des_node::handle_frame_meta(meta);
            }

            if (!screen_writer.isOpened()) {
                const double fps = (meta->fps > 0) ? static_cast<double>(meta->fps) : 25.0;  // 输出 FPS。
                const cv::Size frame_size{resize_frame.cols, resize_frame.rows};  // 输出分辨率。
                std::vector<std::string> candidates;  // 候选管线列表。
                candidates.push_back(this->gst_template);
                candidates.push_back(vp_utils::string_format(this->gst_template_fast, this->video_sink.c_str()));
                candidates.push_back(vp_utils::string_format(this->gst_template_fallback, this->video_sink.c_str()));
                candidates.push_back("appsrc ! videoconvert ! queue ! autovideosink");

                for (auto& candidate : candidates) {
                    if (candidate.empty()) {
                        continue;
                    }
                    if (screen_writer.open(candidate, cv::CAP_GSTREAMER, 0, fps, frame_size)) {
                        opened_gst_template = candidate;
                        VP_INFO(vp_utils::string_format("[%s] open screen writer success: [%s], fps=%.2f, size=%dx%d",
                                                        node_name.c_str(),
                                                        opened_gst_template.c_str(),
                                                        fps,
                                                        frame_size.width,
                                                        frame_size.height));
                        break;
                    }
                }
                if (!screen_writer.isOpened()) {
                    if (!open_failed_logged) {
                        VP_ERROR(vp_utils::string_format("[%s] open screen writer failed. sink=%s, fps=%.2f, size=%dx%d, first_pipeline=[%s]",
                                                         node_name.c_str(),
                                                         this->video_sink.c_str(),
                                                         fps,
                                                         frame_size.width,
                                                         frame_size.height,
                                                         this->gst_template.c_str()));
                        open_failed_logged = true;
                    }
                    // 打不开显示时直接丢帧，后续继续重试 open。
                    return vp_des_node::handle_frame_meta(meta);
                }
            }
            screen_writer.write(resize_frame);

            // for general works defined in base class
            return vp_des_node::handle_frame_meta(meta);
    }

    // re-implementation, return nullptr.
    std::shared_ptr<vp_objects::vp_meta> 
        vp_screen_des_node::handle_control_meta(std::shared_ptr<vp_objects::vp_control_meta> meta) {
            // for general works defined in base class
            return vp_des_node::handle_control_meta(meta);
    }
}
