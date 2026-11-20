#pragma once

#include "gpib.h"

#include <QList>
#include <QMap>
#include <QString>
#include <array>

constexpr int slotCount = 5;

enum class CardTopology { Relays, Changeover, Mux, DualMux, Matrix, Digital, Breadboard };

struct Card
{
    QString model;
    QString name;
    CardTopology topology = CardTopology::Relays;
    QList<int> channels;
};

const Card &cardFor(const QString &model);
QStringList modelsFor(int reported);
int reportedFamily(const QString &model);
QString channelAddress(int slot, int channel);

enum class Action { Connect, Scan, Disconnect, ReadRelays, SwitchRelay, ToggleRelay, WritePort, ReadPort };

struct Request
{
    Action action;
    GpibTarget target;
    int slot = 0;
    QString model;
    int channel = 0;
    int value = 0;
    bool close = false;
};

struct Slot
{
    int reported = 0;
    QString identification;
    int pairedWith = 0;
};

struct Result
{
    bool connected = false;
    std::array<Slot, slotCount> slotsFound{};
    QMap<int, bool> relays;
    int portValue = -1;
    QString error;
};

class Hp3488
{
public:
    Result execute(const Request &request);

private:
    QByteArray query(const QByteArray &command);
    void checkCommand();
    void scan();
    bool readRelay(int slot, int channel);
    void readRelays(Result &result, int slot, const Card &card);
    std::unique_ptr<GpibSession> gpib;
    std::array<Slot, slotCount> slotsFound{};
};
