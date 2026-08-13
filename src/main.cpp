#include "game.hpp"
#include "input.hpp"
#include "profile.hpp"
#include "daily.hpp"
#include "ui_layout.hpp"
#include "ui_text.hpp"
#include "app_state.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
using ta::GameSim;
using ta::Vec2;
using namespace ta::ui;

using FrontendScreen = ta::app::FrontendScreen;

enum class WorkshopPurchase { None, TowerCore, WeaponModule, SupportModule, UltimateEvolution, UltimateModule, SkillNode, SkillUnlock };

ta::UltimateEvolution evolutionForUltimate(ta::Ultimate ultimate, int slot) {
    const int base = static_cast<int>(ultimate) * 3 + 1;
    return static_cast<ta::UltimateEvolution>(base + slot);
}

ta::InputAction indexedAction(ta::InputAction first, int offset) {
    return static_cast<ta::InputAction>(static_cast<std::size_t>(first) + static_cast<std::size_t>(offset));
}

std::vector<int> skillTreeNodeIndices(const ta::ContentConfig& content, int slot, const ta::ProfileData& profile) {
    std::vector<int> result;
    if (slot < 0 || slot >= static_cast<int>(ta::SkillSlotCount)) return result;
    const std::string skillId = ta::skillIdString(profile.skillLoadout.skills[static_cast<std::size_t>(slot)]);
    for (std::size_t index = 0; index < content.skillNodes.size(); ++index) if (content.skillNodes[index].skillId == skillId) result.push_back(static_cast<int>(index));
    return result;
}

int nextSkillNodeIndex(const ta::ProfileData& profile, const ta::ContentConfig& content, int slot) {
    if (slot < 0 || slot >= static_cast<int>(ta::SkillSlotCount)) return -1;
    const ta::SkillId skill = profile.skillLoadout.skills[static_cast<std::size_t>(slot)];
    const std::string skillId = ta::skillIdString(skill);
    for (std::size_t index = 0; index < content.skillNodes.size(); ++index) {
        const ta::SkillNodeDefinition& node = content.skillNodes[index];
        if (node.skillId != skillId) continue;
        if (ta::purchasedSkillNodeRank(profile, node.id) >= node.maxRank) continue;
        if (!node.parentId.empty() && ta::purchasedSkillNodeRank(profile, node.parentId) <= 0) continue;
        return static_cast<int>(index);
    }
    return -1;
}

bool cycleSkillLoadout(ta::ProfileData& profile, ta::GameSim& sim, int slot, const ta::ContentConfig& content) {
    if (slot < 0 || slot >= static_cast<int>(ta::SkillSlotCount)) return false;
    const int current = static_cast<int>(profile.skillLoadout.skills[static_cast<std::size_t>(slot)]);
    for (int offset = 1; offset <= static_cast<int>(ta::SkillId::Count); ++offset) {
        const int candidate = (current + offset) % static_cast<int>(ta::SkillId::Count);
        const ta::SkillId skill = static_cast<ta::SkillId>(candidate);
        if (!ta::isSkillUnlocked(profile, skill)) continue;
        if (!ta::equipSkill(profile, static_cast<std::size_t>(slot), skill)) continue;
        sim.setSkillLoadout(profile.skillLoadout);
        (void)content;
        return true;
    }
    return false;
}

struct Color { Uint8 r, g, b, a = 255; };
void drawFocusOutline(SDL_Renderer* renderer, const UiRect& bounds, Color accent);
std::uint8_t activeColorPalette = 0;
float activeUiScale = 1.0f;

class TextLayoutAudit {
public:
    explicit TextLayoutAudit(bool enabled) : enabled_(enabled) {
        if (enabled_) output_.open("text_layout.log", std::ios::out | std::ios::trunc);
    }

    void setScreen(const std::string& screen) { screen_ = screen; }

    void checkViewport(const char* label, int x, int y, const std::string& text, int scale, int uiScalePercent) {
        if (!enabled_) return;
        const ta::ui::TextMetrics metrics = ta::ui::measureText(text, scale, uiScalePercent);
        if (x < 0 || y < 0 || x + metrics.width > GameSim::Width || y + metrics.height > GameSim::Height) {
            report("VIEWPORT", label, x, y, metrics.width, metrics.height, GameSim::Width, GameSim::Height, text);
        }
    }

    void checkWrapped(const char* label, int x, int y, int width, const std::string& text, int scale, int lineGap, int uiScalePercent) {
        if (!enabled_) return;
        const ta::ui::TextMetrics metrics = ta::ui::measureWrappedText(text, width, scale, lineGap, uiScalePercent);
        if (x < 0 || y < 0 || metrics.width > width || x + width > GameSim::Width || y + metrics.height > GameSim::Height) {
            report("WRAPPED", label, x, y, metrics.width, metrics.height, width, GameSim::Height - y, text);
        }
    }

    void checkBox(const char* label, const ta::ui::UiRect& box, int padding, const std::string& text, int scale, int lineGap, int uiScalePercent) {
        if (!enabled_) return;
        const int innerWidth = std::max(1, box.width - padding * 2);
        const int innerHeight = std::max(1, box.height - padding * 2);
        const ta::ui::TextMetrics metrics = ta::ui::measureWrappedText(text, innerWidth, scale, lineGap, uiScalePercent);
        if (box.x < 0 || box.y < 0 || box.x + box.width > GameSim::Width || box.y + box.height > GameSim::Height ||
            !ta::ui::fitsWithin(metrics, innerWidth, innerHeight)) {
            report("BOX", label, box.x + padding, box.y + padding, metrics.width, metrics.height, innerWidth, innerHeight, text);
        }
    }

private:
    void report(const char* kind, const char* label, int x, int y, int width, int height, int availableWidth, int availableHeight, const std::string& text) {
        const std::string key = screen_ + "|" + kind + "|" + label + "|" + std::to_string(x) + "|" + std::to_string(y) + "|" + text;
        if (!reported_.insert(key).second) return;
        std::string compactText = text;
        for (char& character : compactText) if (character == '\n' || character == '\r') character = ' ';
        const std::string message = "TEXT_OVERFLOW screen=" + screen_ + " type=" + kind + " label=" + label +
            " origin=" + std::to_string(x) + "," + std::to_string(y) + " size=" + std::to_string(width) + "x" + std::to_string(height) +
            " available=" + std::to_string(availableWidth) + "x" + std::to_string(availableHeight) + " text=\"" + compactText + "\"";
        std::cerr << message << '\n';
        if (output_.is_open()) output_ << message << '\n';
    }

    bool enabled_ = false;
    std::string screen_ = "UNKNOWN";
    std::ofstream output_;
    std::set<std::string> reported_;
};

TextLayoutAudit* activeTextAudit = nullptr;

int activeUiScalePercent() {
    return std::max(1, static_cast<int>(std::lround(activeUiScale * 100.0f)));
}

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
    if (activeTextAudit != nullptr) activeTextAudit->checkViewport("drawText", x, y, text, scale, activeUiScalePercent());
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

void drawWrappedTextInBox(SDL_Renderer* renderer, const char* label, const UiRect& box, const std::string& text, int scale, Color color, int padding = 10, int lineGap = 18) {
    const int innerWidth = std::max(1, box.width - padding * 2);
    const int innerHeight = std::max(1, box.height - padding * 2);
    if (activeTextAudit != nullptr) activeTextAudit->checkBox(label, box, padding, text, scale, lineGap, activeUiScalePercent());
    const std::vector<std::string> lines = ta::ui::wrapText(text, innerWidth, scale, activeUiScalePercent());
    const int lineHeight = ta::ui::textLineHeight(scale, activeUiScalePercent());
    const int maxLines = std::max(0, 1 + (innerHeight - lineHeight) / std::max(1, lineGap));
    const int linesToDraw = std::min(static_cast<int>(lines.size()), maxLines);
    for (int index = 0; index < linesToDraw; ++index) {
        if (!lines[static_cast<std::size_t>(index)].empty()) drawText(renderer, box.x + padding, box.y + padding + index * lineGap, lines[static_cast<std::size_t>(index)], scale, color);
    }
}

