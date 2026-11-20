#include "carddiagram.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <array>
#include <utility>

namespace {

constexpr qreal diagramWidth = 680;
constexpr qreal portStep = 61;

QColor mix(const QColor &from, const QColor &to, double weight)
{
    weight = std::clamp(weight, 0.0, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * weight,
                            from.greenF() + (to.greenF() - from.greenF()) * weight,
                            from.blueF() + (to.blueF() - from.blueF()) * weight);
}

double interpolate(double start, double end, double fraction)
{
    return start + (end - start) * fraction;
}

} // namespace

struct CardDiagram::Style
{
    QPainter &painter;
    QColor background;
    QColor wire;
    QColor text;
    QColor active;
    QColor low;
    QColor selected;

    void line(QPointF a, QPointF b, double strength = 0, bool unknown = false)
    {
        painter.setPen(QPen(mix(wire, active, strength), 1.35 + strength * 1.75,
                            unknown ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(a, b);
    }

    void port(QPointF location, double strength = 0)
    {
        painter.setPen(QPen(mix(wire, active, strength), 1.35 + strength));
        painter.setBrush(mix(background, active, strength * 0.65));
        painter.drawEllipse(location, 3.2, 3.2);
    }

    void caption(QRectF box, const QString &value, int align = Qt::AlignCenter)
    {
        painter.setPen(text);
        painter.drawText(box, align, value);
    }

    void selection(QRectF box)
    {
        painter.setPen(QPen(selected, 1.3));
        painter.setBrush(mix(background, selected, 0.12));
        painter.drawRoundedRect(box, 6, 6);
    }
};

CardDiagram::CardDiagram(const Card &card, int slot, std::function<void(int)> activate)
    : card(card), slot(slot), activate(std::move(activate))
{
    setObjectName(QStringLiteral("cardDiagram"));
    setAccessibleName(tr("Interaktives Schaltbild %1").arg(card.model));
    setToolTip(tr("Kontakte oder Bits zum Umschalten anklicken"));
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    animation.setDuration(340);
    animation.setStartValue(0.0);
    animation.setEndValue(1.0);
    animation.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        progress = value.toDouble();
        update();
    });
}

void CardDiagram::setStates(const QMap<int, bool> &relays, std::optional<int> lowOutput,
                            std::optional<int> highOutput, std::optional<int> lowInput,
                            std::optional<int> highInput)
{
    QMap<int, bool> next = relays;
    int newInputPorts = 0;
    const std::array outputs{lowOutput, highOutput};
    const std::array inputs{lowInput, highInput};
    for (int port = 0; port < 2; ++port) {
        const auto value = outputs.at(port) ? outputs.at(port) : inputs.at(port);
        if (!value) {
            continue;
        }
        if (!outputs.at(port)) {
            newInputPorts |= 1 << port;
        }
        for (int bit = 0; bit < 8; ++bit) {
            next.insert(port * 8 + bit, (*value & (1 << bit)) != 0);
        }
    }
    if (next == states && inputPorts == newInputPorts) {
        return;
    }

    const bool startsBetweenContacts = card.topology == CardTopology::Digital
        || card.topology == CardTopology::Breadboard || card.topology == CardTopology::Changeover;
    QMap<int, double> previous;
    for (auto it = next.cbegin(); it != next.cend(); ++it) {
        previous.insert(it.key(), states.contains(it.key()) ? position(it.key()) : (startsBetweenContacts ? 0.5 : 0.0));
    }
    animation.stop();
    starts = previous;
    states = next;
    inputPorts = newInputPorts;
    progress = 0.0;
    animation.start();
    update();
}

void CardDiagram::setSelectedChannel(int channel)
{
    if (selected != channel) {
        selected = channel;
        update();
    }
}

double CardDiagram::position(int channel) const
{
    const auto state = states.constFind(channel);
    if (state == states.cend()) {
        return 0.5;
    }
    return interpolate(starts.value(channel, 0.5), *state ? 1.0 : 0.0, progress);
}

