#include "game.hpp"
#include "profile.hpp"
#include "daily.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
using ta::GameSim;
using ta::Vec2;

struct Color { Uint8 r, g, b, a = 255; };

const std::array<std::array<Uint8, 7>, 36>& pixelFont() {
    static const std::array<std::array<Uint8, 7>, 36> font{{
        {{14,17,17,31,17,17,17}}, {{30,17,17,30,17,17,30}}, {{15,16,16,16,16,16,15}},
        {{30,17,17,17,17,17,30}}, {{31,16,16,30,16,16,31}}, {{31,16,16,30,16,16,16}},
        {{15,16,16,23,17,17,15}}, {{17,17,17,31,17,17,17}}, {{31,4,4,4,4,4,31}},
        {{1,1,1,1,17,17,14}}, {{17,18,20,24,20,18,17}}, {{16,16,16,16,16,16,31}},
        {{17,27,21,21,17,17,17}}, {{17,25,21,19,17,17,17}}, {{14,17,17,17,17,17,14}},
        {{30,17,17,30,16,16,16}}, {{14,17,17,17,21,18,13}}, {{30,17,17,30,20,18,17}},
        {{15,16,16,14,1,1,30}}, {{31,4,4,4,4,4,4}}, {{17,17,17,17,17,17,14}},
        {{17,17,17,17,17,10,4}}, {{17,17,17,21,21,21,10}}, {{17,17,10,4,10,17,17}},
        {{17,17,10,4,4,4,4}}, {{31,1,2,4,8,16,31}}, {{14,17,19,21,25,17,14}},
        {{4,12,4,4,4,4,14}}, {{14,17,1,2,4,8,31}}, {{30,1,1,14,1,1,30}},
        {{2,6,10,18,31,2,2}}, {{31,16,16,30,1,1,30}}, {{14,16,16,30,17,17,14}},
        {{31,1,2,4,8,8,8}}, {{14,17,17,14,17,17,14}}, {{14,17,17,15,1,1,14}}
    }};
    return font;
}

void drawText(SDL_Renderer* renderer, int x, int y, const std::string& text, int scale, Color color) {
    const auto& font = pixelFont();
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int cursor = x;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (char raw : text) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
        if (c == ' ') { cursor += 6 * scale; continue; }
        const std::size_t index = alphabet.find(c);
        if (index == std::string::npos) { cursor += 6 * scale; continue; }
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((font[index][static_cast<std::size_t>(row)] & (1u << (4 - column))) != 0) {
                    SDL_Rect pixel{cursor + column * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
        cursor += 6 * scale;
    }
}

class AudioSynth {
public:
    bool init() {
        SDL_AudioSpec desired{};
        desired.freq = 44100;
        desired.format = AUDIO_F32SYS;
        desired.channels = 1;
        desired.samples = 1024;
        device = SDL_OpenAudioDevice(nullptr, 0, &desired, &spec, 0);
        if (device == 0) return false;
        SDL_PauseAudioDevice(device, 0);
        return true;
    }

    ~AudioSynth() {
        shutdown();
    }

    void setVolume(float value) { gain = std::clamp(value, 0.0f, 1.0f); }

    void shutdown() {
        if (device != 0) {
            SDL_ClearQueuedAudio(device);
            SDL_CloseAudioDevice(device);
            device = 0;
        }
    }

    void tone(float frequency, int milliseconds, float volume = 0.18f) {
        if (device == 0 || spec.freq <= 0) return;
        const int count = std::max(1, spec.freq * milliseconds / 1000);
        std::vector<float> samples(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(spec.freq);
            const float envelope = std::min(1.0f, static_cast<float>(i) / (spec.freq * 0.008f)) * std::min(1.0f, static_cast<float>(count - i) / (spec.freq * 0.035f));
            samples[static_cast<std::size_t>(i)] = std::sin(2.0f * 3.14159265f * frequency * t) * volume * gain * envelope;
        }
        SDL_QueueAudio(device, samples.data(), static_cast<Uint32>(samples.size() * sizeof(float)));
    }

private:
    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec spec{};
    float gain = 1.0f;
};

void rect(SDL_Renderer* renderer, int x, int y, int w, int h, Color color, bool filled = true) {
    SDL_Rect r{x, y, w, h};
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    if (filled) SDL_RenderFillRect(renderer, &r); else SDL_RenderDrawRect(renderer, &r);
}

void circle(SDL_Renderer* renderer, int cx, int cy, int radius, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int y = -radius; y <= radius; ++y) {
        const int span = static_cast<int>(std::sqrt(std::max(0, radius * radius - y * y)));
        SDL_RenderDrawLine(renderer, cx - span, cy + y, cx + span, cy + y);
    }
}

void line(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, Color color, int width = 1) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i < width; ++i) SDL_RenderDrawLine(renderer, x1, y1 + i, x2, y2 + i);
}

void ring(SDL_Renderer* renderer, int cx, int cy, int radius, Color color, int thickness = 1) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int layer = 0; layer < thickness; ++layer) {
        const int r = radius + layer;
        for (int angle = 0; angle < 360; angle += 3) {
            const float radians = static_cast<float>(angle) * 3.14159265f / 180.0f;
            SDL_RenderDrawPoint(renderer, cx + static_cast<int>(std::cos(radians) * r), cy + static_cast<int>(std::sin(radians) * r));
        }
    }
}

void filledDiamond(SDL_Renderer* renderer, int cx, int cy, int radius, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int y = -radius; y <= radius; ++y) {
        const int span = radius - std::abs(y);
        SDL_RenderDrawLine(renderer, cx - span, cy + y, cx + span, cy + y);
    }
}

