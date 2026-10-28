#include "../src/core/YouTubeClient.h"
#include "../src/player/PlayerController.h"
#include "../src/ui/PlayerControlsOverlay.h"
#include "../src/ui/PlayerWidget.h"
#include "../src/utils/AppSettings.h"

#include <QApplication>
#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace {

void testWorkerThreadQPointerAccessRace() {
    std::cout << "[RUN ] testWorkerThreadQPointerAccessRace (500 iterations)..." << std::endl;
    YouTubeClient client;

    for (int iter = 0; iter < 500; ++iter) {
        auto *player = new PlayerWidget();
        player->setYouTubeClient(&client);

        VideoItem item;
        item.id = "iter_" + std::to_string(iter);
        item.title = "Race Test Video";
        item.author = "Race Author";
        item.duration = "1:00";

        player->playVideo(item, "1080p");

        // Concurrently on multiple threads, query QPointer or let tasks run
        std::this_thread::yield();

        // Immediately delete while worker thread might be at line 764
        delete player;

        QCoreApplication::processEvents();
    }

    QThreadPool::globalInstance()->waitForDone(2000);
    QCoreApplication::processEvents();
    std::cout << "[PASS] testWorkerThreadQPointerAccessRace finished without crash." << std::endl;
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    testWorkerThreadQPointerAccessRace();
    return 0;
}
