#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>

#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Helpers + settings snapshot
// ---------------------------------------------------------------------------
namespace {
	std::mt19937& rng() {
		static std::mt19937 gen{std::random_device{}()};
		return gen;
	}

	template <class T>
	T opt(char const* key) {
		return Mod::get()->getSettingValue<T>(key);
	}

	bool isEnabled() {
		return opt<bool>("enabled");
	}

	// Everything the mod needs, read once per event (not per frame).
	struct Config {
		bool enabled = true;
		std::string trigger = "deaths";   // deaths | chance | streak
		std::string mode = "random";      // random | shuffle | weighted | next | previous | ping-pong
		int deaths = 1;
		bool randomThreshold = false;
		int maxDeaths = 5;
		int chance = 40;
		int streakTolerance = 2;
		int avoidRecent = 1;
		bool includeStart = false;
		bool switchOnComplete = false;
		bool clearCheckpoints = false;
		bool ignorePractice = false;
		int minPercent = 0;
		bool notification = false;
		float notifDuration = 0.8f;
		bool sound = false;
		std::string soundFile = "counter003.ogg";
		bool label = false;
		bool showTotal = false;
		std::string labelPos = "top-left";
		std::string labelColor = "white";
		float labelScale = 0.4f;
		int labelOpacity = 60;
	};

	Config loadConfig() {
		Config c;
		c.enabled = opt<bool>("enabled");
		if (!c.enabled) return c;

		c.trigger = opt<std::string>("trigger");
		c.mode = opt<std::string>("mode");
		c.deaths = static_cast<int>(opt<int64_t>("deaths-until-switch"));
		c.randomThreshold = opt<bool>("random-threshold");
		c.maxDeaths = static_cast<int>(opt<int64_t>("max-deaths"));
		c.chance = static_cast<int>(opt<int64_t>("chaos-chance"));
		c.streakTolerance = static_cast<int>(opt<int64_t>("streak-tolerance"));
		c.avoidRecent = static_cast<int>(opt<int64_t>("avoid-recent"));
		c.includeStart = opt<bool>("include-level-start");
		c.switchOnComplete = opt<bool>("switch-on-complete");
		c.clearCheckpoints = opt<bool>("clear-checkpoints");
		c.ignorePractice = opt<bool>("ignore-practice");
		c.minPercent = static_cast<int>(opt<int64_t>("min-percent"));
		c.notification = opt<bool>("show-notification");
		c.notifDuration = static_cast<float>(opt<double>("notification-duration"));
		c.sound = opt<bool>("play-sound");
		c.soundFile = opt<std::string>("sound");
		c.label = opt<bool>("show-label");
		c.showTotal = opt<bool>("show-total-deaths");
		c.labelPos = opt<std::string>("label-position");
		c.labelColor = opt<std::string>("label-color");
		c.labelScale = static_cast<float>(opt<double>("label-scale"));
		c.labelOpacity = static_cast<int>(opt<int64_t>("label-opacity"));

		// Presets override the trigger / mode / counts
		std::string preset = opt<std::string>("preset");
		if (preset == "practice") {
			c.trigger = "deaths"; c.mode = "weighted";
			c.deaths = 3; c.randomThreshold = false; c.avoidRecent = 1;
		} else if (preset == "challenge") {
			c.trigger = "deaths"; c.mode = "shuffle";
			c.deaths = 1; c.randomThreshold = false;
		} else if (preset == "chaos") {
			c.trigger = "chance"; c.mode = "random";
			c.chance = 40; c.avoidRecent = 0;
		} else if (preset == "drill") {
			c.trigger = "streak"; c.mode = "weighted";
			c.deaths = 5; c.streakTolerance = 2; c.avoidRecent = 1;
		}
		return c;
	}

	// "1, 3,4" -> {1,3,4}
	std::set<int> parseExcluded(std::string const& text) {
		std::set<int> out;
		int cur = -1;
		for (char ch : text) {
			if (ch >= '0' && ch <= '9') {
				if (cur < 0) cur = 0;
				cur = cur * 10 + (ch - '0');
				if (cur > 100000) cur = 100000;
			} else {
				if (cur > 0) out.insert(cur);
				cur = -1;
			}
		}
		if (cur > 0) out.insert(cur);
		return out;
	}

