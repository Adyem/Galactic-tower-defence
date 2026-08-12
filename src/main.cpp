#include "game.hpp"
#include "input.hpp"
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

ta::InputAction indexedAction(ta::InputAction first, int offset) {
    return static_cast<ta::InputAction>(static_cast<std::size_t>(first) + static_cast<std::size_t>(offset));
}

struct Color { Uint8 r, g, b, a = 255; };
std::uint8_t activeColorPalette = 0;
float activeUiScale = 1.0f;

Color accessibleColor(Color color) {
    if (activeColorPalette == 1) {
        // Separate hue families make danger/reward/status signals readable
        // without relying on red versus green alone.
        if (color.r > color.g + 24 && color.r > color.b + 24) return {255, 170, 58, color.a};
        if (color.g > color.r + 24 && color.g > color.b + 24) return {74, 188, 255, color.a};
        if (color.b > color.r + 24 && color.b > color.g + 24) return {185, 126, 255, color.a};
    } else if (activeColorPalette == 2) {
        const int luminance = (static_cast<int>(color.r) * 3 + static_cast<int>(color.g) * 6 + static_cast<int>(color.b)) / 10;
        const Uint8 level = luminance >= 128 ? 255 : 32;
        return {level, level, level, color.a};
    }
    return color;
}

// Neo-futurist visual language: near-black blue surfaces, cool cyan system
// chrome, magenta danger, amber economy, and mint success. Every gameplay
// signal gets both a hue and a geometric treatment so it remains legible with
// reduced colour perception or a monochrome palette.
namespace neo {
constexpr Color Void{4, 7, 18};
constexpr Color Deep{7, 13, 30};
constexpr Color Panel{12, 22, 46};
constexpr Color PanelRaised{17, 31, 62};
constexpr Color Grid{16, 38, 70};
constexpr Color GridBright{29, 67, 104};
constexpr Color Cyan{70, 230, 255};
constexpr Color Blue{113, 151, 255};
constexpr Color Magenta{255, 76, 183};
constexpr Color Amber{255, 202, 96};
constexpr Color Mint{94, 245, 180};
constexpr Color Red{255, 82, 111};
constexpr Color Ice{126, 220, 255};
constexpr Color Violet{190, 121, 255};
constexpr Color Text{220, 235, 255};
constexpr Color Muted{123, 157, 195};
constexpr Color DarkText{67, 100, 139};
}

std::string userDataFile(const char* filename) {
    char* directory = SDL_GetPrefPath("TowerAscend", "TowerAscend");
    if (directory == nullptr) return filename;
    const std::string result = std::string(directory) + filename;
    SDL_free(directory);
    return result;
}

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
    const int textScale = std::max(1, static_cast<int>(std::lround(static_cast<float>(scale) * activeUiScale)));
    int cursor = x;
    color = accessibleColor(color);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (char raw : text) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
        if (c == ' ') { cursor += 6 * textScale; continue; }
        const std::size_t index = alphabet.find(c);
        if (index == std::string::npos) { cursor += 6 * textScale; continue; }
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((font[index][static_cast<std::size_t>(row)] & (1u << (4 - column))) != 0) {
                    SDL_Rect pixel{cursor + column * textScale, y + row * textScale, textScale, textScale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
        cursor += 6 * textScale;
    }
}

class AudioSynth {
public:
    enum class Bus { Sfx, Ui };

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
    void setBusVolumes(float sfx, float ui) {
        sfxGain = std::clamp(sfx, 0.0f, 1.0f);
        uiGain = std::clamp(ui, 0.0f, 1.0f);
    }

    void shutdown() {
        if (device != 0) {
            SDL_ClearQueuedAudio(device);
            SDL_CloseAudioDevice(device);
            device = 0;
        }
    }

    void tone(float frequency, int milliseconds, float volume = 0.18f, Bus bus = Bus::Sfx) {
        if (device == 0 || spec.freq <= 0) return;
        const int count = std::max(1, spec.freq * milliseconds / 1000);
        std::vector<float> samples(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(spec.freq);
            const float envelope = std::min(1.0f, static_cast<float>(i) / (spec.freq * 0.008f)) * std::min(1.0f, static_cast<float>(count - i) / (spec.freq * 0.035f));
            const float busGain = bus == Bus::Ui ? uiGain : sfxGain;
            samples[static_cast<std::size_t>(i)] = std::sin(2.0f * 3.14159265f * frequency * t) * volume * gain * busGain * envelope;
        }
        SDL_QueueAudio(device, samples.data(), static_cast<Uint32>(samples.size() * sizeof(float)));
    }

private:
    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec spec{};
    float gain = 1.0f;
    float sfxGain = 1.0f;
    float uiGain = 1.0f;
};

class HapticFeedback {
public:
    bool init() {
        if (SDL_NumHaptics() <= 0) return false;
        haptic = SDL_HapticOpen(0);
        if (haptic == nullptr || SDL_HapticRumbleInit(haptic) != 0) {
            shutdown();
            return false;
        }
        return true;
    }

    ~HapticFeedback() { shutdown(); }

    void pulse(bool enabled, float strength, Uint32 milliseconds) {
        if (!enabled || haptic == nullptr) return;
        SDL_HapticRumblePlay(haptic, std::clamp(strength, 0.0f, 1.0f), milliseconds);
    }

    void shutdown() {
        if (haptic != nullptr) {
            SDL_HapticClose(haptic);
            haptic = nullptr;
        }
    }

private:
    SDL_Haptic* haptic = nullptr;
};

void rect(SDL_Renderer* renderer, int x, int y, int w, int h, Color color, bool filled = true) {
    SDL_Rect r{x, y, w, h};
    color = accessibleColor(color);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    if (filled) SDL_RenderFillRect(renderer, &r); else SDL_RenderDrawRect(renderer, &r);
}

void circle(SDL_Renderer* renderer, int cx, int cy, int radius, Color color) {
    color = accessibleColor(color);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int y = -radius; y <= radius; ++y) {
        const int span = static_cast<int>(std::sqrt(std::max(0, radius * radius - y * y)));
        SDL_RenderDrawLine(renderer, cx - span, cy + y, cx + span, cy + y);
    }
}

void line(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, Color color, int width = 1) {
    color = accessibleColor(color);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i < width; ++i) SDL_RenderDrawLine(renderer, x1, y1 + i, x2, y2 + i);
}

void chamferFill(SDL_Renderer* renderer, int x, int y, int width, int height, Color color, int cut = 10);
void chamferOutline(SDL_Renderer* renderer, int x, int y, int width, int height, Color color, int cut = 10, int thickness = 1);
void neoPanel(SDL_Renderer* renderer, int x, int y, int width, int height, Color accent, bool selected = false, int cut = 10);
void neonDivider(SDL_Renderer* renderer, int x, int y, int width, Color accent);
void segmentedBar(SDL_Renderer* renderer, int x, int y, int width, int height, float ratio, Color accent, int segments = 10);