float renderPathY(float x, int enemyId, ta::Arena arena, const ta::ContentConfig& content) {
    const std::size_t index = static_cast<std::size_t>(arena);
    const float idOffset = index == 0 ? 37.0f : (index == 1 ? 23.0f : 51.0f);
    return 360.0f + std::sin((x + enemyId * idOffset) * content.arenaPathFrequency[index]) * content.arenaPathAmplitude[index];
}

void drawArena(SDL_Renderer* renderer, bool highContrast, ta::Arena arena, const ta::ContentConfig& content) {
    rect(renderer, 0, 0, GameSim::Width, GameSim::Height, {8, 13, 28});
    const Color grid = highContrast ? Color{32, 54, 78} : Color{14, 25, 48};
    for (int x = 0; x < GameSim::Width; x += 64) line(renderer, x, 80, x, 680, grid);
    for (int y = 80; y < 680; y += 64) line(renderer, 0, y, GameSim::Width, y, grid);
    const Color lane = arena == ta::Arena::EmberCrater ? Color{110, 58, 48} : (arena == ta::Arena::NeonRuins ? Color{42, 90, 100} : Color{24, 47, 71});
    for (int x = 80; x < 1160; x += 8) {
        const int y = static_cast<int>(renderPathY(static_cast<float>(x), 0, arena, content));
        circle(renderer, x, y, 31, lane);
    }
    line(renderer, 78, static_cast<int>(renderPathY(78.0f, 0, arena, content)), 1180, static_cast<int>(renderPathY(1180.0f, 0, arena, content)), {42, 79, 102}, 2);
}

void drawHud(SDL_Renderer* renderer, const GameSim& sim, bool highContrast = false) {
    rect(renderer, 0, 0, GameSim::Width, 72, highContrast ? Color{5, 10, 20} : Color{15, 23, 45});
    rect(renderer, 24, 20, 190, 18, {40, 48, 72});
    rect(renderer, 24, 20, std::max(0, sim.livesRemaining()) * 190 / 20, 18, {68, 211, 150});
    rect(renderer, 238, 20, 190, 18, {40, 48, 72});
    rect(renderer, 238, 20, std::min(190, sim.currencyAmount() * 3), 18, {242, 188, 74});
    rect(renderer, 452, 20, 190, 18, {40, 48, 72});
    rect(renderer, 452, 20, static_cast<int>(190.0f * sim.ultimateRatio()), 18, {168, 105, 255});
    rect(renderer, 1000, 16, 240, 28, {25, 38, 64}, false);
    rect(renderer, 1010, 24, std::min(220, sim.waveNumber() * 20), 12, {83, 180, 255});
    // Color-coded loadout chips are legible without a font dependency.
    rect(renderer, 24, 48, 22, 10, {64, 210, 150});
    rect(renderer, 52, 48, 22, 10, {255, 130, 72});
    rect(renderer, 80, 48, 22, 10, {168, 105, 255});
    rect(renderer, 108, 48, 22, 10, {89, 204, 255});
    rect(renderer, 136, 48, 22, 10, {255, 232, 108});
    drawText(renderer, 24, 8, "LIVES", 2, {190, 210, 230});
    drawText(renderer, 238, 8, "CREDITS", 2, {190, 210, 230});
    drawText(renderer, 452, 8, "ULTIMATE", 2, {190, 210, 230});
    drawText(renderer, 178, 24, std::to_string(sim.livesRemaining()), 1, {240, 250, 255});
    drawText(renderer, 390, 24, std::to_string(sim.currencyAmount()), 1, {255, 245, 205});
    drawText(renderer, 605, 24, std::to_string(static_cast<int>(sim.ultimateRatio() * 100.0f)) + "%", 1, {240, 225, 255});
    drawText(renderer, 1000, 50, "WAVE", 2, {190, 210, 230});
    drawText(renderer, 1070, 50, std::to_string(sim.waveNumber()) + "  E" + std::to_string(sim.enemiesRemaining()), 1, {205, 225, 245});
    drawText(renderer, 650, 14, ta::ultimateName(sim.ultimate()), 2, {205, 180, 255});
    drawText(renderer, 650, 40, ta::weaponName(sim.weapon()), 2, {150, 215, 255});
}

