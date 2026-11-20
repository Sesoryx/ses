#include "ui/panels/ModelManagerPanel.h"

#include "inference/Catalog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace ses::ui {

ModelManagerPanel::ModelManagerPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(scroll);
    auto* vl = new QVBoxLayout(body);
    vl->setContentsMargins(10, 10, 10, 10);
    vl->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("Models"), body);
    title->setProperty("role", QStringLiteral("title"));
    vl->addWidget(title);

    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/models");
    auto* rootLabel = new QLabel(QStringLiteral("Models root: %1").arg(root), body);
    rootLabel->setProperty("role", QStringLiteral("secondary"));
    rootLabel->setWordWrap(true);
    rootLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    vl->addWidget(rootLabel);

    auto* note = new QLabel(QStringLiteral("Downloader + sha256 verification arrive with inference (P5). "
                                           "Silero VAD will ship bundled — no download needed. "
                                           "The only network traffic ses ever makes is an explicit model download."), body);
    note->setProperty("role", QStringLiteral("secondary"));
    note->setWordWrap(true);
    vl->addWidget(note);

    for (const auto& e : ses::inference::catalog()) {
        auto* card = new QFrame(body);
        card->setObjectName(QStringLiteral("Card"));
        auto* hl = new QHBoxLayout(card);
        hl->setContentsMargins(12, 8, 12, 8);
        hl->setSpacing(8);

        auto* text = new QVBoxLayout;
        text->setSpacing(2);
        auto* name = new QLabel(e.name, card);
        QFont nf = name->font();
        nf.setBold(true);
        name->setFont(nf);
        auto* meta = new QLabel(QStringLiteral("%1 · %2 · %3").arg(e.packageId, e.params, e.license), card);
        meta->setProperty("role", QStringLiteral("secondary"));
        meta->setWordWrap(true);
        text->addWidget(name);
        text->addWidget(meta);
        hl->addLayout(text, 1);

        auto* state = new QLabel(e.bundled ? QStringLiteral("bundled") : QStringLiteral("not installed"), card);
        state->setObjectName(QStringLiteral("Badge"));
        hl->addWidget(state);

        auto* dl = new QPushButton(e.bundled ? QStringLiteral("Bundled") : QStringLiteral("Download"), card);
        dl->setEnabled(false);
        dl->setToolTip(QStringLiteral("Model download arrives with the inference layer (P5). License is shown before any download."));
        hl->addWidget(dl);
        vl->addWidget(card);
    }

    vl->addStretch(1);
    scroll->setWidget(body);
    outer->addWidget(scroll);
}

} // namespace ses::ui