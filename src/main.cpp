#include "core/Process.h"
#include "core/StreamResolver.h"
#include "core/ThumbnailCache.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "utils/AppPaths.h"
#include "utils/AppSettings.h"
#include "utils/Logger.h"

#include <QApplication>
#include <QImageReader>
#include <QMessageBox>
#include <QPalette>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QThreadPool>
#include <csignal>
#include <exception>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <array>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <winsock2.h>

LONG WINAPI unhandledCrashHandler(EXCEPTION_POINTERS *pExceptionInfo) {
    DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388 || code == 0x40010006 || code == 0x00000000 || code == 0xE06D7363 || code == 0xE24C4A02 ||
        (code >= 0x00010000 && code < 0xC0000000)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void *addr = pExceptionInfo->ExceptionRecord->ExceptionAddress;
    HMODULE hExe = GetModuleHandleA(nullptr);
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    char buf[1024];
    int len = snprintf(buf, sizeof(buf),
                       "\n[FATAL EXCEPTION DETECTED]\n"
                       "  Executable Base: 0x%p\n"
                       "  Exception Code:  0x%08lX\n"
                       "  Fault Address:   0x%p\n",
                       (void *)hExe, code, addr);
    DWORD written = 0;
    WriteFile(hErr, buf, (DWORD)len, &written, nullptr);

    std::array<void *, 32> stack{};
    WORD frames = CaptureStackBackTrace(0, static_cast<DWORD>(stack.size()), stack.data(), nullptr);
    if (frames > 0) {
        const char header[] = "  Callstack:\n";
        WriteFile(hErr, header, sizeof(header) - 1, &written, nullptr);
        for (WORD i = 0; i < frames; ++i) {
            int frameLen = snprintf(buf, sizeof(buf), "    [%02u] 0x%p\n", i, stack[i]);
            WriteFile(hErr, buf, (DWORD)frameLen, &written, nullptr);
        }
    }
    FlushFileBuffers(hErr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
#include <unistd.h>
#endif

void terminateHandler() {
    const char msg[] = "[TERMINATE DETECTED] std::terminate was called!\n";
#ifdef _WIN32
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, sizeof(msg) - 1, &written, nullptr);
#else
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
#endif
    std::abort();
}

void signalHandler(int sig) {
    const char msg[] = "[SIGNAL DETECTED] Fatal signal received\n";
#ifdef _WIN32
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, sizeof(msg) - 1, &written, nullptr);
#else
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
#endif
    signal(sig, SIG_DFL);
    raise(sig);
}

void customQtMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    std::string text = msg.toUtf8().constData();
    switch (type) {
    case QtDebugMsg:
        LOG_DEBUG("[Qt] {}", text);
        break;
    case QtInfoMsg:
        LOG_INFO("[Qt] {}", text);
        break;
    case QtWarningMsg:
        LOG_WARN("[Qt] {}", text);
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        LOG_ERROR("[Qt] {}", text);
        break;
    }
}

void attachOutputToConsole() {
#ifdef _WIN32
    SetUnhandledExceptionFilter(unhandledCrashHandler);
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
#endif
    std::ios::sync_with_stdio(true);
}

int main(int argc, char *argv[]) {
    attachOutputToConsole();
    lightyt::Process::initProcessGroup();

    // Determine log level precedence: CLI flags > LIGHTYT_LOG env > AppSettings > Warn default
    bool cliLevelSet = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            lightyt::log::Logger::instance().setLevel(lightyt::log::Level::Debug);
            cliLevelSet = true;
        } else if (arg == "--trace" || arg == "-vv") {
            lightyt::log::Logger::instance().setLevel(lightyt::log::Level::Trace);
            cliLevelSet = true;
        } else if (arg == "--quiet" || arg == "-q") {
            lightyt::log::Logger::instance().setLevel(lightyt::log::Level::Error);
            cliLevelSet = true;
        }
    }

    if (!cliLevelSet) {
        const char *env = std::getenv("LIGHTYT_LOG");
        if (env != nullptr && env[0] != '\0') {
            lightyt::log::Logger::instance().setLevel(lightyt::log::stringToLevel(env, lightyt::log::Level::Warn));
        } else {
            QString savedLevel = AppSettings::instance().getLogLevel();
            lightyt::log::Logger::instance().setLevel(
                lightyt::log::stringToLevel(savedLevel.toStdString(), lightyt::log::Level::Warn));
        }
    }

    qInstallMessageHandler(customQtMessageHandler);
    std::set_terminate(terminateHandler);
    signal(SIGABRT, signalHandler);
