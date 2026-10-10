#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>

#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace geode::prelude;

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

	// "1, 3,4" -> {1,3,4}
	std::set<int> parseExcluded(std::string const& text) {
		std::set<int> out;
		int cur = -1;
		for (char c : text) {
			if (c >= '0' && c <= '9') {
				if (cur < 0) cur = 0;
				cur = cur * 10 + (c - '0');
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
	struct Fields {
		std::vector<StartPosObject*> startPositions;
		std::vector<StartPosObject*> recent; // nullptr = level start
		StartPosObject* current = nullptr;   // nullptr = level start
		StartPosObject* pendingPick = nullptr;
		bool pending = false;                // a switch is waiting to be applied on respawn
		bool deathHandled = false;           // this death was already counted
		CCLabelBMFont* label = nullptr;
		int deathCount = 0;
		int totalDeaths = 0;
		int threshold = 0;                   // used by "randomize death count"
		int direction = 1;                   // used by ping-pong mode
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

		auto label = CCLabelBMFont::create("", "bigFont.fnt");
		label->setID("random-startpos-label"_spr);
		this->addChild(label, 1000);
		m_fields->label = label;
		this->rspUpdateLabel();
	}

	// PRIMARY death detection
	void destroyPlayer(PlayerObject* player, GameObject* obj) {
		bool wasDead = (m_player1 && m_player1->m_isDead) || (m_player2 && m_player2->m_isDead);
		PlayLayer::destroyPlayer(player, obj);
		bool isDead = (m_player1 && m_player1->m_isDead) || (m_player2 && m_player2->m_isDead);

		// Only react to a genuine, fresh death (ignores noclip, repeat calls)
		if (wasDead || !isDead) return;
		m_fields->deathHandled = true;
		if (obj != nullptr && obj == m_anticheatSpike) return;

		this->rspOnDeath();
	}

	// SAFETY NET + where the new StartPos is applied right before the respawn.
	void resetLevel() {
		// If a death happened that destroyPlayer didn't report, count it here.
		bool died = (m_player1 && m_player1->m_isDead) || (m_player2 && m_player2->m_isDead);
		if (died && !m_fields->deathHandled) {
			this->rspOnDeath();
		}
		m_fields->deathHandled = false;

		// Apply the queued StartPos immediately before the level resets,
		// so the respawn always uses the newly picked one.
		if (m_fields->pending) {
			m_fields->pending = false;
			if (m_isPracticeMode && opt<bool>("clear-checkpoints")) {
				this->removeAllCheckpoints();
			}
			this->setStartPosObject(m_fields->pendingPick);
		}

		PlayLayer::resetLevel();
		this->rspUpdateLabel();
	}

	void levelComplete() {
		PlayLayer::levelComplete();
		if (isEnabled() && opt<bool>("switch-on-complete") && !m_fields->startPositions.empty()) {
			this->rspSwitchStartPos();
		}
	}

	void rspOnDeath() {
		if (!isEnabled()) return;
		if (m_isPracticeMode && opt<bool>("ignore-practice")) return;
		if (m_fields->startPositions.empty()) return;
		if (this->getCurrentPercent() < static_cast<float>(opt<int64_t>("min-percent"))) return;

		m_fields->totalDeaths++;
		m_fields->deathCount++;
		if (m_fields->deathCount >= this->rspThreshold()) {
			this->rspSwitchStartPos();
		}
		this->rspUpdateLabel();
	}

	void rspSortStarts() {
		auto& starts = m_fields->startPositions;
		std::sort(starts.begin(), starts.end(), [](StartPosObject* a, StartPosObject* b) {
			return a->getPositionX() < b->getPositionX();
		});
	}

	int rspThreshold() {
		int base = static_cast<int>(opt<int64_t>("deaths-until-switch"));
		if (!opt<bool>("random-threshold")) return base;
		if (m_fields->threshold <= 0) {
			int hi = std::max(base, static_cast<int>(opt<int64_t>("max-deaths")));
			std::uniform_int_distribution<int> dist(base, hi);
			m_fields->threshold = dist(rng());
		}
		return m_fields->threshold;
	}

	// Picks a new StartPos and queues it for the next respawn.
	// Returns false if there was nothing to pick.
	bool rspSwitchStartPos() {
		this->rspSortStarts();
		auto& starts = m_fields->startPositions;
		if (starts.empty()) return false;

		auto excluded = parseExcluded(opt<std::string>("excluded"));

		// Pool of allowed spawns (nullptr = level start)
		std::vector<StartPosObject*> order;
		if (opt<bool>("include-level-start")) order.push_back(nullptr);
		for (size_t i = 0; i < starts.size(); i++) {
			if (excluded.count(static_cast<int>(i) + 1) == 0) order.push_back(starts[i]);
		}
		if (order.empty()) return false;

		auto& recent = m_fields->recent;
		if (recent.empty()) recent.push_back(m_fields->current);

		std::string mode = opt<std::string>("mode");
		StartPosObject* pick = nullptr;
		int n = static_cast<int>(order.size());
		auto curIt = std::find(order.begin(), order.end(), m_fields->current);
		int pos = (curIt == order.end()) ? -1 : static_cast<int>(curIt - order.begin());

		if (mode == "next") {
			pick = order[(pos + 1) % n];
		} else if (mode == "previous") {
			pick = order[(pos < 0) ? n - 1 : (pos - 1 + n) % n];
		} else if (mode == "ping-pong") {
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
		} else {
			size_t avoid = static_cast<size_t>(std::max<int64_t>(
				0, std::min<int64_t>(opt<int64_t>("avoid-recent"), static_cast<int64_t>(order.size()) - 1)
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

			std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
			pick = candidates[dist(rng())];
		}

		recent.push_back(pick);
		if (recent.size() > 10) recent.erase(recent.begin());

		m_fields->current = pick;
		m_fields->deathCount = 0;
		m_fields->threshold = 0;

		// Queue it AND apply it right now; resetLevel() applies it again just
		// before the respawn so it can never be missed.
		m_fields->pendingPick = pick;
		m_fields->pending = true;
		this->setStartPosObject(pick);

		if (opt<bool>("show-notification")) {
			std::string text = "Random StartPos: Level Start";
			if (pick) {
				auto it = std::find(starts.begin(), starts.end(), pick);
				int idx = static_cast<int>(it - starts.begin()) + 1;
				text = fmt::format("Random StartPos: {}/{}", idx, starts.size());
			}
			Notification::create(
				text, NotificationIcon::None, static_cast<float>(opt<double>("notification-duration"))
			)->show();
		}
		if (opt<bool>("play-sound")) {
			FMODAudioEngine::sharedEngine()->playEffect(opt<std::string>("sound").c_str());
		}

		this->rspUpdateLabel();
		return true;
	}

	void rspUpdateLabel() {
		auto label = m_fields->label;
		if (!label) return;

		auto& starts = m_fields->startPositions;
		bool show = isEnabled() && opt<bool>("show-label") && !starts.empty();
		label->setVisible(show);
		if (!show) return;

		std::string where = "Level Start";
		if (m_fields->current) {
			auto it = std::find(starts.begin(), starts.end(), m_fields->current);
			if (it != starts.end()) {
				where = fmt::format("{}/{}", static_cast<int>(it - starts.begin()) + 1, starts.size());
			}
		}
		int left = std::max(0, this->rspThreshold() - m_fields->deathCount);
		std::string text = fmt::format("StartPos {}  |  switch in {}", where, left);
		if (opt<bool>("show-total-deaths")) {
			text += fmt::format("  |  deaths {}", m_fields->totalDeaths);
		}
		label->setString(text.c_str());
		label->setScale(static_cast<float>(opt<double>("label-scale")));
		label->setColor(labelColor(opt<std::string>("label-color")));
		label->setOpacity(static_cast<GLubyte>(255 * opt<int64_t>("label-opacity") / 100));

		auto win = CCDirector::get()->getWinSize();
		float m = 8.f;
		std::string pos = opt<std::string>("label-position");
		if (pos == "top-right") {
			label->setAnchorPoint({1.f, 1.f});
			label->setPosition({win.width - m, win.height - m});
		} else if (pos == "bottom-left") {
			label->setAnchorPoint({0.f, 0.f});
			label->setPosition({m, m});
		} else if (pos == "bottom-right") {
			label->setAnchorPoint({1.f, 0.f});
			label->setPosition({win.width - m, m});
		} else {
			label->setAnchorPoint({0.f, 1.f});
			label->setPosition({m, win.height - m});
		}
	}
};

// ---------------------------------------------------------------------------
// PauseLayer: settings button, quick toggle, "switch now" button
// ---------------------------------------------------------------------------
struct $modify(RSPPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto menu = CCMenu::create();
		menu->setID("random-startpos-menu"_spr);
		auto win = CCDirector::get()->getWinSize();
		menu->setPosition({win.width - 60.f, 30.f});

		// Settings button
		auto settingsSpr = ButtonSprite::create("StartPos");
		settingsSpr->setScale(0.7f);
		auto settingsBtn = CCMenuItemSpriteExtra::create(
			settingsSpr, this, menu_selector(RSPPauseLayer::onRSPSettings)
		);
		settingsBtn->setPosition({0.f, 0.f});
		menu->addChild(settingsBtn);

		// Switch now button
		auto switchSpr = ButtonSprite::create("Switch Now");
		switchSpr->setScale(0.6f);
		auto switchBtn = CCMenuItemSpriteExtra::create(
			switchSpr, this, menu_selector(RSPPauseLayer::onRSPSwitchNow)
		);
		switchBtn->setPosition({0.f, 28.f});
		menu->addChild(switchBtn);

		// On/off toggle with a tiny label
		auto toggler = CCMenuItemToggler::createWithStandardSprites(
			this, menu_selector(RSPPauseLayer::onRSPToggle), 0.55f
		);
		toggler->toggle(isEnabled());
		toggler->setPosition({-40.f, 56.f});
		menu->addChild(toggler);

		auto text = CCLabelBMFont::create("Random StartPos", "bigFont.fnt");
		text->setScale(0.3f);
		text->setAnchorPoint({0.f, 0.5f});
		text->setPosition({-26.f, 56.f});
		menu->addChild(text);

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

	void onRSPSwitchNow(CCObject*) {
		if (auto pl = static_cast<RSPPlayLayer*>(PlayLayer::get())) {
			if (pl->rspSwitchStartPos()) {
				this->onRestart(nullptr);
			}
		}
	}
};