	ccColor3B labelColor(std::string const& name) {
		if (name == "green") return {120, 255, 120};
		if (name == "yellow") return {255, 230, 90};
		if (name == "red") return {255, 100, 100};
		if (name == "cyan") return {100, 230, 255};
		return {255, 255, 255};
	}
}

// ---------------------------------------------------------------------------
// PlayLayer: collect StartPos objects, detect deaths, switch automatically
// ---------------------------------------------------------------------------
struct $modify(RSPPlayLayer, PlayLayer) {
	// Run our resetLevel code BEFORE other mods' resetLevel hooks, so the new
	// StartPos is already set when StartPos Switcher / Smart StartPos (or any
	// other mod) look at it. We only use the game's own setStartPosObject.
	static void onModify(auto& self) {
		if (!self.setHookPriorityPre("PlayLayer::resetLevel", Priority::Early)) {
			log::warn("Random StartPos: could not set resetLevel hook priority");
		}
	}

	struct Fields {
		std::vector<StartPosObject*> startPositions;           // sorted left to right
		std::vector<StartPosObject*> recent;                   // nullptr = level start
		std::vector<StartPosObject*> bag;                      // shuffle mode
		std::unordered_map<StartPosObject*, int> deathsAt;     // weighted mode (key nullptr = before first StartPos)
		StartPosObject* current = nullptr;                     // nullptr = level start
		StartPosObject* pendingPick = nullptr;
		bool pending = false;                                  // a pick is waiting for the next respawn
		bool deathHandled = false;                             // this death was already counted
		CCNode* hud = nullptr;
		CCScale9Sprite* hudBg = nullptr;
		CCLabelBMFont* hudLabel = nullptr;
		std::string hudKey;
		int deathCount = 0;
		int totalDeaths = 0;
		int threshold = 0;                                     // randomized death count
		int streak = 0;                                        // deaths without improving
		float bestPercent = 0.f;                               // best death percent on current StartPos
		int direction = 1;                                     // ping-pong
	};

	// Object ID 31 is the StartPos object.
	void addObject(GameObject* obj) {
		PlayLayer::addObject(obj);
		if (obj && obj->m_objectID == 31) {
			if (auto sp = typeinfo_cast<StartPosObject*>(obj)) {
				m_fields->startPositions.push_back(sp);
			}
		}
	}

	void setupHasCompleted() {
		PlayLayer::setupHasCompleted();
		this->rspSortStarts();
	}

	// PRIMARY death detection
	void destroyPlayer(PlayerObject* player, GameObject* obj) {
		bool wasDead = (m_player1 && m_player1->m_isDead) || (m_player2 && m_player2->m_isDead);
		PlayLayer::destroyPlayer(player, obj);
		bool isDead = (m_player1 && m_player1->m_isDead) || (m_player2 && m_player2->m_isDead);

		// Only react to a genuine, fresh death (ignores noclip and repeat calls)
		if (wasDead || !isDead) return;
		m_fields->deathHandled = true;
		if (obj != nullptr && obj == m_anticheatSpike) return;
		if (!isEnabled()) return;

		this->rspOnDeath(loadConfig());
	}

	// SAFETY NET, and where the queued StartPos is applied right before respawn.
	void resetLevel() {
		if (!isEnabled()) {
			m_fields->deathHandled = false;
			PlayLayer::resetLevel();
			return;
		}
		auto c = loadConfig();

		// A death that destroyPlayer didn't report is counted here instead.
		bool died = (m_player1 && m_player1->m_isDead) || (m_player2 && m_player2->m_isDead);
		if (died && !m_fields->deathHandled) {
			this->rspOnDeath(c);
		}
		m_fields->deathHandled = false;

		// Apply the queued pick exactly once, immediately before the reset,
		// so the respawn (and its music start) use the new StartPos.
		if (m_fields->pending) {
			m_fields->pending = false;
			if (m_isPracticeMode && c.clearCheckpoints) {
				this->removeAllCheckpoints();
			}
			this->setStartPosObject(m_fields->pendingPick);
		}

		PlayLayer::resetLevel();
		this->rspRefreshHud(c);
	}

	void levelComplete() {
		PlayLayer::levelComplete();
		auto c = loadConfig();
		if (c.enabled && c.switchOnComplete && !m_fields->startPositions.empty()) {
			this->rspSwitchStartPos(c);
		}
	}

