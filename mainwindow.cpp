#include "mainwindow.h"
#include "carddiagram.h"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMovie>
#include <QPainter>
#include <QPointer>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>

namespace {

QIcon globeIcon(bool dark)
{
    QPixmap icon(24, 24);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(dark ? "#d4e3f5" : "#27425a"), 1.35));
    painter.drawEllipse(QRectF(3, 3, 18, 18));
    painter.drawEllipse(QRectF(8, 3, 8, 18));
    painter.drawLine(QPointF(3, 12), QPointF(21, 12));
    painter.drawLine(QPointF(5, 7.5), QPointF(19, 7.5));
    painter.drawLine(QPointF(5, 16.5), QPointF(19, 16.5));
    return QIcon(icon);
}


QLabel *label(const QString &text, const char *role = "")
{
    auto *result = new QLabel(text);
    result->setTextFormat(Qt::PlainText);
    result->setObjectName(QString::fromLatin1(role));
    return result;
}

std::array<QString, slotCount> legacyModels()
{
    std::array<QString, slotCount> result;
    QFile file(QStringLiteral(":/Cards/HP3488.ini"));
    if (!file.open(QIODevice::ReadOnly)) {
        return result;
    }
    const QRegularExpression pattern(QStringLiteral(R"(^CardSlot_([1-5])/Number=.*\b(4447[0-8][AB]?)\s*$)"));
    while (!file.atEnd()) {
        const auto match = pattern.match(QString::fromLatin1(file.readLine()).trimmed());
        if (match.hasMatch()) {
            QString model = match.captured(2);
            if (model == QStringLiteral("44476")) {
                model += QLatin1Char('B'); // The supplied profile exposes channels 00 and 01.
            } else if (model.size() == 5) {
                model += QLatin1Char('A');
            }
            result.at(match.captured(1).toInt() - 1) = model;
        }
    }
    return result;
}

QString portText(const std::optional<int> &value, int hexDigits = 2)
{
    if (!value) {
        return QStringLiteral("—");
    }
    const auto hex = QString::number(*value, 16).toUpper().rightJustified(hexDigits, QLatin1Char('0'));
    return QStringLiteral("%1  ·  0x%2").arg(*value).arg(hex);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setMinimumSize(830, 590);
    resize(1080, 825);
    restoreGeometry(settings.value(QStringLiteral("window")).toByteArray());
    target.board = std::clamp(settings.value(QStringLiteral("gpib/board"), 0).toInt(), 0, 255);
    target.address = std::clamp(settings.value(QStringLiteral("gpib/address"), 7).toInt(), 0, 30);
    dark = settings.value(QStringLiteral("dark"), true).toBool();
    inverted = settings.value(QStringLiteral("attenuator/inverted"), true).toBool();
    selectedSlot = std::clamp(settings.value(QStringLiteral("slot"), 0).toInt(), 0, slotCount - 1);

    const auto defaults = legacyModels();
    for (int i = 0; i < slotCount; ++i) {
        const auto model = settings.value(QStringLiteral("cards/%1").arg(i), defaults.at(i)).toString();
        slotsShown.at(i).model = cardFor(model).model;
    }

    worker.setMaxThreadCount(1);
    worker.setExpiryTimeout(-1);
    connect(&watcher, &QFutureWatcher<Result>::finished, this, &MainWindow::finished);
    installLanguage(settings.value(QStringLiteral("language"), QStringLiteral("de")).toString());
    buildInterface();
}

