#include <iostream>
#include <filesystem>
#include <cstring>
#include <csignal>
#include <atomic>
#include <thread>

// Global flag for signal handling
static std::atomic<bool> g_signal_received(false);

// Signal handler for SIGINT (Ctrl+C) and SIGTERM
void signal_handler(int signal) {
    g_signal_received = true;
}

// Include ALL node headers
#include "nodes/vp_mpp_file_src_node.h"        // NEW: MPP hardware decoder source
#include "nodes/vp_sdl2_des_node.h"         // NEW: SDL2 hardware renderer destination
#include "nodes/vp_file_src_node.h"
#include "nodes/vp_ffmpeg_src_node.h"
#include "nodes/vp_rk_rtsp_src_node.h"

#include "nodes/infer/vp_rk_first_yolo.h"
#include "nodes/infer/vp_rk_second_yolo.h"
#include "nodes/infer/vp_rk_second_cls.h"
#include "nodes/infer/vp_rk_second_rtmpose.h"

#include "nodes/vp_fake_des_node.h"
#include "nodes/vp_rtmp_des_node.h"
#include "nodes/vp_file_des_node.h"
#include "nodes/vp_screen_des_node.h"

#include "nodes/track/vp_sort_track_node.h"
#include "nodes/track/vp_byte_track_node.h"
#include "nodes/broker/vp_json_console_broker_node.h"

#include "nodes/osd/vp_osd_node.h"
#include "nodes/osd/vp_pose_osd_node.h"
#include "vp_utils/analysis_board/vp_analysis_board.h"

/*-------------------------------------------
                  Main Functions
-------------------------------------------*/

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -f <file>       Input video file (default: /mnt/nfs/datasets/video/uav.mp4)\n"
              << "  -m <mode>       Run mode:\n"
              << "                    0 = Full AI pipeline (default)\n"
              << "                    1 = High-performance decode+display (no AI)\n"
              << "  -v               Enable VSync in SDL2 (for mode 1)\n"
              << "  -D <driver>      SDL2 video driver (e.g., x11, wayland, kmsdrm)\n"
              << "  -R <driver>      SDL2 render driver (e.g., opengl, opengles2)\n"
              << "  -fps             Show FPS overlay\n"
              << "  -h               Show this help\n";
    std::cout << "\nPerformance comparison:\n"
              << "  Mode 0: Full AI pipeline with YOLO, tracking, pose estimation\n"
              << "  Mode 1: Pure hardware decode + display (max FPS, ~170 FPS achievable)\n";
}