	// ---- logic ----------------------------------------------------------

	void rspSortStarts() {
		auto& starts = m_fields->startPositions;
		std::sort(starts.begin(), starts.end(), [](StartPosObject* a, StartPosObject* b) {
			return a->getPositionX() < b->getPositionX();
		});
	}

	// Which section (start of which StartPos) contains this X position?
	StartPosObject* rspSectionOf(float x) {
		StartPosObject* sec = nullptr;
		for (auto s : m_fields->startPositions) {
			if (s->getPositionX() <= x) sec = s;
			else break;
		}
		return sec;
	}

	int rspThreshold(Config const& c) {
		if (!c.randomThreshold) return c.deaths;
		if (m_fields->threshold <= 0) {
			int hi = std::max(c.deaths, c.maxDeaths);
			std::uniform_int_distribution<int> dist(c.deaths, hi);
			m_fields->threshold = dist(rng());
		}
		return m_fields->threshold;
	}

	void rspOnDeath(Config const& c) {
		if (!c.enabled) return;
		if (m_isPracticeMode && c.ignorePractice) return;
		if (m_fields->startPositions.empty()) return;

		float percent = this->getCurrentPercent();
		if (percent < static_cast<float>(c.minPercent)) return;

		float x = m_player1 ? m_player1->getPositionX() : 0.f;
		m_fields->deathsAt[this->rspSectionOf(x)]++;
		m_fields->totalDeaths++;

		bool doSwitch = false;
		if (c.trigger == "chance") {
			std::uniform_int_distribution<int> dist(1, 100);
			doSwitch = dist(rng()) <= c.chance;
		} else if (c.trigger == "streak") {
			if (percent > m_fields->bestPercent + static_cast<float>(c.streakTolerance)) {
				m_fields->bestPercent = percent; // improving: stay
				m_fields->streak = 0;
			} else {
				m_fields->streak++;
			}
			doSwitch = m_fields->streak >= c.deaths;
		} else {
			m_fields->deathCount++;
			doSwitch = m_fields->deathCount >= this->rspThreshold(c);
		}

		if (doSwitch) this->rspSwitchStartPos(c);
		this->rspRefreshHud(c);
	}

