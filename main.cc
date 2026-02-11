/*
 * RK_VideoPipe 主程序（MPP 解码直达 SDL 显示）
 */

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "nodes/vp_fake_des_node.h"
#include "nodes/vp_mpp_sdl_src_node.h"

/**
 * @brief 解析可选命令行参数。
 *
 * 参数约定：
 * 1) argv[1]: 输入视频路径（可选）
 * 2) argv[2]: SDL 视频驱动（可选，如 x11/wayland/kmsdrm）
 * 3) argv[3]: SDL 渲染驱动（可选，如 opengl/opengles2）
 *
 * @param argc 参数个数。
 * @param argv 参数数组。
 * @param file_path 输出视频路径。
 * @param sdl_video_driver 输出 SDL 视频驱动。
 * @param sdl_render_driver 输出 SDL 渲染驱动。
 * @param render_to_screen 输出是否渲染到屏幕。
 */
static void parse_args(int argc,
                       char** argv,
                       std::string& file_path,
                       std::string& sdl_video_driver,
                       std::string& sdl_render_driver,
                       bool& render_to_screen) {
    if (argc > 1 && argv[1] != nullptr) {
        file_path = argv[1];
    }
    if (argc > 2 && argv[2] != nullptr) {
        sdl_video_driver = argv[2];
    }
    if (argc > 3 && argv[3] != nullptr) {
        sdl_render_driver = argv[3];
    }
    if (argc > 4 && argv[4] != nullptr) {
        const std::string render_arg = argv[4];
        if (render_arg == "0" || render_arg == "false" || render_arg == "off") {
            render_to_screen = false;
        } else if (render_arg == "1" || render_arg == "true" || render_arg == "on") {
            render_to_screen = true;
        }
    }
}

int main(int argc, char** argv) {
    VP_SET_LOG_INCLUDE_CODE_LOCATION(false);
    VP_SET_LOG_INCLUDE_THREAD_ID(false);
    VP_SET_LOG_LEVEL(vp_utils::INFO);
    VP_LOGGER_INIT();

    // 默认输入视频路径。
    std::string file_path = "/mnt/nfs/datasets/video/uav.mp4";
    // SDL 视频驱动（默认自动选择）。
    std::string sdl_video_driver = "";
    // SDL 渲染驱动（默认自动选择）。
    std::string sdl_render_driver = "";
    // 是否渲染到屏幕（默认开启）。
    bool render_to_screen = true;
    parse_args(argc, argv, file_path, sdl_video_driver, sdl_render_driver, render_to_screen);

    VP_INFO(vp_utils::string_format("[main] file=%s sdl_video_driver=%s sdl_render_driver=%s render=%d",
                                    file_path.c_str(),
                                    sdl_video_driver.empty() ? "auto" : sdl_video_driver.c_str(),
                                    sdl_render_driver.empty() ? "auto" : sdl_render_driver.c_str(),
                                    render_to_screen ? 1 : 0));

    // MPP+SDL 源节点（吞吐优先）。
    auto src_0 = std::make_shared<vp_nodes::vp_mpp_sdl_src_node>(
        "file_src_0",
        0,
        file_path,
        true,               // 循环播放。
        false,              // 不按源 FPS 节奏限速。
        false,              // 关闭 vsync。
        true,               // 关闭 overlay。
        false,              // 不启用全屏，保持与 mp4_hw_dec_sdl2 默认一致。
        render_to_screen,   // 是否开启屏幕渲染。
        false,              // 不向下游发布 frame_meta。
        sdl_video_driver,   // SDL 视频驱动。
        sdl_render_driver   // SDL 渲染驱动。
    );

    // 占位目标节点（满足 pipeline 以 DES 结束）。
    auto des_0 = std::make_shared<vp_nodes::vp_fake_des_node>("fake_des_0", 0);
    des_0->attach_to({src_0});

    src_0->start();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