QString CardDiagram::address(int channel) const
{
    return channelAddress(slot, channel);
}

int CardDiagram::logicalHeight() const
{
    switch (card.topology) {
    case CardTopology::Matrix: return 230;
    case CardTopology::Digital:
    case CardTopology::Breadboard: return 166;
    case CardTopology::Mux:
    case CardTopology::DualMux: return 194;
    case CardTopology::Changeover: return 52 + 57 * ((card.channels.size() + 1) / 2);
    case CardTopology::Relays: return 52 + 32 * ((card.channels.size() + 1) / 2);
    }
    return 202;
}

QSize CardDiagram::sizeHint() const
{
    return {static_cast<int>(diagramWidth), logicalHeight()};
}

QSize CardDiagram::minimumSizeHint() const
{
    return {420, logicalHeight()};
}

void CardDiagram::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto colors = palette();
    const bool dark = colors.color(QPalette::Window).lightness() < 128;
    Style style{painter, QColor(dark ? "#131e29" : "#f4f8fc"),
                QColor(dark ? "#829caf" : "#567084"),
                colors.color(QPalette::WindowText),
                QColor(dark ? "#8ce8b9" : "#07663e"),
                QColor(dark ? "#9cbffc" : "#29548a"),
                QColor(dark ? "#7bcbd8" : "#006b79")};

    painter.setPen(QPen(mix(style.background, style.wire, 0.5)));
    painter.setBrush(style.background);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);

    const qreal height = logicalHeight();
    scale = std::min(width() / diagramWidth, this->height() / height);
    origin = QPointF((width() - diagramWidth * scale) / 2, (this->height() - height * scale) / 2);
    painter.translate(origin);
    painter.scale(scale, scale);
    QFont drawingFont = font();
    drawingFont.setPixelSize(14);
    painter.setFont(drawingFont);
    hitRegions.clear();

    switch (card.topology) {
    case CardTopology::Relays:
    case CardTopology::Changeover: paintRelays(style); break;
    case CardTopology::Mux:
    case CardTopology::DualMux: paintMux(style); break;
    case CardTopology::Matrix: paintMatrix(style); break;
    case CardTopology::Digital:
    case CardTopology::Breadboard: paintDigital(style); break;
    }
}

void CardDiagram::paintRelays(Style &s)
{
    const bool changeover = card.topology == CardTopology::Changeover;
    const bool sma = card.model == QStringLiteral("44476A");
    const qreal rowHeight = changeover ? 57 : 32;
    for (int index = 0; index < card.channels.size(); ++index) {
        const int channel = card.channels.at(index);
        const qreal x = 20 + (index % 2) * 330;
        const qreal y = 35 + (index / 2) * rowHeight;
        const QRectF area(x, y - rowHeight / 2, 310, rowHeight);
        hitRegions.insert(channel, area);
        if (channel == selected) {
            s.selection(area.adjusted(0, 2, 0, -2));
        }
        const bool known = states.contains(channel);
        const double pos = position(channel);
        const QPointF left(x + 70, y);
        const QPointF pivot(x + 130, y);
        s.caption(QRectF(x + 7, y - 12, 42, 24), address(channel));
        if (changeover) {
            const double lower = known ? std::max(0.0, 1.0 - 3.0 * pos) : 0;
            const double upper = known ? std::max(0.0, 3.0 * pos - 2.0) : 0;
            const double engaged = std::max(lower, upper);
            const QPointF upperContact(x + 205, y - 12);
            const QPointF lowerContact(x + 205, y + 12);
            s.line(left, pivot, engaged, !known);
            s.line(upperContact, QPointF(x + 254, upperContact.y()), upper);
            s.line(lowerContact, QPointF(x + 254, lowerContact.y()), lower);
            s.line(pivot, QPointF(upperContact.x(), interpolate(lowerContact.y(), upperContact.y(), pos)), engaged, !known);
            s.port(upperContact, upper);
            s.port(lowerContact, lower);
            s.port(left, engaged);
            s.port(pivot, engaged);
            s.caption(QRectF(x + 255, y - 24, 42, 22), sma ? QStringLiteral("1") : QStringLiteral("NO"));
            s.caption(QRectF(x + 255, y + 2, 42, 22), sma ? QStringLiteral("2") : QStringLiteral("NC"));
        } else {
            const double closed = known ? pos : 0;
            const QPointF fixed(x + 205, y);
            const QPointF right(x + 282, y);
            s.line(left, pivot, closed, !known);
            s.line(fixed, right, closed, !known);
            s.line(pivot, QPointF(fixed.x(), y - 13 * (1 - pos)), closed, !known);
            s.port(left, closed);
            s.port(fixed, closed);
            s.port(right, closed);
        }
    }
    if (changeover) {
        const auto note = sma ? tr("COM schaltet zwischen Port 1 und Port 2")
                              : tr("Jede gemeinsame Klemme C schaltet zwischen NO und NC");
        s.caption(QRectF(20, logicalHeight() - 30, 640, 22), note);
    }
}