	// Picks a new StartPos and queues it for the next respawn.
	bool rspSwitchStartPos(Config const& c) {
		this->rspSortStarts();
		auto& starts = m_fields->startPositions;
		if (starts.empty()) return false;

		auto excluded = parseExcluded(opt<std::string>("excluded"));

		// Pool of allowed spawns (nullptr = level start)
		std::vector<StartPosObject*> order;
		if (c.includeStart) order.push_back(nullptr);
		for (size_t i = 0; i < starts.size(); i++) {
			if (excluded.count(static_cast<int>(i) + 1) == 0) order.push_back(starts[i]);
		}
		if (order.empty()) return false;

		auto& recent = m_fields->recent;
		if (recent.empty()) recent.push_back(m_fields->current);

		int n = static_cast<int>(order.size());
		auto curIt = std::find(order.begin(), order.end(), m_fields->current);
		int pos = (curIt == order.end()) ? -1 : static_cast<int>(curIt - order.begin());
		StartPosObject* pick = nullptr;

		if (c.mode == "next") {
			pick = order[(pos + 1) % n];
		} else if (c.mode == "previous") {
			pick = order[(pos < 0) ? n - 1 : (pos - 1 + n) % n];
		} else if (c.mode == "ping-pong") {
			int target;
			if (n == 1) {
				target = 0;
			} else if (pos < 0) {
				target = 0;
				m_fields->direction = 1;
			} else {
				target = pos + m_fields->direction;
				if (target >= n) { m_fields->direction = -1; target = n - 2; }
				else if (target < 0) { m_fields->direction = 1; target = 1; }
			}
			pick = order[target];
		} else if (c.mode == "shuffle") {
			// Go through every StartPos once (random order) before repeating.
			auto& bag = m_fields->bag;
			bag.erase(std::remove_if(bag.begin(), bag.end(), [&](StartPosObject* o) {
				return std::find(order.begin(), order.end(), o) == order.end();
			}), bag.end());
			if (bag.empty()) {
				bag = order;
				std::shuffle(bag.begin(), bag.end(), rng());
				if (bag.size() > 1 && bag.back() == m_fields->current) {
					std::swap(bag.back(), bag.front());
				}
			}
			pick = bag.back();
			bag.pop_back();
		} else {
			// random / weighted: skip the last N picks
			size_t avoid = static_cast<size_t>(std::max<int64_t>(
				0, std::min<int64_t>(c.avoidRecent, static_cast<int64_t>(order.size()) - 1)
			));
			avoid = std::min(avoid, recent.size());

			std::vector<StartPosObject*> candidates;
			for (auto o : order) {
				bool bad = false;
				for (size_t k = 0; k < avoid; k++) {
					if (recent[recent.size() - 1 - k] == o) { bad = true; break; }
				}
				if (!bad) candidates.push_back(o);
			}
			if (candidates.empty()) candidates = order;

			if (c.mode == "weighted") {
				std::vector<double> weights;
				weights.reserve(candidates.size());
				for (auto o : candidates) {
					auto it = m_fields->deathsAt.find(o);
					weights.push_back(1.0 + (it == m_fields->deathsAt.end() ? 0 : it->second));
				}
				std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
				pick = candidates[dist(rng())];
			} else {
				std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
				pick = candidates[dist(rng())];
			}
		}

		recent.push_back(pick);
		if (recent.size() > 10) recent.erase(recent.begin());

		m_fields->current = pick;
		m_fields->deathCount = 0;
		m_fields->threshold = 0;
		m_fields->streak = 0;
		m_fields->bestPercent = 0.f;

		// Queued only. It is applied once, right before the respawn (resetLevel),
		// so there is no duplicate work and the music start stays in sync.
		m_fields->pendingPick = pick;
		m_fields->pending = true;

		if (c.notification) {
			std::string text = "Random StartPos: Level Start";
			if (pick) {
				auto it = std::find(starts.begin(), starts.end(), pick);
				text = fmt::format("Random StartPos: {}/{}", static_cast<int>(it - starts.begin()) + 1, starts.size());
			}
			Notification::create(text, NotificationIcon::None, c.notifDuration)->show();
		}
		if (c.sound) {
			FMODAudioEngine::sharedEngine()->playEffect(c.soundFile.c_str());
		}

		this->rspRefreshHud(c);
		return true;
	}

	// ---- on-screen label (only built when enabled, only updated on change) ----

	void rspCreateHud() {
		auto hud = CCNode::create();
		hud->setID("random-startpos-hud"_spr);
		hud->ignoreAnchorPointForPosition(false);

		auto bg = CCScale9Sprite::create("square02_001.png");
		bg->setColor({10, 12, 28});
		hud->addChild(bg, 0);

		auto label = CCLabelBMFont::create("", "bigFont.fnt");
		hud->addChild(label, 1);

		this->addChild(hud, 1000);
		m_fields->hud = hud;
		m_fields->hudBg = bg;
		m_fields->hudLabel = label;
	}

