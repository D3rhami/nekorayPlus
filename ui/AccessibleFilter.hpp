#pragma once

#include <functional>

#include <QObject>
#include <QSet>
#include <QString>

class QMenu;
class QPushButton;
class QToolButton;

namespace NekoGui {
    class Group;
    class ProxyEntity;
} // namespace NekoGui

class AccessibleFilterController : public QObject {
    Q_OBJECT

public:
    using LogFn = std::function<void(const QString &)>;
    using RefreshFn = std::function<void()>;

    AccessibleFilterController(QToolButton *excludeButton, QPushButton *applyButton, LogFn log, RefreshFn refresh,
                               QObject *parent = nullptr);

    void refreshForCurrentGroup();

    void apply();

private:
    void rebuildExcludeMenu(const QSet<QString> &types);
    void updateExcludeButtonText();
    void resetSelection();

    static QString profileTypeLabel(const std::shared_ptr<NekoGui::ProxyEntity> &profile);
    static bool typeMatchesSelection(const QString &profileLabel, const QSet<QString> &selected);

    void logNothingToApply(const QString &reason);

    QToolButton *exclude_button_;
    QPushButton *apply_button_;
    QMenu *exclude_menu_;
    LogFn log_;
    RefreshFn refresh_;
    QSet<QString> selected_types_;
    bool apply_busy_ = false;
};
