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
    //lastchar
    str.pop_back();
    return str;
}

#define ghapiauth \
.userAgent(AUTH_DATA["name"].as_string() + ":" + AUTH_DATA["address"].as_string()) \
.header("Accept", "application/vnd.github+json") \
.header("X-GitHub-Api-Version", "2022-11-28") \
.body("{\"access_token\": \"" + ACCESS_TOKEN + "\"}") \

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
std::vector<ReleaseData*> latestReleases;
void setupStats(matjson::Value const& cattogirl, ModMetadata meta, bool isLatest = false) {
    log::info("{}", __FUNCTION__);
    if (isLatest) {
        ghc::filesystem::create_directories(geode::dirs::getIndexDir() / "releases");
        std::ofstream((geode::dirs::getIndexDir() / "releases" / (meta.getID() + "-latest.json")).string()) << cattogirl.dump();
        latestReleases.push_back(ReleaseData::create(cattogirl, meta.getID()));
    }
    else {
        ghc::filesystem::create_directories(geode::dirs::getIndexDir() / "releases");
        std::ofstream((geode::dirs::getIndexDir() / "releases" / (meta.getID() + ".json")).string()) << cattogirl.dump();
        releases.push_back(ReleaseData::create(cattogirl, meta.getID()));
    }
}
void requestLocalStats(std::string repoapi, ModMetadata meta) {
    log::info("{}...", __FUNCTION__);
    {
        std::ifstream json((geode::dirs::getIndexDir() / "releases" / (meta.getID() + ".json")).string());
        if (!json.good()) return log::warn("{}", "cant get locally saved release data");
        setupStats(matjson::parse((std::stringstream() << json.rdbuf()).str()), meta);
    };
    {
        std::ifstream json((geode::dirs::getIndexDir() / "releases" / (meta.getID() + "-latest.json")).string());
        if (!json.good()) return log::warn("{}", "cant get locally saved latest release data");
        setupStats(matjson::parse((std::stringstream() << json.rdbuf()).str()), meta, true);
    };
}
void requestLatestStats(std::string repoapi, ModMetadata meta, bool saveOnly = false) {
    auto endpoint = repoapi + "/releases/latest";
    log::info("{}({})", __FUNCTION__, endpoint);
    web::AsyncWebRequest()
        ghapiauth
        .fetch(endpoint)
        .json()
        .then([repoapi, meta, saveOnly](matjson::Value const& cattogirl) {
                log::info("{}", __FUNCTION__);
                setupStats(cattogirl, meta, saveOnly);
            })
        .expect([repoapi, meta, endpoint](std::string const& error) {
                log::info("{} => {}", endpoint, error);
                requestLocalStats(repoapi, meta);
            });
}
void requestStats(std::string repoapi, ModMetadata meta) {
    requestLocalStats(repoapi, meta);//setup local save instantly and then it got updated later
    auto endpoint = repoapi + "/releases/tags/" + meta.getVersion().toString();
    if (repoapi.find("ungh.cc") != std::string::npos) endpoint = repoapi + "/releases";
    log::info("{}({})", __FUNCTION__, endpoint);
    web::AsyncWebRequest()
        ghapiauth
        .fetch(endpoint)
        .json()
        .then([repoapi, meta](auto cattogirl) {
                log::info("{}", __FUNCTION__);
                if (cattogirl.contains("releases")) {
                    //���� � ��������� ����� ���������� � ���� ����
                    for (auto asd : cattogirl["releases"].as_array()) {
                        //log::debug("{}", asd.dump());
                        if (asd["tag"].as_string() == meta.getVersion().toString()) {
                            setupStats(matjson::parse("{\"release\": " + asd.dump() + "}"), meta);
                            requestLatestStats(repoapi, meta, true);
                            return;
                        }
                    };
                    if (cattogirl["releases"].as_array().size() != 0) {
                        setupStats(matjson::parse("{\"release\": " + cattogirl["releases"].as_array()[0].dump() + "}"), meta);
                        requestLatestStats(repoapi, meta, true);
                    }
                }
                else {
                    setupStats(cattogirl, meta);
                    requestLatestStats(repoapi, meta, true);
                };
            })
        .expect([repoapi, meta](std::string const& error) {
                log::warn("{}", error);
                requestLatestStats(repoapi, meta);
                requestLatestStats(repoapi, meta, true);
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
        auto latestRelease = dynamic_cast<CCLabelTTF*>(this->getChildByIDRecursive("latestRelease"));
        auto webBtn = dynamic_cast<CCMenuItemSpriteExtra*>(this->getChildByIDRecursive("webBtn"));
        auto downloadLatest = dynamic_cast<CCMenuItemSpriteExtra*>(this->getChildByIDRecursive("downloadLatest"));
        if (!statsContainerMenu) return;
        if (!download_count) return;
        if (!size) return;
        if (!published_at) return;
        if (!latestRelease) return;
        if (!webBtn) return;
        if (!downloadLatest) return;
        auto pIndexItemHandle = Index::get()->getItem(getModMeta());
        auto isKnownItem = Index::get()->isKnownItem(getModMeta().getID(), getModMeta().getVersion());
        auto isUpdateAvailable = 
        isKnownItem ? 
        Index::get()->getMajorItem(getModMeta().getID()) != pIndexItemHandle 
        : false;
        bool onLocalPopup = typeinfo_cast<LocalModInfoPopup*>(this);
        //Btns
        webBtn->setVisible(isKnownItem);
        downloadLatest->setVisible(!isUpdateAvailable and onLocalPopup);
        //json
        matjson::Value releaseJson = "{}";
        for (auto asd : releases) {
            if (asd->m_modID == getModMeta().getID()) releaseJson = asd->m_json;
        }
        if (releaseJson.contains("assets")) {
            //setup labels
            download_count->setString(abbreviateNumber(releaseJson["assets"][0]["download_count"].as_int()).c_str());
            size->setString(convertSize(releaseJson["assets"][0]["size"].as_int()).c_str());
            published_at->setString(formatData(releaseJson["assets"][0]["updated_at"].as_string()).c_str());
            statsContainerMenu->updateLayout();
            statsContainerMenu->setVisible(true);
        }
        else if (releaseJson.contains("release")) {
            //setup labels
            published_at->setString(formatData(releaseJson["release"]["publishedAt"].as_string()).c_str());
            statsContainerMenu->updateLayout();
            statsContainerMenu->setVisible(true);
        }
        else return statsContainerMenu->setVisible(false);
        //latestReleases
        /**/matjson::Value latestReleaseJson = "{}";
        for (auto asd : latestReleases) {
            if (asd->m_modID == getModMeta().getID()) latestReleaseJson = asd->m_json;
        }
        std::string inUseStr = (latestReleaseJson == releaseJson) ? ":" : "";
        if (latestReleaseJson.contains("assets")) {
            //setup labels
            latestRelease->setString((latestReleaseJson["tag_name"].as_string() + inUseStr).c_str());
        }
        else if (latestReleaseJson.contains("release")) {
            //setup labels
            latestRelease->setString((latestReleaseJson["release"]["tag"].as_string() + inUseStr).c_str());
        }
    }
    std::string gitrepolnk() {
        auto meta = getModMeta();
        return meta.getRepository()
            .value_or(fmt::format(
                "https://github.com/{}/{}",
                meta.getDeveloper(),
                meta.getID().replace(0, meta.getDeveloper().size() + 1, "")
            ));
    };
    void downloadLatestPopup(CCObject*) {
        auto meta = getModMeta();
        //link
        auto linker = fmt::format(
            "{}/releases/latest/download/"
            "{}.geode"
            ,
            gitrepolnk(),
            meta.getID()
        );
        //oldtag => tartag
        auto latestRelease = dynamic_cast<CCLabelTTF*>(this->getChildByIDRecursive("latestRelease"));
        std::string updprev = fmt::format("\n<cg>Version:</c>\n{} => {}", meta.getVersion().toString(), latestRelease->getString());
        //moreinfo
        std::stringstream moreinfo;
        //releaseJson
        matjson::Value releaseJson = "{}";
        for (auto asd : releases) if (asd->m_modID == getModMeta().getID()) releaseJson = asd->m_json;
        //latestReleaseJson
        matjson::Value latestReleaseJson = "{}";
        for (auto asd : latestReleases) if (asd->m_modID == getModMeta().getID()) latestReleaseJson = asd->m_json;
        //fill the moreinfo if latestReleaseJson and releaseJson is gooood
        if (latestReleaseJson.contains("assets") and releaseJson.contains("assets")) {
            auto assetdata1 = releaseJson["assets"][0];
            auto assetdata2 = latestReleaseJson["assets"][0];
            moreinfo << "\n<cy>Updated at:</c>\n"
                << formatData(assetdata1["updated_at"].as_string()) << "=> " << formatData(assetdata2["updated_at"].as_string()) << "";
            moreinfo << "\n<co>Size:</c>\n" << convertSize(assetdata1["size"].as_int()) << " => " << convertSize(assetdata2["size"].as_int()) << "";
            //updated_at
        }
        else if (latestReleaseJson.contains("release") and releaseJson.contains("release")) {
            auto assetdata1 = releaseJson["release"];
            auto assetdata2 = latestReleaseJson["release"];
            moreinfo << "\n<cy>Updated at:</c>\n"
                << formatData(assetdata1["publishedAt"].as_string()) << "=> " << formatData(assetdata2["publishedAt"].as_string()) << "";
        }
        auto pop = geode::createQuickPopup(
            "Update to latest",
            "Download <cg>last release</c> of <cy>this mod</c>?"
            "\n<cj>" + linker + "</c>"
            + updprev
            + moreinfo.str(),
            "Abort", "Yes",
            430.f,
            [this](auto, bool btn2) {
                if (btn2) this->downloadLatest(this);
            }
        );
    }
    void downloadLatest(CCObject*) {
        auto meta = getModMeta();
        auto linker = fmt::format(
         "{}/releases/latest/download/"
         "{}.geode"
         ,
         gitrepolnk(), 
         meta.getID()
        );
        Notification::create("Downloading...", NotificationIcon::Loading, 3.f)->show();
        web::AsyncWebRequest()
            .fetch(linker)
            .bytes()
            .then([meta](ByteVector& catgirl) {
                    auto path = geode::dirs::getModsDir() / (meta.getID() + ".geode");
                    std::ofstream outfile(path.string().data(), std::ios::binary);
                    for (auto atom : catgirl) {
                        outfile << atom;
                    }
                    outfile.close();
                    geode::createQuickPopup(
                        "Restart Game",
                        "Restart game to load new mod?",
                        "Later", "Yes",
                        [](auto, bool btn2) {
                            if (btn2) utils::game::restart();
                        }
                    );
                    Notification::create("Download finished", NotificationIcon::Success, 5.0f)->show();
                })
            .expect([](std::string const& error) {
                    Notification::create("Downloading failed:\n" + error, NotificationIcon::Error, 5.0f)->show();
            });
    }
    void openWebPage(CCObject*) {
        auto meta = getModMeta();
        CCApplication::sharedApplication()->openURL(
            std::string("https://geode-sdk.org/mods/" + meta.getID()).data()
        );
    }
    void comments(CCObject*) {
        std::string repo = gitrepolnk();
        std::string repoapi = std::regex_replace(
         repo,
         std::regex("https://github.com/"),
         "https://ungh.cc/repos/"
        );
        log::info("{} <= {}", __FUNCTION__, repoapi);
        auto res = web::fetchJSON(repoapi);
        if (res.has_error()) 
            return 
            FLAlertLayer::create(nullptr, "Cant fetch API request!", ("<cr>" + res.error()).c_str(), "GOT THAT", nullptr)
            ->show();
        auto json = res.value();
        log::info("{}", json["repo"]["id"].as_int());
        auto id = json["repo"]["id"].as_int();
        auto levelListSkit = GJLevelList::create();
        levelListSkit->m_listID = id;
        levelListSkit->m_listName = json["repo"]["name"].as_string();
        levelListSkit->m_creatorName = json["repo"]["repo"].as_string();
        levelListSkit->m_listDesc = ZipUtils::base64URLEncode(json["repo"]["description"].as_string());
        auto InfoLayerForMod = InfoLayer::create(nullptr, nullptr, levelListSkit);
        InfoLayerForMod->setID("InfoLayerForMod");
        InfoLayerForMod->show();
    }
    virtual void show() {
        //ModInfoPopup
        ModInfoPopup* aModInfoPopup = typeinfo_cast<ModInfoPopup*>(this);
        if (aModInfoPopup) {
            auto UNGH_API = Mod::get()->getSettingValue<bool>("UNGH_API");
            auto api = UNGH_API ? "https://ungh.cc/repos/" : "https://api.github.com/repos/";
            std::string repoapi = std::regex_replace(gitrepolnk(), std::regex("https://github.com/"), api);
            requestStats(repoapi, getModMeta());
            //add official stuff
            {
                //webBtn.png
                auto webBtn = CCMenuItemSpriteExtra::create(
                    CCSprite::create("webBtn.png"_spr),
                    this, menu_selector(FLAlertLayerExt::openWebPage)
                );
                this->m_buttonMenu->addChild(webBtn);
                webBtn->setID("webBtn");
                webBtn->setPosition(21.5f, 178.f);
                webBtn->setScale(0.9f);
                webBtn->m_baseScale = webBtn->getScale();
            };
            //sus
            {
                //downloadLatestBtnNode
                auto hi = geode::AccountButtonSprite::create(
                    CCSprite::createWithSpriteFrameName("GJ_sRecentIcon_001.png"),
                    AccountBaseColor::Purple
                );
                hi->setScale(0.55f);
                typeinfo_cast<CCNode*>(hi->getChildren()->objectAtIndex(0))->setScale(1.8f);
                //downloadLatestBtn
                auto downloadLatest = CCMenuItemSpriteExtra::create(
                    hi, this, menu_selector(FLAlertLayerExt::downloadLatestPopup)
                );
                this->m_buttonMenu->addChild(downloadLatest);
                downloadLatest->setID("downloadLatest");
                downloadLatest->setPosition(187.f, 220.f);
            };
            //sus
            {
                //comments
                auto comments = CCMenuItemSpriteExtra::create(
                    CCSprite::createWithSpriteFrameName("GJ_sRecentIcon_001.png"),
                    this, menu_selector(FLAlertLayerExt::comments)
                );
                this->m_buttonMenu->addChild(comments);
                comments->setID("comments");
                comments->setPosition(26.f, 290.f);
                comments->setScale(0.9f);
                comments->m_baseScale = comments->getScale();
                comments->setVisible(0);
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
                    auto latestRelease = CCLabelTTF::create(
                        "...",
                        "arial",
                        12.f
                    );
                    statsContainerMenu->addChild(latestRelease);
                    latestRelease->setID("latestRelease");
                    latestRelease->setAnchorPoint({ 0.f, 0.5f });
                    latestRelease->setScale(1.65f);
                    latestRelease->addChild(CCSprite::createWithSpriteFrameName("GJ_sRecentIcon_001.png"), 0, 521);
                    latestRelease->getChildByTag(521)->setAnchorPoint({ 1.10f, 0.0f });
                    latestRelease->getChildByTag(521)->setScale(1.0f);
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
                statsContainerMenu->updateLayout();
            };
            //upd
            this->updateStats(0.f);
            aModInfoPopup->schedule(schedule_selector(FLAlertLayerExt::updateStats), 0.1f);
        }
        FLAlertLayer::show();
    }
};

auto setupedStatsForAllIndex = false;
void setupStatsForAllIndex() {
    log::info("{}", __FUNCTION__);
    if (setupedStatsForAllIndex) return;
    else setupedStatsForAllIndex = true;
    std::vector<IndexItemHandle> vLatestItems = Index::get()->getLatestItems();
    for (auto catgirl : vLatestItems) {
        auto meta = catgirl->getMetadata();
        log::info("{} ... {}", __FUNCTION__, meta.getName());
        std::string repo = meta.getRepository()
            .value_or(fmt::format(
                "https://github.com/{}/{}",
                meta.getDeveloper(),
                meta.getID().replace(0, meta.getDeveloper().size() + 1, "")
            ));
        auto UNGH_API = Mod::get()->getSettingValue<bool>("UNGH_API");
        auto api = UNGH_API ? "https://ungh.cc/repos/" : "https://api.github.com/repos/";
        std::string repoapi = std::regex_replace(repo, std::regex("https://github.com/"), api);
        requestStats(repoapi, meta);
    }
}

#include <Geode/modify/CCLayer.hpp>
class $modify(CCLayerExt, CCLayer) {
    bool init() {
        auto rtn = CCLayer::init();
        //ModListLayer
        ModListLayer* pModListLayer = typeinfo_cast<ModListLayer*>(this);
        if (pModListLayer) {
            auto preloadReleaseDataForAllIndex = Mod::get()->getSettingValue<bool>("preloadReleaseDataForAllIndex");
            if (preloadReleaseDataForAllIndex) setupStatsForAllIndex();
        }
        return rtn;
    };
};

$on_mod(Loaded) {
    generateAuthorizationData();
}