	void rspRefreshHud(Config const& c) {
		bool show = c.enabled && c.label && !m_fields->startPositions.empty();
		if (!show) {
			if (m_fields->hud) m_fields->hud->setVisible(false);
			return;
		}
		if (!m_fields->hud) this->rspCreateHud();

		auto& starts = m_fields->startPositions;
		std::string where = "Level Start";
		if (m_fields->current) {
			auto it = std::find(starts.begin(), starts.end(), m_fields->current);
			if (it != starts.end()) {
				where = fmt::format("{}/{}", static_cast<int>(it - starts.begin()) + 1, starts.size());
			}
		}

		std::string status;
		if (c.trigger == "chance") {
			status = fmt::format("{}% per death", c.chance);
		} else if (c.trigger == "streak") {
			status = fmt::format("streak {}/{}", m_fields->streak, c.deaths);
		} else {
			status = fmt::format("switch in {}", std::max(0, this->rspThreshold(c) - m_fields->deathCount));
		}
		std::string text = fmt::format("StartPos {}  |  {}", where, status);
		if (c.showTotal) text += fmt::format("  |  deaths {}", m_fields->totalDeaths);

		std::string key = fmt::format("{}#{}#{}#{}#{}", text, c.labelPos, c.labelColor, c.labelScale, c.labelOpacity);
		if (key == m_fields->hudKey && m_fields->hud->isVisible()) return; // nothing changed
		m_fields->hudKey = key;

		auto hud = m_fields->hud;
		auto label = m_fields->hudLabel;
		auto bg = m_fields->hudBg;

		label->setString(text.c_str());
		label->setScale(c.labelScale);
		label->setColor(labelColor(c.labelColor));
		GLubyte op = static_cast<GLubyte>(255 * c.labelOpacity / 100);
		label->setOpacity(op);
		bg->setOpacity(static_cast<GLubyte>(op * 0.6f));

		auto size = label->getScaledContentSize();
		CCSize box(size.width + 20.f, size.height + 12.f);
		hud->setContentSize(box);
		bg->setContentSize(box);
		bg->setPosition({box.width / 2, box.height / 2});
		label->setPosition({box.width / 2, box.height / 2});

		auto win = CCDirector::get()->getWinSize();
		float m = 8.f;
		if (c.labelPos == "top-right") {
			hud->setAnchorPoint({1.f, 1.f});
			hud->setPosition({win.width - m, win.height - m});
		} else if (c.labelPos == "bottom-left") {
			hud->setAnchorPoint({0.f, 0.f});
			hud->setPosition({m, m});
		} else if (c.labelPos == "bottom-right") {
			hud->setAnchorPoint({1.f, 0.f});
			hud->setPosition({win.width - m, m});
		} else {
			hud->setAnchorPoint({0.f, 1.f});
			hud->setPosition({m, win.height - m});
		}
		hud->setVisible(true);
	}

	void rspUpdateLabel() {
		this->rspRefreshHud(loadConfig());
	}
};

// ---------------------------------------------------------------------------
// PauseLayer: one small, compact panel (toggle + settings)
// ---------------------------------------------------------------------------
struct $modify(RSPPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto win = CCDirector::get()->getWinSize();
		std::string where = opt<std::string>("pause-button-position");
		float w = 118.f, h = 28.f, margin = 8.f;
		float cx = (where == "bottom-left" || where == "top-left") ? margin + w / 2 : win.width - margin - w / 2;
		float cy = (where == "top-left" || where == "top-right") ? win.height - margin - h / 2 : margin + h / 2;

		auto menu = CCMenu::create();
		menu->setID("random-startpos-menu"_spr);
		menu->setPosition({cx, cy});

		// dark rounded panel
		auto panel = CCScale9Sprite::create("square02_001.png");
		panel->setContentSize(CCSize(w, h));
		panel->setColor({12, 14, 32});
		panel->setOpacity(190);
		menu->addChild(panel, -1);

		// on/off checkbox
		auto toggler = CCMenuItemToggler::createWithStandardSprites(
			this, menu_selector(RSPPauseLayer::onRSPToggle), 0.5f
		);
		toggler->toggle(isEnabled());
		toggler->setPosition({-w / 2 + 16.f, 0.f});
		menu->addChild(toggler);

		// title
		auto title = CCLabelBMFont::create("StartPos", "bigFont.fnt");
		title->setScale(0.36f);
		title->setAnchorPoint({0.f, 0.5f});
		title->setPosition({-w / 2 + 31.f, 0.f});
		title->setColor({190, 235, 255});
		menu->addChild(title);

		// settings button (gear if the sprite exists, otherwise a small text button)
		CCNode* gear = nullptr;
		if (CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName("GJ_optionsBtn02_001.png")) {
			auto spr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
			spr->setScale(0.42f);
			gear = spr;
		} else {
			auto spr = ButtonSprite::create("...");
			spr->setScale(0.5f);
			gear = spr;
		}
		auto settingsBtn = CCMenuItemSpriteExtra::create(
			gear, this, menu_selector(RSPPauseLayer::onRSPSettings)
		);
		settingsBtn->setPosition({w / 2 - 16.f, 0.f});
		menu->addChild(settingsBtn);

		this->addChild(menu);
	}

	void onRSPSettings(CCObject*) {
		openSettingsPopup(Mod::get());
	}

	void onRSPToggle(CCObject*) {
		Mod::get()->setSettingValue<bool>("enabled", !isEnabled());
		if (auto pl = static_cast<RSPPlayLayer*>(PlayLayer::get())) {
			pl->rspUpdateLabel();
		}
	}
};
