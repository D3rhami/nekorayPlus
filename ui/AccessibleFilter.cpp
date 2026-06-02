#include "ui/AccessibleFilter.hpp"

#include "db/Database.hpp"
#include "db/ProxyEntity.hpp"

#include <memory>
#include "main/NekoGui.hpp"

#include <QAction>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>

#include <algorithm>

namespace {

class ExcludeTypeMenu : public QMenu {
public:
    using QMenu::QMenu;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (QAction *action = activeAction(); action && action->isCheckable() && action->isEnabled()) {
            action->setChecked(!action->isChecked());
            event->accept();
            return;
        }
        QMenu::mouseReleaseEvent(event);
    }
};

QString sortedTypeListText(const QSet<QString> &types) {
    auto list = types.values();
    std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return list.join(QStringLiteral(", "));
}

} // namespace

AccessibleFilterController::AccessibleFilterController(QToolButton *excludeButton, QPushButton *applyButton, LogFn log,
                                                       RefreshFn refresh, QObject *parent)
    : QObject(parent), exclude_button_(excludeButton), apply_button_(applyButton), log_(std::move(log)),
      refresh_(std::move(refresh)) {
    exclude_menu_ = new ExcludeTypeMenu(exclude_button_);
    exclude_button_->setMenu(exclude_menu_);
    connect(exclude_menu_, &QMenu::aboutToHide, this, [this] { updateExcludeButtonText(); });
    connect(apply_button_, &QPushButton::clicked, this, &AccessibleFilterController::apply);
}

QString AccessibleFilterController::profileTypeLabel(const std::shared_ptr<NekoGui::ProxyEntity> &profile) {
    if (profile == nullptr || profile->bean == nullptr) return {};
    auto label = profile->bean->DisplayType().trimmed();
    if (label.isEmpty()) label = profile->type.trimmed();
    return label;
}

bool AccessibleFilterController::typeMatchesSelection(const QString &profileLabel, const QSet<QString> &selected) {
    if (profileLabel.isEmpty() || selected.isEmpty()) return false;
    for (const auto &type: selected) {
        if (profileLabel.compare(type, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

void AccessibleFilterController::updateExcludeButtonText() {
    if (!exclude_button_->isEnabled()) return;
    exclude_button_->setText(selected_types_.isEmpty() ? tr("Select…") : sortedTypeListText(selected_types_));
}

void AccessibleFilterController::resetSelection() {
    selected_types_.clear();
    updateExcludeButtonText();
}

void AccessibleFilterController::logNothingToApply(const QString &reason) {
    log_(QStringLiteral("[accessible filter] Nothing to apply (%1).").arg(reason));
}

void AccessibleFilterController::rebuildExcludeMenu(const QSet<QString> &types) {
    QSignalBlocker menu_blocker(exclude_menu_);
    exclude_menu_->clear();
    selected_types_.clear();

    auto sorted = types.values();
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    for (const auto &type: sorted) {
        auto *action = exclude_menu_->addAction(type);
        action->setCheckable(true);
        connect(action, &QAction::toggled, this, [this, type](bool checked) {
            if (checked) {
                selected_types_.insert(type);
            } else {
                selected_types_.remove(type);
            }
            updateExcludeButtonText();
        });
    }

    exclude_button_->setText(tr("Select…"));
}

void AccessibleFilterController::refreshForCurrentGroup() {
    if (apply_busy_) return;

    QSet<QString> types;
    if (auto group = NekoGui::profileManager->CurrentGroup(); group != nullptr) {
        const auto profiles = group->Profiles();
        for (const auto &profile: profiles) {
            const auto label = profileTypeLabel(profile);
            if (!label.isEmpty()) types.insert(label);
        }
    }

    const bool has_types = !types.isEmpty();
    exclude_button_->setEnabled(has_types);
    if (!has_types) {
        QSignalBlocker menu_blocker(exclude_menu_);
        exclude_menu_->clear();
        selected_types_.clear();
        exclude_button_->setText(QStringLiteral("—"));
        return;
    }

    rebuildExcludeMenu(types);
}

void AccessibleFilterController::apply() {
    if (apply_busy_) return;

    auto group = NekoGui::profileManager->CurrentGroup();
    if (group == nullptr) {
        logNothingToApply(tr("no group"));
        return;
    }

    if (selected_types_.isEmpty()) {
        logNothingToApply(tr("no types selected"));
        return;
    }

    if (group->Profiles().isEmpty()) {
        logNothingToApply(tr("group is empty"));
        resetSelection();
        refreshForCurrentGroup();
        return;
    }

    const auto profiles = group->Profiles();
    QList<std::shared_ptr<NekoGui::ProxyEntity>> to_remove;
    to_remove.reserve(profiles.size());
    for (const auto &profile: profiles) {
        if (typeMatchesSelection(profileTypeLabel(profile), selected_types_)) {
            to_remove.append(profile);
        }
    }

    if (to_remove.isEmpty()) {
        logNothingToApply(tr("no profiles match: %1").arg(sortedTypeListText(selected_types_)));
        resetSelection();
        refreshForCurrentGroup();
        return;
    }

    apply_busy_ = true;
    apply_button_->setEnabled(false);

    QString detail;
    detail.reserve(to_remove.size() * 48);
    int removed = 0;
    int skipped = 0;

    for (const auto &ent: to_remove) {
        if (NekoGui::dataStore->started_id == ent->id) {
            skipped++;
            detail += QStringLiteral("[!] %1 (running, skipped)\n").arg(ent->bean->DisplayTypeAndName());
            continue;
        }
        group->order.removeAll(ent->id);
        NekoGui::profileManager->DeleteProfile(ent->id);
        detail += QStringLiteral("[-] %1\n").arg(ent->bean->DisplayTypeAndName());
        removed++;
    }

    if (removed > 0) {
        group->Save();
        NekoGui::profileManager->SaveManager();
        if (refresh_) refresh_();
    }

    auto msg = QStringLiteral("[accessible filter] Removed %1 profile(s) (types: %2).")
                   .arg(removed)
                   .arg(sortedTypeListText(selected_types_));
    if (skipped > 0) msg += QStringLiteral(" Skipped %1 running.").arg(skipped);
    if (!detail.isEmpty()) msg += QStringLiteral("\n") + detail.trimmed();
    log_(msg);

    resetSelection();
    apply_busy_ = false;
    apply_button_->setEnabled(true);
    refreshForCurrentGroup();
}
