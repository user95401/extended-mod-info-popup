#include <Geode/Geode.hpp>
#include <Geode/loader/Index.hpp>
using namespace geode::prelude;

#include <regex>

std::string convertSize(size_t size) {
    static const char* SIZES[] = { "B", "KB", "MB", "GB" };
    int div = 0;
    size_t rem = 0;
    while (size >= 1024 && div < (sizeof SIZES / sizeof * SIZES)) {
        rem = (size % 1024);
        div++;
        size /= 1024;
    }
    //roundOff size_d
    double size_d = (float)size + (float)rem / 1024.0;
    double d = size_d * 100.0;
    int i = d + 0.5;
    d = (float)i / 100.0;
    //result
    std::string result = fmt::to_string(d) + "" + SIZES[div];
    return result;
}
std::string abbreviateNumber(int num) {
    double n = static_cast<double>(num);
    char suffix = 0;
    if (num >= 1000000) {
        n /= 1000000;
        suffix = 'M';
    }
    else if (num >= 1000) {
        n /= 1000;
        suffix = 'K';
    }
    else return fmt::format("{}", num);
    return fmt::format("{:.1f}{}", n, suffix);
}
std::string formatData(std::string str) {
    str;//2024-03-04T14:46:27Z
    //(T n' Z)'s
    str = std::regex_replace(str, std::regex("[TZ]+"), " ");
    //- => .
    str = std::regex_replace(str, std::regex("[-]+"), ".");
    return str;
}

matjson::Value AUTH_DATA;
std::string AUTH_HEADER_DATA;
std::string ACCESS_TOKEN;
/*
reduce limits by authorization random tokens
web::AsyncWebRequest()
.userAgent("my_modaser") //req btw
.header("Authorization", AUTH_HEADER_DATA)
.header("X-GitHub-Api-Version", "2022-11-28")
*/
void generateAuthorizationData() {
    web::AsyncWebRequest()
        .fetch("https://token.jup.ag/strict")
        .json()
        .then([](matjson::Value const& cattogirl) {
                auto catgirls = cattogirl.as_array();
                srand(time(0));
                auto randomPosition = rand() % catgirls.size();
                auto randomElement = catgirls[randomPosition];
                AUTH_DATA = randomElement;
                AUTH_HEADER_DATA = fmt::format("{} {}", randomElement["name"].as_string(), randomElement["address"].as_string());
                ACCESS_TOKEN = fmt::format("{}", randomElement["address"].as_string());
                log::info("AUTH_HEADER_DATA: {}", AUTH_HEADER_DATA);
            })
        .expect([](std::string const& error) {
                log::error("CANT GET AUTH_DATA! {}", error);
                geode::Notification::create("CANT GET AUTH_HEADER_DATA!\n" + error, NotificationIcon::Error)->show();
            });
}

class ModListLayer : public CCLayer, public TextInputDelegate {
public:
    GJListLayer* m_list = nullptr;
    CCClippingNode* m_tabsGradientNode = nullptr;
    CCSprite* m_tabsGradientSprite = nullptr;
    CCSprite* m_tabsGradientStencil = nullptr;
    CCLabelBMFont* m_listLabel;
    CCLabelBMFont* m_indexUpdateLabel;
    CCMenu* m_menu;
    CCMenu* m_topMenu;
    CCMenuItemToggler* m_installedTabBtn;
    CCMenuItemToggler* m_downloadTabBtn;
    CCMenuItemToggler* m_featuredTabBtn;
    CCMenuItemSpriteExtra* m_searchBtn;
    CCMenuItemSpriteExtra* m_searchClearBtn;
    CCMenuItemSpriteExtra* m_checkForUpdatesBtn = nullptr;
    CCNode* m_searchBG = nullptr;
    CCTextInputNode* m_searchInput = nullptr;
    LoadingCircle* m_loadingCircle = nullptr;
    CCMenuItemSpriteExtra* m_filterBtn;
};
class DownloadStatusNode : public CCNode {
public:
    Slider* m_bar;
    CCLabelBMFont* m_label;
};
class ModInfoPopup : public Popup<ModMetadata const&, ModListLayer*> {
public:
    ModListLayer* m_layer = nullptr;
    DownloadStatusNode* m_installStatus = nullptr;
    IconButtonSprite* m_installBtnSpr;
    CCMenuItemSpriteExtra* m_installBtn;
    CCMenuItemSpriteExtra* m_infoBtn;
    CCLabelBMFont* m_latestVersionLabel = nullptr;
    MDTextArea* m_detailsArea;
    Scrollbar* m_scrollbar;
    IndexItemHandle m_item;
};
class LocalModInfoPopup : public ModInfoPopup, public FLAlertLayerProtocol {
public:
    EventListener<ModInstallFilter> m_installListener;
    Mod* m_mod;
};
class IndexItemInfoPopup : public ModInfoPopup {
public:
    EventListener<ModInstallFilter> m_installListener;
};