void drawWorld(SDL_Renderer* renderer, const GameSim& sim) {
    Color projectileColor{255, 224, 115};
    switch (sim.weapon()) {
        case ta::Weapon::RapidFire: projectileColor = {98, 224, 170}; break;
        case ta::Weapon::ExplosiveCannon: projectileColor = {255, 128, 72}; break;
        case ta::Weapon::ArcaneBeam: projectileColor = {180, 120, 255}; break;
        case ta::Weapon::FrostBlaster: projectileColor = {92, 210, 255}; break;
        case ta::Weapon::SniperRailgun: projectileColor = {255, 232, 108}; break;
    }
    for (const ta::Projectile& projectile : sim.projectiles()) {
        const int x = static_cast<int>(projectile.pos.x);
        const int y = static_cast<int>(projectile.pos.y);
        const float speed = std::sqrt(projectile.velocity.x * projectile.velocity.x + projectile.velocity.y * projectile.velocity.y);
        if (speed > 1.0f) {
            const float tailScale = std::min(0.05f, 26.0f / speed);
            line(renderer, x - static_cast<int>(projectile.velocity.x * tailScale), y - static_cast<int>(projectile.velocity.y * tailScale), x, y, projectileColor, projectile.explosive ? 4 : 2);
        }
        circle(renderer, x, y, static_cast<int>(projectile.radius) + (projectile.explosive ? 2 : 0), projectileColor);
        if (projectile.pierces > 0 || projectile.bounces > 0) ring(renderer, x, y, static_cast<int>(projectile.radius) + 4, {235, 245, 255}, 1);
    }
    for (const ta::Enemy& enemy : sim.enemies()) {
        Color body = {236, 85, 95};
        switch (enemy.type) {
            case ta::EnemyType::Runner: body = {255, 170, 72}; break;
            case ta::EnemyType::Tank: body = {173, 91, 210}; break;
            case ta::EnemyType::Shielded: body = {120, 140, 165}; break;
            case ta::EnemyType::Swarmling: body = {255, 224, 115}; break;
            case ta::EnemyType::Teleporter: body = {72, 220, 200}; break;
            case ta::EnemyType::Boss: body = enemy.phase > 1 ? Color{255, 70, 130} : Color{196, 76, 146}; break;
            case ta::EnemyType::Grunt: break;
        }
        const int x = static_cast<int>(enemy.pos.x);
        const int y = static_cast<int>(enemy.pos.y);
        const int radius = static_cast<int>(enemy.radius);
        if (enemy.slow > 0) body = {91, 204, 255};
        switch (enemy.type) {
            case ta::EnemyType::Runner:
                filledDiamond(renderer, x, y, radius, body);
                circle(renderer, x, y, std::max(2, radius / 4), {255, 239, 195});
                break;
            case ta::EnemyType::Tank:
                rect(renderer, x - radius, y - radius, radius * 2, radius * 2, body);
                rect(renderer, x - radius, y - radius, radius * 2, radius * 2, {232, 195, 255}, false);
                line(renderer, x - radius / 2, y, x + radius / 2, y, {80, 42, 102}, 3);
                break;
            case ta::EnemyType::Shielded:
                circle(renderer, x, y, radius, body);
                ring(renderer, x, y, radius + 5, {180, 220, 255}, 2);
                ring(renderer, x, y, std::max(2, radius / 2), {210, 235, 255}, 1);
                break;
            case ta::EnemyType::Swarmling:
                circle(renderer, x, y, radius, body);
                line(renderer, x - radius, y - radius, x - radius - 4, y - radius - 6, {255, 239, 165}, 1);
                line(renderer, x + radius, y - radius, x + radius + 4, y - radius - 6, {255, 239, 165}, 1);
                break;
            case ta::EnemyType::Teleporter:
                circle(renderer, x, y, radius, body);
                ring(renderer, x, y, radius + 5, {125, 255, 235}, 2);
                ring(renderer, x, y, std::max(2, radius - 5), {25, 110, 120}, 1);
                break;
            case ta::EnemyType::Boss:
                circle(renderer, x, y, radius, body);
                ring(renderer, x, y, radius + 7, enemy.phase > 1 ? Color{255, 120, 180} : Color{245, 160, 220}, 3);
                ring(renderer, x, y, std::max(2, radius - 8), {255, 230, 250}, 1);
                break;
            case ta::EnemyType::Grunt:
                circle(renderer, x, y, radius, body);
                circle(renderer, x, y, std::max(2, radius / 3), {255, 180, 170});
                break;
        }
        if (enemy.stun > 0.0f) ring(renderer, x, y, radius + 10, {255, 238, 115}, 2);
        if (enemy.burn > 0.0f) ring(renderer, x, y, radius + 13, {255, 115, 55}, 1);
        if (enemy.poison > 0.0f) ring(renderer, x, y, radius + 16, {105, 235, 125}, 1);
        rect(renderer, static_cast<int>(enemy.pos.x - enemy.radius), static_cast<int>(enemy.pos.y - enemy.radius - 9), static_cast<int>(enemy.radius * 2), 4, {38, 42, 55});
        rect(renderer, static_cast<int>(enemy.pos.x - enemy.radius), static_cast<int>(enemy.pos.y - enemy.radius - 9), static_cast<int>(enemy.radius * 2 * std::max(0.0f, enemy.hp / enemy.maxHp)), 4, {255, 114, 101});
    }
    // Tower: layered silhouette, range ring, and core.
    SDL_SetRenderDrawColor(renderer, 83, 180, 255, 90);
    for (int r = 85; r < 90; ++r) {
        for (int a = 0; a < 360; a += 3) {
            const float rad = static_cast<float>(a) * 3.14159265f / 180.0f;
            SDL_RenderDrawPoint(renderer, static_cast<int>(1110 + std::cos(rad) * r), static_cast<int>(360 + std::sin(rad) * r));
        }
    }
    Color towerOuter{31, 66, 100};
    Color towerCore{72, 178, 224};
    switch (sim.skin()) {
        case ta::TowerSkin::Azure: break;
        case ta::TowerSkin::Ember: towerOuter = {105, 43, 38}; towerCore = {240, 105, 54}; break;
        case ta::TowerSkin::Nebula: towerOuter = {62, 35, 110}; towerCore = {180, 105, 255}; break;
        case ta::TowerSkin::Verdant: towerOuter = {31, 91, 74}; towerCore = {75, 220, 151}; break;
        case ta::TowerSkin::Gold: towerOuter = {100, 80, 26}; towerCore = {255, 218, 86}; break;
    }
    const ta::Enemy* target = nullptr;
    for (const ta::Enemy& enemy : sim.enemies()) if (target == nullptr || enemy.pos.x > target->pos.x) target = &enemy;
    if (target != nullptr) {
        const float dx = target->pos.x - 1110.0f;
        const float dy = target->pos.y - 360.0f;
        const float length = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
        line(renderer, 1110, 360, 1110 + static_cast<int>(dx / length * 54.0f), 360 + static_cast<int>(dy / length * 54.0f), towerCore, 7);
        if (sim.weapon() == ta::Weapon::ArcaneBeam) line(renderer, 1110, 360, static_cast<int>(target->pos.x), static_cast<int>(target->pos.y), {184, 120, 255, 150}, 3);
    }
    circle(renderer, 1110, 360, 42, towerOuter);
    circle(renderer, 1110, 360, 27, towerCore);
    circle(renderer, 1110, 360, 12, {225, 246, 255});
}

