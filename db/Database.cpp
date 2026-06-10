#include "Database.hpp"

#include "ProfileFilter.hpp"
#include "fmt/includes.h"

#include <QFile>
#include <QDir>
#include <QColor>
#include <QLocale>
#include <QSet>

namespace NekoGui {

    ProfileManager *profileManager = new ProfileManager();

    ProfileManager::ProfileManager() : JsonStore("groups/pm.json") {
        _add(new configItem("groups", &groupsTabOrder, itemType::integerList));
    }

    QList<int> filterIntJsonFile(const QString &path) {
        QList<int> result;
        QDir dr(path);
        auto entryList = dr.entryList(QDir::Files);
        for (auto e: entryList) {
            e = e.toLower();
            if (!e.endsWith(".json", Qt::CaseInsensitive)) continue;
            e = e.remove(".json", Qt::CaseInsensitive);
            bool ok;
            auto id = e.toInt(&ok);
            if (ok) {
                result << id;
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    void ProfileManager::LoadManager() {
        JsonStore::Load();
        //
        profiles = {};
        groups = {};
        profilesIdOrder = filterIntJsonFile("profiles");
        groupsIdOrder = filterIntJsonFile("groups");
        // Load Proxys
        QList<int> delProfile;
        for (auto id: profilesIdOrder) {
            auto ent = LoadProxyEntity(QStringLiteral("profiles/%1.json").arg(id));
            // Corrupted profile?
            if (ent == nullptr || ent->bean == nullptr || ent->bean->version == -114514) {
                delProfile << id;
                continue;
            }
            profiles[id] = ent;
        }
        // Clear Corrupted profile
        for (auto id: delProfile) {
            DeleteProfile(id);
        }
        // Load Groups
        auto loadedOrder = groupsTabOrder;
        groupsTabOrder = {};
        for (auto id: groupsIdOrder) {
            auto ent = LoadGroup(QStringLiteral("groups/%1.json").arg(id));
            // Corrupted group?
            if (ent->id != id) {
                continue;
            }
            // Ensure order contains every group
            if (!loadedOrder.contains(id)) {
                loadedOrder << id;
            }
            groups[id] = ent;
            if (ent->NormalizeOrder()) ent->Save();
        }
        // Ensure groups contains order
        for (auto id: loadedOrder) {
            if (groups.count(id)) {
                groupsTabOrder << id;
            }
        }
        // First setup
        if (groups.empty()) {
            auto defaultGroup = NekoGui::ProfileManager::NewGroup();
            defaultGroup->name = QObject::tr("Default");
            NekoGui::profileManager->AddGroup(defaultGroup);

            auto allGroup = NekoGui::ProfileManager::NewGroup();
            allGroup->name = QObject::tr("All");
            allGroup->all_profiles = true;
            NekoGui::profileManager->AddGroup(allGroup);
        }
        EnsureAllProfilesGroup();
        //
        if (dataStore->flag_reorder) {
            {
                // remove all (contains orphan)
                for (const auto &profile: profiles) {
                    QFile::remove(profile.second->fn);
                }
            }
            std::map<int, int> gidOld2New;
            {
                int i = 0;
                int ii = 0;
                QList<int> newProfilesIdOrder;
                std::map<int, std::shared_ptr<ProxyEntity>> newProfiles;
                for (auto gid: groupsTabOrder) {
                    auto group = GetGroup(gid);
                    gidOld2New[gid] = ii++;
                    if (group->all_profiles) {
                        group->order = {};
                        group->Save();
                        continue;
                    }
                    for (auto const &profile: group->ProfilesWithOrder()) {
                        auto oldId = profile->id;
                        auto newId = i++;
                        profile->id = newId;
                        profile->gid = gidOld2New[gid];
                        profile->fn = QStringLiteral("profiles/%1.json").arg(newId);
                        profile->Save();
                        newProfiles[newId] = profile;
                        newProfilesIdOrder << newId;
                    }
                    group->order = {};
                    group->Save();
                }
                profiles = newProfiles;
                profilesIdOrder = newProfilesIdOrder;
            }
            {
                QList<int> newGroupsIdOrder;
                std::map<int, std::shared_ptr<Group>> newGroups;
                for (auto oldGid: groupsTabOrder) {
                    auto newId = gidOld2New[oldGid];
                    auto group = groups[oldGid];
                    QFile::remove(group->fn);
                    group->id = newId;
                    group->fn = QStringLiteral("groups/%1.json").arg(newId);
                    group->Save();
                    newGroups[newId] = group;
                    newGroupsIdOrder << newId;
                }
                groups = newGroups;
                groupsIdOrder = newGroupsIdOrder;
                groupsTabOrder = newGroupsIdOrder;
            }
            MessageBoxInfo(software_name, "Profiles and groups reorder complete.");
        }
    }

    void ProfileManager::SaveManager() {
        JsonStore::Save();
    }

    std::shared_ptr<ProxyEntity> ProfileManager::LoadProxyEntity(const QString &jsonPath) {
        // Load type
        ProxyEntity ent0(nullptr, nullptr);
        ent0.fn = jsonPath;
        auto validJson = ent0.Load();
        auto type = ent0.type;

        // Load content
        std::shared_ptr<ProxyEntity> ent;
        bool validType = validJson;

        if (validType) {
            ent = NewProxyEntity(type);
            validType = ent->bean->version != -114514;
        }

        if (validType) {
            ent->load_control_must = true;
            ent->fn = jsonPath;
            ent->Load();
        }
        return ent;
    }

    //  新建的不给 fn 和 id

    std::shared_ptr<ProxyEntity> ProfileManager::NewProxyEntity(const QString &type) {
        NekoGui_fmt::AbstractBean *bean;

        if (type == "socks") {
            bean = new NekoGui_fmt::SocksHttpBean(NekoGui_fmt::SocksHttpBean::type_Socks5);
        } else if (type == "http") {
            bean = new NekoGui_fmt::SocksHttpBean(NekoGui_fmt::SocksHttpBean::type_HTTP);
        } else if (type == "shadowsocks") {
            bean = new NekoGui_fmt::ShadowSocksBean();
        } else if (type == "chain") {
            bean = new NekoGui_fmt::ChainBean();
        } else if (type == "vmess") {
            bean = new NekoGui_fmt::VMessBean();
        } else if (type == "trojan") {
            bean = new NekoGui_fmt::TrojanVLESSBean(NekoGui_fmt::TrojanVLESSBean::proxy_Trojan);
        } else if (type == "vless") {
            bean = new NekoGui_fmt::TrojanVLESSBean(NekoGui_fmt::TrojanVLESSBean::proxy_VLESS);
        } else if (type == "naive") {
            bean = new NekoGui_fmt::NaiveBean();
        } else if (type == "hysteria2") {
            bean = new NekoGui_fmt::QUICBean(NekoGui_fmt::QUICBean::proxy_Hysteria2);
        } else if (type == "tuic") {
            bean = new NekoGui_fmt::QUICBean(NekoGui_fmt::QUICBean::proxy_TUIC);
        } else if (type == "custom") {
            bean = new NekoGui_fmt::CustomBean();
        } else {
            bean = new NekoGui_fmt::AbstractBean(-114514);
        }

        auto ent = std::make_shared<ProxyEntity>(bean, type);
        return ent;
    }

    std::shared_ptr<Group> ProfileManager::NewGroup() {
        auto ent = std::make_shared<Group>();
        return ent;
    }

    // ProxyEntity

    ProxyEntity::ProxyEntity(NekoGui_fmt::AbstractBean *bean, const QString &type_) {
        if (type_ != nullptr) this->type = type_;

        _add(new configItem("type", &type, itemType::string));
        _add(new configItem("id", &id, itemType::integer));
        _add(new configItem("gid", &gid, itemType::integer));
        _add(new configItem("yc", &latency, itemType::integer));
        _add(new configItem("report", &full_test_report, itemType::string));
        _add(new configItem("exit_country", &exit_country, itemType::string));

        // 可以不关联 bean，只加载 ProxyEntity 的信息
        if (bean != nullptr) {
            this->bean = std::shared_ptr<NekoGui_fmt::AbstractBean>(bean);
            // 有虚函数就要在这里 dynamic_cast
            _add(new configItem("bean", dynamic_cast<JsonStore *>(bean), itemType::jsonStore));
            _add(new configItem("traffic", dynamic_cast<JsonStore *>(traffic_data.get()), itemType::jsonStore));
        }
    };

    QString ProxyEntity::CountryNameForIsoCode(const QString &isoCode) {
        if (isoCode.size() != 2) return {};
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
        const auto territory = QLocale::codeToTerritory(isoCode);
        if (territory == QLocale::AnyTerritory) return isoCode;
        return QLocale::territoryToString(territory);
#else
        return isoCode;
#endif
    }

    QString ProxyEntity::CountryLabel(const ProxyEntity &profile) {
        if (profile.exit_country.isEmpty()) return QObject::tr("Unknown");
        const auto name = CountryNameForIsoCode(profile.exit_country);
        return name.isEmpty() ? profile.exit_country : name;
    }

    bool ProxyEntity::IsUnavailable(const ProxyEntity &profile) {
        return profile.latency < 0;
    }

    bool ProxyEntity::IsVisibleInList(const ProxyEntity &profile) {
        if (!NekoGui::dataStore->hide_unavailable) return true;
        return !IsUnavailable(profile);
    }

    bool ProxyEntity::MatchesCountryFilter(const ProxyEntity &profile) {
        if (!NekoGui::dataStore->country_filter_active || NekoGui::dataStore->country_filter_selected.isEmpty()) {
            return true;
        }
        return NekoGui::dataStore->country_filter_selected.contains(CountryLabel(profile));
    }

    bool ProxyEntity::IsShownInList(const ProxyEntity &profile) {
        if (!IsVisibleInList(profile)) return false;
        return MatchesCountryFilter(profile);
    }

    QList<std::shared_ptr<ProxyEntity>> ProxyEntity::VisibleOnly(const QList<std::shared_ptr<ProxyEntity>> &profiles) {
        QList<std::shared_ptr<ProxyEntity>> out;
        out.reserve(profiles.size());
        for (const auto &profile: profiles) {
            if (IsShownInList(*profile)) out += profile;
        }
        return out;
    }

    QString ProxyEntity::DisplayNameInList() const {
        if (latency < 0) {
            return QStringLiteral("[ - ] %1").arg(bean->name);
        }
        if (!exit_country.isEmpty()) {
            const auto country = CountryNameForIsoCode(exit_country);
            if (!country.isEmpty()) {
                return QStringLiteral("[%1] %2").arg(country, bean->name);
            }
        }
        return bean->name;
    }

    void ProxyEntity::ApplyTestResult(bool isUrlTest, int ms, bool rpcOk, bool hasError, const QString &fullReport) {
        if (!rpcOk || hasError) {
            latency = -1;
            return;
        }
        latency = ms > 0 ? ms : 1;
        if (isUrlTest) {
            full_test_report.clear();
            exit_country.clear();
            const auto prefix = QStringLiteral("__NKR_EXIT__|");
            if (fullReport.startsWith(prefix)) {
                exit_country = fullReport.mid(prefix.size());
            }
        } else {
            full_test_report = fullReport;
        }
    }

    QString ProxyEntity::DisplayLatency() const {
        if (latency < 0) {
            return QObject::tr("Unavailable");
        } else if (latency > 0) {
            return UNICODE_LRO + QStringLiteral("%1 ms").arg(latency);
        } else {
            return "";
        }
    }

    QColor ProxyEntity::DisplayLatencyColor() const {
        if (latency < 0) {
            return Qt::red;
        } else if (latency > 0) {
            auto greenMs = dataStore->test_latency_url.startsWith("https://") ? 200 : 100;
            if (latency < greenMs) {
                return Qt::darkGreen;
            } else {
                return Qt::darkYellow;
            }
        } else {
            return {};
        }
    }

    // Profile

    int ProfileManager::NewProfileID() const {
        if (profiles.empty()) {
            return 0;
        } else {
            return profilesIdOrder.last() + 1;
        }
    }

    bool ProfileManager::AddProfile(const std::shared_ptr<ProxyEntity> &ent, int gid) {
        if (ent->id >= 0) {
            return false;
        }

        auto targetGid = gid < 0 ? dataStore->current_group : gid;
        auto targetGroup = GetGroup(targetGid);
        if (targetGroup != nullptr && targetGroup->all_profiles) {
            targetGid = DefaultAddGroupId();
        }
        ent->gid = targetGid;
        ent->id = NewProfileID();
        profiles[ent->id] = ent;
        profilesIdOrder.push_back(ent->id);

        ent->fn = QStringLiteral("profiles/%1.json").arg(ent->id);
        ent->Save();
        RefreshAllProfilesGroup();
        return true;
    }

    void ProfileManager::DeleteProfile(int id) {
        if (id < 0) return;
        if (dataStore->started_id == id) return;
        profiles.erase(id);
        profilesIdOrder.removeAll(id);
        QFile(QStringLiteral("profiles/%1.json").arg(id)).remove();
        RefreshAllProfilesGroup();
    }

    void ProfileManager::MoveProfile(const std::shared_ptr<ProxyEntity> &ent, int gid) {
        if (gid == ent->gid || gid < 0) return;
        auto newGroup = GetGroup(gid);
        if (newGroup == nullptr || newGroup->all_profiles) return;
        auto oldGroup = GetGroup(ent->gid);
        if (oldGroup != nullptr && !oldGroup->order.isEmpty()) {
            oldGroup->order.removeAll(ent->id);
            oldGroup->Save();
        }
        if (!newGroup->order.isEmpty()) {
            newGroup->order.push_back(ent->id);
            newGroup->Save();
        }
        ent->gid = gid;
        ent->Save();
        RefreshAllProfilesGroup();
    }

    std::shared_ptr<ProxyEntity> ProfileManager::GetProfile(int id) {
        return profiles.count(id) ? profiles[id] : nullptr;
    }

    // Group

    Group::Group() {
        _add(new configItem("id", &id, itemType::integer));
        _add(new configItem("front_proxy_id", &front_proxy_id, itemType::integer));
        _add(new configItem("archive", &archive, itemType::boolean));
        _add(new configItem("all_profiles", &all_profiles, itemType::boolean));
        _add(new configItem("skip_auto_update", &skip_auto_update, itemType::boolean));
        _add(new configItem("name", &name, itemType::string));
        _add(new configItem("order", &order, itemType::integerList));
        _add(new configItem("url", &url, itemType::string));
        _add(new configItem("info", &info, itemType::string));
        _add(new configItem("lastup", &sub_last_update, itemType::integer64));
        _add(new configItem("manually_column_width", &manually_column_width, itemType::boolean));
        _add(new configItem("column_width", &column_width, itemType::integerList));
    }

    std::shared_ptr<Group> ProfileManager::LoadGroup(const QString &jsonPath) {
        auto ent = std::make_shared<Group>();
        ent->fn = jsonPath;
        ent->Load();
        return ent;
    }

    int ProfileManager::NewGroupID() const {
        if (groups.empty()) {
            return 0;
        } else {
            return groupsIdOrder.last() + 1;
        }
    }

    bool ProfileManager::AddGroup(const std::shared_ptr<Group> &ent) {
        if (ent->id >= 0) {
            return false;
        }

        ent->id = NewGroupID();
        groups[ent->id] = ent;
        groupsIdOrder.push_back(ent->id);
        groupsTabOrder.push_back(ent->id);

        ent->fn = QStringLiteral("groups/%1.json").arg(ent->id);
        ent->Save();
        return true;
    }

    void ProfileManager::DeleteGroup(int gid) {
        auto group = GetGroup(gid);
        if (group == nullptr || group->all_profiles) return;
        if (groups.size() <= 1) return;
        QList<int> toDelete;
        for (const auto &[id, profile]: profiles) {
            if (profile->gid == gid) toDelete += id; // map访问中，不能操作
        }
        for (const auto &id: toDelete) {
            DeleteProfile(id);
        }
        groups.erase(gid);
        groupsIdOrder.removeAll(gid);
        groupsTabOrder.removeAll(gid);
        QFile(QStringLiteral("groups/%1.json").arg(gid)).remove();
        RefreshAllProfilesGroup();
    }

    std::shared_ptr<Group> ProfileManager::GetGroup(int id) const {
        return groups.count(id) ? groups.at(id) : nullptr;
    }

    std::shared_ptr<Group> ProfileManager::CurrentGroup() {
        return GetGroup(dataStore->current_group);
    }

    std::shared_ptr<Group> ProfileManager::FindAllProfilesGroup() {
        for (const auto &[_, group]: groups) {
            if (group->all_profiles) return group;
        }
        return nullptr;
    }

    int ProfileManager::DefaultAddGroupId() const {
        for (auto gid: groupsTabOrder) {
            auto group = GetGroup(gid);
            if (group != nullptr && !group->all_profiles && !group->archive) return gid;
        }
        for (const auto &[gid, group]: groups) {
            if (!group->all_profiles && !group->archive) return gid;
        }
        return groupsTabOrder.isEmpty() ? 0 : groupsTabOrder.first();
    }

    void ProfileManager::EnsureAllProfilesGroup() {
        auto allGroup = FindAllProfilesGroup();
        if (allGroup == nullptr) {
            allGroup = NewGroup();
            allGroup->name = QObject::tr("All");
            allGroup->all_profiles = true;
            AddGroup(allGroup);
        }

        groupsTabOrder.removeAll(allGroup->id);
        const int insertAt = groupsTabOrder.isEmpty() ? 0 : 1;
        groupsTabOrder.insert(insertAt, allGroup->id);
        allGroup->RebuildAggregateOrder();
        allGroup->Save();
        SaveManager();
    }

    void ProfileManager::RefreshAllProfilesGroup() {
        auto allGroup = FindAllProfilesGroup();
        if (allGroup == nullptr) return;
        allGroup->RebuildAggregateOrder();
        allGroup->Save();
    }

    QList<std::shared_ptr<ProxyEntity>> ProfileManager::GetDuplicateProfiles(int gid) {
        QList<std::shared_ptr<ProxyEntity>> out_del;
        auto group = GetGroup(gid);
        if (group == nullptr) return out_del;
        const auto profiles = ProxyEntity::VisibleOnly(group->Profiles());
        QList<std::shared_ptr<ProxyEntity>> uniq;
        ProfileFilter::Uniq(profiles, uniq, false, false);
        ProfileFilter::OnlyInSrc_ByPointer(profiles, uniq, out_del);
        return out_del;
    }

    int ProfileManager::RemoveDuplicateProfiles(int gid) {
        auto group = GetGroup(gid);
        if (group == nullptr) return 0;
        const auto to_del = GetDuplicateProfiles(gid);
        int n = 0;
        for (const auto &ent: to_del) {
            if (dataStore->started_id == ent->id) continue;
            if (auto owner = GetGroup(ent->gid); owner != nullptr) {
                owner->order.removeAll(ent->id);
                owner->Save();
            }
            DeleteProfile(ent->id);
            n++;
        }
        if (n > 0) {
            RefreshAllProfilesGroup();
            SaveManager();
        }
        return n;
    }

    void Group::RebuildAggregateOrder() {
        if (!all_profiles) return;

        order.clear();
        QSet<int> seen;
        for (auto gid: profileManager->groupsTabOrder) {
            if (gid == id) continue;
            auto group = profileManager->GetGroup(gid);
            if (group == nullptr || group->all_profiles || group->archive) continue;
            for (const auto &profile: group->ProfilesWithOrder()) {
                if (seen.contains(profile->id)) continue;
                seen.insert(profile->id);
                order += profile->id;
            }
        }
    }

    bool Group::NormalizeOrder() {
        if (all_profiles) {
            const auto oldOrder = order;
            RebuildAggregateOrder();
            return oldOrder != order;
        }
        if (order.isEmpty()) return false;

        QList<int> normalized;
        QSet<int> seen;
        bool changed = false;

        for (auto pid: order) {
            if (seen.contains(pid)) {
                changed = true;
                continue;
            }
            auto ent = profileManager->GetProfile(pid);
            if (ent == nullptr || ent->gid != id) {
                changed = true;
                continue;
            }
            seen.insert(pid);
            normalized += pid;
        }

        if (normalized.size() != order.size()) changed = true;
        if (!changed) return false;

        order = normalized;
        return true;
    }

    QList<std::shared_ptr<ProxyEntity>> Group::Profiles() const {
        if (!all_profiles) {
            QList<std::shared_ptr<ProxyEntity>> ret;
            for (const auto &[_, profile]: profileManager->profiles) {
                if (id == profile->gid) ret += profile;
            }
            return ret;
        }

        QList<std::shared_ptr<ProxyEntity>> ret;
        QSet<int> seen;
        for (auto gid: profileManager->groupsTabOrder) {
            if (gid == id) continue;
            auto group = profileManager->GetGroup(gid);
            if (group == nullptr || group->all_profiles || group->archive) continue;
            for (const auto &profile: group->Profiles()) {
                if (seen.contains(profile->id)) continue;
                seen.insert(profile->id);
                ret += profile;
            }
        }
        return ret;
    }

    QList<std::shared_ptr<ProxyEntity>> Group::ProfilesWithOrder() const {
        const auto allowed = Profiles();
        QSet<int> allowedIds;
        for (const auto &profile: allowed) {
            allowedIds.insert(profile->id);
        }

        if (order.isEmpty()) {
            return allowed;
        }

        QList<std::shared_ptr<ProxyEntity>> ret;
        QSet<int> seen;
        for (auto _id: order) {
            if (seen.contains(_id) || !allowedIds.contains(_id)) continue;
            auto ent = profileManager->GetProfile(_id);
            if (ent == nullptr) continue;
            seen.insert(_id);
            ret += ent;
        }
        for (const auto &profile: allowed) {
            if (!seen.contains(profile->id)) ret += profile;
        }
        return ret;
    }

} // namespace NekoGui