void ring(SDL_Renderer* renderer, int cx, int cy, int radius, Color color, int thickness = 1) {
    color = accessibleColor(color);
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
    color = accessibleColor(color);
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
    const Color arenaAccent = arena == ta::Arena::EmberCrater ? neo::Amber : (arena == ta::Arena::NeonRuins ? neo::Violet : neo::Cyan);
    rect(renderer, 0, 0, GameSim::Width, GameSim::Height, neo::Void);
    rect(renderer, 0, 74, GameSim::Width, 2, {arenaAccent.r, arenaAccent.g, arenaAccent.b, 46});
    rect(renderer, 0, 670, GameSim::Width, 2, {arenaAccent.r, arenaAccent.g, arenaAccent.b, 46});
    const Color grid = highContrast ? neo::GridBright : neo::Grid;
    for (int x = 0; x < GameSim::Width; x += 64) line(renderer, x, 76, x, 670, grid);
    for (int y = 76; y < 671; y += 64) line(renderer, 0, y, GameSim::Width, y, grid);
    for (int x = -240; x < GameSim::Width + 240; x += 96) line(renderer, x, 76, x + 180, 670, {arenaAccent.r, arenaAccent.g, arenaAccent.b, 12}, 1);
    chamferOutline(renderer, 24, 88, 1232, 560, {arenaAccent.r, arenaAccent.g, arenaAccent.b, 72}, 16, 1);
    for (int node = 0; node < 7; ++node) {
        const int x = 72 + node * 178;
        const int y = 108 + (node % 2) * 500;
        circle(renderer, x, y, 3, arenaAccent);
        line(renderer, x - 14, y, x - 5, y, arenaAccent, 1);
        line(renderer, x + 5, y, x + 14, y, arenaAccent, 1);
    }
    // The lane is a recessed mag-rail: broad dark channel, bright edge, and
    // evenly spaced route markers that communicate direction at a glance.
    int previousX = 80;
    int previousY = static_cast<int>(renderPathY(80.0f, 0, arena, content));
    for (int x = 80; x <= 1180; x += 8) {
        const int y = static_cast<int>(renderPathY(static_cast<float>(x), 0, arena, content));
        circle(renderer, x, y, 40, neo::Deep);
        circle(renderer, x, y, 34, neo::PanelRaised);
        if (x > 80) line(renderer, previousX, previousY, x, y, {arenaAccent.r, arenaAccent.g, arenaAccent.b, 92}, 2);
        if ((x / 8) % 6 == 0) {
            circle(renderer, x, y, 3, arenaAccent);
            line(renderer, x - 10, y, x - 4, y, arenaAccent, 1);
        }
        previousX = x;
        previousY = y;
    }
    // Entry and tower-side route beacons.
    const int entryY = static_cast<int>(renderPathY(80.0f, 0, arena, content));
    const int exitY = static_cast<int>(renderPathY(1180.0f, 0, arena, content));
    chamferOutline(renderer, 42, entryY - 25, 62, 50, arenaAccent, 8, 2);
    chamferOutline(renderer, 1152, exitY - 25, 74, 50, neo::Red, 8, 2);
    drawText(renderer, 52, entryY - 4, "IN", 1, arenaAccent);
    drawText(renderer, 1170, exitY - 4, "OUT", 1, neo::Red);
}

void filledHexagon(SDL_Renderer* renderer, int cx, int cy, int radius, Color color) {
    color = accessibleColor(color);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const int halfHeight = std::max(1, static_cast<int>(static_cast<float>(radius) * 0.866f));
    for (int y = -halfHeight; y <= halfHeight; ++y) {
        const int distance = std::abs(y);
        const int span = distance <= halfHeight / 2 ? radius : radius - (distance - halfHeight / 2) * radius / std::max(1, halfHeight / 2);
        SDL_RenderDrawLine(renderer, cx - span, cy + y, cx + span, cy + y);
    }
}

void hexagon(SDL_Renderer* renderer, int cx, int cy, int radius, Color color, int thickness = 1) {
    const int halfHeight = std::max(1, static_cast<int>(static_cast<float>(radius) * 0.866f));
    const std::array<Vec2, 6> points{{
        {static_cast<float>(cx + radius), static_cast<float>(cy)},
        {static_cast<float>(cx + radius / 2), static_cast<float>(cy - halfHeight)},
        {static_cast<float>(cx - radius / 2), static_cast<float>(cy - halfHeight)},
        {static_cast<float>(cx - radius), static_cast<float>(cy)},
        {static_cast<float>(cx - radius / 2), static_cast<float>(cy + halfHeight)},
        {static_cast<float>(cx + radius / 2), static_cast<float>(cy + halfHeight)}}};
    for (std::size_t index = 0; index < points.size(); ++index) {
        const Vec2 a = points[index];
        const Vec2 b = points[(index + 1) % points.size()];
        line(renderer, static_cast<int>(a.x), static_cast<int>(a.y), static_cast<int>(b.x), static_cast<int>(b.y), color, thickness);
    }
}

void drawHud(SDL_Renderer* renderer, const GameSim& sim, bool highContrast = false, bool subtitles = true) {
    rect(renderer, 0, 0, GameSim::Width, 72, highContrast ? neo::Deep : neo::Void);
    neonDivider(renderer, 16, 70, 1248, neo::Cyan);
    neoPanel(renderer, 16, 8, 202, 52, neo::Mint, false, 8);
    neoPanel(renderer, 230, 8, 202, 52, neo::Amber, false, 8);
    neoPanel(renderer, 444, 8, 202, 52, neo::Violet, false, 8);
    segmentedBar(renderer, 24, 32, 154, 16, static_cast<float>(std::max(0, sim.livesRemaining())) / 20.0f, neo::Mint, 10);
    segmentedBar(renderer, 238, 32, 154, 16, std::min(1.0f, static_cast<float>(sim.currencyAmount()) / 60.0f), neo::Amber, 10);
    segmentedBar(renderer, 452, 32, 154, 16, sim.ultimateRatio(), neo::Violet, 10);
    drawText(renderer, 28, 14, "LIVES", 1, neo::Muted);
    drawText(renderer, 242, 14, "CREDITS", 1, neo::Muted);
    drawText(renderer, 456, 14, "ULTIMATE", 1, neo::Muted);
    drawText(renderer, 160, 14, std::to_string(sim.livesRemaining()), 1, neo::Text);
    drawText(renderer, 374, 14, std::to_string(sim.currencyAmount()), 1, neo::Text);
    drawText(renderer, 588, 14, std::to_string(static_cast<int>(sim.ultimateRatio() * 100.0f)) + "%", 1, neo::Text);
    neoPanel(renderer, 658, 8, 324, 52, neo::Blue, false, 8);
    drawText(renderer, 674, 14, ta::ultimateName(sim.ultimate()), 1, neo::Violet);
    drawText(renderer, 674, 38, ta::weaponName(sim.weapon()), 1, neo::Cyan);
    neoPanel(renderer, 994, 8, 250, 52, neo::Cyan, false, 8);
    drawText(renderer, 1010, 14, "WAVE", 1, neo::Muted);
    drawText(renderer, 1010, 38, std::to_string(sim.waveNumber()) + "  //  " + std::to_string(sim.enemiesRemaining()), 1, neo::Text);
    const std::array<Color, 5> chips{{neo::Mint, neo::Amber, neo::Violet, neo::Ice, neo::Red}};
    for (int index = 0; index < 5; ++index) rect(renderer, 24 + index * 28, 61, 20, 3, chips[static_cast<std::size_t>(index)]);
    std::string status = sim.statusText();
    for (const ta::Enemy& enemy : sim.enemies()) if (enemy.boss && enemy.telegraphTicks > 0) status = "BOSS ATTACK INCOMING";
    if (subtitles) drawText(renderer, 24, 690, status, 1, neo::Muted);
}