void MainWindow::buildInterface()
{
    setWindowTitle(tr("HP 3488A · Schaltzentrale"));
    auto *bar = new QMenuBar;
    bar->setNativeMenuBar(false);
    const QPointer<QWidget> previousBar = menuWidget();
    if (auto *oldMenu = qobject_cast<QMenuBar *>(previousBar.data())) {
        if (auto *corner = oldMenu->cornerWidget(Qt::TopRightCorner)) {
            oldMenu->setCornerWidget(nullptr, Qt::TopRightCorner);
            corner->hide();
            corner->setParent(nullptr);
            corner->deleteLater();
        }
    }
    setMenuBar(bar);
    if (previousBar) {
        previousBar->hide();
        previousBar->setParent(nullptr);
        previousBar->deleteLater();
    }
    auto *file = bar->addMenu(tr("&Datei"));
    file->addAction(tr("&Beenden"), this, &QWidget::close, QKeySequence::Quit);
    auto *preferences = bar->addMenu(tr("&Einstellungen"));
    connectionAction = preferences->addAction(tr("GPIB-Verbindung …"), this, &MainWindow::editConnection);
    auto *appearance = preferences->addMenu(tr("Darstellung"));
    auto *themes = new QActionGroup(appearance);
    for (const bool night : {false, true}) {
        auto *action = appearance->addAction(night ? tr("Dunkel") : tr("Hell"));
        themes->addAction(action);
        action->setCheckable(true);
        action->setChecked(night == dark);
        connect(action, &QAction::triggered, this, [this, night] { setTheme(night); });
    }
    auto *right = new QMenuBar(bar);
    right->setNativeMenuBar(false);
    languageMenu = right->addMenu(QString());
    languageMenu->setObjectName(QStringLiteral("languageMenu"));
    languageMenu->menuAction()->setToolTip(tr("Sprache wählen"));
    auto *languages = new QActionGroup(languageMenu);
    const std::array choices{
        std::pair{QStringLiteral("de"), QStringLiteral("Deutsch")},
        std::pair{QStringLiteral("en"), QStringLiteral("English")},
        std::pair{QStringLiteral("fr"), QStringLiteral("Français")}
    };
    for (const auto &[code, name] : choices) {
        auto *action = languageMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(code == language);
        action->setData(code);
        languages->addAction(action);
        connect(action, &QAction::triggered, this, [this, code] { changeLanguage(code); });
    }
    auto *about = right->addMenu(tr("&Über"));
    about->setObjectName(QStringLiteral("aboutMenu"));
    about->addAction(tr("Über HP 3488A"), this, &MainWindow::showAbout)->setObjectName(QStringLiteral("aboutApplication"));
    about->addAction(tr("Über Qt"), this, [this] { QMessageBox::aboutQt(this, tr("Über Qt")); });
    about->addSeparator();
    updateAction = about->addAction(tr("Nach Updates suchen"), this, &MainWindow::checkForUpdates);
    updateAction->setObjectName(QStringLiteral("checkForUpdates"));
    updateAction->setEnabled(!updateCheckInProgress);
    bar->setCornerWidget(right, Qt::TopRightCorner);

    auto *workspace = new QWidget;
    workspace->setObjectName(QStringLiteral("workspace"));
    auto *outer = new QVBoxLayout(workspace);
    outer->setContentsMargins(22, 18, 22, 16);
    outer->setSpacing(16);

    auto *header = new QHBoxLayout;
    auto *heading = new QVBoxLayout;
    heading->setSpacing(2);
    heading->addWidget(label(QStringLiteral("HP 3488A"), "title"));
    busLabel = label(QString(), "muted");
    heading->addWidget(busLabel);
    header->addLayout(heading);
    header->addStretch();
    connectionBadge = label(QString(), "badge");
    header->addWidget(connectionBadge, 0, Qt::AlignVCenter);
    scanButton = new QPushButton(tr("Karten suchen"));
    connectButton = new QPushButton(tr("Verbinden"));
    connectButton->setObjectName(QStringLiteral("primary"));
    header->addWidget(scanButton, 0, Qt::AlignVCenter);
    header->addWidget(connectButton, 0, Qt::AlignVCenter);
    outer->addLayout(header);

    auto *content = new QHBoxLayout;
    content->setSpacing(16);
    auto *rail = new QFrame;
    rail->setObjectName(QStringLiteral("rail"));
    rail->setFixedWidth(216);
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(10, 12, 10, 12);
    railLayout->addWidget(label(tr("Steckplätze"), "section"));
    auto *group = new QButtonGroup(rail);
    for (int i = 0; i < slotCount; ++i) {
        auto *button = new QPushButton;
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("slot%1").arg(i + 1));
        button->setProperty("slotButton", true);
        group->addButton(button, i);
        railLayout->addWidget(button);
        slotButtons.at(i) = button;
        connect(button, &QPushButton::clicked, this, [this, i] {
            selectedSlot = i;
            settings.setValue(QStringLiteral("slot"), i);
            showSlot();
        });
    }
    railLayout->addStretch();
    content->addWidget(rail);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("detailScroll"));
    auto *detailContainer = new QWidget;
    detailContainer->setObjectName(QStringLiteral("detailContainer"));
    detailHost = new QVBoxLayout(detailContainer);
    detailHost->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(detailContainer);
    content->addWidget(scroll, 1);
    outer->addLayout(content, 1);

    status = label(connected ? tr("HP3488A verbunden.") : tr("Kartenansicht aus der vorhandenen Konfiguration."), "status");
    status->setWordWrap(true);
    outer->addWidget(status);
    setCentralWidget(workspace);

    connect(connectButton, &QPushButton::clicked, this, [this] {
        run(Request{connected ? Action::Disconnect : Action::Connect});
    });
    connect(scanButton, &QPushButton::clicked, this, [this] { run(Request{Action::Scan}); });
    setTheme(dark);
    showSlot();
}

MainWindow::~MainWindow()
{
    worker.waitForDone();
    qApp->removeTranslator(&translation);
    qApp->removeTranslator(&qtTranslation);
}

void MainWindow::showAbout()
{
    QMessageBox dialog(this);
    dialog.setWindowTitle(tr("Über HP 3488A"));
    dialog.setTextFormat(Qt::RichText);
    dialog.setText(QStringLiteral("<b>%1</b><br>%2").arg(windowTitle().toHtmlEscaped(),
        tr("Version %1").arg(qApp->applicationVersion())));
    dialog.setStandardButtons(QMessageBox::Ok);

    QMovie logo(QStringLiteral(":/branding/logo_g.gif"));
    if (logo.isValid()) {
        connect(&logo, &QMovie::frameChanged, &dialog, [&] {
            dialog.setIconPixmap(logo.currentPixmap());
        });
        logo.start();
    } else {
        dialog.setIconPixmap(windowIcon().pixmap(64));
    }
    dialog.exec();
}

