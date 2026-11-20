#include "hp3488.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QThread>
#include <stdexcept>

namespace {

QList<int> channels(int count)
{
    QList<int> result;
    for (int n = 0; n < count; ++n) {
        result.append(n);
    }
    return result;
}

const QList<int> dualMux{0, 1, 2, 3, 10, 11, 12, 13};
const QList<int> matrix{0, 1, 2, 3, 10, 11, 12, 13, 20, 21, 22, 23, 30, 31, 32, 33};

const QList<Card> cards{
    {QStringLiteral("44470A"), QString::fromUtf8(QT_TRANSLATE_NOOP("MainWindow", "Relais-Multiplexer")), CardTopology::Mux, channels(10)},
    {QStringLiteral("44471A"), QString::fromUtf8(QT_TRANSLATE_NOOP("MainWindow", "Universalrelais")), CardTopology::Relays, channels(10)},
    {QStringLiteral("44472A"), QString::fromUtf8(QT_TRANSLATE_NOOP("MainWindow", "VHF-Multiplexer")), CardTopology::DualMux, dualMux},
    {QStringLiteral("44473A"), QString::fromUtf8(QT_TRANSLATE_NOOP("MainWindow", "4 × 4 Schaltmatrix")), CardTopology::Matrix, matrix},
    {QStringLiteral("44474A"), QString::fromUtf8(QT_TRANSLATE_NOOP("MainWindow", "Digital-I/O · 16 Bit")), CardTopology::Digital, {}},
    {QStringLiteral("44475A"), QString::fromUtf8(QT_TRANSLATE_NOOP("MainWindow", "Experimentierkarte")), CardTopology::Breadboard, {}},
    {QStringLiteral("44476A"), QString::fromUtf8(QT_TRANSLATE_NOOP("MainWindow", "SMA-Schalter · 3 Kanäle")), CardTopology::Changeover, channels(3)},
    {QStringLiteral("44476B"), QString::fromUtf8(QT_TRANSLATE_NOOP("MainWindow", "SMA-Schalter · 2 Kanäle")), CardTopology::Changeover, channels(2)},
    {QStringLiteral("44477A"), QString::fromUtf8(QT_TRANSLATE_NOOP("MainWindow", "Wechselrelais · 7 Kanäle")), CardTopology::Changeover, channels(7)},
    {QStringLiteral("44478A"), QString::fromUtf8(QT_TRANSLATE_NOOP("MainWindow", "1,3 GHz Multiplexer · 50 Ω")), CardTopology::DualMux, dualMux},
    {QStringLiteral("44478B"), QString::fromUtf8(QT_TRANSLATE_NOOP("MainWindow", "1,3 GHz Multiplexer · 75 Ω")), CardTopology::DualMux, dualMux}
};

[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error(message.toUtf8().constData());
}

int portData(const QByteArray &reply, bool word)
{
    bool ok = false;
    const int value = reply.toInt(&ok);
    if (!ok || value < (word ? -32768 : 0) || value > (word ? 32767 : 255)) {
        fail(QCoreApplication::translate("Hp3488", "Unerwarteter Portwert · %1").arg(QString::fromLatin1(reply)));
    }
    return value & 0xffff;
}

} // namespace

const Card &cardFor(const QString &model)
{
    static const Card empty{};
    for (const auto &card : cards) {
        if (card.model == model) {
            return card;
        }
    }
    return empty;
}

int reportedFamily(const QString &model)
{
    const int number = model.left(5).toInt();
    if (number == 44476 || number == 44477) {
        return 44471;
    }
    return number == 44478 ? 44472 : number;
}

QStringList modelsFor(int reported)
{
    QStringList result;
    for (const auto &card : cards) {
        if (card.model.left(5).toInt() == reported || reportedFamily(card.model) == reported) {
            result.append(card.model);
        }
    }
    return result;
}

QString channelAddress(int slot, int channel)
{
    return QStringLiteral("%1%2").arg(slot + 1).arg(channel, 2, 10, QLatin1Char('0'));
}