void drawLoadout(SDL_Renderer* renderer, const GameSim& sim, const ta::ProfileData& profile, const ta::DailyChallenge& daily) {
    rect(renderer, 0, 0, GameSim::Width, GameSim::Height, {7, 12, 27});
    rect(renderer, 140, 90, 1000, 540, {16, 30, 55});
    rect(renderer, 170, 120, 940, 3, {83, 180, 255});
    drawText(renderer, 190, 132, "TOWER ASCEND", 3, {220, 235, 250});
    drawText(renderer, 190, 165, "CHOOSE YOUR LOADOUT", 2, {135, 175, 210});
    drawText(renderer, 190, 328, "ARENA", 2, {180, 205, 230});
    const std::array<const char*, 3> arenaLabels{{"MOONBASE", "EMBER", "NEON"}};
    for (int i = 0; i < 3; ++i) {
        const int x = 190 + i * 250;
        const bool selected = static_cast<int>(sim.arena()) == i;
        rect(renderer, x, 350, 220, 36, selected ? Color{55, 100, 130} : Color{35, 55, 82}, false);
        drawText(renderer, x + 76, 362, arenaLabels[static_cast<std::size_t>(i)], 1, {220, 230, 240});
    }
    drawText(renderer, 900, 96, "SHARDS " + std::to_string(profile.cosmeticShards), 1, {255, 220, 120});
    drawText(renderer, 900, 108, "BEST " + std::to_string(profile.bestScore), 1, {180, 210, 235});
    drawText(renderer, 190, 378, std::string("VOL ") + std::to_string(profile.masterVolume) + "  C HC  F FLASH", 1, {155, 180, 210});
    drawText(renderer, 650, 578, sim.autoUltimate() ? "AUTO ULTIMATE" : "MANUAL ULTIMATE", 1, sim.autoUltimate() ? Color{120, 235, 175} : Color{175, 195, 220});
    const std::array<Color, 5> colors{{{64, 210, 150}, {255, 130, 72}, {168, 105, 255}, {89, 204, 255}, {255, 232, 108}}};
    for (int i = 0; i < 5; ++i) {
        const int x = 190 + i * 180;
        rect(renderer, x, 190, 140, 150, i == static_cast<int>(sim.weapon()) ? colors[i] : Color{35, 55, 82}, false);
        circle(renderer, x + 70, 250, 34, colors[i]);
        rect(renderer, x + 26, 300, 88, 8, colors[i]);
    }
    const std::array<const char*, 5> weaponLabels{{"RAPID FIRE", "CANNON", "ARCANE BEAM", "FROST", "RAILGUN"}};
    for (int i = 0; i < 5; ++i) drawText(renderer, 190 + i * 180 + 10, 313, weaponLabels[static_cast<std::size_t>(i)], 1, {220, 230, 240});
    for (int i = 0; i < 4; ++i) {
        const int x = 300 + i * 180;
        const bool enabled = sim.hasSkull(static_cast<ta::Skull>(i + 1));
        rect(renderer, x, 420, 140, 72, enabled ? Color{196, 76, 146} : Color{35, 55, 82}, false);
        circle(renderer, x + 70, 456, 18, enabled ? Color{255, 116, 150} : Color{88, 102, 132});
    }
    const std::array<const char*, 4> skullLabels{{"SWARM", "GLASS", "HASTE", "GREED"}};
    for (int i = 0; i < 4; ++i) drawText(renderer, 300 + i * 180 + 38, 445, skullLabels[static_cast<std::size_t>(i)], 1, {220, 230, 240});
    drawText(renderer, 190, 398, "TOGGLE SKULLS  Q-T", 1, {155, 180, 210});
    drawText(renderer, 900, 398, "SCORE X" + std::to_string(sim.skullScoreMultiplier()).substr(0, 4), 1, {255, 205, 120});
    const std::array<Color, 5> ultimateColors{{{255, 110, 70}, {255, 210, 80}, {120, 210, 255}, {170, 120, 255}, {90, 240, 170}}};
    const std::array<const char*, 5> ultimateLabels{{"METEOR", "BULLET", "ZERO", "GRAVITY", "SURGE"}};
    for (int i = 0; i < 5; ++i) {
        const int x = 160 + i * 220;
        rect(renderer, x, 520, 190, 48, i == static_cast<int>(sim.ultimate()) ? ultimateColors[static_cast<std::size_t>(i)] : Color{35, 55, 82}, false);
        drawText(renderer, x + 42, 538, ultimateLabels[static_cast<std::size_t>(i)], 1, {220, 230, 240});
    }
    rect(renderer, 460, 590, 360, 38, {68, 211, 150});
    rect(renderer, 850, 590, 240, 38, {168, 105, 255});
    drawText(renderer, 548, 604, "START RUN", 2, {7, 25, 27});
    drawText(renderer, 896, 604, "DAILY", 2, {245, 235, 255});
    drawText(renderer, 850, 640, "DAILY CHALLENGE", 1, {190, 175, 230});
    drawText(renderer, 850, 650, std::string(ta::weaponName(daily.recommendedWeapon)) + " " + ta::arenaName(daily.arena), 1, {205, 215, 240});
    drawText(renderer, 850, 660, std::string("SKULL ") + ta::skullName(daily.skull) + " +" + std::to_string(daily.bonusShards) + " SHARDS", 1, {255, 215, 135});
}

