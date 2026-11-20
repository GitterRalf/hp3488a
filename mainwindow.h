#pragma once

#include "hp3488.h"
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QSettings>
#include <QThreadPool>
#include <QTranslator>
#include <optional>

class CardDiagram;
class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QGroupBox;
class QMenu;
class QPushButton;
class QSlider;
class QSpinBox;
class QVBoxLayout;

class MainWindow final : public QMainWindow
{
    Q_DECLARE_TR_FUNCTIONS(MainWindow)

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct DisplaySlot {
        QString model;
        Slot hardware;
        QMap<int, bool> relays;
        std::optional<int> lowOutput;
        std::optional<int> highOutput;
        std::optional<int> lowInput;
        std::optional<int> highInput;
    };

    void buildInterface();
    void showAbout();
    void checkForUpdates();
    void checkPrivateRelease();
    void finishUpdateCheck(const QByteArray &data, const QString &error = {});
    void installLanguage(const QString &code);
    void changeLanguage(const QString &code);
    QGroupBox *build33321(const std::optional<int> &highOutput);
    void setTheme(bool dark);
    void editConnection();
    void showSlot();
    void buildRelays(QVBoxLayout *layout, const Card &card);
    void buildDigital(QVBoxLayout *layout, bool breadboard);
    void addDiagram(QVBoxLayout *layout, const Card &card);
    void updateRelaySelection();
    void diagramClicked(int index);
    void updatePortStatus();
    void refresh();
    void run(Request request);
    void finished();

    QSettings settings;
    QTranslator translation;
    QTranslator qtTranslation;
    QString language;
    QMenu *languageMenu = nullptr;
    QAction *updateAction = nullptr;
    bool updateCheckInProgress = false;
    GpibTarget target;
    std::array<DisplaySlot, slotCount> slotsShown{};
    std::array<QPushButton *, slotCount> slotButtons{};
    int selectedSlot = 0;
    int selectedChannel = 0;
    bool connected = false;
    bool busy = false;
    bool closing = false;
    bool dark = true;
    bool inverted = true;
    QThreadPool worker;
    Hp3488 device;
    QFutureWatcher<Result> watcher;
    Request pending{Action::Connect};

    QWidget *detail = nullptr;
    CardDiagram *diagram = nullptr;
    QVBoxLayout *detailHost = nullptr;
    QLabel *connectionBadge = nullptr;
    QLabel *busLabel = nullptr;
    QLabel *status = nullptr;
    QLabel *relaySelection = nullptr;
    QLabel *portStatus = nullptr;
    QCheckBox *attenuatorL6 = nullptr;
    QCheckBox *attenuatorL7 = nullptr;
    QPushButton *connectButton = nullptr;
    QPushButton *scanButton = nullptr;
    QAction *connectionAction = nullptr;
    QList<QWidget *> hardwareActions;
    QMap<int, QPushButton *> relayButtons;
};