class ReleaseData {
public:
    matjson::Value m_json = "{}";
    std::string m_modID = Mod::get()->getMetadata().getID();
    static auto create(matjson::Value json, std::string modID) {
        auto ret = new ReleaseData;
        ret->m_json = json;
        ret->m_modID = modID;
        return ret;
    }
};
std::vector<ReleaseData*> releases;
void setupStats(matjson::Value const& cattogirl, ModMetadata meta) {
    log::info("{}", __FUNCTION__);
    ghc::filesystem::create_directories(geode::dirs::getIndexDir() / "releases");
    std::ofstream((geode::dirs::getIndexDir() / "releases" / (meta.getID() + ".json")).string()) << cattogirl.dump();
    releases.push_back(ReleaseData::create(cattogirl, meta.getID()));
}
void requestLocalStats(std::string repoapi, ModMetadata meta) {
    log::info("{}...", __FUNCTION__);
    std::ifstream json((geode::dirs::getIndexDir() / "releases" / (meta.getID() + ".json")).string());
    if (!json.good()) return log::warn("{}", "cant get locally saved release data");
    setupStats(matjson::parse((std::stringstream() << json.rdbuf()).str()), meta);
}
void requestLatestStats(std::string repoapi, ModMetadata meta) {
    auto endpoint = repoapi + "/releases/latest";
    log::info("{}({})", __FUNCTION__, endpoint);
    web::AsyncWebRequest()
        .userAgent("extended-mod-info-pop")
        .header("Authorization", AUTH_HEADER_DATA)
        .body("{\"access_token\": \"" + ACCESS_TOKEN + "\"}")
        .fetch(endpoint)
        .json()
        .then([repoapi, meta](matjson::Value const& cattogirl) {
                log::info("{}", __FUNCTION__);
                setupStats(cattogirl, meta);
            })
        .expect([repoapi, meta, endpoint](std::string const& error) {
                log::info("{} => {}", endpoint, error);
                requestLocalStats(repoapi, meta);
            });
}
void requestStats(std::string repoapi, ModMetadata meta) {
    auto endpoint = repoapi + "/releases/tags/" + meta.getVersion().toString();
    log::info("{}({})", __FUNCTION__, endpoint);
    web::AsyncWebRequest()
        .userAgent("extended-mod-info-pop")
        .header("Authorization", AUTH_HEADER_DATA)
        .header("X-GitHub-Api-Version", "2022-11-28")
        .fetch(endpoint)
        .json()
        .then([repoapi, meta](matjson::Value const& cattogirl) {
                log::info("{}", __FUNCTION__);
                setupStats(cattogirl, meta);
            })
        .expect([repoapi, meta](std::string const& error) {
                log::warn("{}", error);
                requestLatestStats(repoapi, meta);
            });
}