void drawWorld(SDL_Renderer* renderer, const GameSim& sim) {
    Color projectileColor = neo::Amber;
    switch (sim.weapon()) {
        case ta::Weapon::RapidFire: projectileColor = neo::Mint; break;
        case ta::Weapon::ExplosiveCannon: projectileColor = neo::Amber; break;
        case ta::Weapon::ArcaneBeam: projectileColor = neo::Violet; break;
        case ta::Weapon::FrostBlaster: projectileColor = neo::Ice; break;
        case ta::Weapon::SniperRailgun: projectileColor = neo::Red; break;
    }
    for (const ta::Projectile& projectile : sim.projectiles()) {
        const int x = static_cast<int>(projectile.pos.x);
        const int y = static_cast<int>(projectile.pos.y);
        const float speed = std::sqrt(projectile.velocity.x * projectile.velocity.x + projectile.velocity.y * projectile.velocity.y);
        if (speed > 1.0f) {
            const float tailScale = std::min(0.05f, 26.0f / speed);
            line(renderer, x - static_cast<int>(projectile.velocity.x * tailScale), y - static_cast<int>(projectile.velocity.y * tailScale), x, y, projectileColor, projectile.explosive ? 4 : 2);
        }
        if (projectile.explosive) filledHexagon(renderer, x, y, static_cast<int>(projectile.radius) + 3, projectileColor);
        else filledDiamond(renderer, x, y, static_cast<int>(projectile.radius) + 1, projectileColor);
        if (projectile.pierces > 0 || projectile.bounces > 0) hexagon(renderer, x, y, static_cast<int>(projectile.radius) + 6, neo::Text, 1);
    }
    for (const ta::Enemy& enemy : sim.enemies()) {
        Color body = neo::Red;
        switch (enemy.type) {
            case ta::EnemyType::Runner: body = neo::Amber; break;
            case ta::EnemyType::Tank: body = neo::Violet; break;
            case ta::EnemyType::Shielded: body = neo::Blue; break;
            case ta::EnemyType::Swarmling: body = neo::Amber; break;
            case ta::EnemyType::Teleporter: body = neo::Cyan; break;
            case ta::EnemyType::Boss: body = enemy.phase > 1 ? neo::Magenta : neo::Violet; break;
            case ta::EnemyType::Grunt: break;
        }
        const int x = static_cast<int>(enemy.pos.x);
        const int y = static_cast<int>(enemy.pos.y);
        const int radius = static_cast<int>(enemy.radius);
        if (enemy.slow > 0) body = neo::Ice;
        switch (enemy.type) {
            case ta::EnemyType::Runner:
                filledDiamond(renderer, x, y, radius, body);
                line(renderer, x - radius / 2, y, x + radius / 2, y, neo::Text, 2);
                break;
            case ta::EnemyType::Tank:
                filledHexagon(renderer, x, y, radius, body);
                hexagon(renderer, x, y, radius, neo::Text, 2);
                line(renderer, x - radius / 2, y, x + radius / 2, y, neo::Deep, 3);
                break;
            case ta::EnemyType::Shielded:
                filledHexagon(renderer, x, y, radius, body);
                hexagon(renderer, x, y, radius + 5, neo::Ice, 2);
                line(renderer, x - radius / 2, y, x + radius / 2, y, neo::Text, 1);
                break;
            case ta::EnemyType::Swarmling:
                filledDiamond(renderer, x, y, radius, body);
                line(renderer, x - radius, y - radius, x - radius - 4, y - radius - 6, neo::Amber, 1);
                line(renderer, x + radius, y - radius, x + radius + 4, y - radius - 6, neo::Amber, 1);
                break;
            case ta::EnemyType::Teleporter:
                circle(renderer, x, y, radius, body);
                ring(renderer, x, y, radius + 5, neo::Cyan, 2);
                hexagon(renderer, x, y, std::max(2, radius - 5), neo::Deep, 1);
                break;
            case ta::EnemyType::Boss:
                filledHexagon(renderer, x, y, radius, body);
                hexagon(renderer, x, y, radius + 7, enemy.phase > 1 ? neo::Magenta : neo::Violet, 3);
                hexagon(renderer, x, y, std::max(2, radius - 8), neo::Text, 1);
                break;
            case ta::EnemyType::Grunt:
                filledHexagon(renderer, x, y, radius, body);
                circle(renderer, x, y, std::max(2, radius / 3), neo::Text);
                break;
        }
        if (enemy.stun > 0.0f) ring(renderer, x, y, radius + 10, neo::Amber, 2);
        if (enemy.burn > 0.0f) ring(renderer, x, y, radius + 13, neo::Red, 1);
        if (enemy.poison > 0.0f) ring(renderer, x, y, radius + 16, neo::Mint, 1);
        if (enemy.boss && enemy.telegraphTicks > 0) {
            ring(renderer, x, y, radius + 18 + (enemy.telegraphTicks % 3), neo::Red, 3);
            line(renderer, x - radius - 8, y, x + radius + 8, y, neo::Amber, 2);
        }
        rect(renderer, static_cast<int>(enemy.pos.x - enemy.radius), static_cast<int>(enemy.pos.y - enemy.radius - 9), static_cast<int>(enemy.radius * 2), 4, neo::Deep);
        rect(renderer, static_cast<int>(enemy.pos.x - enemy.radius), static_cast<int>(enemy.pos.y - enemy.radius - 9), static_cast<int>(enemy.radius * 2 * std::max(0.0f, enemy.hp / enemy.maxHp)), 4, enemy.boss ? neo::Magenta : neo::Red);
    }
    // Tower: a luminous hexagonal reactor with a targeting reticle and a
    // restrained range halo; its silhouette stays distinct at phone scale.
    ring(renderer, 1110, 360, 87, {neo::Cyan.r, neo::Cyan.g, neo::Cyan.b, 92}, 2);
    ring(renderer, 1110, 360, 96, {neo::Cyan.r, neo::Cyan.g, neo::Cyan.b, 32}, 1);
    Color towerOuter = neo::PanelRaised;
    Color towerCore = neo::Cyan;
    switch (sim.skin()) {
        case ta::TowerSkin::Azure: break;
        case ta::TowerSkin::Ember: towerOuter = {56, 24, 39}; towerCore = neo::Amber; break;
        case ta::TowerSkin::Nebula: towerOuter = {38, 24, 79}; towerCore = neo::Violet; break;
        case ta::TowerSkin::Verdant: towerOuter = {18, 58, 62}; towerCore = neo::Mint; break;
        case ta::TowerSkin::Gold: towerOuter = {67, 50, 22}; towerCore = neo::Amber; break;
    }
    const ta::Enemy* target = nullptr;
    for (const ta::Enemy& enemy : sim.enemies()) if (target == nullptr || enemy.pos.x > target->pos.x) target = &enemy;
    if (target != nullptr) {
        const float dx = target->pos.x - 1110.0f;
        const float dy = target->pos.y - 360.0f;
        const float length = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
        line(renderer, 1110, 360, 1110 + static_cast<int>(dx / length * 54.0f), 360 + static_cast<int>(dy / length * 54.0f), towerCore, 7);
        if (sim.weapon() == ta::Weapon::ArcaneBeam) line(renderer, 1110, 360, static_cast<int>(target->pos.x), static_cast<int>(target->pos.y), {neo::Violet.r, neo::Violet.g, neo::Violet.b, 150}, 3);
    }
    filledHexagon(renderer, 1110, 360, 48, towerOuter);
    hexagon(renderer, 1110, 360, 48, towerCore, 2);
    filledHexagon(renderer, 1110, 360, 30, towerCore);
    hexagon(renderer, 1110, 360, 31, neo::Text, 1);
    circle(renderer, 1110, 360, 10, neo::Text);
    line(renderer, 1060, 360, 1080, 360, towerCore, 2);
    line(renderer, 1140, 360, 1160, 360, towerCore, 2);
    line(renderer, 1110, 310, 1110, 330, towerCore, 2);
    line(renderer, 1110, 390, 1110, 410, towerCore, 2);
}

