#pragma once

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>

namespace lightyt::ui::icons {

namespace detail {

enum class IconId : uint16_t {
    BrandLogo = 1,
    Search,
    Home,
    Subscriptions,
    Shorts,
    UserAvatar,
    Play,
    Pause,
    Volume,
    Fullscreen,
    BackArrow,
    ThumbUp,
    ThumbDown,
    ShareArrow,
    CheckMark,
    Gear,
    ChevronRight,
    ChevronLeft,
    Close
};

struct IconKey {
    IconId id{IconId::BrandLogo};
    uint16_t width{0};
    uint16_t height{0};
    uint16_t flags{0};
    uint32_t rgba{0};

    auto operator<=>(const IconKey &) const noexcept = default;
};

template<typename DrawFn>
[[nodiscard]] inline QIcon makeHiDpiIcon(int width, int height, DrawFn &&drawFn) {
    constexpr int kScale = 2;
    QImage img(width * kScale, height * kScale, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.scale(kScale, kScale);
        drawFn(p, QRectF(0, 0, width, height));
    }

    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(kScale);
    return QIcon(pm);
}

template<typename DrawFn>
[[nodiscard]] inline QIcon makeCachedIcon(IconId id, int width, int height, uint32_t rgba, uint16_t flags,
                                          DrawFn &&drawFn) {
    static std::mutex s_mutex;
    static std::map<IconKey, QIcon> s_cache;

    IconKey key{id, static_cast<uint16_t>(width), static_cast<uint16_t>(height), flags, rgba};

    {
        std::lock_guard lock(s_mutex);
        auto it = s_cache.find(key);
        if (it != s_cache.end()) {
            return it->second;
        }
    }

    QIcon icon = makeHiDpiIcon(width, height, std::forward<DrawFn>(drawFn));

    {
        std::lock_guard lock(s_mutex);
        s_cache.emplace(key, icon);
    }

    return icon;
}

}  // namespace detail

// YouTube-style rounded red play emblem (28x20)
[[nodiscard]] inline QIcon brandLogo(int width = 28, int height = 20) {
    return detail::makeCachedIcon(detail::IconId::BrandLogo, width, height, 0, 0, [](QPainter &p, const QRectF &r) {
        QPainterPath bg;
        bg.addRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 5.5, 5.5);
        p.fillPath(bg, QColor(0xff, 0x00, 0x00));

        QPainterPath tri;
        const double cx = r.center().x();
        const double cy = r.center().y();
        tri.moveTo(cx - 3.2, cy - 4.4);
        tri.lineTo(cx + 4.8, cy);
        tri.lineTo(cx - 3.2, cy + 4.4);
        tri.closeSubpath();
        p.fillPath(tri, Qt::white);
    });
}

