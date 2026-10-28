#include "CookieDialog.h"

#include "../utils/AppPaths.h"
#include "../utils/AppSettings.h"
#include "../utils/Logger.h"
#include "../utils/StringUtils.h"

#include <QApplication>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QTabWidget>
#include <QThreadPool>
#include <QVBoxLayout>
#include <filesystem>

CookieDialog::CookieDialog(std::shared_ptr<CookieManager> cookieManager, YouTubeClient *client, QWidget *parent)
    : QDialog(parent), m_cookieManager(std::move(cookieManager)), m_client(client) {
    setupUi();
    updateStatusDisplay();
    updateActiveChannelDisplay();
}

void CookieDialog::cancelActiveWorker() {
    if (m_activeCancelToken) {
        m_activeCancelToken->store(true, std::memory_order_relaxed);
        m_activeCancelToken.reset();
    }
}

CookieDialog::~CookieDialog() {
    cancelActiveWorker();
}

void CookieDialog::closeEvent(QCloseEvent *event) {
    cancelActiveWorker();
    QDialog::closeEvent(event);
}

void CookieDialog::done(int r) {
    cancelActiveWorker();
    QDialog::done(r);
}

void CookieDialog::setupUi() {
    setWindowTitle("YouTube Account & Settings");
    resize(600, 580);
    setMinimumSize(540, 520);
    setStyleSheet(
        "QDialog { background-color: #0f0f0f; color: #f1f1f1; }"
        "QLabel { color: #aaaaaa; font-size: 12px; }"
        "QPushButton {"
        "  background-color: #272727; color: #f1f1f1; border: 1px solid rgba(255,255,255,0.1);"
        "  min-height: 32px; max-height: 32px; padding: 0px 16px; border-radius: 16px; font-weight: 600; font-size: "
        "12px;"
        "}"
        "QPushButton:hover { background-color: #3f3f3f; }"
        "QPushButton:disabled { background-color: #1f1f1f; color: #717171; border-color: rgba(255,255,255,0.05); }"
        "QTextEdit {"
        "  background-color: #121212; color: #4ade80; border: 1px solid #383838; font-family: monospace; font-size: "
        "11px; border-radius: 8px; padding: 8px;"
        "}"
        "QTextEdit:focus { border: 1px solid #3ea6ff; }"
        "QComboBox, QLineEdit {"
        "  background-color: #121212; color: #f1f1f1; border: 1px solid #383838; border-radius: 8px; min-height: 30px; "
        "padding: 2px 10px; font-size: 12px;"
        "}"
        "QComboBox:focus, QLineEdit:focus { border: 1px solid #3ea6ff; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView {"
        "  background-color: #212121; color: #ffffff; selection-background-color: #3ea6ff; selection-color: #000000; "
        "border: 1px solid #383838; border-radius: 8px; padding: 4px; outline: none;"
        "}"
        "QTabWidget::pane { border: 1px solid #272727; background-color: #212121; border-radius: 12px; }"
        "QTabBar::tab {"
        "  background-color: transparent; color: #aaaaaa; padding: 10px 18px; border-bottom: 2px solid transparent; "
        "margin-right: 4px; font-weight: 500; font-size: 13px;"
        "}"
        "QTabBar::tab:selected { background-color: #212121; color: #f1f1f1; border-bottom: 2px solid #f1f1f1; "
        "font-weight: 600; border-top-left-radius: 8px; border-top-right-radius: 8px; }"
        "QTabBar::tab:hover:!selected { color: #ffffff; background-color: rgba(255,255,255,0.05); "
        "border-top-left-radius: 8px; border-top-right-radius: 8px; }");

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(14);

    m_tabWidget = new QTabWidget(this);

    // ==========================================
    // TAB 1: Channel & Profile Switcher
    // ==========================================
    auto *channelTab = new QWidget(m_tabWidget);
    auto *channelLayout = new QVBoxLayout(channelTab);
    channelLayout->setContentsMargins(16, 16, 16, 16);
    channelLayout->setSpacing(12);

    auto *channelDesc = new QLabel(
        "Choose which channel or Google account profile to use for recommendations and subscriptions.\n"
        "If you have multiple channels (Brand Accounts) or multiple Google accounts logged in, configure them below:",
        channelTab);
    channelDesc->setWordWrap(true);
    channelDesc->setStyleSheet("color: #aaaaaa; line-height: 130%;");
    channelLayout->addWidget(channelDesc);

    // Detected channels group
    auto *detectGroup = new QGroupBox("Detected Account Channels", channelTab);
    detectGroup->setStyleSheet("QGroupBox { font-weight: bold; color: #f1f1f1; border: 1px solid #272727; "
                               "border-radius: 8px; background-color: #171717; margin-top: 10px; padding-top: 14px; }");
    auto *detectLayout = new QVBoxLayout(detectGroup);
    detectLayout->setSpacing(8);

    auto *detectRow = new QHBoxLayout();
    m_channelCombo = new QComboBox(detectGroup);
    m_channelCombo->addItem("Default Channel (Session Default)");
    detectRow->addWidget(m_channelCombo, 1);

    auto *quickApplyBtn = new QPushButton("Apply", detectGroup);
    quickApplyBtn->setCursor(Qt::PointingHandCursor);
    quickApplyBtn->setFixedHeight(34);
    connect(quickApplyBtn, &QPushButton::clicked, this, &CookieDialog::onApplyChannelClicked);
    detectRow->addWidget(quickApplyBtn);

    m_detectChannelsBtn = new QPushButton("Detect Channels", detectGroup);
    m_detectChannelsBtn->setCursor(Qt::PointingHandCursor);
    m_detectChannelsBtn->setFixedHeight(34);
    m_detectChannelsBtn->setStyleSheet(
        "QPushButton { background-color: #3ea6ff; color: #0f0f0f; font-weight: 600; border: 1px solid #3ea6ff; "
        "min-height: 32px; max-height: 32px; padding: 0px 16px; border-radius: 16px; }"
        "QPushButton:hover { background-color: #65b8ff; border-color: #65b8ff; }");
    connect(m_detectChannelsBtn, &QPushButton::clicked, this, &CookieDialog::onDetectChannelsClicked);
    detectRow->addWidget(m_detectChannelsBtn);
    detectLayout->addLayout(detectRow);

    m_detectStatusLabel =
        new QLabel("Click 'Detect Channels' to scan associated channels from your account.", detectGroup);
    m_detectStatusLabel->setStyleSheet("color: #888888; font-size: 11px;");
    m_detectStatusLabel->setWordWrap(true);
    detectLayout->addWidget(m_detectStatusLabel);
    channelLayout->addWidget(detectGroup);

    connect(m_channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CookieDialog::onChannelComboIndexChanged);

    // Manual / Advanced settings group
    auto *manualGroup = new QGroupBox("Channel & Account Parameters", channelTab);
    manualGroup->setStyleSheet("QGroupBox { font-weight: bold; color: #f1f1f1; border: 1px solid #272727; "
                               "border-radius: 8px; background-color: #171717; margin-top: 10px; padding-top: 14px; }");
    auto *manualLayout = new QVBoxLayout(manualGroup);
    manualLayout->setSpacing(10);

    // Account slot index
    auto *slotRow = new QHBoxLayout();
    auto *slotLabel = new QLabel("Google Account Slot:", manualGroup);
    slotLabel->setFixedWidth(150);
    slotRow->addWidget(slotLabel);

    m_accountIndexCombo = new QComboBox(manualGroup);
    m_accountIndexCombo->addItem("Default / Cookie (No X-Goog-AuthUser)", -1);
    m_accountIndexCombo->addItem("Account 0 (First Google Account)", 0);
    m_accountIndexCombo->addItem("Account 1 (Second Google Account)", 1);
    m_accountIndexCombo->addItem("Account 2 (Third Google Account)", 2);
    m_accountIndexCombo->addItem("Account 3 (Fourth Google Account)", 3);
    m_accountIndexCombo->addItem("Account 4 (Fifth Google Account)", 4);
    m_accountIndexCombo->setToolTip(
        "Select Account 1 if your YouTube account is the secondary Google login in your browser.");
    slotRow->addWidget(m_accountIndexCombo, 1);
    manualLayout->addLayout(slotRow);

    // Brand Page ID
    auto *pageIdRow = new QHBoxLayout();
    auto *pageIdLabel = new QLabel("Brand Account Page ID:", manualGroup);
    pageIdLabel->setFixedWidth(150);
    pageIdRow->addWidget(pageIdLabel);

    m_pageIdEdit = new QLineEdit(manualGroup);
    m_pageIdEdit->setPlaceholderText("e.g. 104958372619485720194 (Leave blank for regular account)");
    m_pageIdEdit->setToolTip("Brand Accounts require a Page ID for InnerTube requests.");
    pageIdRow->addWidget(m_pageIdEdit, 1);
    manualLayout->addLayout(pageIdRow);

    // Channel name
    auto *nameRow = new QHBoxLayout();
    auto *nameLabel = new QLabel("Channel Name / Label:", manualGroup);
    nameLabel->setFixedWidth(150);
    nameRow->addWidget(nameLabel);

    m_channelNameEdit = new QLineEdit(manualGroup);
    m_channelNameEdit->setPlaceholderText("e.g. Main YouTube Channel");
    nameRow->addWidget(m_channelNameEdit, 1);
    manualLayout->addLayout(nameRow);

    channelLayout->addWidget(manualGroup);

    connect(m_pageIdEdit, &QLineEdit::textEdited, this, [this]() {
        m_userHasSelectedChannel = true;
        updateActiveChannelDisplay();
    });
    connect(m_channelNameEdit, &QLineEdit::textEdited, this, [this]() {
        m_userHasSelectedChannel = true;
        updateActiveChannelDisplay();
    });
    connect(m_accountIndexCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_userHasSelectedChannel = true;
        updateActiveChannelDisplay();
    });

    // Current active display
    m_currentActiveLabel = new QLabel(channelTab);
    m_currentActiveLabel->setStyleSheet("padding: 10px 14px; background-color: #171717; border: 1px solid #272727; "
                                        "border-radius: 8px; font-size: 12px;");
    m_currentActiveLabel->setWordWrap(true);
    channelLayout->addWidget(m_currentActiveLabel);

    channelLayout->addStretch();

    auto *channelBtnRow = new QHBoxLayout();
    channelBtnRow->addStretch();

    m_applyChannelBtn = new QPushButton("Apply & Reload Feed", channelTab);
    m_applyChannelBtn->setCursor(Qt::PointingHandCursor);
    m_applyChannelBtn->setFixedHeight(34);
    m_applyChannelBtn->setStyleSheet(
        "QPushButton { background-color: #f1f1f1; color: #0f0f0f; font-weight: 600; border: 1px solid #f1f1f1; "
        "min-height: 32px; max-height: 32px; padding: 0px 20px; border-radius: 16px; }"
        "QPushButton:hover { background-color: #d9d9d9; border-color: #d9d9d9; }");
    connect(m_applyChannelBtn, &QPushButton::clicked, this, &CookieDialog::onApplyChannelClicked);
    channelBtnRow->addWidget(m_applyChannelBtn);

    channelLayout->addLayout(channelBtnRow);

    m_tabWidget->addTab(channelTab, "Channel / Profile");

    // ==========================================
    // TAB 2: Cookies (Authentication)
    // ==========================================
    auto *cookiesTab = new QWidget(m_tabWidget);
    auto *cookiesLayout = new QVBoxLayout(cookiesTab);
    cookiesLayout->setContentsMargins(16, 16, 16, 16);
    cookiesLayout->setSpacing(12);

    auto *instructions = new QLabel("To sync your YouTube account recommendations without running a browser:\n\n"
                                    "1. Open your browser and log into youtube.com\n"
                                    "2. Export cookies via an extension (e.g. 'Get cookies.txt LOCALLY')\n"
                                    "3. Import the file below, or paste the Netscape cookies directly.",
                                    cookiesTab);
    instructions->setWordWrap(true);
    instructions->setStyleSheet("color: #aaaaaa; line-height: 130%;");
    cookiesLayout->addWidget(instructions);

    m_statusLabel = new QLabel(cookiesTab);
    m_statusLabel->setStyleSheet("padding: 8px; border-radius: 6px; font-weight: bold;");
    cookiesLayout->addWidget(m_statusLabel);

    auto *fileBtnLayout = new QHBoxLayout();
    auto *importBtn = new QPushButton("Import cookies.txt File...", cookiesTab);
    importBtn->setCursor(Qt::PointingHandCursor);
    importBtn->setFixedHeight(34);
    connect(importBtn, &QPushButton::clicked, this, &CookieDialog::onImportFileClicked);
    fileBtnLayout->addWidget(importBtn);

    auto *clearBtn = new QPushButton("Clear Cookies", cookiesTab);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setFixedHeight(34);
    clearBtn->setStyleSheet(
        "QPushButton { background-color: #272727; color: #f87171; font-weight: 600; border: 1px solid "
        "rgba(248,113,113,0.25); min-height: 32px; max-height: 32px; padding: 0px 16px; border-radius: 16px; }"
        "QPushButton:hover { background-color: #382424; }");
    connect(clearBtn, &QPushButton::clicked, this, &CookieDialog::onClearClicked);
    fileBtnLayout->addWidget(clearBtn);

    cookiesLayout->addLayout(fileBtnLayout);

    auto *pasteLabel = new QLabel("Or paste Netscape cookie content directly:", cookiesTab);
    cookiesLayout->addWidget(pasteLabel);

    m_cookieTextEdit = new QTextEdit(cookiesTab);
    m_cookieTextEdit->setPlaceholderText("# Netscape HTTP Cookie File\n# Paste .youtube.com cookies here...");
    cookiesLayout->addWidget(m_cookieTextEdit);

    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();

    m_saveButton = new QPushButton("Save & Apply Cookies", cookiesTab);
    m_saveButton->setCursor(Qt::PointingHandCursor);
    m_saveButton->setFixedHeight(34);
    m_saveButton->setStyleSheet(
        "QPushButton { background-color: #f1f1f1; color: #0f0f0f; font-weight: 600; border: 1px solid #f1f1f1; "
        "min-height: 32px; max-height: 32px; padding: 0px 20px; border-radius: 16px; }"
        "QPushButton:hover { background-color: #d9d9d9; border-color: #d9d9d9; }");
    connect(m_saveButton, &QPushButton::clicked, this, &CookieDialog::onSaveClicked);
    bottomLayout->addWidget(m_saveButton);

    cookiesLayout->addLayout(bottomLayout);

    m_tabWidget->addTab(cookiesTab, "Cookies (cookies.txt)");

    // ==========================================
    // TAB 3: Preferences / App Settings
    // ==========================================
    auto *prefTab = new QWidget(m_tabWidget);
    auto *prefLayout = new QVBoxLayout(prefTab);
    prefLayout->setContentsMargins(16, 16, 16, 16);
    prefLayout->setSpacing(16);

    // Group 1: Logging Configuration
    auto *logGroup = new QGroupBox("Application Logging", prefTab);
    logGroup->setStyleSheet("QGroupBox { font-weight: bold; color: #f1f1f1; border: 1px solid #272727; "
                            "border-radius: 8px; background-color: #171717; margin-top: 10px; padding-top: 14px; }");
    auto *logGroupLayout = new QVBoxLayout(logGroup);
    logGroupLayout->setSpacing(10);

    auto *logDescLabel = new QLabel(
        "Control console and diagnostic verbosity. Setting to 'Warning' minimizes I/O and CPU overhead.",
        logGroup);
    logDescLabel->setWordWrap(true);
    logDescLabel->setStyleSheet("color: #aaaaaa; font-size: 11px;");
    logGroupLayout->addWidget(logDescLabel);

    auto *logRow = new QHBoxLayout();
    auto *logLabel = new QLabel("Log Level:", logGroup);
    logLabel->setStyleSheet("color: #f1f1f1; font-weight: 500;");
    m_logLevelCombo = new QComboBox(logGroup);
    m_logLevelCombo->addItem("Warning (Default - Errors & warnings only)", "warn");
    m_logLevelCombo->addItem("Info (Standard application logs)", "info");
    m_logLevelCombo->addItem("Debug (Detailed diagnostic logs)", "debug");
    m_logLevelCombo->addItem("Trace (Exhaustive packet & state tracing)", "trace");
    m_logLevelCombo->addItem("Error (Critical failures only)", "error");
    m_logLevelCombo->addItem("Off (Mute all logging)", "none");
    logRow->addWidget(logLabel);
    logRow->addWidget(m_logLevelCombo, 1);
    logGroupLayout->addLayout(logRow);

    m_logLevelDesc = new QLabel(logGroup);
    m_logLevelDesc->setWordWrap(true);
    m_logLevelDesc->setStyleSheet("color: #3ea6ff; font-size: 11px;");
    logGroupLayout->addWidget(m_logLevelDesc);
    prefLayout->addWidget(logGroup);

    // Group 2: Media & Playback Preferences
    auto *mediaGroup = new QGroupBox("Playback Preferences", prefTab);
    mediaGroup->setStyleSheet("QGroupBox { font-weight: bold; color: #f1f1f1; border: 1px solid #272727; "
                              "border-radius: 8px; background-color: #171717; margin-top: 10px; padding-top: 14px; }");
    auto *mediaGroupLayout = new QVBoxLayout(mediaGroup);
    mediaGroupLayout->setSpacing(10);

    auto *qualityRow = new QHBoxLayout();
    auto *qualityLabel = new QLabel("Default Quality:", mediaGroup);
    qualityLabel->setStyleSheet("color: #f1f1f1; font-weight: 500;");
    m_defaultQualityCombo = new QComboBox(mediaGroup);
    m_defaultQualityCombo->addItems({"1080p", "720p", "480p", "360p", "Auto", "Audio Only"});
    qualityRow->addWidget(qualityLabel);
    qualityRow->addWidget(m_defaultQualityCombo, 1);
    mediaGroupLayout->addLayout(qualityRow);

    m_allowAv1Check = new QCheckBox("Allow AV1 Video Codec", mediaGroup);
    m_allowAv1Check->setStyleSheet("QCheckBox { color: #f1f1f1; font-size: 12px; } QCheckBox::indicator { width: 16px; height: 16px; }");
    auto *av1Desc = new QLabel("When unchecked, AV1 streams are excluded in favor of AVC/VP9 for hardware decoding battery efficiency.", mediaGroup);
    av1Desc->setWordWrap(true);
    av1Desc->setStyleSheet("color: #aaaaaa; font-size: 11px;");
    mediaGroupLayout->addWidget(m_allowAv1Check);
    mediaGroupLayout->addWidget(av1Desc);

    prefLayout->addWidget(mediaGroup);
    prefLayout->addStretch();

    m_tabWidget->addTab(prefTab, "Preferences");

    rootLayout->addWidget(m_tabWidget, 1);

    // Dialog bottom bar
    auto *closeBar = new QHBoxLayout();
    closeBar->addStretch();
    auto *closeBtn = new QPushButton("Close", this);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedHeight(34);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    closeBar->addWidget(closeBtn);
    rootLayout->addLayout(closeBar);

    // Initial state population
    if (m_client) {
        std::string curPageId = m_client->getSelectedChannelPageId();
        int curAuthIdx = m_client->getSelectedAuthUserIndex();
        std::string curName = m_client->getSelectedChannelName();

        if (curAuthIdx < 0 && !curPageId.empty()) {
            curAuthIdx = 0;
        }

        m_pageIdEdit->setText(QString::fromUtf8(curPageId.c_str()));
        m_channelNameEdit->setText(QString::fromUtf8(curName.c_str()));

        for (int i = 0; i < m_accountIndexCombo->count(); ++i) {
            if (m_accountIndexCombo->itemData(i).toInt() == curAuthIdx) {
                m_accountIndexCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    // Populate Preferences tab initial values
    QString curLogLevel = AppSettings::instance().getLogLevel().toLower();
    for (int i = 0; i < m_logLevelCombo->count(); ++i) {
        if (m_logLevelCombo->itemData(i).toString() == curLogLevel) {
            m_logLevelCombo->setCurrentIndex(i);
            break;
        }
    }
    m_logLevelDesc->setText(QString("Active log level: %1").arg(curLogLevel.toUpper()));
    connect(m_logLevelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CookieDialog::onLogLevelChanged);

    QString curQuality = AppSettings::instance().getQuality();
    int qIdx = m_defaultQualityCombo->findText(curQuality);
    if (qIdx >= 0) {
        m_defaultQualityCombo->setCurrentIndex(qIdx);
    }
    connect(m_defaultQualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CookieDialog::onDefaultQualityChanged);

    m_allowAv1Check->setChecked(AppSettings::instance().getAllowAv1());
    connect(m_allowAv1Check, &QCheckBox::toggled, this, &CookieDialog::onAllowAv1Toggled);

    // If already authenticated, show Channel tab first, otherwise Cookies tab
    bool hasAuth = m_cookieManager && m_cookieManager->hasAuth();
    m_tabWidget->setCurrentIndex(hasAuth ? 0 : 1);

    if (m_client) {
        auto cached = m_client->getCachedAccountChannels();
        if (!cached.empty()) {
            m_detectedChannels = std::move(cached);
            m_channelCombo->blockSignals(true);
            m_channelCombo->clear();
            m_channelCombo->addItem("Default Account (Session Default)");
            int selectIdx = 0;
            std::string activePageId = m_client->getSelectedChannelPageId();
            std::string activeName = m_client->getSelectedChannelName();
            int activeAuthIdx = m_client->getSelectedAuthUserIndex();

            for (size_t i = 0; i < m_detectedChannels.size(); ++i) {
                const auto &ch = m_detectedChannels[i];
                QString label = QString::fromUtf8(ch.name.c_str());
                if (!ch.handle.empty() && ch.handle != "No channel") {
                    label += QString(" (%1)").arg(QString::fromUtf8(ch.handle.c_str()));
                } else if (ch.handle == "No channel") {
                    label += " (Primary Account)";
                }

                if (!ch.pageId.empty()) {
                    label += QString(" [Brand Account: %1...]").arg(QString::fromUtf8(ch.pageId.substr(0, 8).c_str()));
                } else {
                    label += QString(" [Personal Slot %1]").arg(ch.authUserIndex >= 0 ? ch.authUserIndex : 0);
                }

                m_channelCombo->addItem(label);

                if (selectIdx == 0) {
                    if (!activePageId.empty() && ch.pageId == activePageId) {
                        selectIdx = static_cast<int>(i) + 1;
                    } else if (activePageId.empty() && !activeName.empty() && ch.name == activeName) {
                        selectIdx = static_cast<int>(i) + 1;
                    } else if (activePageId.empty() && activeAuthIdx >= 0 && ch.authUserIndex == activeAuthIdx) {
                        selectIdx = static_cast<int>(i) + 1;
                    } else if (ch.isSelected) {
                        selectIdx = static_cast<int>(i) + 1;
                    }
                }
            }
            if (selectIdx > 0) {
                m_channelCombo->setCurrentIndex(selectIdx);
            }
            m_channelCombo->blockSignals(false);
            m_detectStatusLabel->setText(
                QString("Cached: %1 channel profile(s) available").arg(m_detectedChannels.size()));
            m_detectStatusLabel->setStyleSheet("color: #4ade80; font-size: 11px;");
        }
    }
}

void CookieDialog::updateStatusDisplay() {
    if (m_cookieManager && m_cookieManager->hasAuth()) {
        m_statusLabel->setText("Authenticated: Cookies loaded successfully");
        m_statusLabel->setStyleSheet("background-color: #142a1d; color: #4ade80; border: 1px solid #1e4d2b; padding: "
                                     "10px 14px; border-radius: 8px; font-weight: bold;");
    } else {
        m_statusLabel->setText("Guest Mode: No cookies loaded");
        m_statusLabel->setStyleSheet("background-color: #2c2112; color: #fbbf24; border: 1px solid #4a361a; padding: "
                                     "10px 14px; border-radius: 8px; font-weight: bold;");
    }
}

void CookieDialog::updateActiveChannelDisplay() {
    if (!m_currentActiveLabel)
        return;
    std::string name;
    std::string pageId;
    int authIdx = -1;

    if (m_userHasSelectedChannel) {
        name = m_channelNameEdit ? m_channelNameEdit->text().trimmed().toStdString() : "";
        pageId = m_pageIdEdit ? m_pageIdEdit->text().trimmed().toStdString() : "";
        authIdx = m_accountIndexCombo ? m_accountIndexCombo->currentData().toInt() : -1;
    } else if (m_client) {
        name = m_client->getSelectedChannelName();
        pageId = m_client->getSelectedChannelPageId();
        authIdx = m_client->getSelectedAuthUserIndex();
    }

    QString prefix = m_userHasSelectedChannel ? "Staged Selection: " : "Active Channel: ";
    QString summary;
    if (!name.empty() || !pageId.empty() || authIdx >= 0) {
        summary = prefix + QString("<b style='color:#4ade80;'>%1</b>")
                               .arg(!name.empty() ? QString::fromUtf8(name.c_str()) : "Configured");
        QStringList details;
        if (authIdx >= 0) {
            details << QString("Account Slot: %1").arg(authIdx);
        } else {
            details << "Account Slot: Default";
        }
        if (!pageId.empty()) {
            details << QString("Page ID: %1").arg(QString::fromUtf8(pageId.c_str()));
        }
        summary += QString(" (%1)").arg(details.join(", "));
    } else {
        summary = prefix + "<b style='color:#fbbf24;'>Default Account</b> (Cookie session default)";
    }
    m_currentActiveLabel->setText(summary);
}

void CookieDialog::onDetectChannelsClicked() {
    if (!m_client)
        return;
    if (!m_cookieManager || !m_cookieManager->hasAuth()) {
        QMessageBox::warning(this, "Authentication Required",
                             "Please import your cookies.txt first in the 'Cookies' tab before detecting channels.");
        m_tabWidget->setCurrentIndex(1);
        return;
    }

    m_detectChannelsBtn->setEnabled(false);
    m_detectStatusLabel->setText("Detecting associated channels from YouTube API...");
    m_detectStatusLabel->setStyleSheet("color: #60a5fa; font-size: 11px;");

    cancelActiveWorker();

    auto cancelToken = std::make_shared<std::atomic<bool>>(false);
    m_activeCancelToken = cancelToken;

    // Create an isolated snapshot of cookies so the background worker never accesses
    // m_client or m_cookieManager across threads.
    auto tempCookieMgr = std::make_shared<CookieManager>(*m_cookieManager);

    QPointer<CookieDialog> self(this);

    QThreadPool::globalInstance()->start([self, tempCookieMgr, cancelToken]() {
        if (cancelToken->load()) {
            return;
        }

        // Self-contained client running purely in background thread pool context
        YouTubeClient detectClient(tempCookieMgr);
        std::string err;
        auto channels = detectClient.fetchAccountChannels(err, /*forceRefresh=*/true, cancelToken.get());

        if (cancelToken->load() || !self) {
            return;
        }

        QMetaObject::invokeMethod(
            self.data(),
            [self, channels = std::move(channels), err = std::move(err), tempCookieMgr, cancelToken]() mutable {
                if (!self || cancelToken->load()) {
                    return;
                }

                self->m_detectChannelsBtn->setEnabled(true);

                // Safely synchronize any refreshed session cookies back to master store
                if (tempCookieMgr && self->m_cookieManager) {
                    *self->m_cookieManager = *tempCookieMgr;
                }

                if (self->m_client) {
                    self->m_client->clearCachedAccountChannels();
                }

                self->m_detectedChannels = std::move(channels);

                self->m_channelCombo->blockSignals(true);
                self->m_channelCombo->clear();
                self->m_channelCombo->addItem("Default Account (Session Default)");

                if (self->m_detectedChannels.empty()) {
                    self->m_detectStatusLabel->setText(
                        QString("Detection note: %1")
                            .arg(err.empty() ? "No channel profiles returned by API. You can manually configure "
                                               "Account Slot or Page ID below."
                                             : QString::fromUtf8(err.c_str())));
                    self->m_detectStatusLabel->setStyleSheet("color: #fbbf24; font-size: 11px;");
                } else {
                    self->m_detectStatusLabel->setText(
                        QString("Successfully found %1 channel profile(s)!").arg(self->m_detectedChannels.size()));
                    self->m_detectStatusLabel->setStyleSheet("color: #4ade80; font-size: 11px;");

                    std::string activePageId;
                    std::string activeName;
                    int activeAuthIdx = -1;
                    if (self->m_userHasSelectedChannel) {
                        activePageId = self->m_pageIdEdit->text().trimmed().toStdString();
                        activeName = self->m_channelNameEdit->text().trimmed().toStdString();
                        activeAuthIdx = self->m_accountIndexCombo->currentData().toInt();
                    } else {
                        activePageId = self->m_client ? self->m_client->getSelectedChannelPageId() : "";
                        activeName = self->m_client ? self->m_client->getSelectedChannelName() : "";
                        activeAuthIdx = self->m_client ? self->m_client->getSelectedAuthUserIndex() : -1;
                    }

                    int selectIdx = 0;
                    for (size_t i = 0; i < self->m_detectedChannels.size(); ++i) {
                        const auto &ch = self->m_detectedChannels[i];
                        QString label = QString::fromUtf8(ch.name.c_str());
                        if (!ch.handle.empty() && ch.handle != "No channel") {
                            label += QString(" (%1)").arg(QString::fromUtf8(ch.handle.c_str()));
                        } else if (ch.handle == "No channel") {
                            label += " (Primary Account)";
                        }

                        if (!ch.pageId.empty()) {
                            label += QString(" [Brand Account: %1...]")
                                         .arg(QString::fromUtf8(ch.pageId.substr(0, 8).c_str()));
                        } else {
                            label += QString(" [Personal Slot %1]").arg(ch.authUserIndex >= 0 ? ch.authUserIndex : 0);
                        }

                        self->m_channelCombo->addItem(label);

                        if (selectIdx == 0) {
                            if (!activePageId.empty() && ch.pageId == activePageId) {
                                selectIdx = static_cast<int>(i) + 1;
                            } else if (activePageId.empty() && !activeName.empty() && ch.name == activeName) {
                                selectIdx = static_cast<int>(i) + 1;
                            } else if (activePageId.empty() && activeAuthIdx >= 0 &&
                                       ch.authUserIndex == activeAuthIdx) {
                                selectIdx = static_cast<int>(i) + 1;
                            } else if (ch.isSelected && !self->m_userHasSelectedChannel) {
                                selectIdx = static_cast<int>(i) + 1;
                            }
                        }
                    }

                    if (selectIdx > 0) {
                        self->m_channelCombo->setCurrentIndex(selectIdx);
                    }
                }
                self->m_channelCombo->blockSignals(false);
                self->updateActiveChannelDisplay();
            },
            Qt::QueuedConnection);
    });
}

void CookieDialog::onChannelComboIndexChanged(int index) {
    if (index <= 0) {
        m_pageIdEdit->clear();
        m_channelNameEdit->clear();
        m_accountIndexCombo->setCurrentIndex(0);
        applyChannelSelection(/*closeDialog=*/false);
        return;
    }
    size_t channelIdx = static_cast<size_t>(index - 1);
    if (channelIdx >= m_detectedChannels.size()) {
        return;
    }

    const auto &ch = m_detectedChannels[channelIdx];
    m_pageIdEdit->setText(QString::fromUtf8(ch.pageId.c_str()));
    m_channelNameEdit->setText(QString::fromUtf8(ch.name.c_str()));

    int targetSlot = ch.authUserIndex;
    if (targetSlot < 0 && !ch.pageId.empty()) {
        targetSlot = 0;
    }

    if (targetSlot >= 0) {
        for (int i = 0; i < m_accountIndexCombo->count(); ++i) {
            if (m_accountIndexCombo->itemData(i).toInt() == targetSlot) {
                m_accountIndexCombo->setCurrentIndex(i);
                break;
            }
        }
    } else {
        m_accountIndexCombo->setCurrentIndex(0);
    }
    applyChannelSelection(/*closeDialog=*/false);
}

void CookieDialog::applyChannelSelection(bool closeDialog) {
    QString pageId = m_pageIdEdit->text().trimmed();
    int authUserIndex = m_accountIndexCombo->currentData().toInt();
    QString name = m_channelNameEdit->text().trimmed();

    if (!pageId.isEmpty() && authUserIndex < 0) {
        authUserIndex = 0;
    }

    if (name.isEmpty()) {
        if (!pageId.isEmpty()) {
            name = QString("Page %1").arg(pageId.left(8));
        } else if (authUserIndex >= 0) {
            name = QString("Account %1").arg(authUserIndex);
        } else {
            name = "Default Account";
        }
    }

    if (m_client) {
        QByteArray pageIdBytes = pageId.toUtf8();
        QByteArray nameBytes = name.toUtf8();
        m_client->setSelectedChannel(std::string_view(pageIdBytes.constData(), pageIdBytes.size()), authUserIndex,
                                     std::string_view(nameBytes.constData(), nameBytes.size()));
    }

    AppSettings::instance().setSelectedChannel(pageId, authUserIndex, name);
    AppSettings::instance().sync();

    m_userHasSelectedChannel = false;
    updateActiveChannelDisplay();
    emit channelChanged(pageId, authUserIndex, name);
    LOG_INFO("[CookieDialog] Applied active channel: '{}' (pageId: '{}', authUser: {})",
             std::string_view(name.toUtf8().constData(), name.toUtf8().size()),
             std::string_view(pageId.toUtf8().constData(), pageId.toUtf8().size()), authUserIndex);
    if (closeDialog) {
        QDialog::accept();
    }
}

void CookieDialog::onApplyChannelClicked() {
    applyChannelSelection(/*closeDialog=*/true);
}

void CookieDialog::accept() {
    if (m_userHasSelectedChannel) {
        applyChannelSelection(/*closeDialog=*/true);
        return;
    }
    QDialog::accept();
}

void CookieDialog::onImportFileClicked() {
    QString fileName =
        QFileDialog::getOpenFileName(this, "Select cookies.txt", "", "Text Files (*.txt);;All Files (*)");
    if (fileName.isEmpty())
        return;

#ifdef _WIN32
    std::filesystem::path sourcePath(fileName.toStdWString());
#else
    std::filesystem::path sourcePath(fileName.toUtf8().constData());
#endif

    if (m_cookieManager->loadFromFile(sourcePath)) {
        if (m_cookieManager->saveToCanonicalAppData()) {
            LOG_INFO("[CookieDialog] Saved imported cookies to {}", m_cookieManager->getCookieFilePath());
        } else {
            m_cookieManager->setCookieFilePath(lightyt::utils::pathToUtf8(sourcePath));
            LOG_WARN("[CookieDialog] Could not mirror cookies to AppData, using source file: {}",
                     lightyt::utils::pathToUtf8(sourcePath));
        }
        if (m_client) {
            m_client->clearCachedAccountChannels();
        }
        updateStatusDisplay();
        emit cookiesUpdated();
        onDetectChannelsClicked();
        QMessageBox::information(this, "Success", "YouTube cookies imported and saved successfully!");
    } else {
        QMessageBox::warning(this, "Import Failed", "Could not parse valid YouTube cookies from selected file.");
    }
}

void CookieDialog::onSaveClicked() {
    QString text = m_cookieTextEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        QMessageBox::information(this, "Info", "No new cookie text was entered.");
        return;
    }

    if (m_cookieManager->loadFromString(text.toUtf8().constData())) {
        if (m_cookieManager->saveToCanonicalAppData()) {
            LOG_INFO("[CookieDialog] Saved pasted cookies to {}", m_cookieManager->getCookieFilePath());
        } else {
            LOG_WARN("[CookieDialog] Failed to persist cookies to AppData");
        }
        if (m_client) {
            m_client->clearCachedAccountChannels();
        }
        updateStatusDisplay();
        emit cookiesUpdated();
        onDetectChannelsClicked();
        QMessageBox::information(this, "Success", "Cookies saved and applied successfully!");
    } else {
        QMessageBox::warning(this, "Parse Failed", "Could not parse YouTube cookies from entered text.");
    }
}

void CookieDialog::onClearClicked() {
    if (m_cookieManager) {
        cancelActiveWorker();
        m_cookieManager->clear();
        m_cookieManager->setCookieFilePath("");
        auto appData = lightyt::paths::getAppDataDir();
        std::error_code ec;
        std::filesystem::remove(appData / "cookies.txt", ec);
        if (m_client) {
            m_client->clearCachedAccountChannels();
            m_client->setSelectedChannel("", -1, "");
        }
        AppSettings::instance().setSelectedChannel("", -1, "");
        m_detectedChannels.clear();
        if (m_channelCombo) {
            m_channelCombo->blockSignals(true);
            m_channelCombo->clear();
            m_channelCombo->addItem("Default Account (Session Default)");
            m_channelCombo->blockSignals(false);
        }
        if (m_detectStatusLabel) {
            m_detectStatusLabel->setText("Cookies cleared. Import or paste cookies to detect channels.");
            m_detectStatusLabel->setStyleSheet("color: #888888; font-size: 11px;");
        }
        updateActiveChannelDisplay();
        updateStatusDisplay();
        emit cookiesUpdated();
    }
}

void CookieDialog::onLogLevelChanged(int index) {
    if (index < 0 || !m_logLevelCombo)
        return;
    QString levelStr = m_logLevelCombo->itemData(index).toString();
    auto level = lightyt::log::stringToLevel(levelStr.toStdString(), lightyt::log::Level::Warn);
    lightyt::log::Logger::instance().setLevel(level);
    AppSettings::instance().setLogLevel(levelStr);
    AppSettings::instance().sync();
    if (m_logLevelDesc) {
        m_logLevelDesc->setText(QString("Active log level: %1").arg(levelStr.toUpper()));
    }
    LOG_WARN("[CookieDialog] Logging level changed to {}", levelStr.toStdString());
}

void CookieDialog::onAllowAv1Toggled(bool checked) {
    AppSettings::instance().setAllowAv1(checked);
    AppSettings::instance().sync();
}

void CookieDialog::onDefaultQualityChanged(int index) {
    if (index < 0 || !m_defaultQualityCombo)
        return;
    QString quality = m_defaultQualityCombo->itemText(index);
    AppSettings::instance().setQuality(quality);
    AppSettings::instance().sync();
}