void drawLoadout(SDL_Renderer* renderer, const GameSim& sim, const ta::ProfileData& profile, const ta::DailyChallenge& daily) {
    rect(renderer, 0, 0, GameSim::Width, GameSim::Height, neo::Void);
    chamferOutline(renderer, 96, 54, 1088, 620, {neo::Cyan.r, neo::Cyan.g, neo::Cyan.b, 80}, 20, 1);
    neoPanel(renderer, 140, 90, 1000, 540, neo::Cyan, false, 16);
    drawText(renderer, 190, 116, "TOWER ASCEND", 3, neo::Text);
    drawText(renderer, 190, 150, "LOADOUT // SYSTEM READY", 1, neo::Cyan);
    neonDivider(renderer, 190, 174, 900, neo::Cyan);
    drawText(renderer, 190, 328, "ARENA // ROUTE PROFILE", 1, neo::Muted);
    const std::array<const char*, 3> arenaLabels{{"MOONBASE", "EMBER", "NEON"}};
    const std::array<Color, 3> arenaColors{{neo::Cyan, neo::Amber, neo::Violet}};
    for (int i = 0; i < 3; ++i) {
        const int x = 190 + i * 250;
        const bool selected = static_cast<int>(sim.arena()) == i;
        neoPanel(renderer, x, 350, 220, 36, arenaColors[static_cast<std::size_t>(i)], selected, 6);
        drawText(renderer, x + 76, 362, arenaLabels[static_cast<std::size_t>(i)], 1, neo::Text);
    }
    drawText(renderer, 900, 96, "SHARDS " + std::to_string(profile.cosmeticShards), 1, neo::Amber);
    drawText(renderer, 900, 108, "BEST " + std::to_string(profile.bestScore), 1, neo::Muted);
    drawText(renderer, 190, 378, std::string("AUDIO M") + std::to_string(profile.masterVolume) + " S" + std::to_string(profile.sfxVolume) + " U" + std::to_string(profile.uiVolume), 1, neo::Muted);
    drawText(renderer, 190, 390, std::string("UI ") + std::to_string(profile.uiScalePercent) + "%  PALETTE " + std::to_string(profile.colorBlindPalette + 1) + "  F10 SETTINGS", 1, neo::Muted);
    drawText(renderer, 650, 578, sim.autoUltimate() ? "AUTO ULTIMATE // ON" : "AUTO ULTIMATE // OFF", 1, sim.autoUltimate() ? neo::Mint : neo::Muted);
    const std::array<Color, 5> colors{{neo::Mint, neo::Amber, neo::Violet, neo::Ice, neo::Red}};
    for (int i = 0; i < 5; ++i) {
        const int x = 190 + i * 180;
        neoPanel(renderer, x, 190, 140, 120, colors[static_cast<std::size_t>(i)], i == static_cast<int>(sim.weapon()), 10);
        drawText(renderer, x + 12, 202, "0" + std::to_string(i + 1), 1, colors[static_cast<std::size_t>(i)]);
        if (i == 0) filledDiamond(renderer, x + 70, 248, 24, colors[static_cast<std::size_t>(i)]);
        else if (i == 1) filledHexagon(renderer, x + 70, 248, 25, colors[static_cast<std::size_t>(i)]);
        else if (i == 2) { circle(renderer, x + 70, 248, 24, colors[static_cast<std::size_t>(i)]); line(renderer, x + 50, 248, x + 90, 248, neo::Text, 2); }
        else if (i == 3) { filledDiamond(renderer, x + 70, 248, 25, colors[static_cast<std::size_t>(i)]); hexagon(renderer, x + 70, 248, 18, neo::Text, 1); }
        else { filledHexagon(renderer, x + 70, 248, 25, colors[static_cast<std::size_t>(i)]); line(renderer, x + 54, 248, x + 86, 248, neo::Text, 2); }
        rect(renderer, x + 26, 286, 88, 4, colors[static_cast<std::size_t>(i)]);
    }
    const std::array<const char*, 5> weaponLabels{{"RAPID FIRE", "CANNON", "ARCANE BEAM", "FROST", "RAILGUN"}};
    for (int i = 0; i < 5; ++i) drawText(renderer, 190 + i * 180 + 10, 313, weaponLabels[static_cast<std::size_t>(i)], 1, neo::Text);
    for (int i = 0; i < 4; ++i) {
        const int x = 300 + i * 180;
        const bool enabled = sim.hasSkull(static_cast<ta::Skull>(i + 1));
        neoPanel(renderer, x, 420, 140, 72, neo::Magenta, enabled, 8);
        if (enabled) filledDiamond(renderer, x + 70, 456, 16, neo::Magenta);
        else hexagon(renderer, x + 70, 456, 16, neo::DarkText, 2);
    }
    const std::array<const char*, 4> skullLabels{{"SWARM", "GLASS", "HASTE", "GREED"}};
    for (int i = 0; i < 4; ++i) drawText(renderer, 300 + i * 180 + 38, 445, skullLabels[static_cast<std::size_t>(i)], 1, neo::Text);
    drawText(renderer, 190, 398, "SKULL MODIFIERS // Q-T", 1, neo::Muted);
    drawText(renderer, 900, 398, "SCORE X" + std::to_string(sim.skullScoreMultiplier()).substr(0, 4), 1, neo::Amber);
    const std::array<Color, 5> ultimateColors{{neo::Red, neo::Amber, neo::Ice, neo::Violet, neo::Mint}};
    const std::array<const char*, 5> ultimateLabels{{"METEOR", "BULLET", "ZERO", "GRAVITY", "SURGE"}};
    for (int i = 0; i < 5; ++i) {
        const int x = 160 + i * 220;
        neoPanel(renderer, x, 520, 190, 48, ultimateColors[static_cast<std::size_t>(i)], i == static_cast<int>(sim.ultimate()), 8);
        drawText(renderer, x + 42, 538, ultimateLabels[static_cast<std::size_t>(i)], 1, neo::Text);
    }
    chamferFill(renderer, 460, 590, 360, 38, neo::Mint, 8);
    chamferOutline(renderer, 460, 590, 360, 38, neo::Text, 8, 1);
    chamferFill(renderer, 850, 590, 240, 38, neo::Violet, 8);
    chamferOutline(renderer, 850, 590, 240, 38, neo::Text, 8, 1);
    drawText(renderer, 548, 604, "START RUN", 2, neo::Void);
    drawText(renderer, 896, 604, "DAILY", 2, neo::Text);
    drawText(renderer, 850, 640, "DAILY // CHALLENGE", 1, neo::Violet);
    drawText(renderer, 850, 650, std::string(ta::weaponName(daily.recommendedWeapon)) + " " + ta::arenaName(daily.arena), 1, neo::Text);
    drawText(renderer, 850, 660, std::string("SKULL ") + ta::skullName(daily.skull) + " +" + std::to_string(daily.bonusShards) + " SHARDS", 1, neo::Amber);
}

void drawSkinStrip(SDL_Renderer* renderer, const ta::ProfileData& profile, ta::TowerSkin equipped) {
    drawText(renderer, 900, 132, "SKINS // MODULES", 2, neo::Text);
    const std::array<Color, 5> colors{{neo::Cyan, neo::Amber, neo::Violet, neo::Mint, neo::Amber}};
    for (int i = 0; i < 5; ++i) {
        const ta::TowerSkin skin = static_cast<ta::TowerSkin>(i);
        const int x = 900 + i * 68;
        const bool unlocked = ta::isSkinUnlocked(profile, skin);
        neoPanel(renderer, x, 150, 58, 28, colors[static_cast<std::size_t>(i)], i == static_cast<int>(equipped), 5);
        if (unlocked) filledDiamond(renderer, x + 14, 164, 7, colors[static_cast<std::size_t>(i)]);
        else hexagon(renderer, x + 14, 164, 7, neo::DarkText, 1);
        drawText(renderer, x + 25, 159, unlocked ? std::to_string(i + 1) : "X", 1, unlocked ? neo::Text : neo::Muted);
    }
    drawText(renderer, 900, 182, "6-0 SELECT  K UNLOCK  F10 SETTINGS", 1, neo::Muted);
    drawText(renderer, 190, 182, "F1-F3 ARENA", 1, neo::Muted);
}