void drawSkinStrip(SDL_Renderer* renderer, const ta::ProfileData& profile, ta::TowerSkin equipped) {
    drawText(renderer, 900, 132, "SKINS", 2, {180, 205, 230});
    const std::array<Color, 5> colors{{{72, 178, 224}, {240, 105, 54}, {180, 105, 255}, {75, 220, 151}, {255, 218, 86}}};
    for (int i = 0; i < 5; ++i) {
        const ta::TowerSkin skin = static_cast<ta::TowerSkin>(i);
        const int x = 900 + i * 68;
        const bool unlocked = ta::isSkinUnlocked(profile, skin);
        rect(renderer, x, 150, 58, 28, i == static_cast<int>(equipped) ? colors[static_cast<std::size_t>(i)] : Color{35, 55, 82}, false);
        circle(renderer, x + 14, 164, 8, unlocked ? colors[static_cast<std::size_t>(i)] : Color{63, 70, 82});
        drawText(renderer, x + 25, 159, unlocked ? std::to_string(i + 1) : "X", 1, {220, 230, 240});
    }
    drawText(renderer, 900, 182, "6-0 SELECT  K UNLOCK", 1, {155, 180, 210});
    drawText(renderer, 190, 182, "F1-F3 ARENA", 1, {155, 180, 210});
}

void drawUpgradeOverlay(SDL_Renderer* renderer, const GameSim& sim) {
    rect(renderer, 170, 150, 940, 400, {10, 18, 35, 235});
    drawText(renderer, 430, 180, "CHOOSE UPGRADE", 3, {235, 245, 255});
    for (int i = 0; i < 3; ++i) {
        const int x = 220 + i * 290;
        rect(renderer, x, 245, 220, 160, {35, 64, 92}, false);
        circle(renderer, x + 110, 315, 38, i == 0 ? Color{68, 211, 150} : (i == 1 ? Color{255, 188, 74} : Color{168, 105, 255}));
        rect(renderer, x + 78, 370, 64, 8, {220, 230, 240});
        rect(renderer, x + 94, 215, 32, 18, {83, 180, 255});
        if (i < static_cast<int>(sim.pendingChoices().size())) {
            const ta::Upgrade upgrade = sim.pendingChoices()[static_cast<std::size_t>(i)];
            drawText(renderer, x + 20, 390, ta::upgradeName(upgrade), 1, {220, 230, 240});
            drawText(renderer, x + 20, 410, ta::upgradeDescription(upgrade), 1, {155, 190, 215});
        }
    }
}

void drawPauseOverlay(SDL_Renderer* renderer) {
    rect(renderer, 330, 270, 620, 150, {8, 13, 28, 235});
    rect(renderer, 540, 310, 36, 70, {220, 230, 240});
    rect(renderer, 630, 310, 36, 70, {220, 230, 240});
}

void drawResultsOverlay(SDL_Renderer* renderer, const GameSim& sim) {
    if (!sim.isGameOver() && !sim.isVictory()) return;
    const ta::RunSummary summary = sim.runSummary();
    rect(renderer, 260, 185, 760, 350, {7, 12, 27, 242});
    drawText(renderer, 440, 215, summary.victory ? "TOWER ASCENDED" : "RUN FAILED", 3, summary.victory ? Color{100, 240, 180} : Color{255, 110, 120});
    drawText(renderer, 450, 265, ta::arenaName(summary.arena), 2, {170, 205, 235});
    drawText(renderer, 390, 315, "SCORE", 2, {190, 215, 240});
    drawText(renderer, 720, 315, std::to_string(summary.score), 2, {245, 225, 120});
    drawText(renderer, 390, 455, "SKULL MULT", 2, {190, 215, 240});
    drawText(renderer, 720, 455, "X" + std::to_string(summary.scoreMultiplier).substr(0, 4), 2, {245, 225, 120});
    drawText(renderer, 390, 355, "WAVE", 2, {190, 215, 240});
    drawText(renderer, 720, 355, std::to_string(summary.wave), 2, {245, 225, 120});
    drawText(renderer, 390, 395, "KILLS", 2, {190, 215, 240});
    drawText(renderer, 720, 395, std::to_string(summary.kills), 2, {245, 225, 120});
    drawText(renderer, 390, 435, "LEAKS", 2, {190, 215, 240});
    drawText(renderer, 720, 435, std::to_string(summary.leaks), 2, {245, 225, 120});
    drawText(renderer, 410, 485, "R TO RESTART", 2, {165, 205, 230});
}

} // namespace

