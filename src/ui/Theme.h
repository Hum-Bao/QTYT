#pragma once

#include <QColor>
#include <QString>

namespace lightyt::ui::theme {

// -----------------------------------------------------------------------------
// Core Color Palette (YouTube 2026 Dark Theme)
// -----------------------------------------------------------------------------
inline const QColor ColorCanvas(0x0f, 0x0f, 0x0f);
inline const QColor ColorSurface(0x21, 0x21, 0x21);
inline const QColor ColorElevated(0x27, 0x27, 0x27);
inline const QColor ColorElevatedHover(0x3f, 0x3f, 0x3f);
inline const QColor ColorBorder(0x30, 0x30, 0x30);
inline const QColor ColorDivider(0x3f, 0x3f, 0x3f);

inline const QColor ColorTextPrimary(0xf1, 0xf1, 0xf1);
inline const QColor ColorTextSecondary(0xaa, 0xaa, 0xaa);
inline const QColor ColorTextMuted(0x71, 0x71, 0x71);

inline const QColor ColorBrandRed(0xff, 0x00, 0x00);
inline const QColor ColorBrandRedHover(0xcc, 0x00, 0x00);
inline const QColor ColorAccentCoral(0xff, 0x4e, 0x45);
inline const QColor ColorAccentBlue(0x3e, 0xa6, 0xff);
inline const QColor ColorFocusBlue(0x1c, 0x62, 0xb9);

inline const QColor ColorBadgeDuration(0x00, 0x00, 0x00, 204);  // 80% opacity black
inline const QColor ColorShortsBadge(0xcc, 0x00, 0x00);

// Geometry tokens
inline constexpr int RadiusThumbnail = 12;
inline constexpr int RadiusCard = 12;
inline constexpr int RadiusBadge = 4;
inline constexpr int RadiusPill = 18;
inline constexpr int RadiusSearchPill = 20;
inline constexpr int AvatarFeedSize = 36;
inline constexpr int AvatarPlayerSize = 40;

// -----------------------------------------------------------------------------
// Global Application Stylesheet (QSS)
// -----------------------------------------------------------------------------
inline const QString GlobalStyleSheet = QStringLiteral(R"(
    QMainWindow, QDialog {
        background-color: #0f0f0f;
        color: #f1f1f1;
    }

    QWidget {
        font-family: "Roboto", "Segoe UI", -apple-system, BlinkMacSystemFont, "Helvetica Neue", Arial, sans-serif;
    }

    QPushButton {
        background-color: #272727;
        color: #f1f1f1;
        border: 1px solid rgba(255, 255, 255, 0.08);
        border-radius: 18px;
        padding: 0px 14px;
        min-height: 34px;
        font-size: 13px;
        font-weight: 500;
        outline: none;
    }
    QPushButton:hover {
        background-color: #3f3f3f;
    }
    QPushButton:pressed {
        background-color: #525252;
    }
    QPushButton:focus {
        outline: none;
    }

    /* Minimalist Modern Scrollbars */
    QScrollBar:vertical {
        background: transparent;
        width: 8px;
        margin: 0;
    }
    QScrollBar::handle:vertical {
        background: #717171;
        min-height: 24px;
        border-radius: 4px;
    }
    QScrollBar::handle:vertical:hover {
        background: #aaaaaa;
    }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
        background: none;
        height: 0;
    }

    QScrollBar:horizontal {
        background: transparent;
        height: 8px;
        margin: 0;
    }
    QScrollBar::handle:horizontal {
        background: #717171;
        min-width: 24px;
        border-radius: 4px;
    }
    QScrollBar::handle:horizontal:hover {
        background: #aaaaaa;
    }
    QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
    QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
        background: none;
        width: 0;
    }

    /* Tooltips */
    QToolTip {
        background-color: #282828;
        color: #ffffff;
        border: 1px solid #3f3f3f;
        border-radius: 4px;
        padding: 5px 8px;
        font-size: 11px;
    }

    /* Comboboxes & Menus */
    QComboBox {
        background-color: #272727;
        color: #f1f1f1;
        border: 1px solid rgba(255, 255, 255, 0.1);
        border-radius: 18px;
        padding: 0px 14px;
        min-height: 34px;
        font-size: 13px;
        font-weight: 500;
    }
    QComboBox:hover {
        background-color: #3f3f3f;
    }
    QComboBox::drop-down {
        border: none;
        width: 18px;
    }
    QComboBox QAbstractItemView {
        background-color: #212121;
        color: #f1f1f1;
        selection-background-color: #3f3f3f;
        selection-color: #ffffff;
        border: 1px solid #383838;
        border-radius: 8px;
        padding: 4px;
        outline: none;
    }

    QMenu {
        background-color: #212121;
        color: #f1f1f1;
        border: 1px solid #383838;
        border-radius: 8px;
        padding: 4px 0;
    }
    QMenu::item {
        color: #f1f1f1;
        background-color: transparent;
        padding: 6px 16px;
        border-radius: 4px;
        margin: 2px 4px;
    }
    QMenu::item:selected {
        background-color: #3f3f3f;
        color: #ffffff;
    }
    QMenu::item:disabled {
        color: #717171;
    }

    /* Status Bar */
    QStatusBar {
        background-color: #0f0f0f;
        color: #717171;
        border-top: 1px solid #1f1f1f;
        font-size: 11px;
    }
)");

}  // namespace lightyt::ui::theme
