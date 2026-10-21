// Port of Sbtias/gdinput for iOS: the F6 keyboard hook is replaced by a mod setting.
// Behavior is otherwise unchanged. Credit: original mod by Sbtias.
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <deque>

using namespace geode::prelude;

namespace gdinput {
    // Buffer window in physics ticks (240 ticks = 1 second). 0 = off.
    inline int bufferTicks = 0;

    inline int ticksForStage(std::string const& s) {
        if (s.starts_with("NOOB"))        return 4;
        if (s.starts_with("PRO"))         return 8;
        if (s.starts_with("BEST MODE+"))  return 32;
        if (s.starts_with("BEST MODE"))   return 16;
        return 0; // OFF
    }

    inline bool verbose() {
        return Mod::get()->getSettingValue<bool>("verbose-log");
    }
}

// Geometry Dash's jump button ID is 1.
constexpr int GDINPUT_JUMP_BUTTON = 1;

struct GDInputPendingJump {
    bool player1;
    int remaining;
    bool released;
};

class $modify(GDInputBaseLayer, GJBaseGameLayer) {
    struct Fields {
        std::deque<GDInputPendingJump> pendingJumps;
        bool applying = false;
    };

    void handleButton(bool down, int button, bool isPlayer1) {
        auto* fields = m_fields.self();

        if (fields->applying || gdinput::bufferTicks <= 0 ||
            button != GDINPUT_JUMP_BUTTON) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto* player = isPlayer1 ? m_player1 : m_player2;
        if (!player) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto pending = fields->pendingJumps.end();
        for (auto it = fields->pendingJumps.begin(); it != fields->pendingJumps.end(); ++it) {
            if (it->player1 == isPlayer1) {
                pending = it;
                break;
            }
        }

        if (!down && pending != fields->pendingJumps.end()) {
            pending->released = true;
            if (gdinput::verbose()) log::info("[GDInput] Remembered release for buffered jump");
            return;
        }

        // Only buffer an early press while airborne (and not as wave).
        if (down && !player->m_isDart && !player->m_isOnGround) {
            const int window = gdinput::bufferTicks;
            if (pending != fields->pendingJumps.end()) {
                pending->remaining = window;
                pending->released = false;
            } else {
                fields->pendingJumps.push_back({isPlayer1, window, false});
            }
            if (gdinput::verbose()) log::info("[GDInput] Buffered early jump for {} ticks", window);
            return;
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto* fields = m_fields.self();

        for (auto it = fields->pendingJumps.begin(); it != fields->pendingJumps.end();) {
            auto* player = it->player1 ? m_player1 : m_player2;

            if (!player) {
                it = fields->pendingJumps.erase(it);
                continue;
            }

            if (player->m_isOnGround && !player->m_isDart) {
                const bool wasReleased = it->released;
                const bool player1 = it->player1;

                fields->applying = true;
                GJBaseGameLayer::handleButton(true, GDINPUT_JUMP_BUTTON, player1);
                if (wasReleased) {
                    GJBaseGameLayer::handleButton(false, GDINPUT_JUMP_BUTTON, player1);
                }
                fields->applying = false;

                if (gdinput::verbose()) log::info("[GDInput] Applied buffered jump on landing");
                it = fields->pendingJumps.erase(it);
                continue;
            }

            --it->remaining;
            if (it->remaining <= 0) {
                if (gdinput::verbose()) log::info("[GDInput] Buffered jump expired");
                it = fields->pendingJumps.erase(it);
            } else {
                ++it;
            }
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

class $modify(GDInputPlayLayer, PlayLayer) {
    void resetLevel() {
        auto* base = static_cast<GDInputBaseLayer*>(
            static_cast<GJBaseGameLayer*>(this)
        );
        base->m_fields->pendingJumps.clear();
        PlayLayer::resetLevel();
    }
};

$on_mod(Loaded) {
    gdinput::bufferTicks = gdinput::ticksForStage(
        Mod::get()->getSettingValue<std::string>("stage"));
    listenForSettingChanges<std::string>("stage", [](std::string value) {
        gdinput::bufferTicks = gdinput::ticksForStage(value);
        log::info("[GDInput] Buffer set to {} ticks", gdinput::bufferTicks);
    });
}