void drawTextFitInBox(SDL_Renderer* renderer, const char* label, const UiRect& box, const std::string& text, int scale, Color color, int padding = 0) {
    const int innerWidth = std::max(1, box.width - padding * 2);
    const int innerHeight = std::max(1, box.height - padding * 2);
    if (activeTextAudit != nullptr) activeTextAudit->checkBox(label, box, padding, text, scale, 18, activeUiScalePercent());
    const std::string fitted = ta::ui::fitTextToWidth(text, innerWidth, scale, activeUiScalePercent());
    if (innerHeight < ta::ui::textLineHeight(scale, activeUiScalePercent())) return;
    drawText(renderer, box.x + padding, box.y + padding, fitted, scale, color);
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

void brokenRing(SDL_Renderer* renderer, int cx, int cy, int radius, Color color, int thickness = 1, int gaps = 4) {
    color = accessibleColor(color);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const int safeGaps = std::max(1, gaps);
    const int arcSize = std::max(8, 360 / safeGaps - 14);
    for (int arc = 0; arc < safeGaps; ++arc) {
        const int start = arc * (360 / safeGaps) + 7;
        for (int angle = start; angle < start + arcSize; angle += 3) {
            const float radians = static_cast<float>(angle) * 3.14159265f / 180.0f;
            for (int layer = 0; layer < thickness; ++layer) {
                const int currentRadius = radius + layer;
                SDL_RenderDrawPoint(renderer, cx + static_cast<int>(std::cos(radians) * currentRadius), cy + static_cast<int>(std::sin(radians) * currentRadius));
            }
        }
    }
}

void cross(SDL_Renderer* renderer, int cx, int cy, int radius, Color color, int width = 1) {
    line(renderer, cx - radius, cy, cx + radius, cy, color, width);
    line(renderer, cx, cy - radius, cx, cy + radius, color, width);
}

void plusGlyph(SDL_Renderer* renderer, int cx, int cy, int radius, Color color, int width = 2) {
    cross(renderer, cx, cy, radius, color, width);
}

void chevrons(SDL_Renderer* renderer, int cx, int cy, int radius, Color color, int count = 4) {
    for (int index = 0; index < count; ++index) {
        const int y = cy - radius + index * std::max(1, (radius * 2) / std::max(1, count - 1));
        line(renderer, cx - 7, y - 3, cx, y + 3, color, 1);
        line(renderer, cx, y + 3, cx + 7, y - 3, color, 1);
    }
}

void filledHexagon(SDL_Renderer* renderer, int cx, int cy, int radius, Color color);
void hexagon(SDL_Renderer* renderer, int cx, int cy, int radius, Color color, int thickness);

Color skillAccent(ta::SkillId skill) {
    switch (skill) {
        case ta::SkillId::GravityWell: return neo::Violet;
        case ta::SkillId::PhaseMine: return neo::Amber;
        case ta::SkillId::VanguardDrop: return neo::Mint;
        case ta::SkillId::ForwardBarracks: return neo::Mint;
        case ta::SkillId::RuinHex: return neo::Magenta;
        case ta::SkillId::RallyBeacon: return neo::Mint;
        case ta::SkillId::SentryFabricator: return neo::Cyan;
        case ta::SkillId::CryoField: return neo::Ice;
        case ta::SkillId::DroneSwarm: return neo::Cyan;
        case ta::SkillId::ResonancePulse: return neo::Violet;
        case ta::SkillId::Count: break;
    }
    return neo::Text;
}

void drawSkillVisualEvent(SDL_Renderer* renderer, const ta::SkillVisualEvent& event) {
    const int x = static_cast<int>(event.position.x);
    const int y = static_cast<int>(event.position.y);
    const int radius = std::max(12, static_cast<int>(event.radius));
    const Color color = skillAccent(event.skill);
    const bool result = event.phase == ta::SkillVisualPhase::Hit || event.phase == ta::SkillVisualPhase::Trigger;
    const int pulse = result ? std::max(8, radius + (12 - event.remainingTicks) * 3) : radius;
    switch (event.skill) {
        case ta::SkillId::GravityWell:
            brokenRing(renderer, x, y, pulse, color, 2, 3);
            ring(renderer, x, y, std::max(8, pulse / 3), neo::Text, 1);
            if (event.phase == ta::SkillVisualPhase::Cast) for (int index = 0; index < 4; ++index) line(renderer, x + (index - 2) * 12, y - pulse / 2, x, y, color, 1);
            break;
        case ta::SkillId::PhaseMine:
            if (event.phase == ta::SkillVisualPhase::Trigger) { filledHexagon(renderer, x, y, std::min(28, pulse / 2), color); brokenRing(renderer, x, y, pulse, color, 2, 6); }
            else { brokenRing(renderer, x, y, pulse, color, 1, 6); hexagon(renderer, x, y, 12, color, 2); cross(renderer, x, y, 7, color, 1); }
            break;
        case ta::SkillId::VanguardDrop:
            line(renderer, x, y - pulse, x, y - 8, color, 2);
            filledDiamond(renderer, x, y, std::min(22, pulse / 2), color);
            if (event.phase == ta::SkillVisualPhase::Spawn) chevrons(renderer, x, y, 24, color, 3);
            break;
        case ta::SkillId::ForwardBarracks:
            chamferOutline(renderer, x - 24, y - 18, 48, 36, color, 6, 2);
            line(renderer, x - 12, y - 18, x - 12, y - 28, color, 2);
            line(renderer, x + 12, y - 18, x + 12, y - 28, color, 2);
            if (event.phase == ta::SkillVisualPhase::Spawn) chevrons(renderer, x, y + 22, 18, color, 2);
            break;
        case ta::SkillId::RuinHex:
            brokenRing(renderer, x, y, pulse, color, 2, 6);
            hexagon(renderer, x, y, std::max(10, pulse / 4), color, 1);
            if (result) { line(renderer, x - 14, y - 14, x - 4, y - 4, neo::Text, 2); line(renderer, x + 4, y + 4, x + 14, y + 14, neo::Text, 2); }
            if (event.branchId == "withering") chevrons(renderer, x, y + 18, 18, color, 3);
            break;
        case ta::SkillId::RallyBeacon:
            ring(renderer, x, y, pulse, color, 2);
            plusGlyph(renderer, x, y, std::max(8, pulse / 4), neo::Text, 2);
            for (int index = 0; index < 4; ++index) line(renderer, x, y - pulse / 2, x, y - pulse / 2 - 8, color, 1);
            if (event.branchId == "war_cry") chevrons(renderer, x, y, 22, neo::Amber, 3);
            break;
        case ta::SkillId::SentryFabricator:
            filledHexagon(renderer, x, y, std::min(25, pulse / 2), color);
            hexagon(renderer, x, y, std::min(31, pulse / 2 + 7), neo::Text, 2);
            line(renderer, x, y, x + 18, y - 8, color, 2);
            if (event.branchId == "mortar") line(renderer, x, y, x + 10, y - 18, neo::Amber, 3);
            break;
        case ta::SkillId::CryoField:
            brokenRing(renderer, x, y, pulse, color, 2, 6);
            for (int index = 0; index < 6; ++index) {
                const float radians = static_cast<float>(index) * 3.14159265f / 3.0f;
                line(renderer, x + static_cast<int>(std::cos(radians) * (pulse / 3)), y + static_cast<int>(std::sin(radians) * (pulse / 3)), x + static_cast<int>(std::cos(radians) * pulse), y + static_cast<int>(std::sin(radians) * pulse), color, 1);
            }
            break;
        case ta::SkillId::DroneSwarm:
            brokenRing(renderer, x, y, pulse, color, 1, 4);
            for (int index = 0; index < 3; ++index) { circle(renderer, x + (index - 1) * 14, y + (index % 2) * 10 - 5, 5, color); line(renderer, x + (index - 1) * 14 - 7, y + (index % 2) * 10 - 5, x + (index - 1) * 14 + 7, y + (index % 2) * 10 - 5, color, 1); }
            if (event.branchId == "hunter") cross(renderer, x, y, 20, neo::Amber, 1);
            break;
        case ta::SkillId::ResonancePulse:
            brokenRing(renderer, x, y, pulse, color, 2, 4);
            brokenRing(renderer, x, y, std::max(8, pulse - 18), neo::Cyan, 1, 4);
            line(renderer, x - 18, y, x - 9, y - 8, color, 2); line(renderer, x - 9, y - 8, x, y + 8, color, 2); line(renderer, x, y + 8, x + 9, y - 8, color, 2); line(renderer, x + 9, y - 8, x + 18, y, color, 2);
            break;
        case ta::SkillId::Count: break;
    }
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

void drawSkillGlyph(SDL_Renderer* renderer, const ta::SkillSnapshot& snapshot, int cx, int cy, Color color) {
    switch (snapshot.skill) {
        case ta::SkillId::GravityWell: circle(renderer, cx, cy, 15, color); circle(renderer, cx, cy, 5, color); break;
        case ta::SkillId::PhaseMine: filledHexagon(renderer, cx, cy, 15, color); hexagon(renderer, cx, cy, 8, neo::Void, 2); break;
        case ta::SkillId::VanguardDrop: filledDiamond(renderer, cx, cy, 15, color); break;
        case ta::SkillId::ForwardBarracks: chamferOutline(renderer, cx - 14, cy - 12, 28, 24, color, 5, 2); break;
        case ta::SkillId::RuinHex: hexagon(renderer, cx, cy, 15, color, 2); break;
        case ta::SkillId::RallyBeacon: circle(renderer, cx, cy, 14, color); line(renderer, cx - 9, cy, cx + 9, cy, color, 2); line(renderer, cx, cy - 9, cx, cy + 9, color, 2); break;
        case ta::SkillId::SentryFabricator: filledHexagon(renderer, cx, cy, 14, color); line(renderer, cx - 9, cy, cx + 9, cy, neo::Void, 2); break;
        case ta::SkillId::CryoField: circle(renderer, cx, cy, 15, color); line(renderer, cx - 10, cy, cx + 10, cy, color, 1); line(renderer, cx, cy - 10, cx, cy + 10, color, 1); break;
        case ta::SkillId::DroneSwarm: circle(renderer, cx - 7, cy, 6, color); circle(renderer, cx + 7, cy, 6, color); circle(renderer, cx, cy - 8, 5, color); break;
        case ta::SkillId::ResonancePulse: circle(renderer, cx, cy, 15, color); circle(renderer, cx, cy, 8, neo::Void); circle(renderer, cx, cy, 5, color); break;
        case ta::SkillId::Count: break;
    }
    if (!snapshot.branchId.empty()) hexagon(renderer, cx, cy, 20, neo::Text, 1);
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
    for (std::size_t slot = 0; slot < ta::SkillSlotCount; ++slot) {
        const UiRect card = skillSlotButton(static_cast<int>(slot));
        const ta::SkillSnapshot snapshot = sim.skillSnapshot(slot);
        const Color accent = chips[slot % chips.size()];
        neoPanel(renderer, card.x, card.y, card.width, card.height, snapshot.cooldownRemaining == 0 && snapshot.charges > 0 ? accent : neo::Muted, false, 8);
        drawSkillGlyph(renderer, snapshot, card.x + 90, card.y + 38, accent);
        drawTextFitInBox(renderer, "hud.skill.name", {card.x + 8, card.y + 7, card.width - 16, 14}, std::to_string(slot + 1) + " // " + std::string(ta::skillName(snapshot.skill)), 1, neo::Text);
        drawTextFitInBox(renderer, "hud.skill.mode", {card.x + 8, card.y + 29, card.width - 16, 14}, std::string(ta::skillTargetModeName(snapshot.targetMode)) + " // " + (snapshot.branchId.empty() ? "BASE" : snapshot.branchId), 1, neo::Muted);
        const std::string cooldown = snapshot.cooldownRemaining > 0 ? (std::to_string(snapshot.cooldownRemaining / GameSim::TickRate) + "s") : "READY";
        drawTextFitInBox(renderer, "hud.skill.cooldown", {card.x + 8, card.y + 50, card.width - 16, 14}, cooldown + "  " + std::to_string(snapshot.charges) + "/" + std::to_string(snapshot.maximumCharges), 1, snapshot.cooldownRemaining == 0 ? accent : neo::Amber);
    }
    neoPanel(renderer, ultimateSkillButton.x, ultimateSkillButton.y, ultimateSkillButton.width, ultimateSkillButton.height, neo::Violet, false, 8);
    drawText(renderer, ultimateSkillButton.x + 10, ultimateSkillButton.y + 9, "ULTIMATE", 1, neo::Muted);
    drawText(renderer, ultimateSkillButton.x + 10, ultimateSkillButton.y + 30, ta::ultimateName(sim.ultimate()), 1, neo::Violet);
    drawText(renderer, ultimateSkillButton.x + 10, ultimateSkillButton.y + 53, sim.ultimateRatio() >= 1.0f ? "READY // SPACE" : std::to_string(static_cast<int>((1.0f - sim.ultimateRatio()) * 18.0f)) + "s", 1, neo::Text);
    std::string status = sim.statusText();
    for (const ta::Enemy& enemy : sim.enemies()) if (enemy.boss && enemy.telegraphTicks > 0) status = "BOSS ATTACK INCOMING";
    if (subtitles) drawText(renderer, 24, 700, status, 1, neo::Muted);
}

void drawSkillTargetPreview(SDL_Renderer* renderer, const GameSim& sim, int slot, int pointerX, int pointerY) {
    if (slot < 0 || slot >= static_cast<int>(ta::SkillSlotCount)) return;
    const ta::SkillSnapshot snapshot = sim.skillSnapshot(static_cast<std::size_t>(slot));
    if (snapshot.targetMode == ta::SkillTargetMode::None) return;
    const float worldX = static_cast<float>(pointerX) > static_cast<float>(GameSim::Width) ? static_cast<float>(pointerX) / GameSim::WorldScale : static_cast<float>(pointerX);
    const float worldY = static_cast<float>(pointerY) > static_cast<float>(GameSim::Height) ? static_cast<float>(pointerY) / GameSim::WorldScale : static_cast<float>(pointerY);
    const int x = std::clamp(static_cast<int>(worldX), 92, GameSim::Width - 40);
    const int y = std::clamp(static_cast<int>(worldY), 24, GameSim::Height - 24);
    const Color color = snapshot.cooldownRemaining == 0 && snapshot.charges > 0 ? skillAccent(snapshot.skill) : neo::Red;
    const auto& definitions = sim.contentConfig().skillDefinitions;
    const float authoredRadius = static_cast<std::size_t>(snapshot.skill) < definitions.size() ? definitions[static_cast<std::size_t>(snapshot.skill)].radius : 100.0f;
    const int targetRadius = std::max(24, static_cast<int>(authoredRadius));
    if (snapshot.skill == ta::SkillId::PhaseMine || snapshot.skill == ta::SkillId::RuinHex || snapshot.skill == ta::SkillId::ResonancePulse) brokenRing(renderer, x, y, targetRadius, color, 2, snapshot.skill == ta::SkillId::PhaseMine ? 6 : 4);
    else if (snapshot.skill == ta::SkillId::CryoField) drawSkillVisualEvent(renderer, {0, snapshot.skill, ta::SkillVisualPhase::Cast, {static_cast<float>(x), static_cast<float>(y)}, static_cast<float>(targetRadius), 10, snapshot.branchId});
    else ring(renderer, x, y, snapshot.targetMode == ta::SkillTargetMode::Placement ? targetRadius / 2 : targetRadius, color, 2);
    line(renderer, x - 12, y, x + 12, y, color, 1);
    line(renderer, x, y - 12, x, y + 12, color, 1);
    drawText(renderer, x + 16, y - 18, snapshot.cooldownRemaining == 0 ? "CONFIRM" : "COOLDOWN", 1, color);
    neoPanel(renderer, skillTargetCancelButton.x, skillTargetCancelButton.y, skillTargetCancelButton.width, skillTargetCancelButton.height, neo::Red, false, 5);
    drawText(renderer, skillTargetCancelButton.x + 18, skillTargetCancelButton.y + 12, "CANCEL TARGET", 1, neo::Text);
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
    for (const ta::SkillVisualEvent& event : sim.skillVisualEvents()) drawSkillVisualEvent(renderer, event);
    for (const ta::SkillZone& zone : sim.skillZones()) {
        const int x = static_cast<int>(zone.center.x);
        const int y = static_cast<int>(zone.center.y);
        const int radius = static_cast<int>(zone.radius);
        const Color color = skillAccent(zone.ownerSkill);
        if (zone.ownerSkill == ta::SkillId::GravityWell) { brokenRing(renderer, x, y, radius, color, 2, 3); ring(renderer, x, y, std::max(8, radius / 3), neo::Text, 1); for (int index = 0; index < 4; ++index) line(renderer, x + (index - 2) * 16, y - radius / 2, x, y, color, 1); }
        else if (zone.ownerSkill == ta::SkillId::PhaseMine) { brokenRing(renderer, x, y, radius, color, 1, 6); hexagon(renderer, x, y, 12, color, 2); cross(renderer, x, y, 7, color, 1); }
        else if (zone.ownerSkill == ta::SkillId::CryoField) drawSkillVisualEvent(renderer, {0, zone.ownerSkill, ta::SkillVisualPhase::Cast, zone.center, zone.radius, 10, {}});
        else if (zone.ownerSkill == ta::SkillId::ResonancePulse) drawSkillVisualEvent(renderer, {0, zone.ownerSkill, ta::SkillVisualPhase::Cast, zone.center, zone.radius, 10, {}});
        else ring(renderer, x, y, radius, color, 2);
    }
    for (const ta::DeployableBuilding& building : sim.deployableBuildings()) {
        const int x = static_cast<int>(building.pos.x);
        const int y = static_cast<int>(building.pos.y);
        const Color color = building.ownerSkill == ta::SkillId::SentryFabricator ? neo::Cyan : neo::Mint;
        if (building.ownerSkill == ta::SkillId::SentryFabricator) {
            filledHexagon(renderer, x, y, 22, color);
            hexagon(renderer, x, y, 30, neo::Text, 2);
            line(renderer, x, y, x + (building.role == "mortar" ? 10 : 18), y - (building.role == "mortar" ? 18 : 8), building.role == "mortar" ? neo::Amber : color, 3);
            line(renderer, x - 16, y + 18, x - 22, y + 26, color, 2);
            line(renderer, x + 16, y + 18, x + 22, y + 26, color, 2);
        } else {
            chamferOutline(renderer, x - 24, y - 18, 48, 36, color, 6, 2);
            line(renderer, x - 12, y - 18, x - 12, y - 28, color, 2);
            line(renderer, x + 12, y - 18, x + 12, y - 28, color, 2);
            line(renderer, x - 8, y + 4, x + 8, y + 4, color, 2);
            if (building.role == "armory") line(renderer, x - 16, y - 8, x + 16, y - 8, neo::Amber, 2);
        }
        segmentedBar(renderer, x - 24, y - 35, 48, 5, building.maxHp > 0.0f ? building.hp / building.maxHp : 0.0f, neo::Mint, 4);
    }
    for (const ta::AlliedUnit& unit : sim.alliedUnits()) {
        const int x = static_cast<int>(unit.pos.x);
        const int y = static_cast<int>(unit.pos.y);
        const Color color = unit.role == "drone" || unit.role == "disruptor" ? neo::Cyan : (unit.role == "striker" ? neo::Amber : neo::Mint);
        if (unit.role == "drone" || unit.role == "disruptor") { circle(renderer, x, y, std::max(5, static_cast<int>(unit.radius) - 2), color); line(renderer, x - 8, y, x + 8, y, color, 1); }
        else { filledDiamond(renderer, x, y, static_cast<int>(unit.radius), color); line(renderer, x + static_cast<int>(unit.radius) / 2, y, x + static_cast<int>(unit.radius) + 5, y, unit.role == "striker" ? neo::Amber : color, 2); }
        if (unit.role == "bulwark") { brokenRing(renderer, x, y, static_cast<int>(unit.radius) + 5, neo::Text, 1, 3); }
        if (unit.buffTicks > 0) chevrons(renderer, x, y, static_cast<int>(unit.radius) + 7, neo::Violet, 2);
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
        if (enemy.vulnerabilityTicks > 0) { brokenRing(renderer, x, y, radius + 8, neo::Magenta, 1, enemy.stun > 0.0f ? 6 : 4); if (enemy.vulnerability > 0.35f) { line(renderer, x - radius - 3, y - radius - 3, x - 3, y - 3, neo::Text, 1); line(renderer, x + 3, y + 3, x + radius + 3, y + radius + 3, neo::Text, 1); } }
        if (enemy.slow > 0.0f) line(renderer, x - radius, y + radius + 4, x + radius, y + radius + 4, neo::Ice, 2);
        if (enemy.stun > 0.0f) hexagon(renderer, x, y, radius + 6, neo::Ice, 2);
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

void drawMenuButton(SDL_Renderer* renderer, const UiRect& bounds, const char* label, Color accent, bool primary = false) {
    neoPanel(renderer, bounds.x, bounds.y, bounds.width, bounds.height, accent, primary, 10);
    const int labelWidth = static_cast<int>(std::string(label).size()) * 6;
    drawText(renderer, bounds.x + std::max(12, (bounds.width - labelWidth) / 2), bounds.y + 22, label, 2, primary ? neo::Void : neo::Text);
}

void drawFocusOutline(SDL_Renderer* renderer, const UiRect& bounds, Color accent) {
    chamferOutline(renderer, bounds.x - 4, bounds.y - 4, bounds.width + 8, bounds.height + 8, accent, 10, 2);
}

void drawMainMenu(SDL_Renderer* renderer, const ta::ProfileData& profile, const ta::DailyChallenge& daily, int focus) {
    rect(renderer, 0, 0, GameSim::Width, GameSim::Height, neo::Void);
    chamferOutline(renderer, 96, 56, 1088, 606, {neo::Cyan.r, neo::Cyan.g, neo::Cyan.b, 80}, 22, 1);
    drawText(renderer, 440, 104, "TOWER ASCEND", 4, neo::Text);
    drawText(renderer, 476, 156, "NEO DEFENSE // COMMAND DECK", 1, neo::Cyan);
    neonDivider(renderer, 250, 190, 780, neo::Cyan);
    drawText(renderer, 430, 222, "WELCOME COMMANDER", 2, neo::Muted);
    drawMenuButton(renderer, mainStartButton, "START RUN", neo::Mint, true);
    drawMenuButton(renderer, mainWorkshopButton, "WORKSHOP", neo::Amber);
    drawMenuButton(renderer, mainCollectionButton, "COLLECTION", neo::Violet);
    drawMenuButton(renderer, mainSettingsButton, "SETTINGS", neo::Cyan);
    drawMenuButton(renderer, mainQuitButton, "QUIT", neo::Red);
    const std::array<UiRect, 5> focusRects{{mainStartButton, mainWorkshopButton, mainCollectionButton, mainSettingsButton, mainQuitButton}};
    if (focus >= 0 && focus < static_cast<int>(focusRects.size())) drawFocusOutline(renderer, focusRects[static_cast<std::size_t>(focus)], neo::Text);
    neoPanel(renderer, 160, 580, 960, 38, neo::Blue, false, 7);
    drawText(renderer, 184, 592, "SHARDS " + std::to_string(profile.cosmeticShards), 1, neo::Amber);
    drawText(renderer, 380, 592, "CORE PARTS " + std::to_string(profile.coreParts), 1, neo::Cyan);
    drawText(renderer, 650, 592, "LEGEND CORES " + std::to_string(profile.legendCores), 1, neo::Violet);
    drawText(renderer, 920, 592, "TODAY " + daily.title, 1, neo::Text);
    neoPanel(renderer, 160, 620, 960, 38, neo::Void, false, 7);
    drawText(renderer, 184, 632, "BEST WAVE " + std::to_string(profile.bestWave), 1, neo::Mint);
    drawText(renderer, 380, 632, "BEST SCORE " + std::to_string(profile.bestScore), 1, neo::Amber);
    drawText(renderer, 650, 632, "RUNS " + std::to_string(profile.runsCompleted), 1, neo::Cyan);
    drawText(renderer, 920, 632, "TOWER " + std::string(ta::chassisName(static_cast<ta::TowerChassis>(profile.equippedChassis))), 1, neo::Violet);
}

void drawRunTypeSelect(SDL_Renderer* renderer, const ta::DailyChallenge& daily, const ta::ProfileData& profile, const ta::ContentConfig& content, int focus) {
    rect(renderer, 0, 0, GameSim::Width, GameSim::Height, neo::Void);
    drawText(renderer, 420, 110, "SELECT RUN TYPE", 3, neo::Text);
    drawText(renderer, 430, 158, "EVERY RUN IS A DIFFERENT TEST OF THE TOWER", 1, neo::Muted);
    neonDivider(renderer, 240, 194, 800, neo::Cyan);
    neoPanel(renderer, runStandardPanel.x, runStandardPanel.y, runStandardPanel.width, runStandardPanel.height, neo::Mint, false, 10);
    neoPanel(renderer, runDailyPanel.x, runDailyPanel.y, runDailyPanel.width, runDailyPanel.height, neo::Violet, false, 10);
    neoPanel(renderer, runEndlessPanel.x, runEndlessPanel.y, runEndlessPanel.width, runEndlessPanel.height, neo::Amber, false, 10);
    drawMenuButton(renderer, runStandardButton, "STANDARD", neo::Mint, true);
    drawMenuButton(renderer, runDailyButton, "DAILY", neo::Violet);
    drawMenuButton(renderer, runEndlessButton, "ENDLESS", neo::Amber);
    drawMenuButton(renderer, runTypeBackButton, "BACK", neo::Cyan);
    const std::array<UiRect, 4> focusRects{{runStandardButton, runDailyButton, runEndlessButton, runTypeBackButton}};
    if (focus >= 0 && focus < static_cast<int>(focusRects.size())) drawFocusOutline(renderer, focusRects[static_cast<std::size_t>(focus)], neo::Text);
    const auto runFactsLine = [&content](int index) {
        return std::to_string(content.runExpectedMinutes[static_cast<std::size_t>(index)]) + " MIN // " +
               (content.runWaveLimit[static_cast<std::size_t>(index)] == 0 ? std::string("ENDLESS") : std::to_string(content.runWaveLimit[static_cast<std::size_t>(index)])) +
               " WAVES // X" + std::to_string(content.runRewardMultiplier[static_cast<std::size_t>(index)]).substr(0, 3) + " REWARD";
    };
    drawTextFitInBox(renderer, "run.standard.description", UiRect{250, 306, 340, 16}, content.runTypeMetadata[0].description, 1, neo::Text);
    drawTextFitInBox(renderer, "run.standard.rules", UiRect{250, 325, 340, 16}, content.runTypeMetadata[0].rules, 1, neo::Muted);
    drawTextFitInBox(renderer, "run.standard.facts", UiRect{250, 344, 340, 16}, runFactsLine(0), 1, neo::Cyan);
    drawTextFitInBox(renderer, "run.endless.description", UiRect{710, 306, 340, 16}, content.runTypeMetadata[2].description, 1, neo::Text);
    drawTextFitInBox(renderer, "run.endless.rules", UiRect{710, 325, 340, 16}, content.runTypeMetadata[2].rules, 1, neo::Muted);
    drawTextFitInBox(renderer, "run.endless.facts", UiRect{710, 344, 340, 16}, runFactsLine(2), 1, neo::Amber);
    drawTextFitInBox(renderer, "run.daily.description", UiRect{250, 456, 340, 16}, content.runTypeMetadata[1].description, 1, neo::Text);
    drawTextFitInBox(renderer, "run.daily.rules", UiRect{250, 475, 340, 16}, content.runTypeMetadata[1].rules, 1, neo::Muted);
    drawTextFitInBox(renderer, "run.daily.facts", UiRect{250, 494, 340, 16}, runFactsLine(1), 1, neo::Violet);
    neoPanel(renderer, dailyBriefingCard.x, dailyBriefingCard.y, dailyBriefingCard.width, dailyBriefingCard.height, neo::Violet, false, 10);
    drawTextFitInBox(renderer, "run.dailyBriefing.title", UiRect{168, 536, 944, 16}, daily.title, 2, neo::Violet);
    drawTextFitInBox(renderer, "run.dailyBriefing.description", UiRect{168, 558, 944, 16}, daily.description, 1, neo::Text);
    const std::string dailyChassis = daily.chassisRequired ? std::string("CHASSIS: ") + ta::chassisName(daily.requiredChassis) : "CHASSIS: OPEN";
    const std::string dailyWeapon = daily.weaponRequired ? std::string("WEAPON: ") + ta::weaponName(daily.requiredWeapon) : "WEAPON: OPEN COUNTERBUILD";
    drawTextFitInBox(renderer, "run.dailyBriefing.loadout", UiRect{168, 580, 944, 16}, daily.loadoutRule + " // " + dailyChassis + " // " + dailyWeapon, 1, neo::Cyan);
    drawTextFitInBox(renderer, "run.dailyBriefing.ultimate", UiRect{168, 598, 944, 16}, std::string("ULTIMATE: ") + ta::ultimateName(daily.requiredUltimate) + " // SUPPORT: " + ta::supportModuleName(daily.requiredSupport), 1, neo::Cyan);
    drawTextFitInBox(renderer, "run.dailyBriefing.skills", UiRect{168, 616, 944, 16}, daily.skillSummary + " // " + daily.skullSummary + " // LOANED IF NOT OWNED", 1, neo::Violet);
    drawTextFitInBox(renderer, "run.dailyBriefing.mission", UiRect{168, 634, 944, 16}, "MISSION // 10 WAVES // " + daily.objective + " // REWARD " + std::to_string(daily.legendCoreReward) + " LC + " + std::to_string(daily.bonusShards) + " SHARDS", 1, neo::Amber);
    drawTextFitInBox(renderer, "run.dailyBriefing.modifier", UiRect{168, 652, 944, 16}, daily.modifierSummary + " // " + daily.modifierDescription, 1, neo::Muted);
    drawTextFitInBox(renderer, "run.dailyBriefing.threat", UiRect{168, 670, 944, 16}, daily.threatSummary + " // " + daily.recommendedUpgradeTags + " // WORKSHOP " + (daily.workshopNormalized ? "NORMALIZED" : "ACTIVE"), 1, neo::Red);
}

int dailyBriefingDetailAt(const ta::DailyChallenge& daily, int x, int y) {
    for (std::size_t index = 0; index < daily.enemyRoster.size(); ++index) {
        if (dailyEnemyBriefingRow(static_cast<int>(index)).contains(x, y)) return static_cast<int>(index) + 1;
    }
    int skullSlot = 0;
    for (int index = 1; index <= 4; ++index) {
        const ta::Skull skull = static_cast<ta::Skull>(index);
        if ((daily.skullMask & (static_cast<ta::SkullMask>(1u) << static_cast<unsigned int>(skull))) == 0) continue;
        if (dailySkullBriefingRow(skullSlot).contains(x, y)) return static_cast<int>(daily.enemyRoster.size()) + skullSlot + 1;
        ++skullSlot;
    }
    return 0;
}

int dailyBriefingDetailCount(const ta::DailyChallenge& daily) {
    int count = static_cast<int>(daily.enemyRoster.size());
    for (int index = 1; index <= 4; ++index) if ((daily.skullMask & (static_cast<ta::SkullMask>(1u) << static_cast<unsigned int>(index))) != 0) ++count;
    return count;
}

void drawDailyBriefingOverlay(SDL_Renderer* renderer, const ta::DailyChallenge& daily, const ta::ContentConfig& content, const ta::ProfileData& profile, int hoverX, int hoverY, int detailFocus) {
    chamferFill(renderer, 90, 74, 1100, 590, {neo::Void.r, neo::Void.g, neo::Void.b, 248}, 18);
    chamferOutline(renderer, 90, 74, 1100, 590, neo::Violet, 18, 2);
    drawText(renderer, 150, 100, daily.title + " // FULL BRIEFING", 3, neo::Violet);
    drawText(renderer, 150, 142, "ESC / I CLOSES DETAILS // CONFIRM IS OUTSIDE THIS PANEL", 1, neo::Muted);
    neonDivider(renderer, 150, 168, 980, neo::Violet);
    drawWrappedTextInBox(renderer, "daily.longDescription", UiRect{140, 180, 980, 52}, daily.longDescription, 1, neo::Text, 10, 18);
    drawText(renderer, 150, 246, "THREAT PROFILE", 2, neo::Red);
    drawTextFitInBox(renderer, "daily.overlay.threat", {150, 278, 540, 14}, daily.threatSummary, 1, neo::Text);
    drawTextFitInBox(renderer, "daily.overlay.seek", {150, 304, 540, 14}, "SEEK " + daily.recommendedUpgradeTags, 1, neo::Cyan);
    drawText(renderer, 150, 346, "ENEMY ROSTER", 2, neo::Amber);
    if (daily.enemyRoster.empty()) {
        drawText(renderer, 150, 378, "UNSPECIFIED", 1, neo::Text);
    } else {
        int rosterY = 374;
        for (std::size_t rosterIndex = 0; rosterIndex < daily.enemyRoster.size(); ++rosterIndex) {
            const ta::EnemyType enemy = daily.enemyRoster[rosterIndex];
            const std::size_t enemyIndex = static_cast<std::size_t>(enemy);
            const ta::ContentMetadata& metadata = content.enemyMetadata[enemyIndex];
            const std::string prevalence = enemy == ta::EnemyType::Boss ? "BOSS" : (rosterIndex < daily.enemyPrevalence.size() ? daily.enemyPrevalence[rosterIndex] : (daily.enemyRoster.size() == 1 ? "FREQUENT" : "COMMON"));
            const std::string label = "[" + prevalence + "] " + (metadata.display.empty() ? ta::enemyTypeName(enemy) : metadata.display);
            const int detailIndex = static_cast<int>(rosterIndex) + 1;
            const bool focused = detailIndex == detailFocus || (detailFocus == 0 && dailyEnemyBriefingRow(static_cast<int>(rosterIndex)).contains(hoverX, hoverY));
            if (focused) drawFocusOutline(renderer, dailyEnemyBriefingRow(static_cast<int>(rosterIndex)), neo::Amber);
            filledDiamond(renderer, 158, rosterY + 6, 6, focused ? neo::Text : neo::Amber);
            drawTextFitInBox(renderer, "daily.overlay.enemy.name", {178, rosterY, 190, 14}, label, 1, neo::Amber);
            drawTextFitInBox(renderer, "daily.overlay.enemy.description", {380, rosterY, 470, 14}, metadata.shortDescription.empty() ? "AUTHORED ENEMY PROFILE" : metadata.shortDescription, 1, neo::Text);
            rosterY += 22;
        }
    }
    drawText(renderer, 150, 444, "ACTIVE SKULLS // DAILY MODIFIERS", 2, neo::Violet);
    std::vector<ta::Skull> activeSkulls;
    for (int index = 1; index <= 4; ++index) {
        const ta::Skull skull = static_cast<ta::Skull>(index);
        if ((daily.skullMask & (static_cast<ta::SkullMask>(1u) << static_cast<unsigned int>(skull))) == 0) continue;
        activeSkulls.push_back(skull);
    }
    if (activeSkulls.empty()) drawText(renderer, 150, 476, "NONE", 1, neo::Text);
    else for (std::size_t slot = 0; slot < activeSkulls.size(); ++slot) {
        const ta::Skull skull = activeSkulls[slot];
        const ta::ContentMetadata& metadata = content.skullMetadata[static_cast<std::size_t>(skull)];
        const int detailIndex = static_cast<int>(daily.enemyRoster.size() + slot + 1);
        const bool focused = detailIndex == detailFocus || (detailFocus == 0 && dailySkullBriefingRow(static_cast<int>(slot)).contains(hoverX, hoverY));
        if (focused) drawFocusOutline(renderer, dailySkullBriefingRow(static_cast<int>(slot)), neo::Violet);
        filledHexagon(renderer, 158, 476 + static_cast<int>(slot) * 22 + 6, 6, focused ? neo::Text : neo::Violet);
        drawTextFitInBox(renderer, "daily.overlay.skull", {178, 476 + static_cast<int>(slot) * 22, 650, 14}, std::string(ta::skullName(skull)) + " // " + (metadata.shortDescription.empty() ? ta::skullDescription(skull) : metadata.shortDescription), 1, neo::Text);
    }
    int selectedDetail = detailFocus;
    if (selectedDetail == 0) selectedDetail = dailyBriefingDetailAt(daily, hoverX, hoverY);
    if (selectedDetail > 0) {
        const int enemyCount = static_cast<int>(daily.enemyRoster.size());
        const ta::ContentMetadata* detailMetadata = nullptr;
        std::string detailTitle;
        if (selectedDetail <= enemyCount) {
            const ta::EnemyType enemy = daily.enemyRoster[static_cast<std::size_t>(selectedDetail - 1)];
            detailMetadata = &content.enemyMetadata[static_cast<std::size_t>(enemy)];
            detailTitle = detailMetadata->display.empty() ? ta::enemyTypeName(enemy) : detailMetadata->display;
        } else {
            int skullIndex = selectedDetail - enemyCount - 1;
            for (int index = 1; index <= 4; ++index) {
                const ta::Skull skull = static_cast<ta::Skull>(index);
                if ((daily.skullMask & (static_cast<ta::SkullMask>(1u) << static_cast<unsigned int>(skull))) == 0) continue;
                if (skullIndex-- == 0) { detailMetadata = &content.skullMetadata[static_cast<std::size_t>(skull)]; detailTitle = ta::skullName(skull); break; }
            }
        }
        if (detailMetadata != nullptr) {
            neoPanel(renderer, 720, 330, 400, 112, neo::Cyan, true, 8);
            drawText(renderer, 740, 344, "DETAIL // " + detailTitle, 1, neo::Cyan);
            drawWrappedTextInBox(renderer, "daily.detailDescription", UiRect{730, 358, 380, 54}, detailMetadata->longDescription.empty() ? detailMetadata->shortDescription : detailMetadata->longDescription, 1, neo::Text, 10, 18);
            drawText(renderer, 740, 420, "FOCUS // ARROWS OR HOVER // TAP TO INSPECT", 1, neo::Muted);
        }
    }
    drawTextFitInBox(renderer, "daily.overlay.objective", {150, 526, 940, 14}, "OBJECTIVE // " + daily.objective + " // MUTATOR // " + daily.modifierSummary + " // " + daily.modifierDescription, 1, neo::Mint);
    const bool evolutionLoaned = daily.requiredEvolution != ta::UltimateEvolution::None && !ta::isUltimateEvolutionUnlocked(profile, daily.requiredEvolution);
    const std::string evolutionText = daily.requiredEvolution == ta::UltimateEvolution::None ? "OPEN EVOLUTION" : std::string(ta::ultimateEvolutionName(daily.requiredEvolution)) + (evolutionLoaned ? " // LOANED FOR DAILY" : " // OWNED");
    drawTextFitInBox(renderer, "daily.overlay.setup", {150, 550, 940, 14}, "ARENA // " + std::string(ta::arenaName(daily.arena)) + " // SETUP // " + (daily.chassisRequired ? std::string(ta::chassisName(daily.requiredChassis)) : "OPEN CHASSIS") + " // " + (daily.weaponRequired ? std::string(ta::weaponName(daily.requiredWeapon)) : "OPEN WEAPON") + " // " + ta::ultimateName(daily.requiredUltimate) + " // " + ta::supportModuleName(daily.requiredSupport) + " // " + evolutionText, 1, neo::Cyan);
    drawTextFitInBox(renderer, "daily.overlay.skills", {150, 574, 940, 14}, daily.skillSummary + " // LOANED IF NOT OWNED", 1, neo::Violet);
    drawTextFitInBox(renderer, "daily.overlay.mission", {150, 598, 940, 14}, "MISSION // 10 WAVES // FINAL BOSS // SCALE " + std::to_string(daily.waveBudgetScale).substr(0, 4) + " // REWARD " + std::to_string(daily.legendCoreReward) + " LEGEND CORES + " + std::to_string(daily.bonusShards) + " SHARDS", 1, neo::Amber);
    chamferFill(renderer, dailyBriefingCloseButton.x, dailyBriefingCloseButton.y, dailyBriefingCloseButton.width, dailyBriefingCloseButton.height, neo::Violet, 8);
    chamferOutline(renderer, dailyBriefingCloseButton.x, dailyBriefingCloseButton.y, dailyBriefingCloseButton.width, dailyBriefingCloseButton.height, neo::Text, 8, 1);
    drawText(renderer, 850, 606, "CLOSE BRIEFING", 1, neo::Void);
}

void drawWorkshopScreen(SDL_Renderer* renderer, const ta::ProfileData& profile, ta::Ultimate ultimate, const ta::ContentConfig& content) {
    rect(renderer, 0, 0, GameSim::Width, GameSim::Height, neo::Void);
    drawText(renderer, 410, 100, "WORKSHOP", 3, neo::Text);
    drawText(renderer, 408, 146, "PERMANENT SYSTEMS // BUILD YOUR FUTURE", 1, neo::Muted);
    neonDivider(renderer, 180, 180, 920, neo::Amber);
    neoPanel(renderer, 190, 220, 270, 170, neo::Cyan, profile.towerCoreLevel > 0, 12);
    neoPanel(renderer, 505, 220, 270, 170, neo::Amber, false, 12);
    neoPanel(renderer, 820, 220, 270, 170, neo::Violet, false, 12);
    drawText(renderer, 230, 248, "TOWER CORE", 2, neo::Cyan);
    drawText(renderer, 545, 248, "MODULES", 2, neo::Amber);
    drawText(renderer, 860, 248, "ULTIMATE EVOLUTIONS", 1, neo::Violet);
    drawText(renderer, 230, 294, "CHASSIS LEVEL " + std::to_string(profile.towerCoreLevel), 1, neo::Text);
        drawText(renderer, 230, 322, "NEXT COST " + std::to_string(ta::workshopTowerCost(profile, content)), 1, neo::Amber);
    drawTextFitInBox(renderer, "workshop.towerDescription", UiRect{230, 346, 210, 16}, content.workshopMetadata[0].shortDescription, 1, neo::Muted);
    drawText(renderer, 545, 294, "WEAPON BRANCHES", 1, neo::Text);
    drawText(renderer, 545, 322, "SELECT BELOW TO UPGRADE", 1, neo::Muted);
    drawText(renderer, 860, 264, std::string(ta::ultimateName(ultimate)), 1, neo::Violet);
    drawText(renderer, 860, 282, "MASTERY " + std::to_string(profile.ultimateMasteryRuns[static_cast<std::size_t>(ultimate)]) + "/3 COMPLETED RUNS", 1, neo::Amber);
    for (int slot = 0; slot < 3; ++slot) {
        const ta::UltimateEvolution evolution = evolutionForUltimate(ultimate, slot);
        const ta::ContentMetadata& evolutionMetadata = content.evolutionMetadata[static_cast<std::size_t>(evolution) - 1u];
        const bool unlocked = ta::isUltimateEvolutionUnlocked(profile, evolution);
        const bool equipped = profile.equippedUltimateEvolution == static_cast<std::uint8_t>(evolution);
        neoPanel(renderer, workshopUltimateButton(slot).x, workshopUltimateButton(slot).y, workshopUltimateButton(slot).width, workshopUltimateButton(slot).height, neo::Violet, equipped, 5);
        drawTextFitInBox(renderer, "workshop.evolutionName", UiRect{832, workshopUltimateButton(slot).y + 4, 160, 16}, evolutionMetadata.display.empty() ? ta::ultimateEvolutionName(evolution) : evolutionMetadata.display, 1, unlocked ? neo::Text : neo::Muted);
        drawTextFitInBox(renderer, "workshop.evolutionStatus", UiRect{998, workshopUltimateButton(slot).y + 4, 66, 16}, unlocked ? (equipped ? "EQUIPPED" : "OWNED") : std::to_string(content.ultimateEvolutionCost[static_cast<std::size_t>(evolution) - 1u]) + " LC", 1, unlocked ? neo::Cyan : neo::Amber);
    }
    for (int sidegrade = 0; sidegrade < 2; ++sidegrade) {
        const int moduleIndex = static_cast<int>(ultimate) * 2 + sidegrade;
        const ta::UltimateModule module = static_cast<ta::UltimateModule>(moduleIndex);
        const UiRect button = workshopUltimateModuleButton(sidegrade);
        const bool unlocked = ta::isUltimateModuleUnlocked(profile, module);
        const bool equipped = profile.equippedUltimateModule == static_cast<std::uint8_t>(module);
        neoPanel(renderer, button.x, button.y, button.width, button.height, neo::Amber, equipped, 3);
        drawTextFitInBox(renderer, "workshop.ultimateModuleName", UiRect{826, button.y, 168, 10}, content.ultimateModuleMetadata[static_cast<std::size_t>(moduleIndex)].display, 1, unlocked ? neo::Text : neo::Muted);
        drawTextFitInBox(renderer, "workshop.ultimateModuleStatus", UiRect{998, button.y, 66, 10}, unlocked ? (equipped ? "ON" : "OWNED") : "CORE", 1, unlocked ? neo::Cyan : neo::Amber);
    }
    const std::array<const char*, 5> moduleLabels{{"RAPID", "CANNON", "ARCANE", "FROST", "RAIL"}};
    for (int index = 0; index < 5; ++index) {
        const int x = 160 + index * 220;
        neoPanel(renderer, x, 410, 190, 52, std::array<Color, 5>{{neo::Mint, neo::Amber, neo::Violet, neo::Ice, neo::Red}}[static_cast<std::size_t>(index)], false, 7);
        drawText(renderer, x + 14, 422, std::string(moduleLabels[static_cast<std::size_t>(index)]) + " L" + std::to_string(profile.weaponModuleLevels[static_cast<std::size_t>(index)]), 1, neo::Text);
        drawText(renderer, x + 14, 442, "COST " + std::to_string(ta::workshopModuleCost(profile, static_cast<ta::Weapon>(index), content)), 1, neo::Muted);
    }
    const std::array<const char*, 5> supportLabels{{"NONE", "CREDITS", "STASIS", "REPAIR", "CORROSION"}};
    for (int index = 0; index < 5; ++index) {
        const UiRect button = workshopSupportButton(index);
        neoPanel(renderer, button.x, button.y, button.width, button.height, neo::Cyan, false, 4);
        drawTextFitInBox(renderer, "workshop.supportLabel", UiRect{button.x + 12, button.y + 7, button.width - 24, 16}, std::string(supportLabels[static_cast<std::size_t>(index)]) + " L" + std::to_string(profile.supportModuleLevels[static_cast<std::size_t>(index)]) + "  " + (index == 0 ? "FREE" : "C" + std::to_string(ta::workshopSupportCost(profile, static_cast<ta::SupportModule>(index), content))), 1, neo::Text);
    }
    drawTextFitInBox(renderer, "workshop.currencySummary", UiRect{160, 496, 930, 16}, "SHARDS " + std::to_string(profile.cosmeticShards) + " // CORE PARTS " + std::to_string(profile.coreParts) + " // LEGEND CORES " + std::to_string(profile.legendCores), 1, neo::Cyan);
    drawTextFitInBox(renderer, "workshop.presetHelp", UiRect{160, 516, 470, 16}, "SKILL PRESETS // F1-F3 EQUIP // 1-3 SAVE CURRENT LOADOUT", 1, neo::Muted);
    for (int preset = 0; preset < 3; ++preset) {
        const UiRect button = workshopPresetButton(preset);
        neoPanel(renderer, button.x, button.y, button.width, button.height, neo::Cyan, false, 4);
        drawTextFitInBox(renderer, "workshop.presetName", UiRect{button.x + 8, button.y + 7, button.width - 16, 16}, "F" + std::to_string(preset + 1) + " // " + profile.skillPresetNames[static_cast<std::size_t>(preset)], 1, neo::Text);
    }
    drawText(renderer, 160, 582, "ACTIVE SKILL TREES // GENERIC NODES BRANCH INTO SPECIALISATIONS", 1, neo::Violet);
    for (int slot = 0; slot < static_cast<int>(ta::SkillSlotCount); ++slot) {
        const UiRect button = workshopSkillButton(slot);
        const ta::SkillId skill = profile.skillLoadout.skills[static_cast<std::size_t>(slot)];
        const int nodeIndex = nextSkillNodeIndex(profile, content, slot);
        neoPanel(renderer, button.x, button.y, button.width, button.height, neo::Violet, nodeIndex >= 0, 5);
        drawTextFitInBox(renderer, "workshop.skillName", UiRect{button.x + 8, button.y + 4, button.width - 16, 16}, std::to_string(slot + 1) + " // " + ta::skillName(skill), 1, neo::Text);
        if (nodeIndex >= 0) {
            const ta::SkillNodeDefinition& node = content.skillNodes[static_cast<std::size_t>(nodeIndex)];
            drawTextFitInBox(renderer, "workshop.skillNext", UiRect{button.x + 8, button.y + 22, button.width - 16, 16}, "NEXT " + node.display, 1, neo::Cyan);
            drawText(renderer, button.x + 8, button.y + 40, "C" + std::to_string(ta::skillNodeCost(profile, node)) + " // T" + std::to_string(node.tier), 1, neo::Amber);
        } else drawText(renderer, button.x + 8, button.y + 28, "TREE COMPLETE", 1, neo::Mint);
    }
    drawTextFitInBox(renderer, "workshop.purchaseHelp", UiRect{700, 516, 390, 16}, "BUY TOWER / MODULE / SUPPORT / SKILL NODES", 1, neo::Muted);
    drawTextFitInBox(renderer, "workshop.skillUnlockHelp", UiRect{160, 674, 930, 16}, "UNLOCK SKILLS 6-9 FROM THE KEYBOARD // COSTS CORE PARTS // DAILY SKILLS ARE LOANED", 1, neo::Amber);
    drawMenuButton(renderer, workshopBackButton, "BACK", neo::Cyan);
}

void drawWorkshopSkillTree(SDL_Renderer* renderer, const ta::ProfileData& profile, int slot, const ta::ContentConfig& content) {
    if (slot < 0 || slot >= static_cast<int>(ta::SkillSlotCount)) return;
    const std::vector<int> nodes = skillTreeNodeIndices(content, slot, profile);
    chamferFill(renderer, 80, 105, 1120, 560, {neo::Void.r, neo::Void.g, neo::Void.b, 250}, 18);
    chamferOutline(renderer, 80, 105, 1120, 560, neo::Violet, 18, 2);
    drawTextFitInBox(renderer, "skillTree.title", UiRect{130, 126, 900, 18}, "SKILL TREE // " + std::string(ta::skillName(profile.skillLoadout.skills[static_cast<std::size_t>(slot)])), 2, neo::Text);
    drawTextFitInBox(renderer, "skillTree.help", UiRect{130, 156, 960, 16}, "GENERIC NODES FEED TWO EXCLUSIVE SPECIALISATIONS // CLICK OWNED NODES TO EQUIP A LEGAL BUILD", 1, neo::Muted);
    for (std::size_t local = 0; local < nodes.size(); ++local) {
        const ta::SkillNodeDefinition& node = content.skillNodes[static_cast<std::size_t>(nodes[local])];
        const UiRect button = workshopSkillTreeNodeButton(static_cast<int>(local));
        const int rank = ta::purchasedSkillNodeRank(profile, node.id);
        const bool parentOwned = node.parentId.empty() || ta::purchasedSkillNodeRank(profile, node.parentId) > 0;
        const bool active = profile.skillLoadout.nodeBuilds[static_cast<std::size_t>(slot)].find(node.id + ":") != std::string::npos;
        const Color accent = node.tier >= 2 ? neo::Violet : neo::Cyan;
        neoPanel(renderer, button.x, button.y, button.width, button.height, accent, active, 6);
        drawTextFitInBox(renderer, "skillTree.nodeName", UiRect{button.x + 10, button.y + 6, button.width - 20, 16}, node.display, 1, parentOwned ? neo::Text : neo::Muted);
        drawText(renderer, button.x + 10, button.y + 29, "T" + std::to_string(node.tier) + " // " + (rank > 0 ? "OWNED R" + std::to_string(rank) : (parentOwned ? "C" + std::to_string(ta::skillNodeCost(profile, node)) : "LOCKED")), 1, rank > 0 ? neo::Mint : (parentOwned ? neo::Amber : neo::Muted));
        drawTextFitInBox(renderer, "skillTree.nodeBranch", UiRect{button.x + 10, button.y + 48, button.width - 20, 16}, node.branchId + " // " + node.iconLayer, 1, neo::Cyan);
    }
    drawWrappedTextInBox(renderer, "workshop.skillDescription", UiRect{120, 495, 780, 64}, ta::skillDescription(profile.skillLoadout.skills[static_cast<std::size_t>(slot)]), 1, neo::Text, 10, 18);
    drawTextFitInBox(renderer, "skillTree.currentBuild", UiRect{130, 562, 760, 16}, "CURRENT BUILD // " + (profile.skillLoadout.nodeBuilds[static_cast<std::size_t>(slot)].empty() ? std::string("BASE SKILL") : profile.skillLoadout.nodeBuilds[static_cast<std::size_t>(slot)]), 1, neo::Amber);
    drawMenuButton(renderer, workshopSkillTreeCloseButton, "CLOSE TREE", neo::Cyan);
}

void drawWorkshopConfirmation(SDL_Renderer* renderer, const ta::ProfileData& profile, WorkshopPurchase purchase, int index, ta::Ultimate ultimate, const ta::ContentConfig& content) {
    chamferFill(renderer, 250, 190, 780, 410, {neo::Void.r, neo::Void.g, neo::Void.b, 248}, 18);
    chamferOutline(renderer, 250, 190, 780, 410, neo::Amber, 18, 2);
    std::string title = "CONFIRM WORKSHOP PURCHASE";
    std::string item = "";
    std::string cost = "";
    std::string change = "";
    std::string currency = "CORE PARTS";
    bool masteryReady = true;
    if (purchase == WorkshopPurchase::TowerCore) {
        item = "TOWER CORE LEVEL " + std::to_string(static_cast<int>(profile.towerCoreLevel) + 1);
        cost = std::to_string(ta::workshopTowerCost(profile, content));
        const int currentLives = static_cast<int>(profile.towerCoreLevel / 5u);
        const int nextLives = static_cast<int>((profile.towerCoreLevel + 1u) / 5u);
        change = content.workshopMetadata[0].shortDescription + " // " + (nextLives > currentLives ? "+1 MAX LIFE // TOWER INTEGRITY MILESTONE" : "NO DIRECT DAMAGE BONUS // NEXT INTEGRITY MILESTONE AT LEVEL " + std::to_string(((profile.towerCoreLevel / 5u) + 1u) * 5u));
    } else if (purchase == WorkshopPurchase::WeaponModule) {
        item = std::string(ta::weaponName(static_cast<ta::Weapon>(index))) + " MODULE LEVEL " + std::to_string(static_cast<int>(profile.weaponModuleLevels[static_cast<std::size_t>(index)]) + 1);
        cost = std::to_string(ta::workshopModuleCost(profile, static_cast<ta::Weapon>(index), content));
        change = content.workshopMetadata[static_cast<std::size_t>(1 + index)].shortDescription + " // +1% PRIMARY WEAPON DAMAGE // THIS MODULE ONLY";
    } else if (purchase == WorkshopPurchase::SupportModule) {
        item = std::string(ta::supportModuleName(static_cast<ta::SupportModule>(index))) + " LEVEL " + std::to_string(static_cast<int>(profile.supportModuleLevels[static_cast<std::size_t>(index)]) + 1);
        cost = std::to_string(ta::workshopSupportCost(profile, static_cast<ta::SupportModule>(index), content));
        const ta::ContentMetadata& metadata = content.workshopMetadata[static_cast<std::size_t>(6 + index - 1)];
        change = metadata.longDescription.empty() ? metadata.shortDescription : metadata.longDescription;
    } else if (purchase == WorkshopPurchase::UltimateEvolution) {
        const ta::UltimateEvolution evolution = evolutionForUltimate(ultimate, index);
        const ta::ContentMetadata& metadata = content.evolutionMetadata[static_cast<std::size_t>(evolution) - 1u];
        item = ta::ultimateEvolutionName(evolution);
        cost = std::to_string(ta::isUltimateEvolutionUnlocked(profile, evolution) ? 0u : content.ultimateEvolutionCost[static_cast<std::size_t>(evolution) - 1u]);
        currency = "LEGEND CORES";
        masteryReady = ta::isUltimateEvolutionUnlocked(profile, evolution) || profile.ultimateMasteryRuns[static_cast<std::size_t>(ultimate)] >= 3;
        title = ta::isUltimateEvolutionUnlocked(profile, evolution) ? "CONFIRM EVOLUTION EQUIP" : "CONFIRM EVOLUTION UNLOCK";
        drawWrappedTextInBox(renderer, "workshop.evolutionDescription", UiRect{370, 420, 560, 80}, metadata.shortDescription, 1, neo::Text, 10, 18);
        std::string tags;
        for (const std::string& tag : metadata.synergyTags) { if (!tags.empty()) tags += "+"; tags += tag; }
        drawText(renderer, 380, 466, "MATCH " + tags, 1, neo::Cyan);
    } else if (purchase == WorkshopPurchase::UltimateModule) {
        const int moduleIndex = static_cast<int>(ultimate) * 2 + index;
        const ta::UltimateModule module = static_cast<ta::UltimateModule>(moduleIndex);
        const ta::ContentMetadata& metadata = content.ultimateModuleMetadata[static_cast<std::size_t>(moduleIndex)];
        item = metadata.display;
        cost = std::to_string(ta::isUltimateModuleUnlocked(profile, module) ? 0u : content.ultimateModuleCost[static_cast<std::size_t>(moduleIndex)]);
        masteryReady = profile.ultimateMasteryRuns[static_cast<std::size_t>(ultimate)] >= 3u;
        currency = "CORE PARTS";
        title = ta::isUltimateModuleUnlocked(profile, module) ? "CONFIRM SIDEGRADE EQUIP" : "CONFIRM SIDEGRADE UNLOCK";
        change = metadata.longDescription.empty() ? metadata.shortDescription : metadata.longDescription;
    } else if (purchase == WorkshopPurchase::SkillNode) {
        if (index < 0 || index >= static_cast<int>(content.skillNodes.size())) return;
        const ta::SkillNodeDefinition& node = content.skillNodes[static_cast<std::size_t>(index)];
        item = node.display;
        cost = std::to_string(ta::skillNodeCost(profile, node));
        change = node.description + " // BRANCH " + (node.branchId.empty() ? "CORE" : node.branchId) + " // ICON LAYER " + node.iconLayer;
        title = "CONFIRM SKILL TALENT";
    } else if (purchase == WorkshopPurchase::SkillUnlock) {
        if (index < 0 || index >= static_cast<int>(ta::SkillId::Count)) return;
        const ta::SkillId skill = static_cast<ta::SkillId>(index);
        item = ta::skillName(skill);
        cost = std::to_string(120u + static_cast<unsigned int>(index) * 20u);
        change = std::string(ta::skillDescription(skill)) + " // UNLOCKS THIS SKILL FOR LOADOUTS AND ITS WORKSHOP TREE";
        title = "CONFIRM SKILL UNLOCK";
    }
    drawTextFitInBox(renderer, "workshop.confirmTitle", UiRect{380, 234, 540, 18}, title, 2, neo::Amber);
    drawTextFitInBox(renderer, "workshop.confirmItem", UiRect{380, 288, 540, 18}, item, 2, neo::Text);
    drawTextFitInBox(renderer, "workshop.confirmCost", UiRect{380, 334, 540, 16}, "COST " + cost + " " + currency, 1, neo::Violet);
    const std::uint32_t balance = currency == "LEGEND CORES" ? profile.legendCores : profile.coreParts;
    const bool affordable = balance >= static_cast<std::uint32_t>(std::stoul(cost)) && masteryReady;
    drawTextFitInBox(renderer, "workshop.confirmAvailable", UiRect{380, 370, 540, 16}, "AVAILABLE " + std::to_string(balance), 1, affordable ? neo::Mint : neo::Red);
    const std::uint32_t numericCost = static_cast<std::uint32_t>(std::stoul(cost));
    drawTextFitInBox(renderer, "workshop.confirmBalance", UiRect{380, 388, 540, 16}, "BALANCE AFTER " + std::to_string(balance >= numericCost ? balance - numericCost : 0u), 1, affordable ? neo::Cyan : neo::Muted);
    if (!change.empty()) drawWrappedTextInBox(renderer, "workshop.purchaseDescription", UiRect{370, 420, 560, 80}, change, 1, neo::Text, 10, 18);
    drawTextFitInBox(renderer, "workshop.confirmNotice", UiRect{380, 410, 540, 16}, !masteryReady ? "MASTERY REQUIRED // COMPLETE 3 RUNS WITH THIS ULTIMATE." : (affordable ? "CONFIRMING WILL APPLY THIS CHANGE IMMEDIATELY." : "NOT ENOUGH CURRENCY // CANCEL OR EARN MORE."), 1, affordable ? neo::Muted : neo::Red);
    drawMenuButton(renderer, workshopConfirmCancelButton, "CANCEL", neo::Cyan);
    drawMenuButton(renderer, workshopConfirmAcceptButton, affordable ? "CONFIRM" : "INSUFFICIENT", affordable ? neo::Mint : neo::Red, affordable);
}

void drawCollectionScreen(SDL_Renderer* renderer, int category, int item, const ta::ContentConfig& content) {
    rect(renderer, 0, 0, GameSim::Width, GameSim::Height, neo::Void);
    drawText(renderer, 420, 100, "COLLECTION", 3, neo::Text);
    drawText(renderer, 400, 146, "TACTICAL CODEX // KNOW YOUR OPTIONS", 1, neo::Muted);
    neonDivider(renderer, 180, 180, 920, neo::Violet);
    const std::array<std::pair<const char*, Color>, 13> entries{{
        {"WEAPONS", neo::Cyan}, {"UPGRADES", neo::Amber}, {"ENEMIES", neo::Red}, {"SYNERGIES", neo::Mint},
        {"ULTIMATES", neo::Violet}, {"SUPPORT", neo::Cyan}, {"CHASSIS", neo::Amber}, {"SKULLS", neo::Magenta},
        {"ARENAS", neo::Amber}, {"EVOLUTIONS", neo::Violet}, {"CURRENCIES", neo::Amber}, {"ULT MODULES", neo::Violet}, {"SKILLS", neo::Mint}
    }};
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const UiRect card = collectionCategoryButton(static_cast<int>(index));
        const int x = card.x;
        neoPanel(renderer, card.x, card.y, card.width, card.height, entries[index].second, static_cast<int>(index) == category, 12);
        drawTextFitInBox(renderer, "collection.categoryName", UiRect{card.x + 20, card.y + 12, card.width - 32, 16}, entries[index].first, 1, neo::Text);
        drawTextFitInBox(renderer, "collection.categoryAction", UiRect{card.x + 20, card.y + 34, card.width - 32, 16}, "INSPECT", 1, entries[index].second);
    }
    const int count = collectionItemCount(category);
    const int selected = std::clamp(item, 0, count - 1);
    std::string title;
    std::string description;
    const ta::ContentMetadata* selectedMetadata = nullptr;
    if (category == 0) {
        const ta::Weapon weapon = static_cast<ta::Weapon>(selected);
        selectedMetadata = &content.weaponMetadata[static_cast<std::size_t>(selected)];
        title = selectedMetadata->display;
        description = selectedMetadata->longDescription;
    } else if (category == 1) {
        const ta::Upgrade upgrade = static_cast<ta::Upgrade>(selected);
        selectedMetadata = &content.upgradeMetadata[static_cast<std::size_t>(selected)];
        title = selectedMetadata->display;
        description = selectedMetadata->longDescription;
    } else if (category == 2) {
        const ta::EnemyType enemy = static_cast<ta::EnemyType>(selected);
        selectedMetadata = &content.enemyMetadata[static_cast<std::size_t>(selected)];
        title = selectedMetadata->display;
        description = selectedMetadata->longDescription;
    } else if (category == 3) {
        selectedMetadata = &content.synergyMetadata[static_cast<std::size_t>(selected)];
        title = selectedMetadata->display;
        description = selectedMetadata->longDescription;
    } else if (category == 4) {
        selectedMetadata = &content.ultimateMetadata[static_cast<std::size_t>(selected)];
        title = selectedMetadata->display;
        description = selectedMetadata->longDescription;
    } else if (category == 5) {
        selectedMetadata = &content.supportMetadata[static_cast<std::size_t>(selected)];
        title = selectedMetadata->display;
        description = selectedMetadata->longDescription;
    } else if (category == 6) {
        selectedMetadata = &content.chassisMetadata[static_cast<std::size_t>(selected)];
        title = selectedMetadata->display;
        description = selectedMetadata->longDescription;
    } else if (category == 7) {
        selectedMetadata = &content.skullMetadata[static_cast<std::size_t>(selected + 1)];
        title = selectedMetadata->display;
        description = selectedMetadata->longDescription;
    } else if (category == 8) {
        selectedMetadata = &content.arenaMetadata[static_cast<std::size_t>(selected)];
        title = selectedMetadata->display;
        description = selectedMetadata->longDescription;
    } else if (category == 10) {
        selectedMetadata = &content.currencyMetadata[static_cast<std::size_t>(selected)];
        title = selectedMetadata->display;
        description = selectedMetadata->longDescription;
    } else if (category == 11) {
        selectedMetadata = &content.ultimateModuleMetadata[static_cast<std::size_t>(selected)];
        title = selectedMetadata->display;
        description = selectedMetadata->longDescription;
    } else if (category == 12) {
        const ta::SkillDefinition& skill = content.skillDefinitions[static_cast<std::size_t>(selected)];
        title = skill.display;
        description = skill.longDescription;
    } else {
        selectedMetadata = &content.evolutionMetadata[static_cast<std::size_t>(selected)];
        title = selectedMetadata->display;
        description = selectedMetadata->longDescription;
    }
    const auto joinMetadata = [](const std::vector<std::string>& values) {
        std::string result;
        for (const std::string& value : values) { if (!result.empty()) result += "+"; result += value; }
        return result.empty() ? std::string("NONE") : result;
    };
    std::string mechanics = "AUTHORED PROFILE // VALUES SHOWN IN LOADOUT";
    if (category == 0) mechanics = "DAMAGE " + std::to_string(static_cast<int>(content.weaponDamage[static_cast<std::size_t>(selected)])) + " // COOLDOWN " + std::to_string(content.weaponCooldown[static_cast<std::size_t>(selected)]) + " TICKS";
    else if (category == 1) mechanics = "EFFECT " + content.upgradeMetadata[static_cast<std::size_t>(selected)].effect + " // MAX STACKS " + std::to_string(content.upgradeMetadata[static_cast<std::size_t>(selected)].maxStacks);
    else if (category == 2) mechanics = "HEALTH X" + std::to_string(content.enemyHealthScale[static_cast<std::size_t>(selected)]).substr(0, 4) + " // SPEED X" + std::to_string(content.enemySpeedScale[static_cast<std::size_t>(selected)]).substr(0, 4) + " // RESIST " + std::to_string(static_cast<int>(content.enemyDamageResistance[static_cast<std::size_t>(selected)] * 100.0f)) + "%";
    else if (category == 4) mechanics = "COOLDOWN " + std::to_string(content.ultimateCooldownTicks[static_cast<std::size_t>(selected)] / ta::GameSim::TickRate) + "S // DAMAGE X" + std::to_string(content.ultimateDamageScale[static_cast<std::size_t>(selected)]).substr(0, 4);
    else if (category == 6) mechanics = "WEAPON DMG X" + std::to_string(content.chassisWeaponDamageScale[static_cast<std::size_t>(selected)]).substr(0, 4) + " // COOLDOWN X" + std::to_string(content.chassisWeaponCooldownScale[static_cast<std::size_t>(selected)]).substr(0, 4) + " // LIVES +" + std::to_string(content.chassisLivesBonus[static_cast<std::size_t>(selected)]);
    else if (category == 7) { const std::size_t skullIndex = static_cast<std::size_t>(selected + 1); mechanics = "SCORE X" + std::to_string(content.skullScoreMultiplier[skullIndex]).substr(0, 4) + " // LIVES " + std::to_string(content.skullLives[skullIndex]) + " // SPEED X" + std::to_string(content.skullSpeedScale[skullIndex]).substr(0, 4); }
    else if (category == 8) mechanics = "HEALTH X" + std::to_string(content.arenaHealthScale[static_cast<std::size_t>(selected)]).substr(0, 4) + " // SPEED X" + std::to_string(content.arenaSpeedScale[static_cast<std::size_t>(selected)]).substr(0, 4);
    else if (category == 9) mechanics = "UNLOCK COST " + std::to_string(content.ultimateEvolutionCost[static_cast<std::size_t>(selected)]) + " LEGEND CORES // MASTERY 3 RUNS";
    else if (category == 11) mechanics = "COST " + std::to_string(content.ultimateModuleCost[static_cast<std::size_t>(selected)]) + " CORE PARTS // COOLDOWN X" + std::to_string(content.ultimateModuleCooldownScale[static_cast<std::size_t>(selected)]).substr(0, 4) + " // DAMAGE X" + std::to_string(content.ultimateModuleDamageScale[static_cast<std::size_t>(selected)]).substr(0, 4);
    else if (category == 12) mechanics = "COOLDOWN " + std::to_string(content.skillDefinitions[static_cast<std::size_t>(selected)].cooldownTicks) + " TICKS // TARGET " + content.skillDefinitions[static_cast<std::size_t>(selected)].targetMode + " // TREE NODES " + std::to_string(std::count_if(content.skillNodes.begin(), content.skillNodes.end(), [&](const ta::SkillNodeDefinition& node) { return node.skillId == content.skillDefinitions[static_cast<std::size_t>(selected)].id; }));
    neoPanel(renderer, 220, 450, 840, 150, neo::Violet, true, 10);
    drawTextFitInBox(renderer, "collection.selectedTitle", UiRect{250, 464, 780, 18}, title + "  //  " + std::to_string(selected + 1) + "/" + std::to_string(count), 2, neo::Violet);
    drawWrappedTextInBox(renderer, "collection.description", UiRect{250, 500, 780, 16}, description, 1, neo::Text, 0, 17);
    drawTextFitInBox(renderer, "collection.mechanics", UiRect{250, 518, 780, 16}, "MECHANICS // " + mechanics, 1, neo::Amber);
    if (selectedMetadata != nullptr) {
        drawTextFitInBox(renderer, "collection.strong", UiRect{250, 538, 780, 16}, "STRONG // " + joinMetadata(selectedMetadata->strengths), 1, neo::Mint);
        drawTextFitInBox(renderer, "collection.weak", UiRect{250, 556, 780, 16}, "WEAK // " + joinMetadata(selectedMetadata->weaknesses), 1, neo::Red);
        drawTextFitInBox(renderer, "collection.tags", UiRect{250, 574, 780, 16}, "TAGS // " + joinMetadata(selectedMetadata->synergyTags), 1, neo::Cyan);
    }
    drawTextFitInBox(renderer, "collection.navigationHelp", UiRect{250, 592, 780, 16}, "LEFT/RIGHT CATEGORY  UP/DOWN ITEM  //  HOVER LOADOUT CARDS FOR QUICK DETAILS", 1, neo::Muted);
    drawMenuButton(renderer, collectionBackButton, "BACK", neo::Cyan);
}

void drawLoadoutTooltip(SDL_Renderer* renderer, int x, int y, const GameSim& sim) {
    std::string title;
    std::string description;
    std::string mechanics;
    const ta::ContentMetadata* metadata = nullptr;
    Color accent = neo::Cyan;
    for (int index = 0; index < 3; ++index) {
        if (loadoutChassisCard(index).contains(x, y)) {
            const ta::TowerChassis chassis = static_cast<ta::TowerChassis>(index);
            title = ta::chassisName(chassis);
            metadata = &sim.contentConfig().chassisMetadata[static_cast<std::size_t>(index)];
            description = metadata->shortDescription;
            mechanics = "WEAPON DMG X" + std::to_string(sim.contentConfig().chassisWeaponDamageScale[static_cast<std::size_t>(index)]).substr(0, 4) + " // COOLDOWN X" + std::to_string(sim.contentConfig().chassisWeaponCooldownScale[static_cast<std::size_t>(index)]).substr(0, 4) + " // LIVES +" + std::to_string(sim.contentConfig().chassisLivesBonus[static_cast<std::size_t>(index)]);
            accent = neo::Violet;
            break;
        }
        if (loadoutArenaCard(index).contains(x, y)) {
            const ta::Arena arena = static_cast<ta::Arena>(index);
            title = ta::arenaName(arena);
            metadata = &sim.contentConfig().arenaMetadata[static_cast<std::size_t>(index)];
            description = metadata->shortDescription;
            mechanics = "ROUTE HEALTH X" + std::to_string(sim.contentConfig().arenaHealthScale[static_cast<std::size_t>(index)]).substr(0, 4) + " // SPEED X" + std::to_string(sim.contentConfig().arenaSpeedScale[static_cast<std::size_t>(index)]).substr(0, 4);
            accent = std::array<Color, 3>{{neo::Cyan, neo::Amber, neo::Violet}}[static_cast<std::size_t>(index)];
            break;
        }
    }
    for (int index = 0; index < 5; ++index) {
        if (!title.empty()) break;
        if (loadoutWeaponCard(index).contains(x, y)) {
            const ta::Weapon weapon = static_cast<ta::Weapon>(index);
            title = ta::weaponName(weapon);
            metadata = &sim.contentConfig().weaponMetadata[static_cast<std::size_t>(index)];
            description = metadata->shortDescription;
            mechanics = "DAMAGE " + std::to_string(static_cast<int>(sim.contentConfig().weaponDamage[static_cast<std::size_t>(index)])) + " // COOLDOWN " + std::to_string(sim.contentConfig().weaponCooldown[static_cast<std::size_t>(index)]) + " TICKS";
            accent = std::array<Color, 5>{{neo::Mint, neo::Amber, neo::Violet, neo::Ice, neo::Red}}[static_cast<std::size_t>(index)];
            break;
        }
        if (loadoutSkullCard(index).contains(x, y)) {
            const ta::Skull skull = static_cast<ta::Skull>(index + 1);
            title = ta::skullName(skull);
            metadata = &sim.contentConfig().skullMetadata[static_cast<std::size_t>(index + 1)];
            description = metadata->shortDescription;
            accent = neo::Magenta;
            break;
        }
        if (loadoutSupportCard(index).contains(x, y)) {
            const ta::SupportModule support = static_cast<ta::SupportModule>(index);
            title = ta::supportModuleName(support);
            metadata = &sim.contentConfig().supportMetadata[static_cast<std::size_t>(index)];
            description = metadata->shortDescription;
            accent = neo::Cyan;
            break;
        }
        if (loadoutUltimateCard(index).contains(x, y)) {
            const ta::Ultimate ultimate = static_cast<ta::Ultimate>(index);
            title = ta::ultimateName(ultimate);
            metadata = &sim.contentConfig().ultimateMetadata[static_cast<std::size_t>(index)];
            description = metadata->shortDescription;
            mechanics = "COOLDOWN " + std::to_string(sim.contentConfig().ultimateCooldownTicks[static_cast<std::size_t>(index)] / ta::GameSim::TickRate) + "S // DAMAGE X" + std::to_string(sim.contentConfig().ultimateDamageScale[static_cast<std::size_t>(index)]).substr(0, 4);
            accent = neo::Violet;
            break;
        }
    }
    if (title.empty()) return;
    const int tooltipX = std::clamp(x + 18, 24, GameSim::Width - 390);
    const int tooltipY = std::clamp(y + 18, 82, GameSim::Height - 178);
    neoPanel(renderer, tooltipX, tooltipY, 366, 160, accent, true, 8);
    drawText(renderer, tooltipX + 14, tooltipY + 10, title, 1, accent);
    drawWrappedTextInBox(renderer, "loadout.tooltipDescription", UiRect{tooltipX + 14, tooltipY + 32, 338, 17}, description, 1, neo::Text, 0, 17);
    if (metadata != nullptr) {
        const std::string detail = metadata->longDescription.empty() ? metadata->shortDescription : metadata->longDescription;
        drawWrappedTextInBox(renderer, "loadout.tooltipDetail", UiRect{tooltipX + 14, tooltipY + 52, 338, 35}, detail, 1, neo::Muted, 0, 17);
        drawText(renderer, tooltipX + 14, tooltipY + 91, "STRONG // " + (metadata->strengths.empty() ? std::string("GENERAL") : metadata->strengths.front()), 1, neo::Mint);
        drawText(renderer, tooltipX + 14, tooltipY + 109, "WEAK // " + (metadata->weaknesses.empty() ? std::string("NONE") : metadata->weaknesses.front()), 1, neo::Red);
        drawText(renderer, tooltipX + 14, tooltipY + 127, "SYNERGY // " + (metadata->synergyTags.empty() ? std::string("GENERAL") : metadata->synergyTags.front()), 1, neo::Cyan);
    }
    if (!mechanics.empty()) drawText(renderer, tooltipX + 14, tooltipY + 145, mechanics, 1, neo::Amber);
    (void)sim;
}

void drawLoadout(SDL_Renderer* renderer, const GameSim& sim, const ta::ProfileData& profile, const ta::DailyChallenge& daily, bool dailyMode) {
    rect(renderer, 0, 0, GameSim::Width, GameSim::Height, neo::Void);
    chamferOutline(renderer, 96, 54, 1088, 620, {neo::Cyan.r, neo::Cyan.g, neo::Cyan.b, 80}, 20, 1);
    neoPanel(renderer, 140, 90, 1000, 540, neo::Cyan, false, 16);
    drawText(renderer, 190, 116, "TOWER ASCEND", 3, neo::Text);
    drawText(renderer, 190, 150, "LOADOUT // SYSTEM READY", 1, neo::Cyan);
    const ta::ContentMetadata& chassisMetadata = sim.contentConfig().chassisMetadata[static_cast<std::size_t>(sim.chassis())];
    drawTextFitInBox(renderer, "loadout.chassis.summary", {190, 168, 900, 14}, std::string("CHASSIS // ") + (chassisMetadata.display.empty() ? ta::chassisName(sim.chassis()) : chassisMetadata.display) + " // " + (chassisMetadata.shortDescription.empty() ? ta::chassisDescription(sim.chassis()) : chassisMetadata.shortDescription), 1, neo::Violet);
    if (daily.chassisRequired) drawText(renderer, 620, 150, "DAILY LOCK // " + std::string(ta::chassisName(daily.requiredChassis)), 1, neo::Violet);
    neonDivider(renderer, 190, 174, 900, neo::Cyan);
    std::string skillLine = "SKILLS // ";
    for (std::size_t slot = 0; slot < ta::SkillSlotCount; ++slot) { if (slot > 0) skillLine += "  "; skillLine += std::to_string(slot + 1) + ":" + ta::skillName(sim.skill(slot)); }
    drawTextFitInBox(renderer, "loadout.skills.summary", {190, 180, 900, 14}, skillLine, 1, neo::Violet);
    drawText(renderer, 190, 306, "CHASSIS // F1-F3", 1, neo::Muted);
    for (int index = 0; index < 3; ++index) {
        const UiRect card = loadoutChassisCard(index);
        const bool chassisLocked = daily.chassisRequired && static_cast<ta::TowerChassis>(index) != daily.requiredChassis;
        neoPanel(renderer, card.x, card.y, card.width, card.height, neo::Violet, static_cast<int>(sim.chassis()) == index, 5);
        const ta::ContentMetadata& metadata = sim.contentConfig().chassisMetadata[static_cast<std::size_t>(index)];
        drawTextFitInBox(renderer, "loadout.chassis.card", {card.x + 12, card.y + 7, card.width - 24, 14}, (chassisLocked ? "LOCK // " : "") + (metadata.display.empty() ? std::string(ta::chassisName(static_cast<ta::TowerChassis>(index))) : metadata.display), 1, chassisLocked ? neo::Muted : neo::Text);
    }
    drawText(renderer, 190, 350, "ARENA // ROUTE PROFILE", 1, neo::Muted);
    const std::array<const char*, 3> arenaLabels{{"MOONBASE", "EMBER", "NEON"}};
    const std::array<Color, 3> arenaColors{{neo::Cyan, neo::Amber, neo::Violet}};
    for (int i = 0; i < 3; ++i) {
        const UiRect originalCard = loadoutArenaCard(i);
        const UiRect card{originalCard.x, originalCard.y + 0, originalCard.width, originalCard.height};
        const bool selected = static_cast<int>(sim.arena()) == i;
        const bool locked = dailyMode && static_cast<ta::Arena>(i) != daily.arena;
        neoPanel(renderer, card.x, card.y, card.width, card.height, arenaColors[static_cast<std::size_t>(i)], selected, 6);
        drawTextFitInBox(renderer, "loadout.arena.card", {card.x + 12, card.y + 12, card.width - 24, 14}, locked ? "LOCKED" : arenaLabels[static_cast<std::size_t>(i)], 1, locked ? neo::Muted : neo::Text);
    }
    drawText(renderer, 900, 96, "SHARDS " + std::to_string(profile.cosmeticShards), 1, neo::Amber);
    drawText(renderer, 900, 108, "BEST " + std::to_string(profile.bestScore), 1, neo::Muted);
    drawText(renderer, 190, 378, std::string("AUDIO M") + std::to_string(profile.masterVolume) + " S" + std::to_string(profile.sfxVolume) + " U" + std::to_string(profile.uiVolume), 1, neo::Muted);
    drawText(renderer, 190, 390, std::string("UI ") + std::to_string(profile.uiScalePercent) + "%  PALETTE " + std::to_string(profile.colorBlindPalette + 1) + "  F10 SETTINGS", 1, neo::Muted);
    drawText(renderer, 650, 578, "ULTIMATE // MANUAL ACTIVATION", 1, neo::Violet);
    const std::array<Color, 5> colors{{neo::Mint, neo::Amber, neo::Violet, neo::Ice, neo::Red}};
    for (int i = 0; i < 5; ++i) {
        const UiRect card = loadoutWeaponCard(i);
        const int x = card.x;
        neoPanel(renderer, card.x, card.y, card.width, card.height, colors[static_cast<std::size_t>(i)], i == static_cast<int>(sim.weapon()), 10);
        drawText(renderer, x + 12, card.y + 12, "0" + std::to_string(i + 1), 1, colors[static_cast<std::size_t>(i)]);
        if (i == 0) filledDiamond(renderer, x + 70, card.y + 58, 24, colors[static_cast<std::size_t>(i)]);
        else if (i == 1) filledHexagon(renderer, x + 70, card.y + 58, 25, colors[static_cast<std::size_t>(i)]);
        else if (i == 2) { circle(renderer, x + 70, card.y + 58, 24, colors[static_cast<std::size_t>(i)]); line(renderer, x + 50, card.y + 58, x + 90, card.y + 58, neo::Text, 2); }
        else if (i == 3) { filledDiamond(renderer, x + 70, card.y + 58, 25, colors[static_cast<std::size_t>(i)]); hexagon(renderer, x + 70, card.y + 58, 18, neo::Text, 1); }
        else { filledHexagon(renderer, x + 70, card.y + 58, 25, colors[static_cast<std::size_t>(i)]); line(renderer, x + 54, card.y + 58, x + 86, card.y + 58, neo::Text, 2); }
        rect(renderer, x + 26, card.y + 96, 88, 4, colors[static_cast<std::size_t>(i)]);
    }
    const std::array<const char*, 5> weaponLabels{{"RAPID FIRE", "CANNON", "ARCANE BEAM", "FROST", "RAILGUN"}};
    for (int i = 0; i < 5; ++i) { const UiRect card = loadoutWeaponCard(i); drawTextFitInBox(renderer, "loadout.weapon.label", {card.x + 10, 313, card.width - 20, 14}, weaponLabels[static_cast<std::size_t>(i)], 1, neo::Text); }
    for (int i = 0; i < 4; ++i) {
        const UiRect card = loadoutSkullCard(i);
        const int x = card.x;
        const bool enabled = sim.hasSkull(static_cast<ta::Skull>(i + 1));
        neoPanel(renderer, card.x, card.y, card.width, card.height, neo::Magenta, enabled, 8);
        if (enabled) filledDiamond(renderer, x + 70, card.y + 36, 16, neo::Magenta);
        else hexagon(renderer, x + 70, card.y + 36, 16, neo::DarkText, 2);
    }
    const std::array<const char*, 4> skullLabels{{"SWARM", "GLASS", "HASTE", "GREED"}};
    for (int i = 0; i < 4; ++i) { const UiRect card = loadoutSkullCard(i); drawTextFitInBox(renderer, "loadout.skull.label", {card.x + 10, 445, card.width - 20, 14}, skullLabels[static_cast<std::size_t>(i)], 1, neo::Text); }
    if (dailyMode) drawText(renderer, 900, 445, "DAILY FIXED", 1, neo::Violet);
    drawText(renderer, 190, 398, "SKULL MODIFIERS // Q-T", 1, neo::Muted);
    drawText(renderer, 900, 398, "SCORE X" + std::to_string(sim.skullScoreMultiplier()).substr(0, 4), 1, neo::Amber);
    drawText(renderer, 190, 502, "SUPPORT // Z-V", 1, neo::Muted);
    drawText(renderer, 900, 502, std::string(ta::supportModuleName(sim.support())), 1, neo::Cyan);
    const std::array<const char*, 5> supportLabels{{"NONE", "CREDITS", "STASIS", "REPAIR", "CORROSION"}};
    for (int i = 0; i < 5; ++i) {
        const UiRect card = loadoutSupportCard(i);
        const bool locked = dailyMode && daily.requiredSupport != ta::SupportModule::None && static_cast<ta::SupportModule>(i) != daily.requiredSupport;
        neoPanel(renderer, card.x, card.y, card.width, card.height, neo::Cyan, i == static_cast<int>(sim.support()), 4);
        drawTextFitInBox(renderer, "loadout.support.card", {card.x + 8, card.y + 5, card.width - 16, 14}, locked ? "LOCKED" : supportLabels[static_cast<std::size_t>(i)], 1, locked ? neo::Muted : neo::Text);
    }
    const std::array<Color, 5> ultimateColors{{neo::Red, neo::Amber, neo::Ice, neo::Violet, neo::Mint}};
    const std::array<const char*, 5> ultimateLabels{{"METEOR", "BULLET", "ZERO", "GRAVITY", "SURGE"}};
    for (int i = 0; i < 5; ++i) {
        const UiRect card = loadoutUltimateCard(i);
        const bool locked = dailyMode && static_cast<ta::Ultimate>(i) != daily.requiredUltimate;
        neoPanel(renderer, card.x, card.y, card.width, card.height, ultimateColors[static_cast<std::size_t>(i)], i == static_cast<int>(sim.ultimate()), 8);
        drawTextFitInBox(renderer, "loadout.ultimate.card", {card.x + 8, card.y + 18, card.width - 16, 14}, locked ? "LOCKED" : ultimateLabels[static_cast<std::size_t>(i)], 1, locked ? neo::Muted : neo::Text);
    }
    drawText(renderer, 190, 666, "ACTIVE SKILLS // CLICK A SLOT TO CYCLE UNLOCKED SKILLS", 1, neo::Violet);
    for (int slot = 0; slot < static_cast<int>(ta::SkillSlotCount); ++slot) {
        const UiRect button = loadoutSkillButton(slot);
        neoPanel(renderer, button.x, button.y, button.width, button.height, neo::Violet, false, 4);
        drawTextFitInBox(renderer, "loadout.skill.name", {button.x + 8, button.y + 6, button.width - 16, 14}, std::to_string(slot + 1) + " // " + ta::skillName(sim.skill(static_cast<std::size_t>(slot))), 1, neo::Text);
        drawTextFitInBox(renderer, "loadout.skill.mode", {button.x + 8, button.y + 22, button.width - 16, 14}, ta::skillTargetModeName(sim.skillSnapshot(static_cast<std::size_t>(slot)).targetMode), 1, neo::Muted);
    }
    chamferFill(renderer, loadoutStartButton.x, loadoutStartButton.y, loadoutStartButton.width, loadoutStartButton.height, neo::Mint, 8);
    chamferOutline(renderer, loadoutStartButton.x, loadoutStartButton.y, loadoutStartButton.width, loadoutStartButton.height, neo::Text, 8, 1);
    chamferFill(renderer, loadoutDailyButton.x, loadoutDailyButton.y, loadoutDailyButton.width, loadoutDailyButton.height, neo::Violet, 8);
    chamferOutline(renderer, loadoutDailyButton.x, loadoutDailyButton.y, loadoutDailyButton.width, loadoutDailyButton.height, neo::Text, 8, 1);
    drawText(renderer, loadoutStartButton.x + 88, loadoutStartButton.y + 14, "START RUN", 2, neo::Void);
    drawText(renderer, loadoutDailyButton.x + 46, loadoutDailyButton.y + 14, "DAILY", 2, neo::Text);
    drawText(renderer, 850, 640, "DAILY // CHALLENGE", 1, neo::Violet);
    drawText(renderer, 850, 650, (daily.chassisRequired ? std::string("REQUIRED ") + ta::chassisName(daily.requiredChassis) : std::string("OPEN CHASSIS")) + " // " + (daily.weaponRequired ? std::string("REQUIRED ") + ta::weaponName(daily.requiredWeapon) : std::string("OPEN WEAPON")), 1, neo::Text);
    drawText(renderer, 850, 660, std::string("DAILY ") + daily.title + " +" + std::to_string(daily.bonusShards) + " SHARDS", 1, neo::Amber);
    const bool dailyEvolutionLoaned = daily.requiredEvolution != ta::UltimateEvolution::None && !ta::isUltimateEvolutionUnlocked(profile, daily.requiredEvolution);
    drawText(renderer, 850, 670, daily.requiredEvolution == ta::UltimateEvolution::None ? "EVOLUTION // OPEN" : std::string("EVOLUTION // REQUIRED ") + ta::ultimateEvolutionName(daily.requiredEvolution) + (dailyEvolutionLoaned ? " // LOANED" : " // OWNED"), 1, neo::Violet);
}

std::string nextWaveThreatPreview(const GameSim& sim) {
    const int nextWave = std::clamp(sim.waveNumber() + 1, 1, 10);
    if (nextWave == 10) return "NEXT WAVE // ASCENDANT BOSS // FOCUSED DAMAGE + ADD CONTROL";
    const auto& weights = sim.contentConfig().waveEnemyTypeWeight[static_cast<std::size_t>(nextWave - 1)];
    std::array<int, 7> order{{0, 1, 2, 3, 4, 5, 6}};
    std::sort(order.begin(), order.end(), [&weights](int left, int right) { return weights[static_cast<std::size_t>(left)] > weights[static_cast<std::size_t>(right)]; });
    std::string result = "NEXT WAVE // ";
    int shown = 0;
    for (const int enemyIndex : order) {
        const float weight = weights[static_cast<std::size_t>(enemyIndex)];
        if (weight <= 0.0f || shown >= 3) continue;
        if (shown > 0) result += "  //  ";
        const ta::EnemyType enemy = static_cast<ta::EnemyType>(enemyIndex);
        const ta::ContentMetadata& metadata = sim.contentConfig().enemyMetadata[static_cast<std::size_t>(enemy)];
        result += (metadata.display.empty() ? ta::enemyTypeName(enemy) : metadata.display) + " " + std::to_string(static_cast<int>(weight)) + "%";
        ++shown;
    }
    return shown == 0 ? "NEXT WAVE // STANDARD PRESSURE" : result;
}

void drawModifierSelect(SDL_Renderer* renderer, const GameSim& sim, const ta::DailyChallenge& daily, bool dailyMode, bool endlessMode, int focus) {
    rect(renderer, 0, 0, GameSim::Width, GameSim::Height, neo::Void);
    drawText(renderer, 360, 92, "MODIFIERS // FINAL CHECK", 3, neo::Text);
    drawText(renderer, 348, 138, "READ THE THREAT PROFILE BEFORE COMMITTING THE RUN", 1, neo::Muted);
    neonDivider(renderer, 180, 172, 920, neo::Cyan);
    neoPanel(renderer, 180, 198, 420, 124, neo::Cyan, true, 10);
    drawText(renderer, 208, 220, "ARENA", 2, neo::Cyan);
    drawText(renderer, 208, 252, ta::arenaName(sim.arena()), 2, neo::Text);
    const ta::ContentMetadata& arenaMetadata = sim.contentConfig().arenaMetadata[static_cast<std::size_t>(sim.arena())];
    drawTextFitInBox(renderer, "modifier.arena.description", {208, 284, 364, 14}, arenaMetadata.shortDescription.empty() ? "AUTHORED ARENA PROFILE" : arenaMetadata.shortDescription, 1, neo::Muted);
    neoPanel(renderer, 620, 198, 420, 124, neo::Violet, true, 10);
    drawText(renderer, 648, 220, "ACTIVE SKULLS", 2, neo::Violet);
    std::string skulls;
    std::string skullDetails;
    for (int index = 1; index <= 4; ++index) {
        const ta::Skull skull = static_cast<ta::Skull>(index);
        if (sim.hasSkull(skull)) {
            if (!skulls.empty()) skulls += " + ";
            skulls += ta::skullName(skull);
            if (!skullDetails.empty()) skullDetails += " // ";
            const ta::ContentMetadata& skullMetadata = sim.contentConfig().skullMetadata[static_cast<std::size_t>(index)];
            skullDetails += skullMetadata.shortDescription.empty() ? ta::skullDescription(skull) : skullMetadata.shortDescription;
        }
    }
    if (skulls.empty()) skulls = "NONE";
    if (skullDetails.empty()) skullDetails = "NO RISK MODIFIER";
    drawTextFitInBox(renderer, "modifier.skulls", {648, 252, 364, 18}, skulls, 2, neo::Text);
    drawTextFitInBox(renderer, "modifier.skull.details", {648, 284, 364, 14}, skullDetails, 1, neo::Muted);
    drawText(renderer, 648, 302, dailyMode ? "DAILY RECIPE // LOCKED" : "RISK MULT X" + std::to_string(sim.skullScoreMultiplier()).substr(0, 4), 1, neo::Amber);
    std::vector<std::string> strengths;
    std::vector<std::string> weaknesses;
    std::vector<std::string> buildTags;
    const auto addBuildMetadata = [&strengths, &weaknesses, &buildTags](const ta::ContentMetadata& metadata) {
        const auto addUnique = [](std::vector<std::string>& values, const std::string& value) {
            if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
        };
        for (const std::string& value : metadata.strengths) addUnique(strengths, value);
        for (const std::string& value : metadata.weaknesses) addUnique(weaknesses, value);
        for (const std::string& value : metadata.synergyTags) addUnique(buildTags, value);
    };
    addBuildMetadata(sim.contentConfig().weaponMetadata[static_cast<std::size_t>(sim.weapon())]);
    addBuildMetadata(sim.contentConfig().chassisMetadata[static_cast<std::size_t>(sim.chassis())]);
    addBuildMetadata(sim.contentConfig().supportMetadata[static_cast<std::size_t>(sim.support())]);
    addBuildMetadata(sim.contentConfig().ultimateMetadata[static_cast<std::size_t>(sim.ultimate())]);
    const auto compact = [](const std::vector<std::string>& values, std::size_t maximum) {
        std::string result;
        for (std::size_t index = 0; index < values.size() && index < maximum; ++index) {
            if (!result.empty()) result += "+";
            result += values[index];
        }
        return result.empty() ? std::string("NONE") : result;
    };
    neoPanel(renderer, dailyModifierBriefingCard.x, dailyModifierBriefingCard.y, dailyModifierBriefingCard.width, dailyModifierBriefingCard.height, dailyMode ? neo::Violet : neo::Amber, false, 10);
    drawTextFitInBox(renderer, "modifier.protocol.title", {208, 374, 804, 18}, dailyMode ? daily.title : (endlessMode ? "ENDLESS PROTOCOL" : "STANDARD PROTOCOL"), 2, dailyMode ? neo::Violet : neo::Amber);
    drawTextFitInBox(renderer, "modifier.protocol.description", {208, 408, 804, 14}, dailyMode ? daily.description : "TEN WAVES // FINAL BOSS // FULL WORKSHOP PROGRESSION", 1, neo::Text);
    drawTextFitInBox(renderer, "modifier.protocol.threat", {208, 432, 804, 14}, dailyMode ? (daily.skullSummary + " // " + daily.enemySummary) : "STRENGTH // " + compact(strengths, 4), 1, neo::Muted);
    drawTextFitInBox(renderer, "modifier.protocol.reward", {208, 456, 804, 14}, dailyMode ? ("REWARD " + std::to_string(daily.legendCoreReward) + " LEGEND CORES + " + std::to_string(daily.bonusShards) + " SHARDS") : "WEAKNESS // " + compact(weaknesses, 3), 1, neo::Cyan);
    drawTextFitInBox(renderer, "modifier.protocol.seek", {208, 478, 804, 14}, dailyMode ? (daily.threatSummary + " // SEEK " + daily.recommendedUpgradeTags) : "SEEK // " + compact(buildTags, 5), 1, neo::Red);
    drawMenuButton(renderer, modifierBackButton, "BACK TO LOADOUT", neo::Cyan);
    drawMenuButton(renderer, modifierConfirmButton, dailyMode ? "CONFIRM DAILY" : "BEGIN RUN", dailyMode ? neo::Violet : neo::Mint);
    if (focus == 0) drawFocusOutline(renderer, modifierBackButton, neo::Text);
    else if (focus == 1) drawFocusOutline(renderer, modifierConfirmButton, neo::Text);
}

void drawSkinStrip(SDL_Renderer* renderer, const ta::ProfileData& profile, ta::TowerSkin equipped) {
    drawText(renderer, 900, 132, "SKINS // MODULES", 2, neo::Text);
    const std::array<Color, 5> colors{{neo::Cyan, neo::Amber, neo::Violet, neo::Mint, neo::Amber}};
    for (int i = 0; i < 5; ++i) {
        const ta::TowerSkin skin = static_cast<ta::TowerSkin>(i);
        const UiRect card = loadoutSkinCard(i);
        const int x = card.x;
        const bool unlocked = ta::isSkinUnlocked(profile, skin);
        neoPanel(renderer, card.x, card.y, card.width, card.height, colors[static_cast<std::size_t>(i)], i == static_cast<int>(equipped), 5);
        if (unlocked) filledDiamond(renderer, x + 14, card.y + 14, 7, colors[static_cast<std::size_t>(i)]);
        else hexagon(renderer, x + 14, card.y + 14, 7, neo::DarkText, 1);
        drawText(renderer, x + 25, card.y + 9, unlocked ? std::to_string(i + 1) : "X", 1, unlocked ? neo::Text : neo::Muted);
    }
    drawText(renderer, 900, 182, "6-0 SELECT  K UNLOCK  F10 SETTINGS", 1, neo::Muted);
    drawText(renderer, 190, 182, "F1-F3 ARENA // SUPPORT Z-V", 1, neo::Muted);
}

std::string upgradeSynergyPreview(const GameSim& sim, ta::Upgrade candidate) {
    std::vector<std::string> activeTags;
    const auto addTags = [&activeTags](const ta::ContentMetadata& metadata) {
        for (const std::string& tag : metadata.synergyTags) {
            if (std::find(activeTags.begin(), activeTags.end(), tag) == activeTags.end()) activeTags.push_back(tag);
        }
    };
    addTags(sim.contentConfig().weaponMetadata[static_cast<std::size_t>(sim.weapon())]);
    addTags(sim.contentConfig().chassisMetadata[static_cast<std::size_t>(sim.chassis())]);
    addTags(sim.contentConfig().supportMetadata[static_cast<std::size_t>(sim.support())]);
    addTags(sim.contentConfig().ultimateMetadata[static_cast<std::size_t>(sim.ultimate())]);
    for (const ta::Upgrade owned : sim.upgrades()) addTags(sim.contentConfig().upgradeMetadata[static_cast<std::size_t>(owned)]);
    addTags(sim.contentConfig().upgradeMetadata[static_cast<std::size_t>(candidate)]);

    const ta::ContentMetadata* best = nullptr;
    int bestMatches = 1;
    for (const ta::ContentMetadata& synergy : sim.contentConfig().synergyMetadata) {
        int matches = 0;
        for (const std::string& tag : synergy.synergyTags) {
            if (std::find(activeTags.begin(), activeTags.end(), tag) != activeTags.end()) ++matches;
        }
        if (matches > bestMatches) {
            best = &synergy;
            bestMatches = matches;
        }
    }
    if (best == nullptr) return "NO COMPLETE REACTION YET";
    return best->display + " // " + best->shortDescription;
}

void drawUpgradeOverlay(SDL_Renderer* renderer, const GameSim& sim) {
    chamferFill(renderer, 170, 150, 940, 460, {neo::Void.r, neo::Void.g, neo::Void.b, 242}, 18);
    chamferOutline(renderer, 170, 150, 940, 460, neo::Cyan, 18, 2);
    drawText(renderer, 430, 180, "CHOOSE UPGRADE", 3, neo::Text);
    drawText(renderer, 220, 207, nextWaveThreatPreview(sim), 1, neo::Red);
    neonDivider(renderer, 220, 225, 840, neo::Cyan);
    const std::array<Color, 3> accents{{neo::Mint, neo::Amber, neo::Violet}};
    for (int i = 0; i < 3; ++i) {
        const UiRect card = upgradeChoiceButton(i);
        const int x = card.x;
        neoPanel(renderer, card.x, card.y, card.width, card.height, accents[static_cast<std::size_t>(i)], false, 10);
        if (i == 0) filledDiamond(renderer, x + 110, 315, 38, accents[static_cast<std::size_t>(i)]);
        else if (i == 1) filledHexagon(renderer, x + 110, 315, 38, accents[static_cast<std::size_t>(i)]);
        else circle(renderer, x + 110, 315, 34, accents[static_cast<std::size_t>(i)]);
        hexagon(renderer, x + 110, 315, 42, neo::Text, 1);
        rect(renderer, x + 78, 370, 64, 4, accents[static_cast<std::size_t>(i)]);
        rect(renderer, x + 94, 215, 32, 18, accents[static_cast<std::size_t>(i)]);
        if (i < static_cast<int>(sim.pendingChoices().size())) {
            const ta::Upgrade upgrade = sim.pendingChoices()[static_cast<std::size_t>(i)];
            const ta::ContentMetadata& metadata = sim.contentConfig().upgradeMetadata[static_cast<std::size_t>(upgrade)];
            std::string tags;
            for (const std::string& tag : metadata.synergyTags) { if (!tags.empty()) tags += "+"; tags += tag; }
            std::string weaknesses;
            for (const std::string& weakness : metadata.weaknesses) { if (!weaknesses.empty()) weaknesses += "+"; weaknesses += weakness; }
            const ta::ContentMetadata& weaponMetadata = sim.contentConfig().weaponMetadata[static_cast<std::size_t>(sim.weapon())];
            bool matchesWeapon = false;
            for (const std::string& tag : metadata.synergyTags) if (std::find(weaponMetadata.synergyTags.begin(), weaponMetadata.synergyTags.end(), tag) != weaponMetadata.synergyTags.end()) { matchesWeapon = true; break; }
            bool matchesBuild = false;
            for (const ta::Upgrade owned : sim.upgrades()) {
                const ta::ContentMetadata& ownedMetadata = sim.contentConfig().upgradeMetadata[static_cast<std::size_t>(owned)];
                for (const std::string& tag : metadata.synergyTags) if (std::find(ownedMetadata.synergyTags.begin(), ownedMetadata.synergyTags.end(), tag) != ownedMetadata.synergyTags.end()) { matchesBuild = true; break; }
                if (matchesBuild) break;
            }
            const char* role = matchesBuild ? "SYNERGY" : (matchesWeapon ? "CORE" : (metadata.weaknesses.empty() ? "UTILITY" : "COVERAGE"));
            std::string prerequisiteText = "NONE";
            if (!metadata.prerequisites.empty()) {
                prerequisiteText = metadata.prerequisites.front();
                if (metadata.prerequisites.size() > 1) prerequisiteText += "+" + metadata.prerequisites[1];
            }
            const char* pairState = matchesBuild ? "BUILD ACTIVE" : (matchesWeapon ? "WEAPON LINK" : "NO LINK YET");
            drawTextFitInBox(renderer, "upgrade.name", {x + 20, 390, card.width - 40, 14}, metadata.display.empty() ? ta::upgradeName(upgrade) : metadata.display, 1, neo::Text);
            drawTextFitInBox(renderer, "upgrade.description", {x + 20, 410, card.width - 40, 14}, metadata.shortDescription.empty() ? ta::upgradeDescription(upgrade) : metadata.shortDescription, 1, neo::Muted);
            drawTextFitInBox(renderer, "upgrade.match", {x + 20, 430, card.width - 40, 14}, std::string(role) + " // MATCH " + (tags.empty() ? std::string("GENERAL") : tags), 1, neo::Cyan);
            drawTextFitInBox(renderer, "upgrade.requirements", {x + 20, 450, card.width - 40, 14}, std::string(pairState) + " // REQ " + prerequisiteText + " // WEAK " + (weaknesses.empty() ? std::string("NONE") : weaknesses), 1, neo::Red);
            drawTextFitInBox(renderer, "upgrade.reaction", {x + 20, 468, card.width - 40, 14}, "REACTION // " + upgradeSynergyPreview(sim, upgrade), 1, neo::Mint);
        }
    }
    neoPanel(renderer, upgradeRerollButton.x, upgradeRerollButton.y, upgradeRerollButton.width, upgradeRerollButton.height, neo::Amber, false, 5);
    drawText(renderer, upgradeRerollButton.x + 28, upgradeRerollButton.y + 8, "REROLL DRAFT [R]  " + std::to_string(sim.rerollsRemaining()), 1, sim.rerollsRemaining() > 0 ? neo::Amber : neo::Muted);
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
    // Reserve the first and last few pixels for chrome. Compact cards place
    // labels close to their edges, so y+6/y+height-6 could paint through glyphs.
    line(renderer, x + cut + 8, y + 2, x + std::min(width - cut - 8, 74), y + 2, accent, 2);
    line(renderer, x + width - 28, y + height - 3, x + width - cut - 8, y + height - 3, selected ? accent : neo::DarkText, 1);
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
    chamferFill(renderer, settingsCloseButton.x, settingsCloseButton.y, settingsCloseButton.width, settingsCloseButton.height, neo::PanelRaised, 8);
    chamferOutline(renderer, settingsCloseButton.x, settingsCloseButton.y, settingsCloseButton.width, settingsCloseButton.height, neo::Cyan, 8, 1);
    drawText(renderer, settingsCloseButton.x + 45, settingsCloseButton.y + 13, "CLOSE SETTINGS", 1, neo::Text);
}

void drawResultsOverlay(SDL_Renderer* renderer, const GameSim& sim, std::uint32_t rewardShards, std::uint32_t rewardCoreParts, std::uint32_t rewardLegendCores, bool dailyMode) {
    if (!sim.isGameOver() && !sim.isVictory()) return;
    const ta::RunSummary summary = sim.runSummary();
    const Color outcome = summary.victory ? neo::Mint : neo::Red;
    chamferFill(renderer, 260, 185, 760, 465, {neo::Void.r, neo::Void.g, neo::Void.b, 242}, 16);
    chamferOutline(renderer, 260, 185, 760, 465, outcome, 16, 2);
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
    drawText(renderer, 390, 485, "REWARDS", 2, neo::Muted);
    drawText(renderer, 620, 485, "+" + std::to_string(rewardCoreParts) + " CORE  +" + std::to_string(rewardShards) + " SHARDS", 1, neo::Cyan);
    drawText(renderer, 390, 507, "CORE PARTS // KILLS + WAVES + FINAL BOSS" + std::string(dailyMode ? " + DAILY BONUS" : ""), 1, neo::Muted);
    drawText(renderer, 390, 524, "SHARDS // KILLS + WAVES" + std::string(dailyMode ? " + DAILY BONUS" : ""), 1, neo::Muted);
    drawText(renderer, 390, 541, rewardLegendCores > 0 ? ("+" + std::to_string(rewardLegendCores) + " LEGEND CORES // " + std::string(dailyMode ? "DAILY FIRST CLEAR" : "RUN MILESTONE")) : "LEGEND CORES // NONE THIS RUN", 1, rewardLegendCores > 0 ? neo::Violet : neo::Muted);
    const ta::ContentMetadata& weaponMetadata = sim.contentConfig().weaponMetadata[static_cast<std::size_t>(sim.weapon())];
    std::string buildTags;
    std::vector<std::string> weaknesses;
    std::vector<std::string> strengths;
    const auto addMetadata = [&buildTags, &weaknesses, &strengths](const ta::ContentMetadata& metadata) {
        const auto addUnique = [](std::vector<std::string>& values, const std::string& value) {
            if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
        };
        for (const std::string& tag : metadata.synergyTags) {
            if (tag.empty() || buildTags.find(tag) != std::string::npos) continue;
            if (!buildTags.empty()) buildTags += "+";
            buildTags += tag;
        }
        for (const std::string& weakness : metadata.weaknesses) addUnique(weaknesses, weakness);
        for (const std::string& strength : metadata.strengths) addUnique(strengths, strength);
    };
    addMetadata(weaponMetadata);
    addMetadata(sim.contentConfig().chassisMetadata[static_cast<std::size_t>(sim.chassis())]);
    addMetadata(sim.contentConfig().supportMetadata[static_cast<std::size_t>(sim.support())]);
    addMetadata(sim.contentConfig().ultimateMetadata[static_cast<std::size_t>(sim.ultimate())]);
    for (const ta::Upgrade upgrade : sim.upgrades()) addMetadata(sim.contentConfig().upgradeMetadata[static_cast<std::size_t>(upgrade)]);
    const auto compact = [](const std::vector<std::string>& values, std::size_t maximum) {
        std::string result;
        for (std::size_t index = 0; index < values.size() && index < maximum; ++index) {
            if (!result.empty()) result += "+";
            result += values[index];
        }
        return result.empty() ? std::string("NONE") : result;
    };
    const bool forceMultiplier = summary.victory && sim.stats().reactionTriggers >= 3;
    drawText(renderer, 390, 558, "OUTPUT " + std::to_string(sim.stats().damageDealt) + " DMG // " + std::to_string(sim.stats().reactionTriggers) + " REACTIONS // " + std::to_string(sim.stats().statusApplications) + " STATUS", 1, neo::Cyan);
    drawText(renderer, 390, 576, "BUILD " + std::string(ta::weaponName(sim.weapon())) + " // " + (buildTags.empty() ? "NO TAGS YET" : buildTags), 1, neo::Cyan);
    const std::string guidance = summary.victory
        ? (forceMultiplier ? "FORCE MULTIPLIER ACTIVE // KEEP MATCHING THE BUILD" : "VICTORY // ADD A REACTION PAIR FOR MORE HEADROOM")
        : (sim.failureGuidance().empty() ? (summary.leaks > 0 ? "NEXT RUN // COVER LEAKS WITH " + compact(weaknesses, 3) : "NEXT RUN // LEAN INTO " + compact(strengths, 3)) : sim.failureGuidance());
    drawText(renderer, 390, 594, guidance, 1, summary.victory ? neo::Mint : neo::Amber);
    drawText(renderer, 390, 612, "STRONG " + compact(strengths, 3) + " // WEAK " + compact(weaknesses, 2), 1, neo::Muted);
    drawText(renderer, 390, 630, "R TO RESTART // ESC TO MAIN MENU", 1, neo::Text);
}

} // namespace

int main(int argc, char** argv) {
    bool headless = false;
    bool renderSmoke = false;
    bool textAuditRequested = std::getenv("TA_TEXT_AUDIT") != nullptr;
    std::string headlessReplayPath;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--headless" || std::string(argv[i]) == "--test") headless = true;
        if (std::string(argv[i]) == "--render-smoke") renderSmoke = true;
        if (std::string(argv[i]) == "--text-audit") textAuditRequested = true;
        if (std::string(argv[i]) == "--record-replay" && i + 1 < argc) headlessReplayPath = argv[++i];
    }
    if (headless) {
        GameSim sim(0x7A2026u);
        ta::ContentConfig authoredContent;
        const bool authoredLoaded = ta::loadContentConfig(ta::defaultContentDirectory(), authoredContent);
        if (authoredLoaded) sim.setContentConfig(authoredContent);
        ta::ReplayData replay;
        replay.seed = sim.initialSeed();
        replay.weapon = sim.weapon(); replay.support = sim.support(); replay.skull = sim.skull(); replay.skullMask = sim.skullMask();
        replay.ultimate = sim.ultimate(); replay.evolution = sim.ultimateEvolution(); replay.autoUltimate = sim.autoUltimate(); replay.arena = sim.arena();
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

    TextLayoutAudit textAudit(textAuditRequested);
    activeTextAudit = &textAudit;

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
    sim.setChassis(static_cast<ta::TowerChassis>(profile.equippedChassis));
    sim.setWeapon(static_cast<ta::Weapon>(profile.equippedWeapon));
    sim.setUltimate(static_cast<ta::Ultimate>(profile.equippedUltimate));
    if (profile.equippedUltimateModule < 10u && ta::isUltimateModuleUnlocked(profile, static_cast<ta::UltimateModule>(profile.equippedUltimateModule))) sim.setUltimateModule(static_cast<ta::UltimateModule>(profile.equippedUltimateModule));
    sim.setWorkshopProgress(profile.towerCoreLevel, profile.weaponModuleLevels);
    sim.setSupportProgress(profile.supportModuleLevels);
    sim.setSupport(static_cast<ta::SupportModule>(profile.equippedSupportModule));
    sim.setUltimateEvolution(static_cast<ta::UltimateEvolution>(profile.equippedUltimateEvolution));
    sim.setSkillLoadout(profile.skillLoadout);
    bool endlessMode = false;
    bool dailyMode = false;
    bool dailyBriefingExpanded = false;
    int dailyBriefingFocus = 0;
    const auto makeReplay = [&]() {
        ta::ReplayData result;
        result.seed = sim.initialSeed(); result.weapon = sim.weapon(); result.chassis = sim.chassis(); result.support = sim.support(); result.skull = sim.skull(); result.skullMask = sim.skullMask();
        result.ultimate = sim.ultimate(); result.evolution = sim.ultimateEvolution(); result.ultimateModule = sim.ultimateModule(); result.autoUltimate = sim.autoUltimate(); result.arena = sim.arena();
        result.contentHash = authoredContentHash;
        result.endless = endlessMode;
        result.dailyDateKey = dailyMode ? daily.dateKey : 0;
        result.skillLoadout = sim.skillLoadout();
        return result;
    };
    const auto prepareDailyRun = [&]() {
        ta::SkillLoadout dailySkillLoadout = profile.skillLoadout;
        for (const ta::SkillId required : daily.requiredSkills) {
            bool equipped = false;
            for (const ta::SkillId equippedSkill : dailySkillLoadout.skills) if (equippedSkill == required) equipped = true;
            if (equipped) continue;
            std::size_t replacement = ta::SkillSlotCount;
            for (std::size_t candidate = 0; candidate < ta::SkillSlotCount; ++candidate) {
                bool protectedSlot = false;
                for (const ta::SkillId other : daily.requiredSkills) if (dailySkillLoadout.skills[candidate] == other) protectedSlot = true;
                for (const ta::SkillId forbidden : daily.forbiddenSkills) if (dailySkillLoadout.skills[candidate] == forbidden) protectedSlot = false;
                if (!protectedSlot) { replacement = candidate; break; }
            }
            if (replacement < ta::SkillSlotCount) {
                dailySkillLoadout.skills[replacement] = required;
                dailySkillLoadout.nodeBuilds[replacement].clear();
            }
        }
        sim.setSkillLoadout(dailySkillLoadout);
        sim.setSkillRules(daily.requiredSkills, daily.forbiddenSkills, daily.allowedSkillBranches);
        sim.setWorkshopProgress(daily.workshopNormalized ? 0 : profile.towerCoreLevel, daily.workshopNormalized ? std::array<std::uint8_t, 5>{{0, 0, 0, 0, 0}} : profile.weaponModuleLevels);
        sim.setSupportProgress(daily.workshopNormalized ? std::array<std::uint8_t, 5>{{0, 0, 0, 0, 0}} : profile.supportModuleLevels);
        sim.setEndless(false);
        sim.setSupport(ta::SupportModule::None);
        if (daily.chassisRequired) sim.setChassis(daily.requiredChassis);
        sim.setWeapon(daily.weaponRequired ? daily.requiredWeapon : daily.recommendedWeapon);
        sim.setUltimate(daily.requiredUltimate);
        sim.setUltimateModule(static_cast<ta::UltimateModule>(255));
        sim.setUltimateEvolution(daily.requiredEvolution);
        sim.setSupport(daily.requiredSupport);
        sim.setSkullMask(daily.skullMask);
        sim.setArena(daily.arena);
        sim.setAutoUltimate(false);
        sim.setContentConfig(ta::contentForDailyChallenge(authoredContent, daily));
        sim.reset(daily.seed);
    };
    const auto prepareStandardRun = [&](std::uint32_t seed) {
        sim.setContentConfig(authoredContent);
        sim.setSkillLoadout(profile.skillLoadout);
        sim.setSkillRules({}, {});
        sim.setWorkshopProgress(profile.towerCoreLevel, profile.weaponModuleLevels);
        sim.setSupportProgress(profile.supportModuleLevels);
        sim.setEndless(endlessMode);
        sim.reset(seed);
    };
    ta::ReplayData replay = makeReplay();
    bool running = true;
    bool started = false;
    FrontendScreen screen = FrontendScreen::MainMenu;
    bool resultSaved = false;
    std::uint32_t resultRewardShards = 0;
    std::uint32_t resultRewardCoreParts = 0;
    std::uint32_t resultRewardLegendCores = 0;
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
    int hoverX = -1;
    int hoverY = -1;
    int targetingSkillSlot = -1;
    std::uint32_t hoverSince = SDL_GetTicks();
    int touchFocusX = -1;
    int touchFocusY = -1;
    bool touchFocusPrimed = false;
    int collectionCategory = 0;
    int collectionItem = 0;
    int menuFocus = 0;
    WorkshopPurchase workshopPurchase = WorkshopPurchase::None;
    int workshopPurchaseIndex = 0;
    int workshopSkillFocus = -1;
    const auto clearWorkshopPurchase = [&]() {
        workshopPurchase = WorkshopPurchase::None;
        workshopPurchaseIndex = 0;
    };
    const auto commitWorkshopPurchase = [&]() {
        bool purchased = false;
        if (workshopPurchase == WorkshopPurchase::TowerCore) purchased = ta::purchaseTowerCore(profile, authoredContent);
        else if (workshopPurchase == WorkshopPurchase::WeaponModule) purchased = ta::purchaseWeaponModule(profile, static_cast<ta::Weapon>(workshopPurchaseIndex), authoredContent);
        else if (workshopPurchase == WorkshopPurchase::SupportModule) purchased = ta::purchaseSupportModule(profile, static_cast<ta::SupportModule>(workshopPurchaseIndex), authoredContent);
        else if (workshopPurchase == WorkshopPurchase::UltimateEvolution) {
            const ta::UltimateEvolution evolution = evolutionForUltimate(sim.ultimate(), workshopPurchaseIndex);
            if (!ta::isUltimateEvolutionUnlocked(profile, evolution)) purchased = ta::unlockUltimateEvolution(profile, evolution, authoredContent);
            if (ta::isUltimateEvolutionUnlocked(profile, evolution)) {
                sim.setUltimateEvolution(evolution);
                profile.equippedUltimateEvolution = static_cast<std::uint8_t>(evolution);
                purchased = true;
            }
        } else if (workshopPurchase == WorkshopPurchase::UltimateModule) {
            const ta::UltimateModule module = static_cast<ta::UltimateModule>(static_cast<int>(sim.ultimate()) * 2 + workshopPurchaseIndex);
            if (!ta::isUltimateModuleUnlocked(profile, module)) purchased = ta::unlockUltimateModule(profile, module, authoredContent);
            if (ta::isUltimateModuleUnlocked(profile, module) && ta::equipUltimateModule(profile, module)) {
                sim.setUltimateModule(module);
                profile.equippedUltimateModule = static_cast<std::uint8_t>(module);
                purchased = true;
            }
        } else if (workshopPurchase == WorkshopPurchase::SkillNode && workshopPurchaseIndex >= 0 && workshopPurchaseIndex < static_cast<int>(authoredContent.skillNodes.size())) {
            purchased = ta::purchaseSkillNode(profile, authoredContent.skillNodes[static_cast<std::size_t>(workshopPurchaseIndex)], authoredContent);
        } else if (workshopPurchase == WorkshopPurchase::SkillUnlock && workshopPurchaseIndex >= 0 && workshopPurchaseIndex < static_cast<int>(ta::SkillId::Count)) {
            purchased = ta::unlockSkill(profile, static_cast<ta::SkillId>(workshopPurchaseIndex), authoredContent);
        }
        if (purchased) { saveCurrentProfile(); clearWorkshopPurchase(); }
    };
    const auto beginPreparedRun = [&]() {
        replay = makeReplay();
        started = true;
        resultSaved = false;
        previousKills = 0;
        previousShots = 0;
        previousUltimates = 0;
        previousWave = sim.waveNumber();
        previousUpgradePending = false;
        previousBossTelegraph = false;
        previousTerminal = false;
        resultRewardShards = 0;
        resultRewardCoreParts = 0;
        resultRewardLegendCores = 0;
        targetingSkillSlot = -1;
    };
    const auto castSkillFromPointer = [&](std::size_t slot, int pointerX, int pointerY) {
        if (slot >= ta::SkillSlotCount || !started || sim.upgradePending()) return false;
        const ta::SkillSnapshot snapshot = sim.skillSnapshot(slot);
        ta::TargetSpec target;
        target.mode = snapshot.targetMode;
        const float worldX = static_cast<float>(pointerX) > static_cast<float>(GameSim::Width) ? static_cast<float>(pointerX) / GameSim::WorldScale : static_cast<float>(pointerX);
        const float worldY = static_cast<float>(pointerY) > static_cast<float>(GameSim::Height) ? static_cast<float>(pointerY) / GameSim::WorldScale : static_cast<float>(pointerY);
        target.world = {std::clamp(worldX, 92.0f, static_cast<float>(GameSim::Width - 40)), std::clamp(worldY, 24.0f, static_cast<float>(GameSim::Height - 24))};
        if (target.mode == ta::SkillTargetMode::Enemy) {
            if (sim.enemies().empty()) return false;
            target.entityId = sim.enemies().front().id;
            target.world = sim.enemies().front().pos;
        } else if (target.mode == ta::SkillTargetMode::Ally) {
            if (sim.alliedUnits().empty()) return false;
            target.entityId = sim.alliedUnits().front().id;
            target.world = sim.alliedUnits().front().pos;
        }
        std::string skillError;
        if (!sim.activateSkill(slot, target, &skillError)) return false;
        ta::ReplayEvent event;
        event.tick = static_cast<std::uint32_t>(sim.stats().ticks + 1);
        event.action = ta::ReplayAction::SkillCast;
        event.slot = static_cast<std::uint8_t>(slot);
        event.skill = snapshot.skill;
        event.target = target;
        event.sequence = sim.lastSkillCastSequence();
        replay.events.push_back(event);
        targetingSkillSlot = -1;
        return true;
    };
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
            if (event.type == SDL_MOUSEMOTION) {
                if (hoverX != event.motion.x || hoverY != event.motion.y) hoverSince = SDL_GetTicks();
                hoverX = event.motion.x;
                hoverY = event.motion.y;
            }
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
                    case SDL_CONTROLLER_BUTTON_DPAD_UP: mappedKey = SDLK_UP; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: mappedKey = SDLK_DOWN; break;
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
                hoverSince = 0;
                if (!started && !settingsOpen && workshopPurchase == WorkshopPurchase::None && !dailyBriefingExpanded &&
                    (screen == FrontendScreen::MainMenu || screen == FrontendScreen::RunType || screen == FrontendScreen::ModifierSelect)) {
                    const int focusCount = screen == FrontendScreen::MainMenu ? 5 : (screen == FrontendScreen::RunType ? 4 : 2);
                    if (key == SDLK_UP || key == SDLK_LEFT) { menuFocus = (menuFocus + focusCount - 1) % focusCount; continue; }
                    if (key == SDLK_DOWN || key == SDLK_RIGHT) { menuFocus = (menuFocus + 1) % focusCount; continue; }
                    if (key == SDLK_RETURN || key == SDLK_SPACE || key == profile.inputBindings.key(ta::InputAction::Confirm)) {
                        if (screen == FrontendScreen::MainMenu) {
                            if (menuFocus == 0) { screen = FrontendScreen::RunType; menuFocus = 0; }
                            else if (menuFocus == 1) screen = FrontendScreen::Workshop;
                            else if (menuFocus == 2) screen = FrontendScreen::Collection;
                            else if (menuFocus == 3) { screen = ta::app::mainMenuSelection(menuFocus); menuFocus = 0; }
                            else running = false;
                        } else if (screen == FrontendScreen::RunType) {
                            if (menuFocus == 0) { endlessMode = false; dailyMode = false; screen = FrontendScreen::Loadout; }
                            else if (menuFocus == 1) { endlessMode = false; prepareDailyRun(); replay = makeReplay(); screen = FrontendScreen::Loadout; dailyMode = true; }
                            else if (menuFocus == 2) { endlessMode = true; dailyMode = false; screen = FrontendScreen::Loadout; }
                            else { screen = FrontendScreen::MainMenu; menuFocus = 0; }
                        } else if (menuFocus == 0) { screen = FrontendScreen::Loadout; menuFocus = 0; }
                        else { if (dailyMode) prepareDailyRun(); else prepareStandardRun(0x7A2026u); beginPreparedRun(); }
                        continue;
                    }
                }
                if (!started && !settingsOpen && screen != FrontendScreen::Loadout) {
                    if (screen == FrontendScreen::MainMenu) {
                        if (key == SDLK_RETURN || key == SDLK_SPACE) screen = FrontendScreen::RunType;
                        else if (key == SDLK_w) screen = FrontendScreen::Workshop;
                        else if (key == SDLK_c) screen = FrontendScreen::Collection;
                    } else if (screen == FrontendScreen::Collection) {
                        if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) screen = FrontendScreen::MainMenu;
                        else if (key == SDLK_LEFT || key == SDLK_a) { collectionCategory = (collectionCategory + 12) % 13; collectionItem = 0; }
                        else if (key == SDLK_RIGHT || key == SDLK_d) { collectionCategory = (collectionCategory + 1) % 13; collectionItem = 0; }
                        else if (key == SDLK_UP) --collectionItem;
                        else if (key == SDLK_DOWN) ++collectionItem;
                         const int count = collectionItemCount(collectionCategory);
                        if (collectionItem < 0) collectionItem = count - 1;
                        if (collectionItem >= count) collectionItem = 0;
                    } else if (screen == FrontendScreen::Workshop && workshopSkillFocus >= 0 && (key == SDLK_ESCAPE || key == SDLK_BACKSPACE)) {
                        workshopSkillFocus = -1;
                    } else if (screen == FrontendScreen::Workshop && key >= SDLK_F1 && key <= SDLK_F3) {
                        const std::size_t preset = static_cast<std::size_t>(key - SDLK_F1);
                        if (ta::equipSkillPreset(profile, preset)) { sim.setSkillLoadout(profile.skillLoadout); saveCurrentProfile(); }
                    } else if (screen == FrontendScreen::Workshop && key >= SDLK_1 && key <= SDLK_3) {
                        const std::size_t preset = static_cast<std::size_t>(key - SDLK_1);
                        if (ta::saveSkillPreset(profile, preset)) saveCurrentProfile();
                    } else if (screen == FrontendScreen::Workshop && key >= SDLK_6 && key <= SDLK_9) {
                        const int skillIndex = 5 + (key - SDLK_6);
                        if (skillIndex < static_cast<int>(ta::SkillId::Count) && !ta::isSkillUnlocked(profile, static_cast<ta::SkillId>(skillIndex))) { workshopPurchase = WorkshopPurchase::SkillUnlock; workshopPurchaseIndex = skillIndex; }
                    } else if (workshopPurchase != WorkshopPurchase::None) {
                        if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) clearWorkshopPurchase();
                        else if (key == SDLK_RETURN || key == SDLK_SPACE || key == profile.inputBindings.key(ta::InputAction::Confirm)) commitWorkshopPurchase();
                    } else if (dailyBriefingExpanded) {
                        if (key == SDLK_ESCAPE || key == SDLK_i) { dailyBriefingExpanded = false; dailyBriefingFocus = 0; }
                        else if (key == SDLK_LEFT || key == SDLK_UP) { dailyBriefingFocus = dailyBriefingFocus <= 1 ? dailyBriefingDetailCount(daily) : dailyBriefingFocus - 1; }
                        else if (key == SDLK_RIGHT || key == SDLK_DOWN) { dailyBriefingFocus = dailyBriefingFocus >= dailyBriefingDetailCount(daily) ? 1 : dailyBriefingFocus + 1; }
                    } else if ((screen == FrontendScreen::RunType || screen == FrontendScreen::ModifierSelect) && key == SDLK_i) {
                        dailyBriefingExpanded = true;
                    } else if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) {
                        screen = ta::app::backFrom(screen);
                    } else if (screen == FrontendScreen::ModifierSelect && (key == profile.inputBindings.key(ta::InputAction::Confirm) || key == SDLK_RETURN || key == SDLK_SPACE)) {
                        if (dailyMode) prepareDailyRun();
                        else prepareStandardRun(0x7A2026u);
                        beginPreparedRun();
                    } else if (screen == FrontendScreen::RunType && key == SDLK_d) {
                        endlessMode = false;
                        prepareDailyRun();
                        replay = makeReplay();
                        screen = FrontendScreen::Loadout;
                        dailyMode = true;
                        dailyBriefingExpanded = false;
                    } else if (screen == FrontendScreen::RunType && key == SDLK_s) {
                        endlessMode = false;
                        dailyMode = false;
                        screen = FrontendScreen::Loadout;
                    } else if (screen == FrontendScreen::RunType && key == SDLK_e) {
                        endlessMode = true;
                        dailyMode = false;
                        screen = FrontendScreen::Loadout;
                    }
                    continue;
                }
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
                if (key == SDLK_ESCAPE && started && (sim.isGameOver() || sim.isVictory())) {
                    started = false;
                    paused = false;
                    dailyMode = false;
                    screen = FrontendScreen::MainMenu;
                    menuFocus = 0;
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
                    if (!started && screen == FrontendScreen::Loadout) {
                     for (int index = 0; index < 3; ++index) if (key == SDLK_F4 + index && (!dailyMode || !daily.chassisRequired || static_cast<ta::TowerChassis>(index) == daily.requiredChassis)) { sim.setChassis(static_cast<ta::TowerChassis>(index)); profile.equippedChassis = static_cast<std::uint8_t>(index); saveCurrentProfile(); hoverX = loadoutChassisCard(index).x + 12; hoverY = loadoutChassisCard(index).y + 10; }
                     for (int index = 0; index < 5; ++index) if (key == profile.inputBindings.key(indexedAction(ta::InputAction::Weapon1, index))) { sim.setWeapon(static_cast<ta::Weapon>(index)); if (!dailyMode) { profile.equippedWeapon = static_cast<std::uint8_t>(index); saveCurrentProfile(); } hoverX = loadoutWeaponCard(index).x + 70; hoverY = loadoutWeaponCard(index).y + 58; }
                     for (int index = 0; index < 4; ++index) if (key == profile.inputBindings.key(indexedAction(ta::InputAction::Skull1, index)) && !dailyMode) { sim.toggleSkull(static_cast<ta::Skull>(index + 1)); hoverX = loadoutSkullCard(index).x + 70; hoverY = loadoutSkullCard(index).y + 36; }
                     for (int index = 0; index < 5; ++index) if (key == profile.inputBindings.key(indexedAction(ta::InputAction::Ultimate1, index)) && (!dailyMode || static_cast<ta::Ultimate>(index) == daily.requiredUltimate)) { sim.setUltimate(static_cast<ta::Ultimate>(index)); if (!dailyMode) { profile.equippedUltimate = static_cast<std::uint8_t>(index); profile.equippedUltimateModule = 255u; saveCurrentProfile(); } hoverX = loadoutUltimateCard(index).x + 80; hoverY = loadoutUltimateCard(index).y + 24; }
                     if (key == SDLK_z && (!dailyMode || daily.requiredSupport == ta::SupportModule::None)) { sim.setSupport(ta::SupportModule::None); profile.equippedSupportModule = 0; saveCurrentProfile(); hoverX = loadoutSupportCard(0).x + 18; hoverY = loadoutSupportCard(0).y + 5; }
                     if (key == SDLK_x && (!dailyMode || daily.requiredSupport == ta::SupportModule::CreditRelay)) { sim.setSupport(ta::SupportModule::CreditRelay); profile.equippedSupportModule = 1; saveCurrentProfile(); hoverX = loadoutSupportCard(1).x + 18; hoverY = loadoutSupportCard(1).y + 5; }
                     if (key == SDLK_c && (!dailyMode || daily.requiredSupport == ta::SupportModule::StasisField)) { sim.setSupport(ta::SupportModule::StasisField); profile.equippedSupportModule = 2; saveCurrentProfile(); hoverX = loadoutSupportCard(2).x + 18; hoverY = loadoutSupportCard(2).y + 5; }
                     if (key == SDLK_v && (!dailyMode || daily.requiredSupport == ta::SupportModule::RepairDrones)) { sim.setSupport(ta::SupportModule::RepairDrones); profile.equippedSupportModule = 3; saveCurrentProfile(); hoverX = loadoutSupportCard(3).x + 18; hoverY = loadoutSupportCard(3).y + 5; }
                     if (key == SDLK_b && (!dailyMode || daily.requiredSupport == ta::SupportModule::CorrosionAmp)) { sim.setSupport(ta::SupportModule::CorrosionAmp); profile.equippedSupportModule = 4; saveCurrentProfile(); hoverX = loadoutSupportCard(4).x + 18; hoverY = loadoutSupportCard(4).y + 5; }
                     if (key == SDLK_F1 && (!dailyMode || daily.arena == ta::Arena::Moonbase)) { sim.setArena(ta::Arena::Moonbase); hoverX = loadoutArenaCard(0).x + 76; hoverY = loadoutArenaCard(0).y + 12; }
                     if (key == SDLK_F2 && (!dailyMode || daily.arena == ta::Arena::EmberCrater)) { sim.setArena(ta::Arena::EmberCrater); hoverX = loadoutArenaCard(1).x + 76; hoverY = loadoutArenaCard(1).y + 12; }
                     if (key == SDLK_F3 && (!dailyMode || daily.arena == ta::Arena::NeonRuins)) { sim.setArena(ta::Arena::NeonRuins); hoverX = loadoutArenaCard(2).x + 76; hoverY = loadoutArenaCard(2).y + 12; }
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
                        prepareDailyRun();
                        replay = makeReplay();
                        screen = FrontendScreen::ModifierSelect;
                        dailyMode = true;
                    }
                    if (key == profile.inputBindings.key(ta::InputAction::Confirm) || key == SDLK_SPACE) {
                        screen = FrontendScreen::ModifierSelect;
                    }
                 } else {
                    if (targetingSkillSlot >= 0 && (key == SDLK_ESCAPE || key == SDLK_BACKSPACE)) { targetingSkillSlot = -1; continue; }
                    if (!sim.upgradePending() && key >= SDLK_1 && key <= SDLK_5) {
                        const std::size_t slot = static_cast<std::size_t>(key - SDLK_1);
                        const ta::SkillSnapshot snapshot = sim.skillSnapshot(slot);
                        if (snapshot.targetMode == ta::SkillTargetMode::None) castSkillFromPointer(slot, GameSim::Width / 2, GameSim::Height / 2);
                        else targetingSkillSlot = targetingSkillSlot == static_cast<int>(slot) ? -1 : static_cast<int>(slot);
                    }
                    if (key == SDLK_r && sim.upgradePending()) {
                        if (sim.rerollUpgradeChoices()) replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Reroll, 0});
                    }
                    const int upgradeChoice = key == profile.inputBindings.key(ta::InputAction::Upgrade1) ? 0 : (key == profile.inputBindings.key(ta::InputAction::Upgrade2) ? 1 : (key == profile.inputBindings.key(ta::InputAction::Upgrade3) ? 2 : -1));
                    if (upgradeChoice >= 0 && sim.upgradePending() && !(key == SDLK_r)) {
                        sim.chooseUpgrade(upgradeChoice);
                        replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Upgrade, static_cast<std::uint8_t>(upgradeChoice)});
                    }
                    if (key == profile.inputBindings.key(ta::InputAction::Ultimate) || key == SDLK_u || key == SDLK_SPACE) {
                        const int previous = sim.stats().ultimates;
                        sim.activateUltimate();
                        if (sim.stats().ultimates != previous) replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Ultimate, 0});
                    }
                    if (key == profile.inputBindings.key(ta::InputAction::Restart)) {
                        if (dailyMode) prepareDailyRun();
                        else prepareStandardRun(0x7A2026u);
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
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT && targetingSkillSlot >= 0) {
                targetingSkillSlot = -1;
                continue;
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
                if (event.type == SDL_FINGERDOWN && !started && screen == FrontendScreen::Loadout) {
                    bool detailTarget = false;
                    for (int index = 0; index < 3; ++index) detailTarget = detailTarget || loadoutChassisCard(index).contains(x, y) || loadoutArenaCard(index).contains(x, y);
                    for (int index = 0; index < 5; ++index) {
                        detailTarget = detailTarget || loadoutWeaponCard(index).contains(x, y) || loadoutSupportCard(index).contains(x, y) || loadoutUltimateCard(index).contains(x, y);
                    }
                    for (int index = 0; index < 4; ++index) detailTarget = detailTarget || loadoutSkullCard(index).contains(x, y);
                    if (detailTarget && (!touchFocusPrimed || touchFocusX != x || touchFocusY != y)) {
                        touchFocusX = x;
                        touchFocusY = y;
                        touchFocusPrimed = true;
                        hoverX = x;
                        hoverY = y;
                        hoverSince = 0;
                        continue;
                    }
                    if (detailTarget) {
                        touchFocusPrimed = false;
                        touchFocusX = -1;
                        touchFocusY = -1;
                        hoverX = -1;
                        hoverY = -1;
                    }
                }
                // SDL scales mouse button events into the renderer's logical
                // coordinates when SDL_RenderSetLogicalSize is active. Do
                // not pass those already-logical values through
                // SDL_RenderWindowToLogical a second time; doing so shifts
                // hit tests and makes adjacent buttons appear misaligned.
                if (settingsOpen || (!started && screen == FrontendScreen::Settings)) {
                    activeDevice = event.type == SDL_FINGERDOWN ? "TOUCH" : "MOUSE";
                    if (settingsCloseButton.contains(x, y)) {
                        settingsOpen = false;
                        if (!started && screen == FrontendScreen::Settings) { screen = ta::app::backFrom(screen); menuFocus = 3; }
                        if (started) paused = false;
                    } else if (settingsToggleButton(0).contains(x, y) || settingsToggleButton(1).contains(x, y) || settingsToggleButton(2).contains(x, y) || settingsToggleButton(3).contains(x, y)) {
                        if (settingsToggleButton(0).contains(x, y)) { profile.reducedFlashes = !profile.reducedFlashes; reducedFlashes = profile.reducedFlashes; }
                        else if (settingsToggleButton(1).contains(x, y)) { profile.highContrast = !profile.highContrast; highContrast = profile.highContrast; }
                        else if (settingsToggleButton(2).contains(x, y)) profile.subtitles = !profile.subtitles;
                        else profile.vibration = !profile.vibration;
                        saveCurrentProfile();
                    }
                    continue;
                }
                if (!started && screen != FrontendScreen::Loadout) {
                    if (screen == FrontendScreen::MainMenu) {
                        if (mainStartButton.contains(x, y)) screen = FrontendScreen::RunType;
                        else if (mainWorkshopButton.contains(x, y)) screen = FrontendScreen::Workshop;
                        else if (mainCollectionButton.contains(x, y)) screen = FrontendScreen::Collection;
                        else if (mainSettingsButton.contains(x, y)) { screen = FrontendScreen::Settings; menuFocus = 0; }
                        else if (mainQuitButton.contains(x, y)) running = false;
                    } else if (screen == FrontendScreen::RunType) {
                        if (dailyBriefingExpanded) {
                            if (dailyBriefingCloseButton.contains(x, y)) { dailyBriefingExpanded = false; dailyBriefingFocus = 0; }
                            else if (dailyBriefingDetailAt(daily, x, y) > 0) dailyBriefingFocus = dailyBriefingDetailAt(daily, x, y);
                        } else if (runStandardButton.contains(x, y)) { endlessMode = false; dailyMode = false; screen = FrontendScreen::Loadout; }
                        else if (runEndlessButton.contains(x, y)) { endlessMode = true; dailyMode = false; screen = FrontendScreen::Loadout; }
                        else if (runDailyButton.contains(x, y)) {
                            endlessMode = false;
                            prepareDailyRun();
                            replay = makeReplay();
                            screen = FrontendScreen::Loadout;
                            dailyMode = true;
                            dailyBriefingExpanded = false;
                        } else if (dailyBriefingCard.contains(x, y)) {
                            dailyBriefingExpanded = true;
                        } else if (workshopSkillFocus >= 0) {
                            if (workshopSkillTreeCloseButton.contains(x, y)) workshopSkillFocus = -1;
                            else {
                                const std::vector<int> nodes = skillTreeNodeIndices(authoredContent, workshopSkillFocus, profile);
                                for (std::size_t local = 0; local < nodes.size(); ++local) if (workshopSkillTreeNodeButton(static_cast<int>(local)).contains(x, y)) {
                                    const ta::SkillNodeDefinition& node = authoredContent.skillNodes[static_cast<std::size_t>(nodes[local])];
                                    if (ta::purchasedSkillNodeRank(profile, node.id) > 0) {
                                        if (ta::equipSkillNode(profile, static_cast<std::size_t>(workshopSkillFocus), node.id, authoredContent)) { sim.setSkillLoadout(profile.skillLoadout); saveCurrentProfile(); }
                                    } else if (node.parentId.empty() || ta::purchasedSkillNodeRank(profile, node.parentId) > 0) {
                                        workshopPurchaseIndex = nodes[local];
                                        workshopPurchase = WorkshopPurchase::SkillNode;
                                    }
                                }
                            }
                        } else if (runTypeBackButton.contains(x, y)) screen = FrontendScreen::MainMenu;
                    } else if (screen == FrontendScreen::ModifierSelect) {
                        if (dailyBriefingExpanded) {
                            if (dailyBriefingCloseButton.contains(x, y)) { dailyBriefingExpanded = false; dailyBriefingFocus = 0; }
                            else if (dailyBriefingDetailAt(daily, x, y) > 0) dailyBriefingFocus = dailyBriefingDetailAt(daily, x, y);
                        } else if (modifierBackButton.contains(x, y)) screen = FrontendScreen::Loadout;
                        else if (modifierConfirmButton.contains(x, y)) {
                            if (dailyMode) prepareDailyRun();
                            else prepareStandardRun(0x7A2026u);
                            beginPreparedRun();
                        } else if (dailyMode && dailyModifierBriefingCard.contains(x, y)) {
                            dailyBriefingExpanded = true;
                        }
                    } else if (screen == FrontendScreen::Workshop) {
                        if (workshopPurchase != WorkshopPurchase::None) {
                            if (workshopConfirmCancelButton.contains(x, y)) clearWorkshopPurchase();
                            else if (workshopConfirmAcceptButton.contains(x, y)) commitWorkshopPurchase();
                        } else if (workshopBackButton.contains(x, y)) screen = FrontendScreen::MainMenu;
                        else for (int preset = 0; preset < 3; ++preset) if (workshopPresetButton(preset).contains(x, y)) { if (ta::equipSkillPreset(profile, static_cast<std::size_t>(preset))) { sim.setSkillLoadout(profile.skillLoadout); saveCurrentProfile(); } }
                        else if (workshopTowerButton.contains(x, y)) { workshopPurchase = WorkshopPurchase::TowerCore; workshopPurchaseIndex = 0; }
                        else for (int index = 0; index < 5; ++index) if (workshopModuleButton(index).contains(x, y)) { workshopPurchase = WorkshopPurchase::WeaponModule; workshopPurchaseIndex = index; }
                        else for (int index = 1; index < 5; ++index) if (workshopSupportButton(index).contains(x, y)) { workshopPurchase = WorkshopPurchase::SupportModule; workshopPurchaseIndex = index; }
                        else for (int slot = 0; slot < 3; ++slot) if (workshopUltimateButton(slot).contains(x, y)) { workshopPurchase = WorkshopPurchase::UltimateEvolution; workshopPurchaseIndex = slot; }
                        else for (int slot = 0; slot < 2; ++slot) if (workshopUltimateModuleButton(slot).contains(x, y)) { workshopPurchase = WorkshopPurchase::UltimateModule; workshopPurchaseIndex = slot; }
                        else for (int slot = 0; slot < static_cast<int>(ta::SkillSlotCount); ++slot) if (workshopSkillButton(slot).contains(x, y)) workshopSkillFocus = slot;
                    } else if (screen == FrontendScreen::Collection) {
                        if (collectionBackButton.contains(x, y)) screen = FrontendScreen::MainMenu;
                        else {
                            for (int index = 0; index < 13; ++index) if (collectionCategoryButton(index).contains(x, y)) { collectionCategory = index; collectionItem = 0; }
                        }
                    }
                    continue;
                }
                if (!started && screen == FrontendScreen::Loadout) {
                    for (int index = 0; index < 3; ++index) if (loadoutChassisCard(index).contains(x, y) && (!dailyMode || !daily.chassisRequired || static_cast<ta::TowerChassis>(index) == daily.requiredChassis)) { sim.setChassis(static_cast<ta::TowerChassis>(index)); profile.equippedChassis = static_cast<std::uint8_t>(index); saveCurrentProfile(); }
                    for (int i = 0; i < 5; ++i) if (loadoutWeaponCard(i).contains(x, y) && (!dailyMode || !daily.weaponRequired || static_cast<ta::Weapon>(i) == daily.requiredWeapon)) { sim.setWeapon(static_cast<ta::Weapon>(i)); if (!dailyMode) { profile.equippedWeapon = static_cast<std::uint8_t>(i); saveCurrentProfile(); } }
                    for (int i = 0; i < 3; ++i) if (loadoutArenaCard(i).contains(x, y) && (!dailyMode || static_cast<ta::Arena>(i) == daily.arena)) sim.setArena(static_cast<ta::Arena>(i));
                    for (int i = 0; i < 4; ++i) if (loadoutSkullCard(i).contains(x, y) && !dailyMode) sim.toggleSkull(static_cast<ta::Skull>(i + 1));
                    for (int i = 0; i < 5; ++i) if (loadoutSkinCard(i).contains(x, y)) {
                        skinPreview = i;
                        const ta::TowerSkin skin = static_cast<ta::TowerSkin>(i);
                        if (!ta::isSkinUnlocked(profile, skin)) ta::unlockSkin(profile, skin);
                        if (ta::isSkinUnlocked(profile, skin)) { ta::equipSkin(profile, skin); sim.setSkin(skin); saveCurrentProfile(); }
                    }
                    for (int i = 0; i < 5; ++i) if (loadoutUltimateCard(i).contains(x, y) && (!dailyMode || static_cast<ta::Ultimate>(i) == daily.requiredUltimate)) { sim.setUltimate(static_cast<ta::Ultimate>(i)); if (!dailyMode) { profile.equippedUltimate = static_cast<std::uint8_t>(i); profile.equippedUltimateModule = 255u; saveCurrentProfile(); } }
                    for (int i = 0; i < 5; ++i) if (loadoutSupportCard(i).contains(x, y) && (!dailyMode || static_cast<ta::SupportModule>(i) == daily.requiredSupport)) { sim.setSupport(static_cast<ta::SupportModule>(i)); profile.equippedSupportModule = static_cast<std::uint8_t>(i); saveCurrentProfile(); }
                    if (!dailyMode) for (int slot = 0; slot < static_cast<int>(ta::SkillSlotCount); ++slot) if (loadoutSkillButton(slot).contains(x, y) && cycleSkillLoadout(profile, sim, slot, authoredContent)) saveCurrentProfile();
                    if (loadoutStartButton.contains(x, y)) {
                        dailyMode = false;
                        screen = FrontendScreen::ModifierSelect;
                    }
                    if (loadoutDailyButton.contains(x, y)) {
                        prepareDailyRun();
                        dailyMode = true;
                        screen = FrontendScreen::ModifierSelect;
                    }
                } else if (sim.upgradePending()) {
                    if (upgradeRerollButton.contains(x, y)) {
                        if (sim.rerollUpgradeChoices()) replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Reroll, 0});
                    }
                    for (int i = 0; i < 3; ++i) if (upgradeChoiceButton(i).contains(x, y)) {
                        const int previous = sim.stats().upgrades;
                        sim.chooseUpgrade(i);
                        if (sim.stats().upgrades != previous) replay.events.push_back({static_cast<std::uint32_t>(sim.stats().ticks + 1), ta::ReplayAction::Upgrade, static_cast<std::uint8_t>(i)});
                    }
                } else if (targetingSkillSlot >= 0 && y < 610) {
                    castSkillFromPointer(static_cast<std::size_t>(targetingSkillSlot), x, y);
                } else if (targetingSkillSlot >= 0 && skillTargetCancelButton.contains(x, y)) {
                    targetingSkillSlot = -1;
                } else if (y >= 610 && y < 700) {
                    for (int index = 0; index < static_cast<int>(ta::SkillSlotCount); ++index) if (skillSlotButton(index).contains(x, y)) {
                        const ta::SkillSnapshot snapshot = sim.skillSnapshot(static_cast<std::size_t>(index));
                        if (snapshot.targetMode == ta::SkillTargetMode::None) castSkillFromPointer(static_cast<std::size_t>(index), GameSim::Width / 2, GameSim::Height / 2);
                        else targetingSkillSlot = targetingSkillSlot == index ? -1 : index;
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
            resultRewardShards = static_cast<std::uint32_t>(sim.stats().kills / 8 + (sim.stats().wave >= 10 ? 40 : sim.stats().wave * 2) + (dailyMode ? daily.bonusShards : 0));
            resultRewardCoreParts = static_cast<std::uint32_t>(sim.stats().kills / 4 + sim.stats().wave * 3 + (sim.stats().wave >= 10 ? 50 : 0) + (dailyMode ? 20 : 0));
            ta::awardRunCosmetics(profile, sim.stats(), dailyMode, daily.bonusShards);
            resultRewardLegendCores += ta::awardRunProgression(profile, sim.stats(), dailyMode, sim.ultimate());
            if (dailyMode && sim.isVictory() && ta::claimDailyLegendCores(profile, daily.dateKey, daily.legendCoreReward)) resultRewardLegendCores = daily.legendCoreReward;
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
        if (activeTextAudit != nullptr) {
            if (started) activeTextAudit->setScreen("GAMEPLAY");
            else if (screen == FrontendScreen::MainMenu) activeTextAudit->setScreen("MAIN_MENU");
            else if (screen == FrontendScreen::RunType) activeTextAudit->setScreen("RUN_TYPE");
            else if (screen == FrontendScreen::ModifierSelect) activeTextAudit->setScreen("MODIFIERS");
            else if (screen == FrontendScreen::Workshop) activeTextAudit->setScreen("WORKSHOP");
            else if (screen == FrontendScreen::Collection) activeTextAudit->setScreen("COLLECTION");
            else if (screen == FrontendScreen::Settings) activeTextAudit->setScreen("SETTINGS");
            else activeTextAudit->setScreen("LOADOUT");
        }
        if (!started) {
            if (screen == FrontendScreen::MainMenu) drawMainMenu(renderer, profile, daily, menuFocus);
            else if (screen == FrontendScreen::RunType) drawRunTypeSelect(renderer, daily, profile, authoredContent, menuFocus);
            else if (screen == FrontendScreen::ModifierSelect) drawModifierSelect(renderer, sim, daily, dailyMode, endlessMode, menuFocus);
            else if (screen == FrontendScreen::Workshop) drawWorkshopScreen(renderer, profile, sim.ultimate(), authoredContent);
            else if (screen == FrontendScreen::Collection) drawCollectionScreen(renderer, collectionCategory, collectionItem, sim.contentConfig());
            else if (screen == FrontendScreen::Settings) drawMainMenu(renderer, profile, daily, 3);
            else { drawLoadout(renderer, sim, profile, daily, dailyMode); drawSkinStrip(renderer, profile, sim.skin()); }
        }
        if (!started && dailyBriefingExpanded) drawDailyBriefingOverlay(renderer, daily, authoredContent, profile, hoverX, hoverY, dailyBriefingFocus);
        if (!started && screen == FrontendScreen::Workshop && workshopSkillFocus >= 0 && workshopPurchase == WorkshopPurchase::None) drawWorkshopSkillTree(renderer, profile, workshopSkillFocus, authoredContent);
        if (!started && screen == FrontendScreen::Workshop && workshopPurchase != WorkshopPurchase::None) drawWorkshopConfirmation(renderer, profile, workshopPurchase, workshopPurchaseIndex, sim.ultimate(), authoredContent);
        if (!started && screen == FrontendScreen::Loadout && hoverX >= 0 && hoverY >= 0 && (hoverSince == 0 || SDL_GetTicks() - hoverSince >= 300u)) drawLoadoutTooltip(renderer, hoverX, hoverY, sim);
        if (started) { drawArena(renderer, highContrast, sim.arena(), authoredContent); drawWorld(renderer, sim); drawSkillTargetPreview(renderer, sim, targetingSkillSlot, hoverX, hoverY); drawHud(renderer, sim, highContrast, profile.subtitles); if (sim.upgradePending()) drawUpgradeOverlay(renderer, sim); if (paused && !settingsOpen) drawPauseOverlay(renderer); drawResultsOverlay(renderer, sim, resultRewardShards, resultRewardCoreParts, resultRewardLegendCores, dailyMode); }
        if (settingsOpen || (!started && screen == FrontendScreen::Settings)) drawSettingsOverlay(renderer, profile, remappingAction, activeDevice);
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
