#pragma once

#include <functional>
#include <memory>

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

class CountryFilterController : public QObject {
    Q_OBJECT

public:
    using LogFn = std::function<void(const QString &)>;
    using RefreshFn = std::function<void()>;

    CountryFilterController(QToolButton *selectButton, QPushButton *filterButton, QPushButton *clearTagsButton,
                            LogFn log, RefreshFn refresh, QObject *parent = nullptr);

    void refreshForCurrentGroup();

    void updateCountryOptions();

    void applyFilter();

    void clearTags();

private:
    void rebuildCountryMenu(const QSet<QString> &countries, bool preserveSelection);
    void updateSelectButtonText();
    void resetSelection();
    void deactivateFilter();

    static QString countryLabel(const std::shared_ptr<NekoGui::ProxyEntity> &profile);
    static QSet<QString> countriesInGroup();

    QToolButton *select_button_;
    QPushButton *filter_button_;
    QPushButton *clear_tags_button_;
    QMenu *country_menu_;
    LogFn log_;
    RefreshFn refresh_;
    QSet<QString> selected_countries_;
};