void drawUpgradeOverlay(SDL_Renderer* renderer, const GameSim& sim) {
    chamferFill(renderer, 170, 150, 940, 400, {neo::Void.r, neo::Void.g, neo::Void.b, 242}, 18);
    chamferOutline(renderer, 170, 150, 940, 400, neo::Cyan, 18, 2);
    drawText(renderer, 430, 180, "CHOOSE UPGRADE", 3, neo::Text);
    neonDivider(renderer, 220, 225, 840, neo::Cyan);
    const std::array<Color, 3> accents{{neo::Mint, neo::Amber, neo::Violet}};
    for (int i = 0; i < 3; ++i) {
        const int x = 220 + i * 290;
        neoPanel(renderer, x, 245, 220, 160, accents[static_cast<std::size_t>(i)], false, 10);
        if (i == 0) filledDiamond(renderer, x + 110, 315, 38, accents[static_cast<std::size_t>(i)]);
        else if (i == 1) filledHexagon(renderer, x + 110, 315, 38, accents[static_cast<std::size_t>(i)]);
        else circle(renderer, x + 110, 315, 34, accents[static_cast<std::size_t>(i)]);
        hexagon(renderer, x + 110, 315, 42, neo::Text, 1);
        rect(renderer, x + 78, 370, 64, 4, accents[static_cast<std::size_t>(i)]);
        rect(renderer, x + 94, 215, 32, 18, accents[static_cast<std::size_t>(i)]);
        if (i < static_cast<int>(sim.pendingChoices().size())) {
            const ta::Upgrade upgrade = sim.pendingChoices()[static_cast<std::size_t>(i)];
            drawText(renderer, x + 20, 390, ta::upgradeName(upgrade), 1, neo::Text);
            drawText(renderer, x + 20, 410, ta::upgradeDescription(upgrade), 1, neo::Muted);
        }
    }
}

void drawPauseOverlay(SDL_Renderer* renderer) {
    chamferFill(renderer, 330, 270, 620, 150, {neo::Void.r, neo::Void.g, neo::Void.b, 242}, 14);
    chamferOutline(renderer, 330, 270, 620, 150, neo::Cyan, 14, 2);
    drawText(renderer, 480, 290, "SYSTEM HOLD", 2, neo::Cyan);
    neonDivider(renderer, 480, 315, 300, neo::Cyan);
    rect(renderer, 540, 335, 30, 58, neo::Text);
    rect(renderer, 630, 335, 30, 58, neo::Text);
    drawText(renderer, 458, 398, "PRESS PAUSE TO RESUME", 1, neo::Muted);
}

void chamferFill(SDL_Renderer* renderer, int x, int y, int width, int height, Color color, int cut) {
    color = accessibleColor(color);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    cut = std::clamp(cut, 1, std::min(width, height) / 2);
    for (int row = 0; row < height; ++row) {
        const int inset = row < cut ? cut - row : (row >= height - cut ? row - (height - cut - 1) : 0);
        SDL_RenderDrawLine(renderer, x + inset, y + row, x + width - 1 - inset, y + row);
    }
}

void chamferOutline(SDL_Renderer* renderer, int x, int y, int width, int height, Color color, int cut, int thickness) {
    color = accessibleColor(color);
    cut = std::clamp(cut, 1, std::min(width, height) / 2);
    line(renderer, x + cut, y, x + width - cut, y, color, thickness);
    line(renderer, x + width - cut, y, x + width, y + cut, color, thickness);
    line(renderer, x + width, y + cut, x + width, y + height - cut, color, thickness);
    line(renderer, x + width, y + height - cut, x + width - cut, y + height, color, thickness);
    line(renderer, x + width - cut, y + height, x + cut, y + height, color, thickness);
    line(renderer, x + cut, y + height, x, y + height - cut, color, thickness);
    line(renderer, x, y + height - cut, x, y + cut, color, thickness);
    line(renderer, x, y + cut, x + cut, y, color, thickness);
}

void neoPanel(SDL_Renderer* renderer, int x, int y, int width, int height, Color accent, bool selected, int cut) {
    chamferFill(renderer, x, y, width, height, selected ? neo::PanelRaised : neo::Panel, cut);
    chamferOutline(renderer, x, y, width, height, selected ? accent : neo::GridBright, cut, selected ? 2 : 1);
    line(renderer, x + cut + 8, y + 6, x + std::min(width - cut - 8, 74), y + 6, accent, 2);
    line(renderer, x + width - 28, y + height - 6, x + width - cut - 8, y + height - 6, selected ? accent : neo::DarkText, 1);
}

void neonDivider(SDL_Renderer* renderer, int x, int y, int width, Color accent) {
    line(renderer, x, y, x + width, y, neo::GridBright, 1);
    line(renderer, x + 8, y, x + width / 3, y, accent, 2);
    line(renderer, x + width - 42, y, x + width - 8, y, accent, 2);
}

void segmentedBar(SDL_Renderer* renderer, int x, int y, int width, int height, float ratio, Color accent, int segments) {
    neoPanel(renderer, x, y, width, height, accent, false, 4);
    const int innerX = x + 6;
    const int innerY = y + 6;
    const int innerW = std::max(1, width - 12);
    const int innerH = std::max(1, height - 12);
    const int active = static_cast<int>(std::round(std::clamp(ratio, 0.0f, 1.0f) * static_cast<float>(segments)));
    for (int index = 0; index < segments; ++index) {
        const int left = innerX + index * innerW / segments;
        const int right = innerX + (index + 1) * innerW / segments - 2;
        rect(renderer, left, innerY, std::max(1, right - left), innerH, index < active ? accent : neo::Deep);
    }
}

void drawSettingsOverlay(SDL_Renderer* renderer, const ta::ProfileData& profile, const ta::InputAction* remappingAction, const char* activeDevice) {
    chamferFill(renderer, 190, 100, 900, 520, {neo::Void.r, neo::Void.g, neo::Void.b, 248}, 18);
    chamferOutline(renderer, 190, 100, 900, 520, neo::Cyan, 18, 2);
    neonDivider(renderer, 220, 135, 840, neo::Cyan);
    drawText(renderer, 250, 155, "SETTINGS // CONTROL", 3, neo::Text);
    drawText(renderer, 250, 205, std::string("MASTER ") + std::to_string(profile.masterVolume) + "  SFX " + std::to_string(profile.sfxVolume) + "  UI " + std::to_string(profile.uiVolume), 2, neo::Muted);
    drawText(renderer, 250, 245, std::string("UI SCALE ") + std::to_string(profile.uiScalePercent) + "%  PALETTE " + std::to_string(profile.colorBlindPalette + 1), 2, neo::Muted);
    neoPanel(renderer, 230, 270, 800, 60, neo::Blue, false, 8);
    drawText(renderer, 250, 278, std::string("FLASH ") + (profile.reducedFlashes ? "REDUCED" : "FULL") + " HC " + (profile.highContrast ? "ON" : "OFF") + " CAPTIONS " + (profile.subtitles ? "ON" : "OFF"), 1, neo::Muted);
    drawText(renderer, 250, 300, std::string("VIBRATION ") + (profile.vibration ? "ON" : "OFF") + " DEVICE " + activeDevice, 1, neo::Muted);
    drawText(renderer, 250, 355, "REMAP DESKTOP ACTIONS", 2, neo::Text);
    const ta::InputAction remappable[] = {ta::InputAction::Confirm, ta::InputAction::Pause, ta::InputAction::Ultimate, ta::InputAction::Restart};
    for (std::size_t index = 0; index < std::size(remappable); ++index) {
        const ta::InputAction action = remappable[index];
        const bool pending = remappingAction != nullptr && *remappingAction == action;
        drawText(renderer, 250, 395 + static_cast<int>(index) * 28, std::string(ta::inputActionName(action)) + "  [" + (pending ? "PRESS KEY" : ta::inputKeyName(profile.inputBindings.key(action))) + "]", 1, pending ? neo::Amber : neo::Text);
    }
    drawText(renderer, 250, 530, "F6 ULTIMATE  F7 PAUSE  F8 CONFIRM  F9 RESTART", 1, neo::Muted);
    chamferFill(renderer, 760, 555, 270, 42, neo::PanelRaised, 8);
    chamferOutline(renderer, 760, 555, 270, 42, neo::Cyan, 8, 1);
    drawText(renderer, 805, 568, "CLOSE SETTINGS", 1, neo::Text);
}

