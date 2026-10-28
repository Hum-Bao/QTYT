#pragma once

#include "../core/CookieManager.h"
#include "../core/YouTubeClient.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextEdit>
#include <atomic>
#include <memory>
#include <vector>

class CookieDialog : public QDialog {
    Q_OBJECT
public:
    explicit CookieDialog(std::shared_ptr<CookieManager> cookieManager, YouTubeClient *client = nullptr,
                          QWidget *parent = nullptr);
    ~CookieDialog() override;

    void accept() override;
    void done(int r) override;

signals:
    void cookiesUpdated();
    void channelChanged(const QString &pageId, int authUserIndex, const QString &channelName);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onImportFileClicked();
    void onSaveClicked();
    void onClearClicked();
    void onDetectChannelsClicked();
    void onChannelComboIndexChanged(int index);
    void onApplyChannelClicked();
    void onLogLevelChanged(int index);
    void onAllowAv1Toggled(bool checked);
    void onDefaultQualityChanged(int index);

private:
    std::shared_ptr<CookieManager> m_cookieManager;
    YouTubeClient *m_client = nullptr;
    std::shared_ptr<std::atomic<bool>> m_activeCancelToken;
    void cancelActiveWorker();

    QTabWidget *m_tabWidget = nullptr;

    // Preferences tab
    QComboBox *m_logLevelCombo = nullptr;
    QLabel *m_logLevelDesc = nullptr;
    QCheckBox *m_allowAv1Check = nullptr;
    QComboBox *m_defaultQualityCombo = nullptr;

    // Cookies tab
    QLabel *m_statusLabel = nullptr;
    QTextEdit *m_cookieTextEdit = nullptr;
    QPushButton *m_saveButton = nullptr;

    // Channel tab
    QComboBox *m_channelCombo = nullptr;
    QPushButton *m_detectChannelsBtn = nullptr;
    QLabel *m_detectStatusLabel = nullptr;
    QComboBox *m_accountIndexCombo = nullptr;
    QLineEdit *m_pageIdEdit = nullptr;
    QLineEdit *m_channelNameEdit = nullptr;
    QLabel *m_currentActiveLabel = nullptr;
    QPushButton *m_applyChannelBtn = nullptr;

    void applyChannelSelection(bool closeDialog = false);

    std::vector<AccountChannel> m_detectedChannels;
    bool m_userHasSelectedChannel = false;

    void setupUi();
    void updateStatusDisplay();
    void updateActiveChannelDisplay();
};
