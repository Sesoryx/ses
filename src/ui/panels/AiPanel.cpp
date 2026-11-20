#include "ui/panels/AiPanel.h"

#include "inference/Catalog.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace ses::ui {

namespace {

QFrame* entryCard(const ses::inference::CatalogEntry& e, QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("Card"));
    card->setProperty("status", QStringLiteral("pending"));
    auto* vl = new QVBoxLayout(card);
    vl->setContentsMargins(12, 10, 12, 10);
    vl->setSpacing(4);

    auto* top = new QHBoxLayout;
    auto* name = new QLabel(e.name, card);
    QFont nf = name->font();
    nf.setBold(true);
    name->setFont(nf);
    auto* params = new QLabel(e.params, card);
    params->setObjectName(QStringLiteral("Badge"));
    params->setToolTip(QStringLiteral("Verified parameter count"));
    top->addWidget(name);
    top->addStretch(1);
    top->addWidget(params);
    vl->addLayout(top);

    auto* meta = new QLabel(QStringLiteral("%1 · %2%3").arg(e.license, e.sizeNote,
                               e.bundled ? QStringLiteral(" · bundled") : (e.optIn ? QStringLiteral(" · opt-in") : QString())), card);
    meta->setProperty("role", QStringLiteral("secondary"));
    meta->setWordWrap(true);
    vl->addWidget(meta);

    if (!e.note.isEmpty()) {
        auto* note = new QLabel(e.note, card);
        note->setProperty("role", QStringLiteral("secondary"));
        note->setWordWrap(true);
        vl->addWidget(note);
    }

    auto* bottom = new QHBoxLayout;
    auto* state = new QLabel(e.bundled ? QStringLiteral("Ships with the app") : QStringLiteral("Not installed"), card);
    state->setProperty("role", QStringLiteral("secondary"));
    auto* run = new QPushButton(QStringLiteral("Run"), card);
    run->setProperty("variant", QStringLiteral("primary"));
    run->setEnabled(false);
    run->setToolTip(QStringLiteral("Inference engine is not connected yet — audio.cpp integration lands in P5. Editing and playback work without it."));
    bottom->addWidget(state);
    bottom->addStretch(1);
    bottom->addWidget(run);
    vl->addLayout(bottom);
    return card;
}

} // namespace

AiPanel::AiPanel(core::ProjectModel* model, QWidget* parent)
    : QWidget(parent), model_(model)
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

    scope_ = new QLabel(QStringLiteral("Scope: whole project — select clips to scope AI actions"), body);
    scope_->setProperty("role", QStringLiteral("secondary"));
    scope_->setWordWrap(true);
    vl->addWidget(scope_);

    auto* info = new QFrame(body);
    info->setObjectName(QStringLiteral("Card"));
    auto* il = new QVBoxLayout(info);
    il->setContentsMargins(12, 10, 12, 10);
    auto* it = new QLabel(QStringLiteral("Local & private"), info);
    QFont tf = it->font();
    tf.setBold(true);
    it->setFont(tf);
    auto* id = new QLabel(QStringLiteral("No cloud, no accounts. Backend probe arrives with inference (P5). "
                                         "This machine: CPU-only — no compatible GPU detected (HD 4000 / GT 635M). "
                                         "Light models are chosen to feel instant on this CPU."), info);
    id->setProperty("role", QStringLiteral("secondary"));
    id->setWordWrap(true);
    il->addWidget(it);
    il->addWidget(id);
    vl->addWidget(info);

    const auto section = [&](const QString& title, std::initializer_list<ses::inference::Capability> caps) {
        auto* header = new QLabel(title, body);
        header->setProperty("role", QStringLiteral("title"));
        vl->addWidget(header);
        for (const auto& e : ses::inference::catalog())
            for (auto c : caps)
                if (e.capability == c)
                    vl->addWidget(entryCard(e, body));
    };
    section(QStringLiteral("Transcribe (ASR)"), { ses::inference::Capability::Asr });
    section(QStringLiteral("Enhance / Super-resolution"), { ses::inference::Capability::Enhance, ses::inference::Capability::SuperRes });
    section(QStringLiteral("Speech (TTS)"), { ses::inference::Capability::Tts });
    section(QStringLiteral("Voice conversion"), { ses::inference::Capability::VoiceConversion });
    section(QStringLiteral("Music / SFX"), { ses::inference::Capability::Music });

    vl->addStretch(1);
    scroll->setWidget(body);
    outer->addWidget(scroll);
}

void AiPanel::setSelectionScope(const QStringList& clipIds)
{
    if (clipIds.isEmpty()) {
        scope_->setText(QStringLiteral("Scope: whole project — select clips to scope AI actions"));
        return;
    }
    QStringList names;
    for (const auto& id : clipIds) {
        const auto* c = model_->project().clipById(id);
        if (c)
            names.append(c->name.isEmpty() ? QStringLiteral("(unnamed)") : c->name);
    }
    scope_->setText(QStringLiteral("Scope: %1 clip(s) — %2").arg(clipIds.size()).arg(names.join(QStringLiteral(", "))));
}

} // namespace ses::ui