void drawResultsOverlay(SDL_Renderer* renderer, const GameSim& sim) {
    if (!sim.isGameOver() && !sim.isVictory()) return;
    const ta::RunSummary summary = sim.runSummary();
    const Color outcome = summary.victory ? neo::Mint : neo::Red;
    chamferFill(renderer, 260, 185, 760, 350, {neo::Void.r, neo::Void.g, neo::Void.b, 242}, 16);
    chamferOutline(renderer, 260, 185, 760, 350, outcome, 16, 2);
    drawText(renderer, 440, 215, summary.victory ? "TOWER ASCENDED" : "RUN FAILED", 3, outcome);
    neonDivider(renderer, 390, 255, 500, outcome);
    drawText(renderer, 450, 275, ta::arenaName(summary.arena), 2, neo::Muted);
    const Color value = summary.victory ? neo::Amber : neo::Text;
    drawText(renderer, 390, 315, "SCORE", 2, neo::Muted);
    drawText(renderer, 720, 315, std::to_string(summary.score), 2, value);
    drawText(renderer, 390, 455, "SKULL MULT", 2, neo::Muted);
    drawText(renderer, 720, 455, "X" + std::to_string(summary.scoreMultiplier).substr(0, 4), 2, value);
    drawText(renderer, 390, 355, "WAVE", 2, neo::Muted);
    drawText(renderer, 720, 355, std::to_string(summary.wave), 2, value);
    drawText(renderer, 390, 395, "KILLS", 2, neo::Muted);
    drawText(renderer, 720, 395, std::to_string(summary.kills), 2, value);
    drawText(renderer, 390, 435, "LEAKS", 2, neo::Muted);
    drawText(renderer, 720, 435, std::to_string(summary.leaks), 2, value);
    drawText(renderer, 410, 485, "R TO RESTART", 2, neo::Text);
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    const bool hapticSubsystem = SDL_InitSubSystem(SDL_INIT_HAPTIC) == 0;
    AudioSynth audio;
    const bool audioEnabled = audio.init();
    HapticFeedback haptics;
    const bool hapticsEnabled = hapticSubsystem && haptics.init();
    SDL_GameController* controller = SDL_NumJoysticks() > 0 && SDL_IsGameController(0) ? SDL_GameControllerOpen(0) : nullptr;
    SDL_Window* window = SDL_CreateWindow("Tower Ascend", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, GameSim::DesignWidth, GameSim::DesignHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (window && !renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!window || !renderer) {
        std::fprintf(stderr, "SDL renderer failed: %s\n", SDL_GetError());
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(renderer, GameSim::DesignWidth, GameSim::DesignHeight);
    SDL_RenderSetScale(renderer, GameSim::WorldScale, GameSim::WorldScale);
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
    const std::string profilePath = userDataFile("tower_ascend.profile");
    const std::string replayPath = userDataFile("tower_ascend.last.replay");
    const auto saveCurrentProfile = [&]() { ta::saveProfile(profilePath, profile); };
    ta::loadProfile(profilePath, profile);
    audio.setVolume(static_cast<float>(profile.masterVolume) / 100.0f);
    audio.setBusVolumes(static_cast<float>(profile.sfxVolume) / 100.0f, static_cast<float>(profile.uiVolume) / 100.0f);
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
    bool settingsOpen = false;
    ta::InputAction* remappingAction = nullptr;
    ta::InputAction remappingStorage = ta::InputAction::Ultimate;
    const char* activeDevice = "KEYBOARD";
    bool reducedFlashes = profile.reducedFlashes;
    bool highContrast = profile.highContrast;
    int previousKills = 0;
    int previousShots = 0;
    int previousUltimates = 0;
    int previousWave = 1;
    bool previousUpgradePending = false;
    bool previousBossTelegraph = false;
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
            if (event.type == SDL_CONTROLLERDEVICEADDED && controller == nullptr && SDL_IsGameController(event.cdevice.which)) controller = SDL_GameControllerOpen(event.cdevice.which);
            if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller != nullptr && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == event.cdevice.which) {
                SDL_GameControllerClose(controller);
                controller = nullptr;
                activeDevice = "KEYBOARD";
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                activeDevice = "CONTROLLER";
                SDL_Keycode mappedKey = SDLK_UNKNOWN;
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A: mappedKey = static_cast<SDL_Keycode>(profile.inputBindings.key(ta::InputAction::Confirm)); break;
                    case SDL_CONTROLLER_BUTTON_B: mappedKey = static_cast<SDL_Keycode>(profile.inputBindings.key(ta::InputAction::Pause)); break;
                    case SDL_CONTROLLER_BUTTON_X: mappedKey = static_cast<SDL_Keycode>(profile.inputBindings.key(ta::InputAction::Ultimate)); break;
                    case SDL_CONTROLLER_BUTTON_Y: mappedKey = static_cast<SDL_Keycode>(profile.inputBindings.key(ta::InputAction::Restart)); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: mappedKey = static_cast<SDL_Keycode>(profile.inputBindings.key(ta::InputAction::Weapon1)); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: mappedKey = static_cast<SDL_Keycode>(profile.inputBindings.key(ta::InputAction::Weapon5)); break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: mappedKey = static_cast<SDL_Keycode>(profile.inputBindings.key(ta::InputAction::Skull1)); break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: mappedKey = static_cast<SDL_Keycode>(profile.inputBindings.key(ta::InputAction::Skull4)); break;
                    default: break;
                }
                if (mappedKey != SDLK_UNKNOWN) {
                    SDL_Event synthetic{};
                    synthetic.type = SDL_KEYDOWN;
                    synthetic.key.keysym.sym = mappedKey;
                    SDL_PushEvent(&synthetic);
                }
            }
            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                const SDL_Keycode key = event.key.keysym.sym;
                activeDevice = "KEYBOARD";
                if (key == SDLK_F10) {
                    settingsOpen = !settingsOpen;
                    if (!settingsOpen) remappingAction = nullptr;
                    if (settingsOpen) paused = true;
                    else if (started) paused = false;
                    continue;
                }
                if (key == SDLK_F6 || key == SDLK_F7 || key == SDLK_F8 || key == SDLK_F9) {
                    remappingStorage = key == SDLK_F6 ? ta::InputAction::Ultimate : (key == SDLK_F7 ? ta::InputAction::Pause : (key == SDLK_F8 ? ta::InputAction::Confirm : ta::InputAction::Restart));
                    remappingAction = &remappingStorage;
                    settingsOpen = true;
                    paused = true;
                    continue;
                }
                if (remappingAction != nullptr) {
                    if (key == SDLK_ESCAPE) {
                        remappingAction = nullptr;
                        settingsOpen = false;
                        if (started) paused = false;
                        continue;
                    }
                    if (key != SDLK_ESCAPE && ta::validInputKey(key)) {
                        profile.inputBindings.key(*remappingAction) = key;
                        saveCurrentProfile();
                        remappingAction = nullptr;
                    }
                    continue;
                }
                if (key == SDLK_ESCAPE && settingsOpen) {
                    settingsOpen = false;
                    if (started) paused = false;
                    continue;
                }
                    if (key == SDLK_ESCAPE) running = false;
                    if (key == profile.inputBindings.key(ta::InputAction::Pause) && started) paused = !paused;
                    if (key == profile.inputBindings.key(ta::InputAction::ReducedFlashes)) { reducedFlashes = !reducedFlashes; profile.reducedFlashes = reducedFlashes; saveCurrentProfile(); }
                    if (key == profile.inputBindings.key(ta::InputAction::HighContrast)) { highContrast = !highContrast; profile.highContrast = highContrast; saveCurrentProfile(); }
                    if (key == profile.inputBindings.key(ta::InputAction::Captions)) { profile.subtitles = !profile.subtitles; saveCurrentProfile(); }
                    if (key == profile.inputBindings.key(ta::InputAction::Vibration)) { profile.vibration = !profile.vibration; saveCurrentProfile(); }
                    if (key == profile.inputBindings.key(ta::InputAction::Palette)) { profile.colorBlindPalette = static_cast<std::uint8_t>((profile.colorBlindPalette + 1) % 3); saveCurrentProfile(); }
                    if (key == profile.inputBindings.key(ta::InputAction::UiScaleDown)) { profile.uiScalePercent = static_cast<std::uint8_t>(profile.uiScalePercent >= 10 ? profile.uiScalePercent - 10 : 80); profile.uiScalePercent = std::max<std::uint8_t>(80, profile.uiScalePercent); saveCurrentProfile(); }
                    if (key == profile.inputBindings.key(ta::InputAction::UiScaleUp)) { profile.uiScalePercent = static_cast<std::uint8_t>(std::min(140, static_cast<int>(profile.uiScalePercent) + 10)); saveCurrentProfile(); }
                    if (key == profile.inputBindings.key(ta::InputAction::MasterVolumeDown)) { profile.masterVolume = static_cast<std::uint8_t>(profile.masterVolume >= 10 ? profile.masterVolume - 10 : 0); audio.setVolume(static_cast<float>(profile.masterVolume) / 100.0f); saveCurrentProfile(); }
                    if (key == profile.inputBindings.key(ta::InputAction::MasterVolumeUp)) { profile.masterVolume = static_cast<std::uint8_t>(std::min(100, static_cast<int>(profile.masterVolume) + 10)); audio.setVolume(static_cast<float>(profile.masterVolume) / 100.0f); saveCurrentProfile(); }
                    if (key == profile.inputBindings.key(ta::InputAction::SfxVolumeDown)) { profile.sfxVolume = static_cast<std::uint8_t>(profile.sfxVolume >= 10 ? profile.sfxVolume - 10 : 0); audio.setBusVolumes(static_cast<float>(profile.sfxVolume) / 100.0f, static_cast<float>(profile.uiVolume) / 100.0f); saveCurrentProfile(); }
                    if (key == profile.inputBindings.key(ta::InputAction::SfxVolumeUp)) { profile.sfxVolume = static_cast<std::uint8_t>(std::min(100, static_cast<int>(profile.sfxVolume) + 10)); audio.setBusVolumes(static_cast<float>(profile.sfxVolume) / 100.0f, static_cast<float>(profile.uiVolume) / 100.0f); saveCurrentProfile(); }
                    if (key == profile.inputBindings.key(ta::InputAction::UiVolumeDown)) { profile.uiVolume = static_cast<std::uint8_t>(profile.uiVolume >= 10 ? profile.uiVolume - 10 : 0); audio.setBusVolumes(static_cast<float>(profile.sfxVolume) / 100.0f, static_cast<float>(profile.uiVolume) / 100.0f); saveCurrentProfile(); }
                    if (key == profile.inputBindings.key(ta::InputAction::UiVolumeUp)) { profile.uiVolume = static_cast<std::uint8_t>(std::min(100, static_cast<int>(profile.uiVolume) + 10)); audio.setBusVolumes(static_cast<float>(profile.sfxVolume) / 100.0f, static_cast<float>(profile.uiVolume) / 100.0f); saveCurrentProfile(); }
                    if (!started) {
                    for (int index = 0; index < 5; ++index) if (key == profile.inputBindings.key(indexedAction(ta::InputAction::Weapon1, index))) sim.setWeapon(static_cast<ta::Weapon>(index));
                    for (int index = 0; index < 4; ++index) if (key == profile.inputBindings.key(indexedAction(ta::InputAction::Skull1, index))) sim.toggleSkull(static_cast<ta::Skull>(index + 1));
                    for (int index = 0; index < 5; ++index) if (key == profile.inputBindings.key(indexedAction(ta::InputAction::Ultimate1, index))) sim.setUltimate(static_cast<ta::Ultimate>(index));
                    if (key == SDLK_F1) sim.setArena(ta::Arena::Moonbase);
                    if (key == SDLK_F2) sim.setArena(ta::Arena::EmberCrater);
                    if (key == SDLK_F3) sim.setArena(ta::Arena::NeonRuins);
                    if (key == profile.inputBindings.key(ta::InputAction::AutoUltimate) && !started) sim.setAutoUltimate(!sim.autoUltimate());
                    for (int index = 0; index < 5; ++index) {
                        if (key == profile.inputBindings.key(indexedAction(ta::InputAction::Skin1, index))) {
                            skinPreview = index;
                            const ta::TowerSkin skin = static_cast<ta::TowerSkin>(index);
                            if (ta::isSkinUnlocked(profile, skin)) { sim.setSkin(skin); profile.equippedSkin = static_cast<std::uint8_t>(index); saveCurrentProfile(); }
                        }
                    }
                    if (key == profile.inputBindings.key(ta::InputAction::UnlockSkin) && ta::unlockSkin(profile, static_cast<ta::TowerSkin>(skinPreview))) {
                        ta::equipSkin(profile, static_cast<ta::TowerSkin>(skinPreview));
                        sim.setSkin(static_cast<ta::TowerSkin>(skinPreview));
                        saveCurrentProfile();
                    }
                    if (key == profile.inputBindings.key(ta::InputAction::Daily)) {
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
                        previousBossTelegraph = false;
                        previousTerminal = false;
                        dailyMode = true;
                        resultSaved = false;
                    }
                    if (key == profile.inputBindings.key(ta::InputAction::Confirm) || key == SDLK_SPACE) {
                        sim.reset(0x7A2026u);
                        replay = makeReplay();
                        started = true;
                        previousKills = 0;
                        previousShots = 0;
                        previousUltimates = 0;
                        previousWave = sim.waveNumber();
                        previousUpgradePending = false;
                        previousBossTelegraph = false;
                        previousTerminal = false;
                        dailyMode = false;
                        resultSaved = false;
                    }
                } else {
                    const int upgradeChoice = key == profile.inputBindings.key(ta::InputAction::Upgrade1) ? 0 : (key == profile.inputBindings.key(ta::InputAction::Upgrade2) ? 1 : (key == profile.inputBindings.key(ta::InputAction::Upgrade3) ? 2 : -1));
                    if (upgradeChoice >= 0 && sim.upgradePending()) {
                        sim.chooseUpgrade(upgradeChoice);
                        replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Upgrade, static_cast<std::uint8_t>(upgradeChoice)});
                    }
                    if (key == profile.inputBindings.key(ta::InputAction::Ultimate) || key == SDLK_u || key == SDLK_SPACE) {
                        const int previous = sim.stats().ultimates;
                        sim.activateUltimate();
                        if (sim.stats().ultimates != previous) replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Ultimate, 0});
                    }
                    if (key == profile.inputBindings.key(ta::InputAction::Restart)) {
                        sim.reset(0x7A2026u);
                        replay = makeReplay();
                        started = true;
                        previousKills = 0;
                        previousShots = 0;
                        previousUltimates = 0;
                        previousWave = sim.waveNumber();
                        previousUpgradePending = false;
                        previousBossTelegraph = false;
                        previousTerminal = false;
                        dailyMode = false;
                        resultSaved = false;
                    }
                }
            }
            if ((event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) || event.type == SDL_FINGERDOWN) {
                int x = event.type == SDL_MOUSEBUTTONDOWN ? event.button.x : 0;
                int y = event.type == SDL_MOUSEBUTTONDOWN ? event.button.y : 0;
                if (event.type == SDL_FINGERDOWN) {
                    int windowWidth = GameSim::DesignWidth;
                    int windowHeight = GameSim::DesignHeight;
                    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
                    const int pointerX = static_cast<int>(event.tfinger.x * static_cast<float>(windowWidth));
                    const int pointerY = static_cast<int>(event.tfinger.y * static_cast<float>(windowHeight));
                    float logicalX = static_cast<float>(pointerX);
                    float logicalY = static_cast<float>(pointerY);
                    SDL_RenderWindowToLogical(renderer, pointerX, pointerY, &logicalX, &logicalY);
                    x = static_cast<int>(std::lround(logicalX));
                    y = static_cast<int>(std::lround(logicalY));
                }
                // SDL scales mouse button events into the renderer's logical
                // coordinates when SDL_RenderSetLogicalSize is active. Do
                // not pass those already-logical values through
                // SDL_RenderWindowToLogical a second time; doing so shifts
                // hit tests and makes adjacent buttons appear misaligned.
                if (settingsOpen) {
                    activeDevice = event.type == SDL_FINGERDOWN ? "TOUCH" : "MOUSE";
                    if (x >= 760 && x < 1030 && y >= 555 && y < 600) {
                        settingsOpen = false;
                        if (started) paused = false;
                    } else if (y >= 270 && y < 330) {
                        if (x < 420) { profile.reducedFlashes = !profile.reducedFlashes; reducedFlashes = profile.reducedFlashes; }
                        else if (x < 600) { profile.highContrast = !profile.highContrast; highContrast = profile.highContrast; }
                        else if (x < 800) profile.subtitles = !profile.subtitles;
                        else profile.vibration = !profile.vibration;
                        saveCurrentProfile();
                    }
                    continue;
                }
                if (!started) {
                    for (int i = 0; i < 5; ++i) if (x >= 190 + i * 180 && x < 330 + i * 180 && y >= 190 && y < 340) sim.setWeapon(static_cast<ta::Weapon>(i));
                    for (int i = 0; i < 3; ++i) if (x >= 190 + i * 250 && x < 410 + i * 250 && y >= 350 && y < 386) sim.setArena(static_cast<ta::Arena>(i));
                    for (int i = 0; i < 4; ++i) if (x >= 300 + i * 180 && x < 440 + i * 180 && y >= 420 && y < 492) sim.toggleSkull(static_cast<ta::Skull>(i + 1));
                    for (int i = 0; i < 5; ++i) if (x >= 900 + i * 68 && x < 958 + i * 68 && y >= 150 && y < 178) {
                        skinPreview = i;
                        const ta::TowerSkin skin = static_cast<ta::TowerSkin>(i);
                        if (!ta::isSkinUnlocked(profile, skin)) ta::unlockSkin(profile, skin);
                        if (ta::isSkinUnlocked(profile, skin)) { ta::equipSkin(profile, skin); sim.setSkin(skin); saveCurrentProfile(); }
                    }
                    for (int i = 0; i < 5; ++i) if (x >= 160 + i * 220 && x < 350 + i * 220 && y >= 520 && y < 568) sim.setUltimate(static_cast<ta::Ultimate>(i));
                    if (x >= 460 && x < 820 && y >= 570 && y < 588) sim.setAutoUltimate(!sim.autoUltimate());
                    if (x >= 460 && x < 820 && y >= 590 && y < 628) {
                        sim.reset(0x7A2026u); replay = makeReplay(); started = true; dailyMode = false; resultSaved = false; previousKills = 0; previousShots = 0; previousUltimates = 0; previousWave = sim.waveNumber(); previousUpgradePending = false; previousBossTelegraph = false; previousTerminal = false;
                    }
                    if (x >= 850 && x < 1090 && y >= 590 && y < 628) {
                        sim.setWeapon(daily.recommendedWeapon); sim.setSkull(daily.skull); sim.setArena(daily.arena); sim.setAutoUltimate(false); sim.reset(daily.seed); replay = makeReplay(); started = true; dailyMode = true; resultSaved = false; previousKills = 0; previousShots = 0; previousUltimates = 0; previousWave = sim.waveNumber(); previousUpgradePending = false; previousBossTelegraph = false; previousTerminal = false;
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
        const auto playCue = [&](float frequencyHz, int milliseconds, float volume, bool uiCue = false) { if (audioEnabled) audio.tone(frequencyHz, milliseconds, reducedFlashes ? volume * 0.35f : volume, uiCue ? AudioSynth::Bus::Ui : AudioSynth::Bus::Sfx); };
        if (started && sim.stats().shotsFired > previousShots) {
            const float shotTone = sim.weapon() == ta::Weapon::ExplosiveCannon ? 125.0f : (sim.weapon() == ta::Weapon::ArcaneBeam ? 510.0f : (sim.weapon() == ta::Weapon::FrostBlaster ? 760.0f : (sim.weapon() == ta::Weapon::SniperRailgun ? 250.0f : 380.0f)));
            playCue(shotTone, sim.weapon() == ta::Weapon::SniperRailgun ? 90 : 28, 0.07f);
            previousShots = sim.stats().shotsFired;
        }
        bool bossTelegraph = false;
        for (const ta::Enemy& enemy : sim.enemies()) if (enemy.boss && enemy.telegraphTicks > 0) { bossTelegraph = true; break; }
        if (started && bossTelegraph && !previousBossTelegraph) playCue(95.0f, 320, 0.22f, true);
        previousBossTelegraph = bossTelegraph;
        if (started && sim.stats().kills > previousKills) { playCue(320.0f + (sim.stats().kills % 4) * 80.0f, 45, 0.18f); if (hapticsEnabled) haptics.pulse(profile.vibration, 0.18f, 18); previousKills = sim.stats().kills; }
        if (started && sim.stats().ultimates > previousUltimates) { playCue(900.0f, 180, 0.20f); if (hapticsEnabled) haptics.pulse(profile.vibration, 0.55f, 90); previousUltimates = sim.stats().ultimates; }
        if (started && sim.waveNumber() != previousWave) { playCue(190.0f, 180, 0.22f, true); playCue(380.0f, 140, 0.18f, true); previousWave = sim.waveNumber(); }
        if (started && sim.upgradePending() && !previousUpgradePending) playCue(620.0f, 130, 0.16f, true);
        previousUpgradePending = started && sim.upgradePending();
        if (started && audioEnabled && (sim.isGameOver() || sim.isVictory()) && !previousTerminal) {
            playCue(sim.isVictory() ? 760.0f : 120.0f, 260, 0.24f, true);
            if (hapticsEnabled) haptics.pulse(profile.vibration, sim.isVictory() ? 0.7f : 0.35f, 180);
            previousTerminal = true;
        }
        if (started && !resultSaved && (sim.isGameOver() || sim.isVictory())) {
            profile.bestScore = std::max(profile.bestScore, sim.stats().score);
            profile.bestWave = std::max(profile.bestWave, sim.waveNumber());
            ++profile.runsCompleted;
            profile.totalKills += static_cast<std::uint32_t>(sim.stats().kills);
            ta::awardRunCosmetics(profile, sim.stats(), dailyMode, daily.bonusShards);
            saveCurrentProfile();
            replay.save(replayPath);
            resultSaved = true;
        }
        if (started) {
            char title[200];
            std::snprintf(title, sizeof(title), "Tower Ascend%s%s%s%s%s | %s | %s | Wave %d | Score %d | %s", dailyMode ? " DAILY" : "", paused ? " PAUSED" : "", highContrast ? " HC" : "", reducedFlashes ? " RF" : "", sim.autoUltimate() ? " AUTO" : "", ta::arenaName(sim.arena()), ta::weaponName(sim.weapon()), sim.waveNumber(), sim.stats().score, sim.statusText().c_str());
            SDL_SetWindowTitle(window, title);
        }
        activeColorPalette = profile.colorBlindPalette;
        activeUiScale = static_cast<float>(profile.uiScalePercent) / 100.0f;
        if (!started) { drawLoadout(renderer, sim, profile, daily); drawSkinStrip(renderer, profile, sim.skin()); }
        else { drawArena(renderer, highContrast, sim.arena(), authoredContent); drawWorld(renderer, sim); drawHud(renderer, sim, highContrast, profile.subtitles); if (sim.upgradePending()) drawUpgradeOverlay(renderer, sim); if (paused && !settingsOpen) drawPauseOverlay(renderer); drawResultsOverlay(renderer, sim); }
        if (settingsOpen) drawSettingsOverlay(renderer, profile, remappingAction, activeDevice);
        SDL_RenderPresent(renderer);
        if (renderSmoke && ++renderedFrames >= 3) running = false;
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (controller != nullptr) SDL_GameControllerClose(controller);
    audio.shutdown();
    SDL_Quit();
    return 0;
}
