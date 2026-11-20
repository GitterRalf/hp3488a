#pragma once

#include "hp3488.h"

#include <QCoreApplication>
#include <QVariantAnimation>
#include <QWidget>
#include <functional>
#include <optional>

class CardDiagram final : public QWidget
{
    Q_DECLARE_TR_FUNCTIONS(CardDiagram)

public:
    CardDiagram(const Card &card, int slot, std::function<void(int)> activate);

    void setStates(const QMap<int, bool> &relays, std::optional<int> lowOutput,
                   std::optional<int> highOutput, std::optional<int> lowInput,
                   std::optional<int> highInput);
    void setSelectedChannel(int channel);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    struct Style;
    void paintRelays(Style &style);
    void paintMux(Style &style);
    void paintMatrix(Style &style);
    void paintDigital(Style &style);
    int logicalHeight() const;
    std::optional<int> hitAt(const QPointF &point) const;
    double position(int channel) const;
    QString address(int channel) const;

    const Card &card;
    int slot;
    std::function<void(int)> activate;
    QMap<int, bool> states;
    QMap<int, double> starts;
    QMap<int, QRectF> hitRegions;
    QVariantAnimation animation;
    QPointF origin;
    double scale = 1.0;
    double progress = 1.0;
    int inputPorts = 0;
    int selected = -1;
    std::optional<int> pressed;
};