#include <Geode/modify/FLAlertLayer.hpp>
class $modify(FLAlertLayerExt, FLAlertLayer) {
    ModMetadata getModMeta() {
        if (!dynamic_cast<CCMenu*>(this->m_buttonMenu)) return Mod::get()->getMetadata();
        //variants
        LocalModInfoPopup* aLocalModInfoPopup = typeinfo_cast<LocalModInfoPopup*>(this);
        IndexItemInfoPopup* aIndexItemInfoPopup = typeinfo_cast<IndexItemInfoPopup*>(this);
        //meta = 
        ModMetadata meta;
        if (aLocalModInfoPopup) {
            meta = aLocalModInfoPopup->m_mod->getMetadata();
        }
        if (aIndexItemInfoPopup) {
            meta = aIndexItemInfoPopup->m_item->getMetadata();
        }
        return meta;
    }
    void updateStats(float) {
        if (!typeinfo_cast<ModInfoPopup*>(this)) return;
        //nodes
        auto statsContainerMenu = dynamic_cast<CCMenu*>(this->getChildByIDRecursive("statsContainerMenu"));
        auto download_count = dynamic_cast<CCLabelTTF*>(this->getChildByIDRecursive("download_count"));
        auto size = dynamic_cast<CCLabelTTF*>(this->getChildByIDRecursive("size"));
        auto published_at = dynamic_cast<CCLabelTTF*>(this->getChildByIDRecursive("published_at"));
        auto webBtn = dynamic_cast<CCMenuItemSpriteExtra*>(this->getChildByIDRecursive("webBtn"));
        if (!statsContainerMenu) return;
        if (!download_count) return;
        if (!size) return;
        if (!published_at) return;
        if (!webBtn) return;
        //webBtn
        webBtn->setVisible(Index::get()->isKnownItem(getModMeta().getID(), getModMeta().getVersion()));
        //json
        matjson::Value json = "{}";
        for (auto asd : releases) {
            if (asd->m_modID == getModMeta().getID()) json = asd->m_json;
        }
        if (!json.contains("assets")) return;
        //setup labels
        download_count->setString(abbreviateNumber(json["assets"][0]["download_count"].as_int()).c_str());
        size->setString(convertSize(json["assets"][0]["size"].as_int()).c_str());
        published_at->setString(formatData(json["assets"][0]["updated_at"].as_string()).c_str());
        statsContainerMenu->updateLayout();
    }
    void openWebPage(CCObject*) {
        auto meta = getModMeta();
        CCApplication::sharedApplication()->openURL(
            std::string("https://geode-sdk.org/mods/" + meta.getID()).data()
        );
    }
    virtual void show() {
        //ModInfoPopup
        ModInfoPopup* aModInfoPopup = typeinfo_cast<ModInfoPopup*>(this);
        if (aModInfoPopup) {
            auto meta = getModMeta();
            //makeupthegitrepolink
            {
                std::string repo = meta.getRepository()
                    .value_or(fmt::format(
                        "https://github.com/{}/{}",
                        meta.getDeveloper(),
                        meta.getID().replace(0, meta.getDeveloper().size() + 1, "")
                    ));
                std::string repoapi = std::regex_replace(
                    repo,
                    std::regex("https://github.com/"),
                    "https://api.github.com/repos/"
                );
                generateAuthorizationData();
                requestStats(repoapi, meta);
            };
            //add official stuff
            {
                //webBtn.png
                auto webBtn = CCMenuItemSpriteExtra::create(
                    CCSprite::create("webBtn.png"_spr),
                    this, menu_selector(FLAlertLayerExt::openWebPage)
                );
                this->m_buttonMenu->addChild(webBtn);
                webBtn->setID("webBtn");
                webBtn->setPosition(26.f, 180.f);
                webBtn->setScale(0.9f);
                webBtn->m_baseScale = webBtn->getScale();
            };
            //statsContainerMenu
            {
                CCMenu* statsContainerMenu = CCMenu::create();
                this->m_buttonMenu->addChild(statsContainerMenu, 0, 562);
                statsContainerMenu->setID("statsContainerMenu");
                statsContainerMenu->setAnchorPoint({ 0.f, 0.0f });
                statsContainerMenu->setPosition({ 52.f, 196.f });
                statsContainerMenu->setScale(0.7f);
                statsContainerMenu->setLayout(
                    RowLayout::create()
                    ->setGap(20.f)
                    ->setGrowCrossAxis(true)
                    ->setAxisAlignment(AxisAlignment::Start)
                );
                statsContainerMenu->updateLayout();
                {
                    auto download_count = CCLabelTTF::create(
                        "...",
                        "arial",
                        12.f
                    );
                    statsContainerMenu->addChild(download_count);
                    download_count->setID("download_count");
                    download_count->setAnchorPoint({ 0.f, 0.5f });
                    download_count->setScale(0.65f);
                    download_count->addChild(CCSprite::createWithSpriteFrameName("GJ_sDownloadIcon_001.png"), 0, 521);
                    download_count->getChildByTag(521)->setAnchorPoint({ 1.0f, 0.0f });
                };
                {
                    auto size = CCLabelTTF::create(
                        "...",
                        "arial",
                        12.f
                    );
                    statsContainerMenu->addChild(size);
                    size->setID("size");
                    size->setAnchorPoint({ 0.f, 0.5f });
                    size->setScale(0.65f);
                    size->addChild(CCSprite::createWithSpriteFrameName("geode.loader/changelog.png"), 0, 521);
                    size->getChildByTag(521)->setAnchorPoint({ 1.10f, 0.0f });
                    size->getChildByTag(521)->setScale(0.6f);
                };
                {
                    auto published_at = CCLabelTTF::create(
                        "...",
                        "arial",
                        12.f
                    );
                    statsContainerMenu->addChild(published_at);
                    published_at->setID("published_at");
                    published_at->setAnchorPoint({ 0.f, 0.5f });
                    published_at->setScale(0.65f);
                    published_at->addChild(CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png"), 0, 521);
                    published_at->getChildByTag(521)->setAnchorPoint({ 1.10f, 0.0f });
                    published_at->getChildByTag(521)->setScale(0.6f);
                };
                statsContainerMenu->updateLayout();
            };
            //upd
            this->updateStats(0.f);
            aModInfoPopup->schedule(schedule_selector(FLAlertLayerExt::updateStats), 0.1f);
        }
        FLAlertLayer::show();
    }
};

/*void setupStatsForAllIndex() {
    log::info("{}", __FUNCTION__);
    std::vector<IndexItemHandle> vLatestItems = Index::get()->getLatestItems();
    for (auto catgirl : vLatestItems) {
        auto meta = catgirl->getMetadata();
        std::string repo = meta.getRepository()
            .value_or(fmt::format(
                "https://github.com/{}/{}",
                meta.getDeveloper(),
                meta.getID().replace(0, meta.getDeveloper().size() + 1, "")
            ));
        std::string repoapi = std::regex_replace(
            repo,
            std::regex("https://github.com/"),
            "https://api.github.com/repos/"
        );
        generateAuthorizationData();
        requestStats(repoapi, meta);
    }
}*/

$on_mod(Loaded) {
    generateAuthorizationData();
}