int main(int argc, char** argv) {
    bool headless = false;
    bool renderSmoke = false;
    std::string headlessReplayPath;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--headless" || std::string(argv[i]) == "--test") headless = true;
        if (std::string(argv[i]) == "--render-smoke") renderSmoke = true;
        if (std::string(argv[i]) == "--record-replay" && i + 1 < argc) headlessReplayPath = argv[++i];
    }
    if (headless) {
        GameSim sim(0x7A2026u);
        ta::ContentConfig authoredContent;
        const bool authoredLoaded = ta::loadContentConfig(ta::defaultContentDirectory(), authoredContent);
        if (authoredLoaded) sim.setContentConfig(authoredContent);
        ta::ReplayData replay{sim.initialSeed(), sim.weapon(), sim.skull(), sim.skullMask(), sim.ultimate(), sim.autoUltimate(), sim.arena(), {}};
        replay.contentHash = authoredLoaded ? ta::contentFingerprint(authoredContent) : 0;
        for (int i = 0; i < 100000 && !sim.isGameOver() && !sim.isVictory(); ++i) {
            if (sim.upgradePending()) {
                sim.chooseUpgrade(0);
                replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Upgrade, 0});
            }
            if (i % 240 == 0) {
                const int previous = sim.stats().ultimates;
                sim.activateUltimate();
                if (sim.stats().ultimates != previous) replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Ultimate, 0});
            }
            sim.tick();
        }
        if (!headlessReplayPath.empty()) {
            std::string replayError;
            if (!replay.save(headlessReplayPath, &replayError)) { std::cerr << "headless replay save failed: " << replayError << '\n'; return 1; }
        }
        std::cout << "Tower Ascend headless run: wave=" << sim.waveNumber() << " score=" << sim.stats().score
                  << " kills=" << sim.stats().kills << " state=" << sim.statusText() << '\n';
        return sim.isVictory() ? 0 : 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    AudioSynth audio;
    const bool audioEnabled = audio.init();
    SDL_Window* window = SDL_CreateWindow("Tower Ascend", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, GameSim::Width, GameSim::Height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (window && !renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!window || !renderer) {
        std::fprintf(stderr, "SDL renderer failed: %s\n", SDL_GetError());
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    GameSim sim(0x7A2026u);
    const ta::DailyChallenge daily = ta::currentDailyChallenge();
    ta::ContentConfig authoredContent;
    std::string contentError;
    std::uint32_t authoredContentHash = 0;
    if (ta::loadContentConfig(ta::defaultContentDirectory(), authoredContent, &contentError)) {
        sim.setContentConfig(authoredContent);
        authoredContentHash = ta::contentFingerprint(authoredContent);
    }
    ta::ProfileData profile;
    ta::loadProfile("tower_ascend.profile", profile);
    audio.setVolume(static_cast<float>(profile.masterVolume) / 100.0f);
    sim.setSkin(static_cast<ta::TowerSkin>(profile.equippedSkin));
    const auto makeReplay = [&]() {
        ta::ReplayData result{sim.initialSeed(), sim.weapon(), sim.skull(), sim.skullMask(), sim.ultimate(), sim.autoUltimate(), sim.arena(), {}};
        result.contentHash = authoredContentHash;
        return result;
    };
    ta::ReplayData replay = makeReplay();
    bool running = true;
    bool started = false;
    bool dailyMode = false;
    bool resultSaved = false;
    bool paused = false;
    bool reducedFlashes = profile.reducedFlashes;
    bool highContrast = profile.highContrast;
    int previousKills = 0;
    int previousShots = 0;
    int previousUltimates = 0;
    int previousWave = 1;
    bool previousUpgradePending = false;
    bool previousTerminal = false;
    int renderedFrames = 0;
    int skinPreview = static_cast<int>(profile.equippedSkin);
    std::uint64_t accumulator = 0;
    std::uint64_t last = SDL_GetPerformanceCounter();
    while (running) {
        const std::uint64_t now = SDL_GetPerformanceCounter();
        const std::uint64_t elapsed = now - last;
        last = now;
        accumulator += elapsed * GameSim::TickRate;
        const std::uint64_t frequency = SDL_GetPerformanceFrequency();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                const SDL_Keycode key = event.key.keysym.sym;
                    if (key == SDLK_ESCAPE) running = false;
                    if (key == SDLK_p && started) paused = !paused;
                    if (key == SDLK_f) { reducedFlashes = !reducedFlashes; profile.reducedFlashes = reducedFlashes; ta::saveProfile("tower_ascend.profile", profile); }
                    if (key == SDLK_c) { highContrast = !highContrast; profile.highContrast = highContrast; ta::saveProfile("tower_ascend.profile", profile); }
                    if (key == SDLK_LEFTBRACKET) { profile.masterVolume = static_cast<std::uint8_t>(profile.masterVolume >= 10 ? profile.masterVolume - 10 : 0); audio.setVolume(static_cast<float>(profile.masterVolume) / 100.0f); ta::saveProfile("tower_ascend.profile", profile); }
                    if (key == SDLK_RIGHTBRACKET) { profile.masterVolume = static_cast<std::uint8_t>(std::min(100, static_cast<int>(profile.masterVolume) + 10)); audio.setVolume(static_cast<float>(profile.masterVolume) / 100.0f); ta::saveProfile("tower_ascend.profile", profile); }
                    if (!started) {
                    if (key >= SDLK_1 && key <= SDLK_5) sim.setWeapon(static_cast<ta::Weapon>(key - SDLK_1));
                    if (key >= SDLK_q && key <= SDLK_t) sim.toggleSkull(static_cast<ta::Skull>(key - SDLK_q + 1));
                    if (key == SDLK_y || key == SDLK_u || key == SDLK_i || key == SDLK_o || key == SDLK_p) {
                        const SDL_Keycode ultimateKeys[] = {SDLK_y, SDLK_u, SDLK_i, SDLK_o, SDLK_p};
                        for (int index = 0; index < 5; ++index) if (key == ultimateKeys[index]) sim.setUltimate(static_cast<ta::Ultimate>(index));
                    }
                    if (key == SDLK_F1) sim.setArena(ta::Arena::Moonbase);
                    if (key == SDLK_F2) sim.setArena(ta::Arena::EmberCrater);
                    if (key == SDLK_F3) sim.setArena(ta::Arena::NeonRuins);
                    if (key == SDLK_a && !started) sim.setAutoUltimate(!sim.autoUltimate());
                    const SDL_Keycode skinKeys[] = {SDLK_6, SDLK_7, SDLK_8, SDLK_9, SDLK_0};
                    for (int index = 0; index < 5; ++index) {
                        if (key == skinKeys[index]) {
                            skinPreview = index;
                            const ta::TowerSkin skin = static_cast<ta::TowerSkin>(index);
                            if (ta::isSkinUnlocked(profile, skin)) { sim.setSkin(skin); profile.equippedSkin = static_cast<std::uint8_t>(index); ta::saveProfile("tower_ascend.profile", profile); }
                        }
                    }
                    if (key == SDLK_k && ta::unlockSkin(profile, static_cast<ta::TowerSkin>(skinPreview))) {
                        ta::equipSkin(profile, static_cast<ta::TowerSkin>(skinPreview));
                        sim.setSkin(static_cast<ta::TowerSkin>(skinPreview));
                        ta::saveProfile("tower_ascend.profile", profile);
                    }
                    if (key == SDLK_d) {
                        sim.setWeapon(daily.recommendedWeapon);
                        sim.setSkull(daily.skull);
                        sim.setArena(daily.arena);
                        sim.reset(daily.seed);
                        replay = makeReplay();
                        started = true;
                        previousKills = 0;
                        previousShots = 0;
                        previousUltimates = 0;
                        previousWave = sim.waveNumber();
                        previousUpgradePending = false;
                        previousTerminal = false;
                        dailyMode = true;
                        resultSaved = false;
                    }
                    if (key == SDLK_RETURN || key == SDLK_SPACE) {
                        sim.reset(0x7A2026u);
                        replay = makeReplay();
                        started = true;
                        previousKills = 0;
                        previousShots = 0;
                        previousUltimates = 0;
                        previousWave = sim.waveNumber();
                        previousUpgradePending = false;
                        previousTerminal = false;
                        dailyMode = false;
                        resultSaved = false;
                    }
                } else {
                    if ((key == SDLK_1 || key == SDLK_2 || key == SDLK_3) && sim.upgradePending()) {
                        sim.chooseUpgrade(key - SDLK_1);
                        replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Upgrade, static_cast<std::uint8_t>(key - SDLK_1)});
                    }
                    if (key == SDLK_u || key == SDLK_SPACE) {
                        const int previous = sim.stats().ultimates;
                        sim.activateUltimate();
                        if (sim.stats().ultimates != previous) replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Ultimate, 0});
                    }
                    if (key == SDLK_r) {
                        sim.reset(0x7A2026u);
                        replay = makeReplay();
                        started = true;
                        previousKills = 0;
                        previousShots = 0;
                        previousUltimates = 0;
                        previousWave = sim.waveNumber();
                        previousUpgradePending = false;
                        previousTerminal = false;
                        dailyMode = false;
                        resultSaved = false;
                    }
                }
            }
            if ((event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) || event.type == SDL_FINGERDOWN) {
                int windowWidth = GameSim::Width, windowHeight = GameSim::Height;
                SDL_GetWindowSize(window, &windowWidth, &windowHeight);
                const int pointerX = event.type == SDL_FINGERDOWN ? static_cast<int>(event.tfinger.x * windowWidth) : event.button.x;
                const int pointerY = event.type == SDL_FINGERDOWN ? static_cast<int>(event.tfinger.y * windowHeight) : event.button.y;
                const int x = pointerX * GameSim::Width / std::max(1, windowWidth);
                const int y = pointerY * GameSim::Height / std::max(1, windowHeight);
                if (!started) {
                    for (int i = 0; i < 5; ++i) if (x >= 190 + i * 180 && x < 330 + i * 180 && y >= 190 && y < 340) sim.setWeapon(static_cast<ta::Weapon>(i));
                    for (int i = 0; i < 3; ++i) if (x >= 190 + i * 250 && x < 410 + i * 250 && y >= 350 && y < 386) sim.setArena(static_cast<ta::Arena>(i));
                    for (int i = 0; i < 4; ++i) if (x >= 300 + i * 180 && x < 440 + i * 180 && y >= 420 && y < 492) sim.toggleSkull(static_cast<ta::Skull>(i + 1));
                    for (int i = 0; i < 5; ++i) if (x >= 900 + i * 68 && x < 958 + i * 68 && y >= 150 && y < 178) {
                        skinPreview = i;
                        const ta::TowerSkin skin = static_cast<ta::TowerSkin>(i);
                        if (!ta::isSkinUnlocked(profile, skin)) ta::unlockSkin(profile, skin);
                        if (ta::isSkinUnlocked(profile, skin)) { ta::equipSkin(profile, skin); sim.setSkin(skin); ta::saveProfile("tower_ascend.profile", profile); }
                    }
                    for (int i = 0; i < 5; ++i) if (x >= 160 + i * 220 && x < 350 + i * 220 && y >= 520 && y < 568) sim.setUltimate(static_cast<ta::Ultimate>(i));
                    if (x >= 460 && x < 820 && y >= 570 && y < 588) sim.setAutoUltimate(!sim.autoUltimate());
                    if (x >= 460 && x < 820 && y >= 590 && y < 628) {
                        sim.reset(0x7A2026u); replay = makeReplay(); started = true; dailyMode = false; resultSaved = false; previousKills = 0; previousShots = 0; previousUltimates = 0; previousWave = sim.waveNumber(); previousUpgradePending = false; previousTerminal = false;
                    }
                    if (x >= 850 && x < 1090 && y >= 590 && y < 628) {
                        sim.setWeapon(daily.recommendedWeapon); sim.setSkull(daily.skull); sim.setArena(daily.arena); sim.setAutoUltimate(false); sim.reset(daily.seed); replay = makeReplay(); started = true; dailyMode = true; resultSaved = false; previousKills = 0; previousShots = 0; previousUltimates = 0; previousWave = sim.waveNumber(); previousUpgradePending = false; previousTerminal = false;
                    }
                } else if (sim.upgradePending()) {
                    for (int i = 0; i < 3; ++i) if (x >= 220 + i * 290 && x < 440 + i * 290 && y >= 245 && y < 405) {
                        const int previous = sim.stats().upgrades;
                        sim.chooseUpgrade(i);
                        if (sim.stats().upgrades != previous) replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Upgrade, static_cast<std::uint8_t>(i)});
                    }
                } else if (x >= 1030 && x < 1220 && y >= 80 && y < 160) {
                    const int previous = sim.stats().ultimates; sim.activateUltimate();
                    if (sim.stats().ultimates != previous) replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Ultimate, 0});
                }
            }
        }
        while (started && !paused && accumulator >= frequency) { sim.tick(); accumulator -= frequency; }
        const auto playCue = [&](float frequencyHz, int milliseconds, float volume) { if (audioEnabled) audio.tone(frequencyHz, milliseconds, reducedFlashes ? volume * 0.35f : volume); };
        if (started && sim.stats().shotsFired > previousShots) {
            const float shotTone = sim.weapon() == ta::Weapon::ExplosiveCannon ? 125.0f : (sim.weapon() == ta::Weapon::ArcaneBeam ? 510.0f : (sim.weapon() == ta::Weapon::FrostBlaster ? 760.0f : (sim.weapon() == ta::Weapon::SniperRailgun ? 250.0f : 380.0f)));
            playCue(shotTone, sim.weapon() == ta::Weapon::SniperRailgun ? 90 : 28, 0.07f);
            previousShots = sim.stats().shotsFired;
        }
        if (started && sim.stats().kills > previousKills) { playCue(320.0f + (sim.stats().kills % 4) * 80.0f, 45, 0.18f); previousKills = sim.stats().kills; }
        if (started && sim.stats().ultimates > previousUltimates) { playCue(900.0f, 180, 0.20f); previousUltimates = sim.stats().ultimates; }
        if (started && sim.waveNumber() != previousWave) { playCue(190.0f, 180, 0.22f); playCue(380.0f, 140, 0.18f); previousWave = sim.waveNumber(); }
        if (started && sim.upgradePending() && !previousUpgradePending) playCue(620.0f, 130, 0.16f);
        previousUpgradePending = started && sim.upgradePending();
        if (started && audioEnabled && (sim.isGameOver() || sim.isVictory()) && !previousTerminal) {
            playCue(sim.isVictory() ? 760.0f : 120.0f, 260, 0.24f);
            previousTerminal = true;
        }
        if (started && !resultSaved && (sim.isGameOver() || sim.isVictory())) {
            profile.bestScore = std::max(profile.bestScore, sim.stats().score);
            profile.bestWave = std::max(profile.bestWave, sim.waveNumber());
            ++profile.runsCompleted;
            profile.totalKills += static_cast<std::uint32_t>(sim.stats().kills);
            ta::awardRunCosmetics(profile, sim.stats(), dailyMode, daily.bonusShards);
            ta::saveProfile("tower_ascend.profile", profile);
            replay.save("tower_ascend.last.replay");
            resultSaved = true;
        }
        if (started) {
            char title[200];
            std::snprintf(title, sizeof(title), "Tower Ascend%s%s%s%s%s | %s | %s | Wave %d | Score %d | %s", dailyMode ? " DAILY" : "", paused ? " PAUSED" : "", highContrast ? " HC" : "", reducedFlashes ? " RF" : "", sim.autoUltimate() ? " AUTO" : "", ta::arenaName(sim.arena()), ta::weaponName(sim.weapon()), sim.waveNumber(), sim.stats().score, sim.statusText().c_str());
            SDL_SetWindowTitle(window, title);
        }
        SDL_RenderSetLogicalSize(renderer, GameSim::Width, GameSim::Height);
        if (!started) { drawLoadout(renderer, sim, profile, daily); drawSkinStrip(renderer, profile, sim.skin()); }
        else { drawArena(renderer, highContrast, sim.arena(), authoredContent); drawWorld(renderer, sim); drawHud(renderer, sim, highContrast); if (sim.upgradePending()) drawUpgradeOverlay(renderer, sim); if (paused) drawPauseOverlay(renderer); drawResultsOverlay(renderer, sim); }
        SDL_RenderPresent(renderer);
        if (renderSmoke && ++renderedFrames >= 3) running = false;
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    audio.shutdown();
    SDL_Quit();
    return 0;
}
