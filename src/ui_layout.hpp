#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ta::ui {

struct UiRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    constexpr bool contains(int pointX, int pointY) const {
        return pointX >= x && pointX < x + width && pointY >= y && pointY < y + height;
    }
};

enum class LayoutRole { Container, Interactive, Text, Decoration };

struct LayoutElement {
    const char* name = "";
    UiRect bounds{};
    LayoutRole role = LayoutRole::Decoration;
};

inline constexpr bool intersects(const UiRect& first, const UiRect& second) {
    return first.x < second.x + second.width && second.x < first.x + first.width &&
           first.y < second.y + second.height && second.y < first.y + first.height;
}

inline constexpr bool containsRect(const UiRect& outer, const UiRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

// Containment is intentional: text, decoration, and interactive controls are
// expected to live inside their panel. Sibling controls/text must never share
// pixels because hit testing then becomes ambiguous and labels become unreadable.
inline constexpr bool allowedOverlap(const LayoutElement& first, const LayoutElement& second) {
    if (!intersects(first.bounds, second.bounds)) return true;
    if (first.role == LayoutRole::Container) return containsRect(first.bounds, second.bounds);
    if (second.role == LayoutRole::Container) return containsRect(second.bounds, first.bounds);
    return false;
}

inline std::vector<std::string> unexpectedOverlaps(const std::vector<LayoutElement>& elements) {
    std::vector<std::string> issues;
    for (std::size_t first = 0; first < elements.size(); ++first) {
        for (std::size_t second = first + 1; second < elements.size(); ++second) {
            if (allowedOverlap(elements[first], elements[second])) continue;
            issues.push_back(std::string(elements[first].name) + " overlaps " + elements[second].name);
        }
    }
    return issues;
}

inline constexpr UiRect mainStartButton{430, 270, 420, 58};
inline constexpr UiRect mainWorkshopButton{430, 344, 420, 58};
inline constexpr UiRect mainCollectionButton{430, 418, 420, 58};
inline constexpr UiRect mainSettingsButton{430, 492, 200, 52};
inline constexpr UiRect mainQuitButton{650, 492, 200, 52};
inline constexpr UiRect runStandardPanel{220, 220, 380, 140};
inline constexpr UiRect runDailyPanel{680, 220, 380, 140};
inline constexpr UiRect runEndlessPanel{220, 370, 380, 140};
inline constexpr UiRect runStandardButton{240, 235, 340, 62};
inline constexpr UiRect runDailyButton{700, 235, 340, 62};
inline constexpr UiRect runEndlessButton{240, 385, 340, 62};
inline constexpr UiRect runTypeBackButton{1040, 96, 160, 42};
inline constexpr UiRect workshopBackButton{1040, 96, 160, 42};
inline constexpr UiRect collectionBackButton{1040, 96, 160, 42};
inline constexpr UiRect modifierConfirmButton{760, 570, 300, 52};
inline constexpr UiRect modifierBackButton{420, 570, 300, 52};
inline constexpr UiRect dailyBriefingCard{140, 525, 1000, 175};
inline constexpr UiRect dailyBriefingCloseButton{790, 590, 250, 46};
inline constexpr UiRect dailyModifierBriefingCard{180, 350, 860, 174};
inline UiRect dailyEnemyBriefingRow(int index) { return {140, 370 + index * 22, 1040, 21}; }
inline UiRect dailySkullBriefingRow(int index) { return {140, 470 + index * 22, 1040, 21}; }
inline constexpr UiRect loadoutAutoButton{460, 570, 360, 18};
inline constexpr UiRect loadoutStartButton{460, 640, 360, 38};
inline constexpr UiRect loadoutDailyButton{850, 640, 240, 38};
inline constexpr UiRect loadoutFrame{96, 54, 1088, 646};
inline constexpr UiRect loadoutPanel{140, 90, 1000, 610};
inline constexpr UiRect loadoutDailyHeaderRegion{610, 90, 200, 64};
inline constexpr UiRect loadoutSkinHeaderRegion{840, 90, 280, 92};
inline constexpr UiRect workshopConfirmCancelButton{400, 510, 260, 52};
inline constexpr UiRect workshopConfirmAcceptButton{700, 510, 260, 52};
inline constexpr UiRect settingsCloseButton{760, 555, 270, 42};
inline constexpr UiRect workshopTowerButton{190, 220, 270, 170};
inline constexpr UiRect upgradeRerollButton{500, 574, 220, 28};

inline UiRect settingsToggleButton(int index) {
    const std::array<UiRect, 4> buttons{{
        {230, 270, 190, 60}, {420, 270, 180, 60}, {600, 270, 200, 60}, {800, 270, 230, 60}
    }};
    return buttons[static_cast<std::size_t>(std::clamp(index, 0, 3))];
}

inline UiRect workshopModuleButton(int index) { return {160 + index * 205, 410, 190, 52}; }
inline UiRect workshopSupportButton(int index) { return {160 + index * 205, 470, 190, 28}; }
inline UiRect workshopSkillButton(int index) { return {160 + index * 220, 600, 190, 54}; }
inline UiRect workshopPresetButton(int index) { return {160 + index * 220, 545, 190, 28}; }
inline constexpr UiRect workshopSkillTreeCloseButton{930, 135, 180, 38};
inline UiRect workshopSkillTreeNodeButton(int index) { return {150 + (index % 4) * 270, 205 + (index / 4) * 92, 245, 78}; }
inline UiRect loadoutWeaponCard(int index) { return {190 + index * 180, 190, 140, 120}; }
inline UiRect loadoutChassisCard(int index) { return {190 + index * 250, 320, 220, 24}; }
inline UiRect loadoutArenaCard(int index) { return {190 + index * 250, 350, 220, 36}; }
inline UiRect loadoutSkullCard(int index) { return {300 + index * 180, 420, 140, 72}; }
inline UiRect loadoutSupportCard(int index) { return {160 + index * 190, 494, 170, 20}; }
inline UiRect loadoutUltimateCard(int index) { return {160 + index * 190, 520, 170, 48}; }
inline UiRect loadoutSkillButton(int index) { return {190 + index * 180, 590, 140, 38}; }
inline UiRect loadoutSkinCard(int index) { return {850 + index * 52, 118, 44, 28}; }
inline UiRect upgradeChoiceButton(int index) { return {220 + index * 290, 245, 220, 235}; }
inline UiRect skillSlotButton(int index) { return {270 + index * 124, 620, 108, 72}; }
inline constexpr UiRect ultimateSkillButton{910, 610, 150, 82};
inline constexpr UiRect skillTargetCancelButton{1080, 620, 130, 34};
inline UiRect collectionCategoryButton(int index) { return {130 + (index % 5) * 220, 215 + (index / 5) * 72, 200, 56}; }

inline int collectionItemCount(int category) {
    if (category == 0) return 5;
    if (category == 1) return 15;
    if (category == 2) return 7;
    if (category == 6) return 3;
    if (category == 7) return 4;
    if (category == 8) return 3;
    if (category == 10) return 4;
    if (category == 11) return 10;
    if (category == 12) return 10;
    return 5;
}

inline UiRect workshopUltimateButton(int index) { return {820, 296 + index * 24, 250, 22}; }
inline UiRect workshopUltimateModuleButton(int index) { return {820, 368 + index * 11, 250, 10}; }

} // namespace ta::ui