void CardDiagram::paintMux(Style &s)
{
    const bool dual = card.topology == CardTopology::DualMux;
    const int groupSize = dual ? 4 : 10;
    const int groups = dual ? 2 : 1;
    const qreal groupWidth = diagramWidth / groups;
    for (int group = 0; group < groups; ++group) {
        const qreal left = group * groupWidth + 22;
        const qreal spacing = (groupWidth - 44) / groupSize;
        const qreal middle = (group + 0.5) * groupWidth;
        const qreal yBus = 130;
        s.line({left + spacing / 2, yBus}, {left + spacing * (groupSize - 0.5), yBus});
        s.line({middle, yBus}, {middle, 155});
        double common = 0;
        for (int index = 0; index < groupSize; ++index) {
            const int channel = card.channels.at(group * groupSize + index);
            const bool known = states.contains(channel);
            const double pos = position(channel);
            const double closed = known ? pos : 0;
            const qreal x = left + (index + 0.5) * spacing;
            const QRectF area(x - spacing * 0.46, 16, spacing * 0.92, 107);
            hitRegions.insert(channel, area);
            if (channel == selected) {
                s.selection(area);
            }
            s.caption(QRectF(x - 22, 18, 44, 21), address(channel));
            s.line({x, 48}, {x, 75}, closed, !known);
            s.line({x, 105}, {x, yBus}, closed, !known);
            s.line({x, 75}, {x + 12 * (1 - pos), 105}, closed, !known);
            s.port({x, 48}, closed);
            s.port({x, 105}, closed);
            if (closed > 0) {
                s.line({x, yBus}, {middle, yBus}, closed);
                common = std::max(common, closed);
            }
        }
        s.line({middle, yBus}, {middle, 155}, common);
        s.port({middle, 155}, common);
        s.caption(QRectF(middle - 78, 162, 156, 22), dual
            ? QStringLiteral("COM %1").arg(group * 10, 2, 10, QLatin1Char('0'))
            : QStringLiteral("COM (HI/LO)"));
    }
}

