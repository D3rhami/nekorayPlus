#include "ui/CountryFilter.hpp"

#include "db/Database.hpp"
#include "db/ProxyEntity.hpp"

#include "main/NekoGui.hpp"

#include <QAction>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>

#include <algorithm>

namespace {

class CountrySelectMenu : public QMenu {
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

QString sortedCountryListText(const QSet<QString> &countries) {
    auto list = countries.values();
    std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) {
        if (a == QObject::tr("Unknown")) return false;
        if (b == QObject::tr("Unknown")) return true;
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return list.join(QStringLiteral(", "));
}

} // namespace

CountryFilterController::CountryFilterController(QToolButton *selectButton, QPushButton *filterButton,
                                                 QPushButton *clearTagsButton, LogFn log, RefreshFn refresh,
                                                 QObject *parent)
    : QObject(parent), select_button_(selectButton), filter_button_(filterButton), clear_tags_button_(clearTagsButton),
      log_(std::move(log)), refresh_(std::move(refresh)) {
    country_menu_ = new CountrySelectMenu(select_button_);
    select_button_->setMenu(country_menu_);
    connect(country_menu_, &QMenu::aboutToHide, this, [this] { updateSelectButtonText(); });
    connect(filter_button_, &QPushButton::clicked, this, &CountryFilterController::applyFilter);
    connect(clear_tags_button_, &QPushButton::clicked, this, &CountryFilterController::clearTags);
}

QString CountryFilterController::countryLabel(const std::shared_ptr<NekoGui::ProxyEntity> &profile) {
    if (profile == nullptr) return {};
    return NekoGui::ProxyEntity::CountryLabel(*profile);
}

void CountryFilterController::updateSelectButtonText() {
    if (!select_button_->isEnabled()) return;
    select_button_->setText(selected_countries_.isEmpty() ? tr("Select…") : sortedCountryListText(selected_countries_));
}

void CountryFilterController::resetSelection() {
    selected_countries_.clear();
    updateSelectButtonText();
}

void CountryFilterController::deactivateFilter() {
    NekoGui::dataStore->country_filter_active = false;
    NekoGui::dataStore->country_filter_selected.clear();
}

void CountryFilterController::rebuildCountryMenu(const QSet<QString> &countries, bool preserveSelection) {
    const auto previous = preserveSelection ? selected_countries_ : QSet<QString>{};

    QSignalBlocker menu_blocker(country_menu_);
    country_menu_->clear();
    selected_countries_.clear();

    auto sorted = countries.values();
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
        if (a == QObject::tr("Unknown")) return false;
        if (b == QObject::tr("Unknown")) return true;
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    for (const auto &country: sorted) {
        auto *action = country_menu_->addAction(country);
        action->setCheckable(true);
        if (preserveSelection && previous.contains(country)) {
            action->setChecked(true);
            selected_countries_.insert(country);
        }
        connect(action, &QAction::toggled, this, [this, country](bool checked) {
            if (checked) {
                selected_countries_.insert(country);
            } else {
                selected_countries_.remove(country);
            }
            updateSelectButtonText();
        });
    }

    updateSelectButtonText();
}

QSet<QString> CountryFilterController::countriesInGroup() {
    QSet<QString> countries;
    if (auto group = NekoGui::profileManager->CurrentGroup(); group != nullptr) {
        bool has_unknown = false;
        for (const auto &profile: group->Profiles()) {
            if (profile->exit_country.isEmpty()) {
                has_unknown = true;
            } else {
                const auto label = countryLabel(profile);
                if (!label.isEmpty()) countries.insert(label);
            }
        }
        if (has_unknown) countries.insert(QObject::tr("Unknown"));
    }
    return countries;
}

void CountryFilterController::refreshForCurrentGroup() {
    deactivateFilter();
    resetSelection();
    updateCountryOptions();
}

void CountryFilterController::updateCountryOptions() {
    const auto countries = countriesInGroup();
    const bool has_countries = !countries.isEmpty();
    select_button_->setEnabled(has_countries);
    filter_button_->setEnabled(has_countries);
    clear_tags_button_->setEnabled(has_countries);
    if (!has_countries) {
        QSignalBlocker menu_blocker(country_menu_);
        country_menu_->clear();
        selected_countries_.clear();
        select_button_->setText(QStringLiteral("—"));
        return;
    }

    if (NekoGui::dataStore->country_filter_active) {
        selected_countries_ = QSet<QString>(NekoGui::dataStore->country_filter_selected.begin(),
                                              NekoGui::dataStore->country_filter_selected.end());
    }
    rebuildCountryMenu(countries, NekoGui::dataStore->country_filter_active);
}

void CountryFilterController::applyFilter() {
    if (selected_countries_.isEmpty()) {
        deactivateFilter();
        log_(tr("[country filter] Filter cleared."));
    } else {
        NekoGui::dataStore->country_filter_active = true;
        NekoGui::dataStore->country_filter_selected = selected_countries_.values();
        log_(tr("[country filter] Showing: %1").arg(sortedCountryListText(selected_countries_)));
    }
    if (refresh_) refresh_();
}

void CountryFilterController::clearTags() {
    auto group = NekoGui::profileManager->CurrentGroup();
    if (group == nullptr) return;

    int cleared = 0;
    for (const auto &profile: group->Profiles()) {
        if (profile->exit_country.isEmpty()) continue;
        profile->exit_country.clear();
        profile->Save();
        cleared++;
    }

    if (cleared > 0) {
        log_(tr("[country filter] Cleared country tags from %1 profile(s).").arg(cleared));
        updateCountryOptions();
        if (refresh_) refresh_();
    } else {
        log_(tr("[country filter] No country tags to clear."));
    }
}