void MainWindow::installLanguage(const QString &code)
{
    qApp->removeTranslator(&translation);
    qApp->removeTranslator(&qtTranslation);
    language = QStringLiteral("de");
    if ((code == QStringLiteral("en") || code == QStringLiteral("fr"))
        && translation.load(QStringLiteral(":/i18n/hp3488_") + code)) {
        language = code;
    }
    const QLocale selectedLocale(language);
    QLocale::setDefault(selectedLocale);
    setLocale(selectedLocale);
    if (qtTranslation.load(QStringLiteral(":/qt-i18n/qtbase_") + language)) {
        qApp->installTranslator(&qtTranslation);
    }
    if (language != QStringLiteral("de")) {
        qApp->installTranslator(&translation);
    }
}

void MainWindow::changeLanguage(const QString &code)
{
    if (code == language || busy || closing) {
        return;
    }

    const auto *pa51 = detail->findChild<QSpinBox *>(QStringLiteral("attenuationDb"));
    const auto *hp33321 = detail->findChild<QComboBox *>(QStringLiteral("attenuation33321"));
    const auto *rawPort = detail->findChild<QComboBox *>(QStringLiteral("rawPort"));
    const auto *rawValue = detail->findChild<QSpinBox *>(QStringLiteral("rawValue"));
    const int pa51Value = pa51 ? pa51->value() : 0;
    const int hp33321Index = hp33321 ? hp33321->currentIndex() : 0;
    const int rawIndex = rawPort ? rawPort->currentIndex() : 0;
    const int rawOutputValue = rawValue ? rawValue->value() : 0;
    const bool l6 = attenuatorL6 && attenuatorL6->isChecked();
    const bool l7 = attenuatorL7 && attenuatorL7->isChecked();
    const int channel = selectedChannel;

    installLanguage(code);
    settings.setValue(QStringLiteral("language"), language);
    takeCentralWidget()->deleteLater();
    detail = nullptr;
    buildInterface();
    if (auto *editor = detail->findChild<QSpinBox *>(QStringLiteral("attenuationDb"))) {
        editor->setValue(pa51Value);
    }
    if (auto *editor = detail->findChild<QComboBox *>(QStringLiteral("attenuation33321"))) {
        editor->setCurrentIndex(hp33321Index);
    }
    if (auto *port = detail->findChild<QComboBox *>(QStringLiteral("rawPort"))) {
        port->setCurrentIndex(rawIndex);
        detail->findChild<QSpinBox *>(QStringLiteral("rawValue"))->setValue(rawOutputValue);
    }
    if (attenuatorL6 && attenuatorL7) {
        attenuatorL6->setChecked(l6);
        attenuatorL7->setChecked(l7);
    }
    if (relayButtons.contains(channel)) {
        selectedChannel = channel;
        updateRelaySelection();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    settings.setValue(QStringLiteral("window"), saveGeometry());
    closing = true;
    if (!busy && !connected) {
        event->accept();
        return;
    }
    event->ignore();
    if (!busy) {
        run(Request{Action::Disconnect});
    }
}

void MainWindow::showSlot()
{
    if (detail) {
        detailHost->removeWidget(detail);
        detail->hide();
        detail->setParent(nullptr);
        detail->deleteLater();
    }
    hardwareActions.clear();
    relayButtons.clear();
    relaySelection = nullptr;
    portStatus = nullptr;
    diagram = nullptr;
    attenuatorL6 = nullptr;
    attenuatorL7 = nullptr;
    auto &slot = slotsShown.at(selectedSlot);
    const Card &card = cardFor(slot.model);

    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("panel"));
    detail = panel;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(13);
    auto *top = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    titles->addWidget(label(tr("Slot %1").arg(selectedSlot + 1), "muted"));
    titles->addWidget(label(card.model.isEmpty() ? tr("Keine Karte ausgewählt") : tr(card.name.toUtf8().constData()), "cardTitle"));
    const QString identity = connected ? slot.hardware.identification : QString();
    if (!identity.isEmpty()) {
        titles->addWidget(label(identity, "muted"));
    }
    if (connected && slot.hardware.pairedWith) {
        titles->addWidget(label(tr("Gekoppelt mit Slot %1").arg(slot.hardware.pairedWith), "notice"));
    }
    top->addLayout(titles, 1);
    if (connected && !card.model.isEmpty()) {
        const auto candidates = modelsFor(slot.hardware.reported);
        if (candidates.size() > 1) {
            auto *variant = new QComboBox;
            variant->setObjectName(QStringLiteral("cardVariant"));
            variant->setAccessibleName(tr("Kartentyp"));
            variant->setToolTip(tr("Kartentyp am Modul ablesen"));
            variant->addItems(candidates);
            variant->setCurrentText(card.model);
            top->addWidget(variant, 0, Qt::AlignTop);
            connect(variant, &QComboBox::textActivated, this, [this](const QString &model) {
                auto &current = slotsShown.at(selectedSlot);
                current.model = model;
                current.relays.clear();
                settings.setValue(QStringLiteral("cards/%1").arg(selectedSlot), model);
                showSlot();
            });
        } else {
            top->addWidget(label(card.model, "model"), 0, Qt::AlignTop);
        }
    } else if (!card.model.isEmpty()) {
        top->addWidget(label(card.model, "model"), 0, Qt::AlignTop);
    }
    layout->addLayout(top);

    auto *controls = new QVBoxLayout;
    controls->setSpacing(12);
    if (card.model == QStringLiteral("44474A") || card.model == QStringLiteral("44475A")) {
        buildDigital(controls, card.model == QStringLiteral("44475A"));
    } else if (!card.channels.isEmpty()) {
        selectedChannel = card.channels.front();
        buildRelays(controls, card);
    } else {
        auto *empty = label(connected && slot.hardware.reported != 0
                            ? tr("Für diese Kartenkennung sind keine Bedienelemente eingerichtet.")
                            : tr("Dieser Steckplatz ist leer."));
        empty->setWordWrap(true);
        controls->addWidget(empty);
    }
    layout->addLayout(controls);
    if (!card.model.isEmpty() && !(selectedSlot == 0 && card.topology == CardTopology::Digital)) {
        addDiagram(layout, card);
    }
    layout->addStretch();
    detailHost->addWidget(detail, 1);
    refresh();
}