void CardDiagram::paintMatrix(Style &s)
{
    constexpr qreal rowStart = 128;
    constexpr qreal columnStart = 180;
    constexpr qreal columnStep = 112;
    constexpr qreal firstRow = 70;
    constexpr qreal rowStep = 41;
    for (int index = 0; index < 4; ++index) {
        const qreal y = firstRow + index * rowStep;
        const qreal x = columnStart + index * columnStep;
        s.caption(QRectF(28, y - 12, 86, 24), tr("Zeile %1").arg(index));
        s.caption(QRectF(x - 52, 8, 104, 25), tr("Spalte %1").arg(index));
        s.line({rowStart, y}, {600, y});
        s.line({x, 36}, {x, 210});
        s.port({rowStart, y});
        s.port({x, 36});
    }
    for (const int channel : card.channels) {
        const qreal x = columnStart + (channel % 10) * columnStep;
        const qreal y = firstRow + (channel / 10) * rowStep;
        const double closed = states.contains(channel) ? position(channel) : 0;
        if (closed > 0) {
            s.line({rowStart, y}, {x, y}, closed);
            s.line({x, y}, {x, 36}, closed);
        }
    }
    for (const int channel : card.channels) {
        const qreal x = columnStart + (channel % 10) * columnStep;
        const qreal y = firstRow + (channel / 10) * rowStep;
        const QRectF area(x - 17, y - 11, 72, 39);
        hitRegions.insert(channel, area);
        const auto known = states.contains(channel);
        const double closed = known ? position(channel) : 0;
        if (channel == selected) {
            // Outline the contact label while leaving the bus paths visible.
            s.selection(QRectF(x + 8, y + 4, 42, 22));
        }
        if (closed > 0.02) {
            s.port({x, y}, closed);
        } else if (!known) {
            s.caption(QRectF(x - 16, y - 20, 12, 16), QStringLiteral("?"));
        }
        s.caption(QRectF(x + 8, y + 4, 42, 22), address(channel));
    }
}

void CardDiagram::paintDigital(Style &s)
{
    const bool breadboard = card.topology == CardTopology::Breadboard;
    for (int port = 0; port < 2; ++port) {
        const qreal top = 7 + port * 77;
        const auto title = breadboard ? (port == 0 ? tr("Ausgang") : tr("Eingang"))
                                     : (port == 0 ? tr("Low Byte") : tr("High Byte"));
        const int first = port * 8;
        const bool knownPort = states.contains(first);
        const bool input = inputPorts & (1 << port);
        const QString direction = !knownPort ? tr("Unbekannt")
            : input ? tr("Gelesen") : tr("Gesendet");
        s.caption(QRectF(16, top + 13, 118, 24), title, Qt::AlignLeft | Qt::AlignVCenter);
        s.caption(QRectF(16, top + 38, 118, 24), direction, Qt::AlignLeft | Qt::AlignVCenter);
        for (int bit = 0; bit < 8; ++bit) {
            const int channel = first + bit;
            const qreal x = 168 + bit * portStep;
            const QRectF area(x - 23, top, 46, 76);
            hitRegions.insert(channel, area);
            const QString pin = (breadboard ? QString() : port == 0 ? QStringLiteral("L") : QStringLiteral("H"))
                + QString::number(bit);
            s.caption(QRectF(x - 22, top, 44, 20), pin);
            const bool known = states.contains(channel);
            const double level = position(channel);
            const QColor wire = known ? mix(s.low, s.active, level) : s.wire;
            s.painter.setPen(QPen(wire, known ? 2.6 : 1.3, known ? Qt::SolidLine : Qt::DashLine));
            s.painter.drawLine(QPointF(x, top + 22), QPointF(x, top + 48));
            const QRectF valueBox(x - 15, top + 48, 30, 24);
            s.painter.setBrush(known ? mix(s.background, wire, 0.22) : s.background);
            s.painter.drawRoundedRect(valueBox, 4, 4);
            s.caption(valueBox, !known ? QStringLiteral("—") : states.value(channel) ? QStringLiteral("1") : QStringLiteral("0"));
        }
    }
}

std::optional<int> CardDiagram::hitAt(const QPointF &point) const
{
    if (scale <= 0) {
        return std::nullopt;
    }
    const QPointF logical = (point - origin) / scale;
    for (auto it = hitRegions.cbegin(); it != hitRegions.cend(); ++it) {
        if (it.value().contains(logical)) {
            return it.key();
        }
    }
    return std::nullopt;
}

void CardDiagram::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        pressed = hitAt(event->position());
        if (pressed) {
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void CardDiagram::mouseMoveEvent(QMouseEvent *event)
{
    setCursor(hitAt(event->position()) ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void CardDiagram::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const auto down = std::exchange(pressed, std::nullopt);
        if (const auto clicked = hitAt(event->position()); clicked && clicked == down) {
            activate(*clicked);
            event->accept();
            return;
        }
    }
    QWidget::mouseReleaseEvent(event);
}
