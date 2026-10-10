#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>

#include <algorithm>
#include <random>
#include <vector>

using namespace geode::prelude;

namespace {
	std::mt19937& rng() {
		static std::mt19937 gen{std::random_device{}()};
		return gen;
	}

	bool isEnabled() {
		return Mod::get()->getSettingValue<bool>("enabled");
	}
}

// ---------------------------------------------------------------------------
// PlayLayer: collect StartPos objects, count deaths, switch randomly
// ---------------------------------------------------------------------------
class $modify(RSPPlayLayer, PlayLayer) {
	struct Fields {
		std::vector<StartPosObject*> startPositions;
		StartPosObject* current = nullptr; // nullptr = level start / unknown
		int deathCount = 0;
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

	void destroyPlayer(PlayerObject* player, GameObject* obj) {
		bool wasDead = (m_player1 && m_player1->m_isDead) || (m_player2 && m_player2->m_isDead);
		PlayLayer::destroyPlayer(player, obj);
		bool isDead = (m_player1 && m_player1->m_isDead) || (m_player2 && m_player2->m_isDead);

		// Only react to a genuine, fresh death (ignores noclip, anti-cheat spike, repeat calls)
		if (wasDead || !isDead) return;
		if (obj != nullptr && obj == m_anticheatSpike) return;
		if (!isEnabled()) return;
		if (m_isPracticeMode && Mod::get()->getSettingValue<bool>("ignore-practice")) return;
		if (m_fields->startPositions.empty()) return;

		m_fields->deathCount++;
		int64_t threshold = Mod::get()->getSettingValue<int64_t>("deaths-until-switch");
		if (m_fields->deathCount >= threshold) {
			m_fields->deathCount = 0;
			this->rspSwitchStartPos();
		}
	}

	void rspSwitchStartPos() {
		auto& starts = m_fields->startPositions;
		if (starts.empty()) return;

		// Sort left-to-right so the numbering matches the level layout
		std::sort(starts.begin(), starts.end(), [](StartPosObject* a, StartPosObject* b) {
			return a->getPositionX() < b->getPositionX();
		});

		bool includeStart = Mod::get()->getSettingValue<bool>("include-level-start");
		bool noRepeat = Mod::get()->getSettingValue<bool>("no-repeat");

		// Candidates: -1 = level start, 0..n-1 = StartPos index
		std::vector<int> candidates;
		if (includeStart) candidates.push_back(-1);
		for (int i = 0; i < static_cast<int>(starts.size()); i++) candidates.push_back(i);

		int currentIdx = -1;
		if (m_fields->current) {
			auto it = std::find(starts.begin(), starts.end(), m_fields->current);
			if (it != starts.end()) currentIdx = static_cast<int>(it - starts.begin());
		}

		if (noRepeat && candidates.size() > 1) {
			candidates.erase(std::remove(candidates.begin(), candidates.end(), currentIdx), candidates.end());
		}
		if (candidates.empty()) return;

		std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
		int pick = candidates[dist(rng())];

		if (pick < 0) {
			m_fields->current = nullptr;
			this->setStartPosObject(nullptr);
		} else {
			m_fields->current = starts[pick];
			this->setStartPosObject(starts[pick]);
		}

		if (Mod::get()->getSettingValue<bool>("show-notification")) {
			std::string text = pick < 0
				? std::string("Random StartPos: Level Start")
				: fmt::format("Random StartPos: {}/{}", pick + 1, starts.size());
			Notification::create(text, NotificationIcon::None, 0.8f)->show();
		}
	}
};

// ---------------------------------------------------------------------------
// PauseLayer: button that opens this mod's settings menu
// ---------------------------------------------------------------------------
class $modify(RSPPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto spr = ButtonSprite::create("StartPos");
		spr->setScale(0.7f);

		auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(RSPPauseLayer::onRSPSettings));

		auto menu = CCMenu::create();
		menu->addChild(btn);
		auto win = CCDirector::get()->getWinSize();
		menu->setPosition({win.width - 60.f, 30.f});
		menu->setID("random-startpos-menu"_spr);
		this->addChild(menu);
	}

	void onRSPSettings(CCObject*) {
		openSettingsPopup(Mod::get());
	}
};