void MainWindow::addDiagram(QVBoxLayout *layout, const Card &card)
{
    const bool digital = card.topology == CardTopology::Digital || card.topology == CardTopology::Breadboard;
    auto *caption = new QHBoxLayout;
    caption->addWidget(label(tr("Schaltbild"), "section"));

    QString explanation = digital ? tr("Grün = 1   ·   Blau = 0   ·   Gestrichelt = unbekannt")
                                  : tr("Grün = geschalteter Pfad   ·   Gestrichelt / ? = unbekannt");
    if (card.topology == CardTopology::Matrix || card.topology == CardTopology::Mux) {
        explanation += QStringLiteral("   ·   HI/LO");
    }
    auto *legend = label(explanation, "muted");
    legend->setWordWrap(true);
    legend->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    caption->addWidget(legend, 1);
    layout->addLayout(caption);
    diagram = new CardDiagram(card, selectedSlot, [this](int index) { diagramClicked(index); });
    layout->addWidget(diagram);
}

void MainWindow::buildRelays(QVBoxLayout *layout, const Card &card)
{
    auto *header = new QHBoxLayout;
    header->addWidget(label(card.model == QStringLiteral("44473A") ? tr("Kreuzpunkte") : tr("Kanäle"), "section"));
    header->addStretch();
    auto *read = new QPushButton(tr("Zustände lesen"));
    hardwareActions.append(read);
    header->addWidget(read);
    layout->addLayout(header);

    auto *grid = new QGridLayout;
    grid->setSpacing(6);
    auto *group = new QButtonGroup(detail);
    int position = 0;
    const int columns = card.model == QStringLiteral("44471A") || card.model == QStringLiteral("44470A") ? 5 : 4;
    for (const int channel : card.channels) {
        auto *button = new QPushButton;
        button->setCheckable(true);
        button->setProperty("channel", true);
        button->setObjectName(QStringLiteral("channel%1").arg(channel));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        group->addButton(button, channel);
        relayButtons.insert(channel, button);
        grid->addWidget(button, position / columns, position % columns);
        ++position;
        connect(button, &QPushButton::clicked, this, [this, channel] {
            selectedChannel = channel;
            updateRelaySelection();
        });
    }
    layout->addLayout(grid);

    relaySelection = label(QString(), "badge");
    auto *actions = new QHBoxLayout;
    actions->addWidget(relaySelection);
    actions->addStretch();
    for (const bool close : {false, true}) {
        auto *button = new QPushButton(close ? tr("Schließen") : tr("Öffnen"));
        if (close) {
            button->setObjectName(QStringLiteral("primary"));
        }
        actions->addWidget(button);
        hardwareActions.append(button);
        connect(button, &QPushButton::clicked, this, [this, close] {
            Request request{Action::SwitchRelay};
            request.channel = selectedChannel;
            request.close = close;
            run(request);
        });
    }
    layout->addLayout(actions);
    connect(read, &QPushButton::clicked, this, [this] { run(Request{Action::ReadRelays}); });
}

