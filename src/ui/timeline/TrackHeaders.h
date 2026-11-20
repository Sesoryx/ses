#pragma once
// Track header column — vertical companion to the timeline (§10.1).
// Scroll-synced with TimelineWidget; rows carry name, M/S, A/B routing chip.

#include "core/model/ProjectModel.h"

#include <QHash>
#include <QScrollArea>

class QLabel;
class QToolButton;
class QVBoxLayout;

namespace ses::ui {

class TrackHeaders : public QScrollArea {
    Q_OBJECT
public:
    explicit TrackHeaders(core::ProjectModel* model, QWidget* parent = nullptr);
    void setTrackHeight(int h);
    void syncVertical(int value);

signals:
    void addTrackRequested();
    void renameRequested(const QString& trackId);
    void verticalWheel(int delta);

protected:
    void wheelEvent(QWheelEvent* ev) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    struct Row {
        QLabel* name = nullptr;
        QToolButton* mute = nullptr;
        QToolButton* solo = nullptr;
        QToolButton* chip = nullptr;
        QWidget* dot = nullptr;
    };
    void rebuild();
    void refresh();

    core::ProjectModel* model_;
    QWidget* container_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    QHash<QString, Row> rows_;
    int trackH_ = 84;
};

} // namespace ses::ui