int main(int argc, char** argv)
{
    // Default parameters
    const char* input_file = "/mnt/nfs/datasets/video/uav.mp4";
    int mode = 0;              // 0 = full AI pipeline, 1 = high-perf decode+display
    bool enable_vsync = false;
    bool show_fps = true;
    const char* sdl_video_driver = "";
    const char* sdl_render_driver = "";

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            input_file = argv[++i];
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            mode = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-v") == 0) {
            enable_vsync = true;
        } else if (strcmp(argv[i], "-D") == 0 && i + 1 < argc) {
            sdl_video_driver = argv[++i];
        } else if (strcmp(argv[i], "-R") == 0 && i + 1 < argc) {
            sdl_render_driver = argv[++i];
        } else if (strcmp(argv[i], "-fps") == 0) {
            show_fps = true;
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    VP_SET_LOG_INCLUDE_CODE_LOCATION(false);
    VP_SET_LOG_INCLUDE_THREAD_ID(false);
    VP_SET_LOG_LEVEL(vp_utils::INFO);
    VP_LOGGER_INIT();

    std::cout << "==========================================\n";
    std::cout << "RK_VideoPipe - Optimized MPP+SDL2\n";
    std::cout << "==========================================\n";
    std::cout << "Input file: " << input_file << "\n";
    std::cout << "Mode: " << (mode == 0 ? "Full AI Pipeline" : "High-Performance Decode+Display") << "\n";
    std::cout << "VSync: " << (enable_vsync ? "enabled" : "disabled (max throughput)") << "\n";
    std::cout << "==========================================\n\n";

    if (mode == 0) {
        /* ==========================================================
         * MODE 0: Full AI Pipeline (Original Configuration)
         * This mode includes YOLO detection, tracking, pose estimation,
         * classification, OSD, and message broker.
         * Expected FPS: 15-30 (depending on model complexity)
         * ========================================================== */
        std::cout << "Creating Full AI Pipeline...\n";

        // Use original vp_file_src_node with MPP decoder
        auto src_0 = std::make_shared<vp_nodes::vp_file_src_node>(
            "file_src_0", 0, input_file, 1.0, true, "mppvideodec");

        auto yolo_0 = std::make_shared<vp_nodes::vp_rk_first_yolo>("rk_yolo_0", "assets/configs/person.json");
        auto track_0 = std::make_shared<vp_nodes::vp_byte_track_node>("track_0");
        auto pose_0 = std::make_shared<vp_nodes::vp_rk_second_rtmpose>(
            "rk_rtmpose_0", "assets/configs/rtmpose.json", std::vector<int>{0});
        auto cls_0 = std::make_shared<vp_nodes::vp_rk_second_cls>(
            "rk_cls_0", "assets/configs/stand_sit.json", std::vector<int>{0});

        auto osd_0 = std::make_shared<vp_nodes::vp_osd_node>("osd_0");
        auto pose_osd_0 = std::make_shared<vp_nodes::vp_pose_osd_node>("pose_osd_0");
        auto msg_broker = std::make_shared<vp_nodes::vp_json_console_broker_node>("broker_0");

        // Use SDL2 destination for better performance
        auto des_0 = std::make_shared<vp_nodes::vp_sdl2_des_node>(
            "sdl2_des_0", 0, true, show_fps, enable_vsync,
            sdl_video_driver, sdl_render_driver);

        // Connect the pipeline
        yolo_0->attach_to({ src_0 });
        track_0->attach_to({ yolo_0 });
        cls_0->attach_to({ track_0 });
        pose_0->attach_to({ cls_0 });
        osd_0->attach_to({ pose_0 });
        pose_osd_0->attach_to({ osd_0 });
        msg_broker->attach_to({ pose_osd_0 });
        des_0->attach_to({ msg_broker });

        src_0->start();

        std::cout << "\nFull AI Pipeline Running...\n";
        std::cout << "Pipeline: File Src -> YOLO -> ByteTrack -> RTMPose -> Classification -> OSD -> Screen\n";
        std::cout << "Press ESC or close window to exit\n";
        std::cout << "Press Ctrl+C to exit at any time\n\n";

        // Register signal handlers
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // Run analysis board in non-blocking mode
        vp_utils::vp_analysis_board board({ src_0 });
        board.display(1, false);  // Non-blocking display

        // Wait for exit signal
        while (!g_signal_received && src_0->is_alive()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\nPipeline finished.\n";

    } else if (mode == 1) {
        /* ==========================================================
         * MODE 1: High-Performance Decode+Display (NEW)
         * This mode bypasses all AI processing for maximum FPS.
         * Direct path: MPP HW Decode -> SDL2 Display
         * Expected FPS: 150-170 (similar to mp4_hw_dec_sdl2 example)
         * ========================================================== */
        std::cout << "Creating High-Performance Decode+Display Pipeline...\n";

        // MPP hardware decoder source node
        // Uses FFmpeg for demuxing + MPP for decoding
        // Outputs NV12 format directly for zero-copy rendering
        auto src_0 = std::make_shared<vp_nodes::vp_mpp_file_src_node>(
            "mpp_src_0", 0, input_file, true);

        // SDL2 hardware renderer destination node
        // Renders NV12 frames directly using SDL2
        auto des_0 = std::make_shared<vp_nodes::vp_sdl2_des_node>(
            "sdl2_des_0", 0, false, show_fps, enable_vsync,
            sdl_video_driver, sdl_render_driver);

        // Direct connection: Source -> Destination (no intermediate nodes)
        des_0->attach_to({ src_0 });

        src_0->start();

        std::cout << "\nHigh-Performance Pipeline Running...\n";
        std::cout << "Pipeline: MPP File Src -> SDL2 Display\n";
        std::cout << "Target FPS: ~170 FPS (similar to mp4_hw_dec_sdl2)\n";
        std::cout << "\nFeatures:\n";
        std::cout << "  - Data flow visualization window (left) shows pipeline structure\n";
        std::cout << "  - SDL2 video display (right) renders decoded frames\n";
        std::cout << "  - FPS overlay displayed on video\n";
        std::cout << "\nExit options:\n";
        std::cout << "  - Press ESC in data flow window OR in SDL window\n";
        std::cout << "  - Press Ctrl+C to exit at any time\n";
        std::cout << "  - Program will auto-exit after video finishes\n\n";

        // Register signal handlers for Ctrl+C
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // Run analysis board in non-blocking mode
        // This allows us to check for exit conditions periodically
        vp_utils::vp_analysis_board board({ src_0 });
        board.display(1, false);  // Non-blocking display

        // Wait for any exit condition:
        // 1. Signal received (Ctrl+C)
        // 2. Video source finished (src_0->finished)
        // 3. ESC pressed in SDL window (des_0->is_alive() becomes false)
        // 4. ESC pressed in analysis board (handled internally by board)
        while (!g_signal_received && !src_0->finished && des_0->is_alive() && src_0->is_alive()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (g_signal_received) {
            std::cout << "\nSignal received, exiting...\n";
        } else if (src_0->finished) {
            std::cout << "\nVideo source finished, exiting...\n";
        } else if (!des_0->is_alive()) {
            std::cout << "\nSDL window closed, exiting...\n";
        } else {
            std::cout << "\nPipeline finished.\n";
        }
    }

    return 0;
}
