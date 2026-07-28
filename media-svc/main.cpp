#include "core/MediaServer.h"
#include "utils/Logger.h"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        std::string configPath = "configs/media-svc.ini";
        if (argc > 1) configPath = argv[1];

        Logger::init(configPath);
        LOG_INFO("media-svc starting...");

        MediaServer server(configPath);
        server.run();
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[main] Fatal: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