void MainWindow::buildDigital(QVBoxLayout *layout, bool breadboard)
{
    const auto &slot = slotsShown.at(selectedSlot);
    auto *sections = new QHBoxLayout;
    sections->setAlignment(Qt::AlignTop);
    const bool slotOne = selectedSlot == 0 && !breadboard;
    if (!breadboard) {
        auto *attenuator = new QGroupBox(slotOne ? tr("PA51 · Attenuator 0–63 dB") : tr("Attenuator · 0–63 dB"));
        auto *group = new QVBoxLayout(attenuator);
        auto *setpoint = new QHBoxLayout;
        auto *slider = new QSlider(Qt::Horizontal);
        auto *db = new QSpinBox;
        db->setObjectName(QStringLiteral("attenuationDb"));
        db->setAccessibleName(tr("Gewünschte Dämpfung"));
        slider->setAccessibleName(tr("Gewünschte Dämpfung"));
        slider->setRange(0, 63);
        db->setRange(0, 63);
        db->setSuffix(QStringLiteral(" dB"));
        db->setMinimumWidth(88);
        if (slot.lowOutput) {
            const int attenuationBits = *slot.lowOutput & 63;
            db->setValue(inverted ? 63 - attenuationBits : attenuationBits);
            slider->setValue(db->value());
        }
        setpoint->addWidget(slider, 1);
        setpoint->addWidget(db);
        group->addLayout(setpoint);
        connect(slider, &QSlider::valueChanged, db, &QSpinBox::setValue);
        connect(db, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
        auto *polarity = new QCheckBox(tr("L0–L5 invertieren"));
        polarity->setChecked(inverted);
        group->addWidget(polarity);
        auto *otherBits = new QHBoxLayout;
        auto *otherLabel = label(tr("Weitere Ausgänge im Low Byte"), "muted");
        otherLabel->setWordWrap(true);
        otherBits->addWidget(otherLabel, 1);
        auto *l6 = new QCheckBox(QStringLiteral("L6"));
        auto *l7 = new QCheckBox(QStringLiteral("L7"));
        attenuatorL6 = l6;
        attenuatorL7 = l7;
        if (slot.lowOutput) {
            l6->setChecked(*slot.lowOutput & 64);
            l7->setChecked(*slot.lowOutput & 128);
        }
        otherBits->addWidget(l6);
        otherBits->addWidget(l7);
        group->addLayout(otherBits);
        auto *send = new QPushButton(tr("Dämpfung setzen"));
        send->setObjectName(QStringLiteral("primary"));
        auto *preview = label(QString(), "muted");
        const auto output = [db, polarity, l6, l7] {
            const int code = polarity->isChecked() ? 63 - db->value() : db->value();
            return code | (l6->isChecked() ? 64 : 0) | (l7->isChecked() ? 128 : 0);
        };
        const auto update = [preview, output] {
            preview->setText(tr("L0–L5 · 1–32 dB   |   Low Byte %1").arg(portText(output())));
        };
        connect(db, qOverload<int>(&QSpinBox::valueChanged), this, update);
        for (auto *check : {polarity, l6, l7}) {
            connect(check, &QCheckBox::toggled, this, update);
        }
        connect(polarity, &QCheckBox::toggled, this, [this](bool value) {
            inverted = value;
            settings.setValue(QStringLiteral("attenuator/inverted"), value);
        });
        connect(send, &QPushButton::clicked, this, [this, output] {
            Request request{Action::WritePort};
            request.channel = 0;
            request.value = output();
            run(request);
        });
        group->addWidget(preview);
        group->addWidget(send);
        hardwareActions.append(send);
        update();
        sections->addWidget(attenuator, 1);
    }
    if (slotOne) {
        sections->addWidget(build33321(slot.highOutput), 1);
        layout->addLayout(sections);
    }

    auto *raw = new QGroupBox(breadboard ? tr("8-Bit-Port") : slotOne ? tr("Direkte Digitalsteuerung") : tr("Digitalports"));
    auto *controls = new QVBoxLayout(raw);
    auto *fields = new QHBoxLayout;
    auto *port = new QComboBox;
    port->setObjectName(QStringLiteral("rawPort"));
    port->setAccessibleName(tr("Digitalport"));
    if (breadboard) {
        port->addItem(QStringLiteral("8 Bit"), 0);
    } else {
        port->addItem(tr("Low Byte · L0–L7"), 0);
        port->addItem(tr("High Byte · H0–H7"), 1);
        port->addItem(tr("16-Bit-Wort"), 2);
    }
    auto *value = new QSpinBox;
    value->setObjectName(QStringLiteral("rawValue"));
    value->setAccessibleName(tr("Digitaler Ausgabewert"));
    value->setMinimumWidth(88);
    const auto portChanged = [port, value] {
        value->setRange(0, port->currentData().toInt() == 2 ? 65535 : 255);
    };
    portChanged();
    connect(port, qOverload<int>(&QComboBox::currentIndexChanged), this, portChanged);
    fields->addWidget(port, 1);
    fields->addWidget(value);
    controls->addLayout(fields);
    auto *buttons = slotOne ? fields : new QHBoxLayout;
    auto *write = new QPushButton(tr("Schreiben"));
    auto *read = new QPushButton(breadboard ? tr("Eingang lesen") : tr("Als Eingang lesen"));
    if (!breadboard) {
        read->setToolTip(tr("Schaltet den gewählten Port beim Lesen auf Eingang."));
    }
    buttons->addWidget(write);
    buttons->addWidget(read);
    hardwareActions.append(write);
    hardwareActions.append(read);
    if (!slotOne) {
        controls->addLayout(buttons);
    }
    portStatus = label(QString(), "muted");
    portStatus->setWordWrap(true);
    controls->addWidget(portStatus);
    connect(write, &QPushButton::clicked, this, [this, port, value] {
        Request request{Action::WritePort};
        request.channel = port->currentData().toInt();
        request.value = value->value();
        run(request);
    });
    connect(read, &QPushButton::clicked, this, [this, port, breadboard] {
        Request request{Action::ReadPort};
        request.channel = breadboard ? 4 : port->currentData().toInt();
        run(request);
    });
    if (slotOne) {
        addDiagram(controls, cardFor(slot.model));
        layout->addWidget(raw);
    } else {
        sections->addWidget(raw, 1);
        layout->addLayout(sections);
    }
}

QGroupBox *MainWindow::build33321(const std::optional<int> &highOutput)
{
    auto *box = new QGroupBox(tr("33321 · Attenuator 0–35 dB"));
    auto *layout = new QVBoxLayout(box);
    auto *row = new QHBoxLayout;
    row->addWidget(label(tr("Dämpfung"), "muted"), 1);
    auto *selector = new QComboBox;
    selector->setObjectName(QStringLiteral("attenuation33321"));
    selector->setAccessibleName(tr("33321-Dämpfung"));
    selector->setMinimumWidth(88);

    // High Byte, active low. The 25 dB position uses H3 and H4.
    constexpr std::array<int, 8> bytes{0xff, 0xfb, 0xf7, 0xf3, 0xef, 0xe7, 0xe3, 0xdf};
    for (int i = 0; i < static_cast<int>(bytes.size()); ++i) {
        selector->addItem(tr("%1 dB").arg(i * 5), bytes.at(i));
    }
    if (highOutput) {
        const int index = selector->findData(*highOutput);
        if (index >= 0) {
            selector->setCurrentIndex(index);
        }
    }
    row->addWidget(selector);
    layout->addLayout(row);

    auto *preview = label(QString(), "muted");
    const auto update = [preview, selector] {
        preview->setText(tr("H2–H5 · aktiv Low   |   High Byte %1").arg(portText(selector->currentData().toInt())));
    };
    connect(selector, qOverload<int>(&QComboBox::currentIndexChanged), this, update);
    layout->addWidget(preview);
    layout->addStretch();
    auto *send = new QPushButton(tr("Dämpfung setzen"));
    send->setObjectName(QStringLiteral("apply33321"));
    send->setProperty("primaryButton", true);
    connect(send, &QPushButton::clicked, this, [this, selector] {
        Request request{Action::WritePort};
        request.channel = 1;
        request.value = selector->currentData().toInt();
        run(request);
    });
    hardwareActions.append(send);
    layout->addWidget(send);
    update();
    return box;
}

void MainWindow::updateRelaySelection()
{
    const auto &states = slotsShown.at(selectedSlot).relays;
    for (auto it = relayButtons.cbegin(); it != relayButtons.cend(); ++it) {
        const QString address = channelAddress(selectedSlot, it.key());
        const auto state = states.constFind(it.key());
        const QString stateText = state == states.cend() ? QStringLiteral("—")
                                 : *state ? tr("Zu") : tr("Offen");
        auto *button = it.value();
        button->setText(QStringLiteral("%1\n%2").arg(address, stateText));
        button->setProperty("closed", state != states.cend() && *state);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->setChecked(it.key() == selectedChannel);
        button->setAccessibleName(tr("Kanal %1 · %2").arg(address, stateText));
    }
    if (relaySelection) {
        if (diagram) {
            diagram->setSelectedChannel(selectedChannel);
        }
        relaySelection->setText(tr("Ausgewählter Kanal %1").arg(channelAddress(selectedSlot, selectedChannel)));
    }
}

void MainWindow::diagramClicked(int index)
{
    if (busy) {
        return;
    }
    const auto &slot = slotsShown.at(selectedSlot);
    const auto &card = cardFor(slot.model);
    if (card.channels.contains(index)) {
        selectedChannel = index;
        updateRelaySelection();
        if (connected) {
            Request request{Action::ToggleRelay};
            request.channel = index;
            run(request);
        }
        return;
    }
    if (!connected || index < 0 || index >= 16) {
        return;
    }
    if (card.topology == CardTopology::Breadboard && index >= 8) {
        Request request{Action::ReadPort};
        request.channel = 4;
        run(request);
        return;
    }

    const int port = index / 8;
    const auto output = port == 0 ? slot.lowOutput : slot.highOutput;
    if (!output) {
        status->setText(tr("Ausgangswert unbekannt. Das Byte zuerst über die Bedienung schreiben."));
        return;
    }
    Request request{Action::WritePort};
    request.channel = port;
    request.value = *output ^ (1 << (index % 8));
    run(request);
}

void MainWindow::updatePortStatus()
{
    if (!portStatus) {
        return;
    }
    const auto &slot = slotsShown.at(selectedSlot);
    if (selectedSlot == 0 && slot.model == QStringLiteral("44474A")) {
        portStatus->setText(tr("Zuletzt gesendet · Low %1 · High %2").arg(portText(slot.lowOutput), portText(slot.highOutput)));
        return;
    }
    portStatus->setText(slot.model == QStringLiteral("44475A")
        ? tr("Zuletzt gesendet · %1").arg(portText(slot.lowOutput))
        : tr("Zuletzt gesendet\nLow  %1     High  %2").arg(portText(slot.lowOutput), portText(slot.highOutput)));
}

void MainWindow::refresh()
{
    busLabel->setText(tr("Schaltzentrale    ·    NI-488.2    ·    GPIB %1 / %2").arg(target.board).arg(target.address));
    connectionBadge->setText(busy ? tr("GPIB aktiv …") : connected ? tr("● Verbunden") : tr("○ Getrennt"));
    connectButton->setText(connected ? tr("Trennen") : tr("Verbinden"));
    connectButton->setEnabled(!busy);
    scanButton->setEnabled(connected && !busy);
    connectionAction->setEnabled(!connected && !busy);
    languageMenu->menuAction()->setEnabled(!busy && !closing);
    for (int i = 0; i < slotCount; ++i) {
        auto *button = slotButtons.at(i);
        const auto &slot = slotsShown.at(i);
        QString caption = tr("Slot %1    ·    %2").arg(i + 1).arg(slot.model.isEmpty()
            ? (slot.hardware.reported ? QString::number(slot.hardware.reported) : tr("Leer"))
            : slot.model);
        const QString name = tr(cardFor(slot.model).name.toUtf8().constData());
        if (!name.isEmpty()) {
            caption += QLatin1Char('\n') + name.section(QStringLiteral(" · "), 0, 0);
        }
        button->setText(caption);
        button->setToolTip(name);
        button->setChecked(i == selectedSlot);
        button->setEnabled(!busy);
    }
    for (auto *control : hardwareActions) {
        control->setEnabled(connected && !busy);
    }
    if (auto *variant = detail->findChild<QComboBox *>(QStringLiteral("cardVariant"))) {
        variant->setEnabled(!busy);
    }
    updateRelaySelection();
    updatePortStatus();
    if (diagram) {
        const auto &slot = slotsShown.at(selectedSlot);
        diagram->setStates(slot.relays, slot.lowOutput, slot.highOutput, slot.lowInput, slot.highInput);
    }
}

void MainWindow::run(Request request)
{
    if (busy) {
        return;
    }
    request.target = target;
    request.slot = selectedSlot;
    request.model = slotsShown.at(selectedSlot).model;
    pending = request;
    busy = true;
    status->setText(request.action == Action::Connect || request.action == Action::Scan
        ? tr("Gerätekennung und Karten werden abgefragt …")
        : tr("GPIB-Befehl läuft …"));
    refresh();
    watcher.setFuture(QtConcurrent::run(&worker, [this, request] { return device.execute(request); }));
}

void MainWindow::finished()
{
    const Result result = watcher.result();
    connected = result.connected;
    busy = false;
    const auto action = pending.action;
    if (!result.error.isEmpty() || action == Action::Disconnect || action == Action::Connect || action == Action::Scan) {
        for (int i = 0; i < slotCount; ++i) {
            auto &slot = slotsShown.at(i);
            slot.relays.clear();
            slot.lowOutput.reset();
            slot.highOutput.reset();
            slot.lowInput.reset();
            slot.highInput.reset();
            slot.hardware = connected ? result.slotsFound.at(i) : Slot{};
            if (connected) {
                const auto models = modelsFor(slot.hardware.reported);
                if (!models.contains(slot.model)) {
                    slot.model = models.value(0);
                }
            }
        }
        showSlot();
    }

    if (!result.error.isEmpty()) {
        status->setText(result.error);
    } else if (action == Action::Connect || action == Action::Scan) {
        int cards = 0;
        for (const auto &slot : slotsShown) {
            cards += slot.hardware.reported != 0;
        }
        status->setText(tr("HP3488A erkannt · %1 von 5 Slots belegt").arg(cards));
    } else if (action == Action::Disconnect) {
        status->setText(tr("Verbindung getrennt."));
    } else {
        auto &slot = slotsShown.at(pending.slot);
        if (action == Action::ReadRelays || action == Action::SwitchRelay || action == Action::ToggleRelay) {
            slot.relays = result.relays;
            if (action != Action::ReadRelays && slot.hardware.pairedWith) {
                slotsShown.at(slot.hardware.pairedWith - 1).relays.clear();
            }
            status->setText(tr("Kanalzustände von Slot %1 gelesen.").arg(pending.slot + 1));
        } else if (action == Action::WritePort) {
            if (pending.channel != 1) {
                slot.lowOutput = pending.value & 0xff;
                slot.lowInput.reset();
                if (auto *db = detail->findChild<QSpinBox *>(QStringLiteral("attenuationDb"))) {
                    const int attenuationBits = *slot.lowOutput & 63;
                    db->setValue(inverted ? 63 - attenuationBits : attenuationBits);
                }
                if (attenuatorL6 && attenuatorL7) {
                    attenuatorL6->setChecked(*slot.lowOutput & 64);
                    attenuatorL7->setChecked(*slot.lowOutput & 128);
                }
            }
            if (pending.channel != 0) {
                slot.highOutput = pending.channel == 1 ? pending.value : pending.value >> 8;
                slot.highInput.reset();
                if (auto *selector = detail->findChild<QComboBox *>(QStringLiteral("attenuation33321"))) {
                    const int index = selector->findData(*slot.highOutput);
                    if (index >= 0) {
                        selector->setCurrentIndex(index);
                    }
                }
            }
            if (slot.hardware.pairedWith) {
                auto &paired = slotsShown.at(slot.hardware.pairedWith - 1);
                paired.lowOutput.reset();
                paired.highOutput.reset();
                paired.lowInput.reset();
                paired.highInput.reset();
            }
            status->setText(tr("Slot %1 · Port %2 · Wert %3 gesendet.")
                .arg(pending.slot + 1).arg(pending.channel, 2, 10, QLatin1Char('0')).arg(pending.value));
        } else {
            if (pending.model == QStringLiteral("44474A")) {
                if (pending.channel != 1) {
                    slot.lowOutput.reset();
                    slot.lowInput = result.portValue & 0xff;
                }
                if (pending.channel != 0) {
                    slot.highOutput.reset();
                    slot.highInput = pending.channel == 1 ? result.portValue : result.portValue >> 8;
                }
            } else {
                slot.highInput = result.portValue;
            }
            status->setText(tr("Slot %1 · Port %2 · Eingang %3")
                .arg(pending.slot + 1).arg(pending.channel, 2, 10, QLatin1Char('0')).arg(portText(result.portValue, pending.channel == 2 ? 4 : 2)));
        }
    }
    refresh();
    if (closing) {
        if (connected) {
            run(Request{Action::Disconnect});
        } else {
            close();
        }
    }
}

void MainWindow::editConnection()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("GPIB-Verbindung"));
    auto *form = new QFormLayout(&dialog);
    auto *board = new QSpinBox;
    board->setRange(0, 255);
    board->setValue(target.board);
    form->addRow(QStringLiteral("NI-488.2 Bus"), board);
    auto *address = new QSpinBox;
    address->setRange(0, 30);
    address->setValue(target.address);
    form->addRow(tr("Primäradresse"), address);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        target = {board->value(), address->value()};
        settings.setValue(QStringLiteral("gpib/board"), target.board);
        settings.setValue(QStringLiteral("gpib/address"), target.address);
        refresh();
    }
}