Result Hp3488::execute(const Request &request)
{
    Result result;
    try {
        if (request.action == Action::Connect) {
            gpib = openGpib(request.target);
            const auto identity = query("ID?");
            if (identity != "HP3488A") {
                fail(QCoreApplication::translate("Hp3488", "Unerwartete Gerätekennung · %1").arg(QString::fromLatin1(identity)));
            }
            scan();
        } else if (request.action == Action::Disconnect) {
            if (gpib) {
                gpib->local();
                gpib.reset();
            }
        } else {
            if (!gpib) {
                fail(QCoreApplication::translate("Hp3488", "Keine GPIB-Verbindung."));
            }
            if (request.action == Action::Scan) {
                scan();
            } else {
                if (request.slot < 0 || request.slot >= slotCount) {
                    fail(QCoreApplication::translate("Hp3488", "Ungültiger Steckplatz."));
                }
                const Card &card = cardFor(request.model);
                const auto &slot = slotsFound.at(request.slot);
                if (card.model.isEmpty() || !modelsFor(slot.reported).contains(card.model)) {
                    fail(QCoreApplication::translate("Hp3488", "Kartentyp passt nicht zum erkannten Steckplatz."));
                }
                const auto address = channelAddress(request.slot, request.channel).toLatin1();
                const bool switchRelay = request.action == Action::SwitchRelay || request.action == Action::ToggleRelay;
                if (request.action == Action::ReadRelays || switchRelay) {
                    if (card.channels.isEmpty() ||
                        (switchRelay && !card.channels.contains(request.channel))) {
                        fail(QCoreApplication::translate("Hp3488", "Dieser Relaiskanal ist für die Karte ungültig."));
                    }
                    if (switchRelay) {
                        const bool close = request.action == Action::ToggleRelay
                            ? !readRelay(request.slot, request.channel) : request.close;
                        gpib->write((close ? "CLOSE " : "OPEN ") + address);
                        checkCommand();
                    }
                    readRelays(result, request.slot, card);
                } else {
                    const bool breadboard = card.model == QStringLiteral("44475A");
                    const bool digitalIo = card.model == QStringLiteral("44474A");
                    const bool writing = request.action == Action::WritePort;
                    const bool word = digitalIo && request.channel == 2;
                    if ((!breadboard && !digitalIo) || request.channel < 0 ||
                        (breadboard && request.channel != (writing ? 0 : 4)) ||
                        (digitalIo && request.channel > 2) ||
                        (writing && (request.value < 0 || request.value > (word ? 65535 : 255)))) {
                        fail(QCoreApplication::translate("Hp3488", "Port oder Wert ist für diese Karte ungültig."));
                    }
                    if (writing) {
                        int value = request.value;
                        if (word && value > 32767) {
                            value -= 65536;
                        }
                        gpib->write((breadboard ? "SWRITE " : "DWRITE ") + address + ',' + QByteArray::number(value));
                        checkCommand();
                    } else {
                        result.portValue = portData(query((breadboard ? "SREAD " : "DREAD ") + address), word);
                    }
                }
            }
        }
    } catch (const std::exception &error) {
        result.error = QString::fromUtf8(error.what());
        // A failed exchange can leave a stale reply in the device's output buffer.
        gpib.reset();
        result.relays.clear();
    }
    result.connected = bool(gpib);
    result.slotsFound = slotsFound;
    return result;
}

QByteArray Hp3488::query(const QByteArray &command)
{
    gpib->write(command);
    QByteArray reply = gpib->read().trimmed();
    if (reply.isEmpty()) {
        fail(QCoreApplication::translate("Hp3488", "Keine Antwort auf %1.").arg(QString::fromLatin1(command)));
    }
    return reply;
}

void Hp3488::checkCommand()
{
    QElapsedTimer timeout;
    timeout.start();
    do {
        const auto status = gpib->status();
        if (status & 0x20) {
            fail(QCoreApplication::translate("Hp3488", "Gerätefehler · %1").arg(QString::fromLatin1(query("ERROR"))));
        }
        if (status & 0x10) {
            return;
        }
        QThread::msleep(10);
    } while (timeout.elapsed() < 2000);
    fail(QCoreApplication::translate("Hp3488", "Das Gerät meldet sich nach dem Schreibbefehl nicht bereit."));
}

void Hp3488::scan()
{
    std::array<Slot, slotCount> detected{};
    static const QRegularExpression typeCode(QStringLiteral(R"((^|\s)([0-9]{5})(?:[AB])?$)"));
    for (int i = 0; i < slotCount; ++i) {
        auto &slot = detected.at(i);
        slot.identification = QString::fromLatin1(query("CTYPE " + QByteArray::number(i + 1)));
        const auto match = typeCode.match(slot.identification);
        if (!match.hasMatch()) {
            fail(QCoreApplication::translate("Hp3488", "Unerwartete Kartenkennung in Slot %1 · %2").arg(i + 1).arg(slot.identification));
        }
        slot.reported = match.captured(2).toInt();
    }

    const auto pairs = query("CPAIR").split(',');
    if (pairs.size() != 4) {
        fail(QCoreApplication::translate("Hp3488", "Unerwartete Antwort auf CPAIR."));
    }
    for (int i = 0; i < pairs.size(); i += 2) {
        bool firstOk = false, secondOk = false;
        const int a = pairs.at(i).trimmed().toInt(&firstOk);
        const int b = pairs.at(i + 1).trimmed().toInt(&secondOk);
        if (!firstOk || !secondOk || a < 0 || b < 0 || a > slotCount || b > slotCount || ((a == 0) != (b == 0))) {
            fail(QCoreApplication::translate("Hp3488", "Ungültige Kartenpaarung vom Gerät."));
        }
        if (a && b) {
            detected.at(a - 1).pairedWith = b;
            detected.at(b - 1).pairedWith = a;
        }
    }
    slotsFound = detected;
}

bool Hp3488::readRelay(int slot, int channel)
{
    const auto reply = query("VIEW " + channelAddress(slot, channel).toLatin1()).simplified();
    if (reply == "OPEN 1" || reply == "1") {
        return false;
    }
    if (reply == "CLOSED 0" || reply == "0") {
        return true;
    }
    fail(QCoreApplication::translate("Hp3488", "Unerwartete Kanalantwort · %1").arg(QString::fromLatin1(reply)));
}

void Hp3488::readRelays(Result &result, int slot, const Card &card)
{
    for (const int channel : card.channels) {
        result.relays.insert(channel, readRelay(slot, channel));
    }
}