// Crisp vector magnifying glass (matches ytSearchboxComponentSearchButton SVG)
[[nodiscard]] inline QIcon search(const QColor &color = QColor(0xf1, 0xf1, 0xf1), int size = 20) {
    return detail::makeCachedIcon(detail::IconId::Search, size, size, color.rgba(), 0,
                                  [color](QPainter &p, const QRectF &) {
                                      QPen pen(color, 1.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.setBrush(Qt::NoBrush);
                                      p.drawEllipse(QPointF(8.8, 8.8), 5.6, 5.6);
                                      p.drawLine(QPointF(13.0, 13.0), QPointF(17.6, 17.6));
                                  });
}

// Home house icon
[[nodiscard]] inline QIcon home(const QColor &color = QColor(0xf1, 0xf1, 0xf1), int size = 18) {
    return detail::makeCachedIcon(detail::IconId::Home, size, size, color.rgba(), 0,
                                  [color](QPainter &p, const QRectF &) {
                                      QPainterPath path;
                                      path.moveTo(9.0, 2.2);
                                      path.lineTo(2.0, 8.2);
                                      path.lineTo(3.8, 8.2);
                                      path.lineTo(3.8, 15.6);
                                      path.lineTo(7.4, 15.6);
                                      path.lineTo(7.4, 10.8);
                                      path.lineTo(10.6, 10.8);
                                      path.lineTo(10.6, 15.6);
                                      path.lineTo(14.2, 15.6);
                                      path.lineTo(14.2, 8.2);
                                      path.lineTo(16.0, 8.2);
                                      path.closeSubpath();
                                      p.fillPath(path, color);
                                  });
}

// Subscriptions stacked play icon
[[nodiscard]] inline QIcon subscriptions(const QColor &color = QColor(0xf1, 0xf1, 0xf1), int size = 18) {
    return detail::makeCachedIcon(detail::IconId::Subscriptions, size, size, color.rgba(), 0,
                                  [color](QPainter &p, const QRectF &) {
                                      QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.drawLine(QPointF(5.0, 2.5), QPointF(13.0, 2.5));
                                      p.drawLine(QPointF(3.5, 4.8), QPointF(14.5, 4.8));

                                      QPainterPath box;
                                      box.addRoundedRect(QRectF(2.0, 6.8, 14.0, 8.8), 2.0, 2.0);
                                      p.drawPath(box);

                                      QPainterPath tri;
                                      tri.moveTo(7.5, 9.0);
                                      tri.lineTo(11.6, 11.2);
                                      tri.lineTo(7.5, 13.4);
                                      tri.closeSubpath();
                                      p.fillPath(tri, color);
                                  });
}

// Shorts icon (YouTube Shorts angled capsule with play triangle)
[[nodiscard]] inline QIcon shorts(const QColor &color = QColor(0xff, 0x00, 0x00), int size = 18) {
    return detail::makeCachedIcon(detail::IconId::Shorts, size, size, color.rgba(), 0,
                                  [color](QPainter &p, const QRectF &) {
                                      QPen pen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.setBrush(Qt::NoBrush);

                                      QPainterPath body;
                                      body.addRoundedRect(QRectF(3.5, 2.2, 11.0, 13.6), 4.2, 4.2);
                                      p.drawPath(body);

                                      QPainterPath tri;
                                      tri.moveTo(7.4, 6.3);
                                      tri.lineTo(11.6, 9.0);
                                      tri.lineTo(7.4, 11.7);
                                      tri.closeSubpath();
                                      p.fillPath(tri, color);
                                  });
}

// User profile circle icon
[[nodiscard]] inline QIcon userAvatar(const QColor &color = QColor(0x3e, 0xa6, 0xff), int size = 18) {
    return detail::makeCachedIcon(detail::IconId::UserAvatar, size, size, color.rgba(), 0,
                                  [color](QPainter &p, const QRectF &) {
                                      QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.setBrush(Qt::NoBrush);
                                      p.drawEllipse(QPointF(9.0, 9.0), 7.2, 7.2);
                                      p.drawEllipse(QPointF(9.0, 6.8), 2.3, 2.3);

                                      QPainterPath shoulders;
                                      shoulders.moveTo(4.4, 14.3);
                                      shoulders.quadTo(9.0, 10.6, 13.6, 14.3);
                                      p.drawPath(shoulders);
                                  });
}

// Play icon
[[nodiscard]] inline QIcon play(const QColor &color = QColor(0xf1, 0xf1, 0xf1), int size = 16) {
    return detail::makeCachedIcon(detail::IconId::Play, size, size, color.rgba(), 0,
                                  [color, size](QPainter &p, const QRectF &) {
                                      const double ox = (size - 16.0) / 2.0;
                                      const double oy = (size - 16.0) / 2.0;
                                      QPainterPath tri;
                                      tri.moveTo(ox + 4.0, oy + 2.5);
                                      tri.lineTo(ox + 13.5, oy + 8.0);
                                      tri.lineTo(ox + 4.0, oy + 13.5);
                                      tri.closeSubpath();
                                      p.fillPath(tri, color);
                                  });
}
[[nodiscard]] inline QIcon play(int size, const QColor &color) {
    return play(color, size);
}

// Pause icon
[[nodiscard]] inline QIcon pause(const QColor &color = QColor(0xf1, 0xf1, 0xf1), int size = 16) {
    return detail::makeCachedIcon(detail::IconId::Pause, size, size, color.rgba(), 0,
                                  [color, size](QPainter &p, const QRectF &) {
                                      const double ox = (size - 16.0) / 2.0;
                                      const double oy = (size - 16.0) / 2.0;
                                      QPainterPath bars;
                                      bars.addRoundedRect(QRectF(ox + 3.5, oy + 2.5, 3.2, 11.0), 1.0, 1.0);
                                      bars.addRoundedRect(QRectF(ox + 9.3, oy + 2.5, 3.2, 11.0), 1.0, 1.0);
                                      p.fillPath(bars, color);
                                  });
}
[[nodiscard]] inline QIcon pause(int size, const QColor &color) {
    return pause(color, size);
}

// Volume / speaker icon
[[nodiscard]] inline QIcon volume(bool muted = false, const QColor &color = QColor(0xf1, 0xf1, 0xf1), int size = 18) {
    return detail::makeCachedIcon(detail::IconId::Volume, size, size, color.rgba(), muted ? 1 : 0,
                                  [muted, color, size](QPainter &p, const QRectF &) {
                                      const double ox = (size - 18.0) / 2.0;
                                      const double oy = (size - 18.0) / 2.0;
                                      QPainterPath cone;
                                      cone.moveTo(ox + 2.5, oy + 6.8);
                                      cone.lineTo(ox + 5.6, oy + 6.8);
                                      cone.lineTo(ox + 9.5, oy + 3.2);
                                      cone.lineTo(ox + 9.5, oy + 14.8);
                                      cone.lineTo(ox + 5.6, oy + 11.2);
                                      cone.lineTo(ox + 2.5, oy + 11.2);
                                      cone.closeSubpath();
                                      p.fillPath(cone, color);

                                      QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.setBrush(Qt::NoBrush);
                                      if (muted) {
                                          p.drawLine(QPointF(ox + 11.8, oy + 6.8), QPointF(ox + 15.6, oy + 11.2));
                                          p.drawLine(QPointF(ox + 15.6, oy + 6.8), QPointF(ox + 11.8, oy + 11.2));
                                      } else {
                                          QPainterPath wave1;
                                          wave1.moveTo(ox + 11.6, oy + 6.5);
                                          wave1.quadTo(ox + 13.8, oy + 9.0, ox + 11.6, oy + 11.5);
                                          p.drawPath(wave1);

                                          QPainterPath wave2;
                                          wave2.moveTo(ox + 13.4, oy + 4.5);
                                          wave2.quadTo(ox + 16.8, oy + 9.0, ox + 13.4, oy + 13.5);
                                          p.drawPath(wave2);
                                      }
                                  });
}
[[nodiscard]] inline QIcon volume(int size, const QColor &color, bool muted = false) {
    return volume(muted, color, size);
}

// Fullscreen frame corners icon
[[nodiscard]] inline QIcon fullscreen(bool isExit = false, const QColor &color = QColor(0xf1, 0xf1, 0xf1),
                                      int size = 16) {
    return detail::makeCachedIcon(detail::IconId::Fullscreen, size, size, color.rgba(), isExit ? 1 : 0,
                                  [isExit, color, size](QPainter &p, const QRectF &) {
                                      const double ox = (size - 16.0) / 2.0;
                                      const double oy = (size - 16.0) / 2.0;
                                      QPen pen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.setBrush(Qt::NoBrush);

                                      if (!isExit) {
                                          // Top-left
                                          p.drawLine(QPointF(ox + 2.5, oy + 6.0), QPointF(ox + 2.5, oy + 2.5));
                                          p.drawLine(QPointF(ox + 2.5, oy + 2.5), QPointF(ox + 6.0, oy + 2.5));
                                          // Top-right
                                          p.drawLine(QPointF(ox + 10.0, oy + 2.5), QPointF(ox + 13.5, oy + 2.5));
                                          p.drawLine(QPointF(ox + 13.5, oy + 2.5), QPointF(ox + 13.5, oy + 6.0));
                                          // Bottom-left
                                          p.drawLine(QPointF(ox + 2.5, oy + 10.0), QPointF(ox + 2.5, oy + 13.5));
                                          p.drawLine(QPointF(ox + 2.5, oy + 13.5), QPointF(ox + 6.0, oy + 13.5));
                                          // Bottom-right
                                          p.drawLine(QPointF(ox + 10.0, oy + 13.5), QPointF(ox + 13.5, oy + 13.5));
                                          p.drawLine(QPointF(ox + 13.5, oy + 13.5), QPointF(ox + 13.5, oy + 10.0));
                                      } else {
                                          // Inward corners
                                          p.drawLine(QPointF(ox + 2.5, oy + 5.5), QPointF(ox + 5.5, oy + 5.5));
                                          p.drawLine(QPointF(ox + 5.5, oy + 2.5), QPointF(ox + 5.5, oy + 5.5));

                                          p.drawLine(QPointF(ox + 13.5, oy + 5.5), QPointF(ox + 10.5, oy + 5.5));
                                          p.drawLine(QPointF(ox + 10.5, oy + 2.5), QPointF(ox + 10.5, oy + 5.5));

                                          p.drawLine(QPointF(ox + 2.5, oy + 10.5), QPointF(ox + 5.5, oy + 10.5));
                                          p.drawLine(QPointF(ox + 5.5, oy + 13.5), QPointF(ox + 5.5, oy + 10.5));

                                          p.drawLine(QPointF(ox + 13.5, oy + 10.5), QPointF(ox + 10.5, oy + 10.5));
                                          p.drawLine(QPointF(ox + 10.5, oy + 13.5), QPointF(ox + 10.5, oy + 10.5));
                                      }
                                  });
}
[[nodiscard]] inline QIcon fullscreen(int size, const QColor &color, bool isExit = false) {
    return fullscreen(isExit, color, size);
}

// Back arrow icon
[[nodiscard]] inline QIcon backArrow(const QColor &color = QColor(0xf1, 0xf1, 0xf1), int size = 16) {
    return detail::makeCachedIcon(detail::IconId::BackArrow, size, size, color.rgba(), 0,
                                  [color](QPainter &p, const QRectF &) {
                                      QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.drawLine(QPointF(13.2, 8.0), QPointF(3.2, 8.0));
                                      p.drawLine(QPointF(7.2, 4.0), QPointF(3.0, 8.0));
                                      p.drawLine(QPointF(7.2, 12.0), QPointF(3.0, 8.0));
                                  });
}
[[nodiscard]] inline QIcon backArrow(int size, const QColor &color) {
    return backArrow(color, size);
}

// Thumbs Up (Like) icon
[[nodiscard]] inline QIcon thumbUp(const QColor &color = QColor(0xf1, 0xf1, 0xf1), bool filled = false, int size = 18) {
    return detail::makeCachedIcon(detail::IconId::ThumbUp, size, size, color.rgba(), filled ? 1 : 0,
                                  [color, filled](QPainter &p, const QRectF &) {
                                      QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.setBrush(filled ? QBrush(color) : Qt::NoBrush);

                                      p.drawRoundedRect(QRectF(2.2, 7.8, 3.2, 7.2), 1.0, 1.0);

                                      QPainterPath hand;
                                      hand.moveTo(5.4, 8.2);
                                      hand.lineTo(8.5, 2.8);
                                      hand.quadTo(10.2, 2.5, 10.2, 4.5);
                                      hand.lineTo(9.8, 7.2);
                                      hand.lineTo(14.4, 7.2);
                                      hand.quadTo(15.8, 7.4, 15.4, 9.2);
                                      hand.lineTo(14.2, 14.0);
                                      hand.quadTo(13.8, 15.0, 12.4, 15.0);
                                      hand.lineTo(5.4, 15.0);
                                      hand.closeSubpath();
                                      p.drawPath(hand);
                                  });
}
[[nodiscard]] inline QIcon thumbUp(int size, const QColor &color, bool filled = false) {
    return thumbUp(color, filled, size);
}

// Thumbs Down (Dislike) icon
[[nodiscard]] inline QIcon thumbDown(const QColor &color = QColor(0xf1, 0xf1, 0xf1), bool filled = false,
                                     int size = 18) {
    return detail::makeCachedIcon(detail::IconId::ThumbDown, size, size, color.rgba(), filled ? 1 : 0,
                                  [color, filled](QPainter &p, const QRectF &) {
                                      QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.setBrush(filled ? QBrush(color) : Qt::NoBrush);

                                      p.drawRoundedRect(QRectF(12.6, 3.0, 3.2, 7.2), 1.0, 1.0);

                                      QPainterPath hand;
                                      hand.moveTo(12.6, 9.8);
                                      hand.lineTo(9.5, 15.2);
                                      hand.quadTo(7.8, 15.5, 7.8, 13.5);
                                      hand.lineTo(8.2, 10.8);
                                      hand.lineTo(3.6, 10.8);
                                      hand.quadTo(2.2, 10.6, 2.6, 8.8);
                                      hand.lineTo(3.8, 4.0);
                                      hand.quadTo(4.2, 3.0, 5.6, 3.0);
                                      hand.lineTo(12.6, 3.0);
                                      hand.closeSubpath();
                                      p.drawPath(hand);
                                  });
}
[[nodiscard]] inline QIcon thumbDown(int size, const QColor &color, bool filled = false) {
    return thumbDown(color, filled, size);
}

// Share curved arrow icon
[[nodiscard]] inline QIcon shareArrow(const QColor &color = QColor(0xf1, 0xf1, 0xf1), int size = 18) {
    return detail::makeCachedIcon(detail::IconId::ShareArrow, size, size, color.rgba(), 0,
                                  [color](QPainter &p, const QRectF &) {
                                      QPen pen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.setBrush(Qt::NoBrush);

                                      QPainterPath curve;
                                      curve.moveTo(15.2, 8.2);
                                      curve.lineTo(10.2, 3.6);
                                      curve.lineTo(10.2, 6.4);
                                      curve.quadTo(3.6, 6.8, 2.8, 14.2);
                                      curve.quadTo(5.6, 10.2, 10.2, 10.2);
                                      curve.lineTo(10.2, 13.0);
                                      curve.closeSubpath();
                                      p.drawPath(curve);
                                  });
}
[[nodiscard]] inline QIcon shareArrow(int size, const QColor &color) {
    return shareArrow(color, size);
}

// Checkmark icon
[[nodiscard]] inline QIcon checkMark(const QColor &color = QColor(0x4a, 0xde, 0x80), int size = 18) {
    return detail::makeCachedIcon(detail::IconId::CheckMark, size, size, color.rgba(), 0,
                                  [color](QPainter &p, const QRectF &) {
                                      QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.drawLine(QPointF(3.5, 9.5), QPointF(7.4, 13.4));
                                      p.drawLine(QPointF(7.4, 13.4), QPointF(14.6, 4.8));
                                  });
}
[[nodiscard]] inline QIcon checkMark(int size, const QColor &color) {
    return checkMark(color, size);
}

// Settings Gear icon
[[nodiscard]] inline QIcon gear(const QColor &color = QColor(0xf1, 0xf1, 0xf1), int size = 20) {
    return detail::makeCachedIcon(detail::IconId::Gear, size, size, color.rgba(), 0,
                                  [color, size](QPainter &p, const QRectF &) {
                                      p.setPen(Qt::NoPen);
                                      p.setBrush(color);

                                      const double cx = size / 2.0;
                                      const double cy = size / 2.0;
                                      const double rOuter = size * 0.42;
                                      const double rInner = size * 0.31;
                                      const double rHole = size * 0.15;
                                      constexpr int numTeeth = 6;
                                      constexpr double kPi = 3.14159265358979323846;

                                      QPainterPath path;
                                      path.setFillRule(Qt::OddEvenFill);

                                      for (int i = 0; i < numTeeth; ++i) {
                                          double baseAngle = i * (2.0 * kPi / numTeeth);
                                          double a1 = baseAngle - 0.20;
                                          double a2 = baseAngle - 0.12;
                                          double a3 = baseAngle + 0.12;
                                          double a4 = baseAngle + 0.20;
                                          double aNext = baseAngle + (2.0 * kPi / numTeeth) - 0.20;

                                          QPointF p1(cx + rInner * std::cos(a1), cy + rInner * std::sin(a1));
                                          QPointF p2(cx + rOuter * std::cos(a2), cy + rOuter * std::sin(a2));
                                          QPointF p3(cx + rOuter * std::cos(a3), cy + rOuter * std::sin(a3));
                                          QPointF p4(cx + rInner * std::cos(a4), cy + rInner * std::sin(a4));
                                          QPointF p5(cx + rInner * std::cos(aNext), cy + rInner * std::sin(aNext));

                                          if (i == 0) {
                                              path.moveTo(p1);
                                          } else {
                                              path.lineTo(p1);
                                          }
                                          path.lineTo(p2);
                                          path.lineTo(p3);
                                          path.lineTo(p4);
                                          path.lineTo(p5);
                                      }
                                      path.closeSubpath();
                                      path.addEllipse(QPointF(cx, cy), rHole, rHole);

                                      p.drawPath(path);
                                  });
}
[[nodiscard]] inline QIcon gear(int size, const QColor &color) {
    return gear(color, size);
}

// Right Chevron icon (submenu indicator)
[[nodiscard]] inline QIcon chevronRight(const QColor &color = QColor(0xf1, 0xf1, 0xf1), int size = 18) {
    return detail::makeCachedIcon(detail::IconId::ChevronRight, size, size, color.rgba(), 0,
                                  [color](QPainter &p, const QRectF &) {
                                      QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.drawLine(QPointF(7.0, 4.5), QPointF(11.5, 9.0));
                                      p.drawLine(QPointF(11.5, 9.0), QPointF(7.0, 13.5));
                                  });
}
[[nodiscard]] inline QIcon chevronRight(int size, const QColor &color) {
    return chevronRight(color, size);
}

// Left Chevron icon (back navigation)
[[nodiscard]] inline QIcon chevronLeft(const QColor &color = QColor(0xf1, 0xf1, 0xf1), int size = 18) {
    return detail::makeCachedIcon(detail::IconId::ChevronLeft, size, size, color.rgba(), 0,
                                  [color](QPainter &p, const QRectF &) {
                                      QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      p.drawLine(QPointF(11.0, 4.5), QPointF(6.5, 9.0));
                                      p.drawLine(QPointF(6.5, 9.0), QPointF(11.0, 13.5));
                                  });
}
[[nodiscard]] inline QIcon chevronLeft(int size, const QColor &color) {
    return chevronLeft(color, size);
}

// Close / Cross icon (dialog / stats dismiss)
[[nodiscard]] inline QIcon close(const QColor &color = QColor(0xf1, 0xf1, 0xf1), int size = 18) {
    return detail::makeCachedIcon(detail::IconId::Close, size, size, color.rgba(), 0,
                                  [color, size](QPainter &p, const QRectF &) {
                                      QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                                      p.setPen(pen);
                                      const double pad = size * 0.28;
                                      const double extent = size - pad;
                                      p.drawLine(QPointF(pad, pad), QPointF(extent, extent));
                                      p.drawLine(QPointF(extent, pad), QPointF(pad, extent));
                                  });
}
[[nodiscard]] inline QIcon close(int size, const QColor &color) {
    return close(color, size);
}

}  // namespace lightyt::ui::icons