void MainWindow::setTheme(bool night)
{
    dark = night;
    settings.setValue(QStringLiteral("dark"), dark);
    const auto color = [night](const char *d, const char *l) { return QColor(night ? d : l); };
    QPalette palette;
    palette.setColor(QPalette::Window, color("#111820", "#edf2f6"));
    palette.setColor(QPalette::WindowText, color("#eef5fb", "#162737"));
    palette.setColor(QPalette::Base, color("#131e29", "#ffffff"));
    palette.setColor(QPalette::Text, palette.color(QPalette::WindowText));
    palette.setColor(QPalette::Button, color("#243443", "#e5edf3"));
    palette.setColor(QPalette::ButtonText, palette.color(QPalette::WindowText));
    palette.setColor(QPalette::Highlight, color("#2e6773", "#b3e1e2"));
    palette.setColor(QPalette::HighlightedText, color("#ffffff", "#122e35"));
    palette.setColor(QPalette::ToolTipBase, palette.color(QPalette::Base));
    palette.setColor(QPalette::ToolTipText, palette.color(QPalette::WindowText));
    const QColor muted = color("#aebecb", "#486072");
    for (const auto role : {QPalette::WindowText, QPalette::ButtonText, QPalette::Text}) {
        palette.setColor(QPalette::Disabled, role, muted);
    }
    qApp->setPalette(palette);

    QString style = QStringLiteral(R"(
        QWidget#workspace, QWidget#detailContainer, QScrollArea#detailScroll { background: $background; }
        QFrame#panel, QFrame#rail { background: $surface; border: 1px solid $line; border-radius: 12px; }
        QLabel#title { font-size: 33px; font-weight: 700; }
        QLabel#cardTitle { font-size: 24px; font-weight: 700; }
        QLabel#section { font-size: 14px; font-weight: 700; padding: 4px; }
        QLabel#muted { color: $muted; }
        QLabel#badge, QLabel#model, QLabel#notice { padding: 6px 10px; border: 1px solid $line; border-radius: 7px; background: $background; }
        QLabel#status { padding: 8px 10px; color: $muted; }
        QPushButton { padding: 7px 11px; min-height: 23px; border: 1px solid $line; border-radius: 7px; background: $button; color: $text; }
        QPushButton:hover { border-color: $accent; }
        QPushButton:focus { border: 1px solid $accent; }
        QPushButton#primary, QPushButton[primaryButton="true"] { background: $accent; color: $accentText; font-weight: 700; border-color: $accent; }
        QPushButton[slotButton="true"] { text-align: left; padding: 10px 12px; }
        QPushButton[slotButton="true"]:checked, QPushButton[channel="true"]:checked { border: 1px solid $accent; background: $selected; }
        QPushButton[channel="true"] { min-height: 32px; padding: 5px 8px; }
        QPushButton[channel="true"][closed="true"] { color: $active; font-weight: 700; }
        QPushButton:disabled, QPushButton#primary:disabled, QPushButton[primaryButton="true"]:disabled { background: $background; border-color: $line; color: $muted; }
        QGroupBox { border: 1px solid $line; border-radius: 9px; margin-top: 14px; padding: 10px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
    )");
    const std::array replacements{
        std::pair{"$background", color("#111820", "#edf2f6")},
        std::pair{"$surface", color("#1a2632", "#fbfdff")},
        std::pair{"$line", color("#415567", "#a8b9c6")},
        std::pair{"$muted", muted},
        std::pair{"$button", palette.color(QPalette::Button)},
        std::pair{"$text", palette.color(QPalette::ButtonText)},
        std::pair{"$accentText", color("#102328", "#ffffff")},
        std::pair{"$accent", color("#8ce0de", "#006a71")},
        std::pair{"$selected", color("#244b55", "#d0edee")},
        std::pair{"$active", color("#9be6c1", "#11603b")}
    };
    for (const auto &[name, value] : replacements) {
        style.replace(QString::fromLatin1(name), value.name());
    }
    qApp->setStyleSheet(style);
    languageMenu->setIcon(globeIcon(dark));
}