#ifndef _WIN32
    signal(SIGSEGV, signalHandler);
#endif
    signal(SIGILL, signalHandler);
    signal(SIGFPE, signalHandler);

    LOG_INFO("Starting QTYT (Qt v{}, Compiled Qt v{})", qVersion(), QT_VERSION_STR);

    try {
        // Request OpenGL 3.3 Core Profile for hardware-accelerated video rendering via mpv
        QSurfaceFormat glFormat;
        glFormat.setVersion(3, 3);
        glFormat.setProfile(QSurfaceFormat::CoreProfile);
        glFormat.setRenderableType(QSurfaceFormat::OpenGL);
        glFormat.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
        QSurfaceFormat::setDefaultFormat(glFormat);

        QApplication app(argc, argv);

        // Prune stale disk cache entries asynchronously on startup via thread pool
        QThreadPool::globalInstance()->start([]() { ThumbnailCache::instance().prune(); });

        // Ensure image format plugins (qjpeg, qwebp, etc.) are discovered from application directory
        QString appDir = QApplication::applicationDirPath();
        QCoreApplication::addLibraryPath(appDir);
        QCoreApplication::addLibraryPath(appDir + "/imageformats");
        QCoreApplication::addLibraryPath(appDir + "/plugins");

        QStringList supportedFormats;
        for (const auto &fmt : QImageReader::supportedImageFormats()) {
            supportedFormats.append(QString::fromUtf8(fmt));
        }
        LOG_INFO("Supported Qt image formats: {}", supportedFormats.join(", ").toUtf8().constData());

        // Use Fusion style across platforms so QSS border-radius and colors render identically on Windows & Linux
        if (auto *fusionStyle = QStyleFactory::create("Fusion")) {
            app.setStyle(fusionStyle);
        }

        // Apply clean YouTube dark palette
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(15, 15, 15));
        darkPalette.setColor(QPalette::WindowText, QColor(241, 241, 241));
        darkPalette.setColor(QPalette::Base, QColor(18, 18, 18));
        darkPalette.setColor(QPalette::AlternateBase, QColor(33, 33, 33));
        darkPalette.setColor(QPalette::ToolTipBase, QColor(40, 40, 40));
        darkPalette.setColor(QPalette::ToolTipText, QColor(241, 241, 241));
        darkPalette.setColor(QPalette::Text, QColor(241, 241, 241));
        darkPalette.setColor(QPalette::Button, QColor(39, 39, 39));
        darkPalette.setColor(QPalette::ButtonText, QColor(241, 241, 241));
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, QColor(62, 166, 255));
        darkPalette.setColor(QPalette::Highlight, QColor(62, 166, 255));
        darkPalette.setColor(QPalette::HighlightedText, QColor(15, 15, 15));
        app.setPalette(darkPalette);
        app.setStyleSheet(lightyt::ui::theme::GlobalStyleSheet);

        MainWindow window;
        window.show();

        const int ret = app.exec();
        StreamResolver::shutdownPrefetch();
        return ret;
    } catch (const std::exception &e) {
        LOG_ERROR("Startup Exception: {}", e.what());
        QMessageBox::critical(nullptr, "QTYT Fatal Error", QString("Startup Exception: %1").arg(e.what()));
        return 1;
    }
}
