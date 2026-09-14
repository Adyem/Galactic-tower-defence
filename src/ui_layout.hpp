#pragma once

#include "ui_text.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>
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

// The legacy LayoutElement helpers are useful for small static checks. UiNode
// and UiScene are the migration boundary for the full interface: one scene
// description will eventually drive drawing, hit testing, focus, tooltips,
// and diagnostics instead of each screen maintaining separate rectangles.
struct UiNode {
    std::string id;
    std::string parentId;
    std::string actionId;
    UiRect bounds{};
    LayoutRole role = LayoutRole::Decoration;
    int zOrder = 0;
    bool enabled = true;
};

struct UiValidationIssue {
    std::string code;
    std::string nodeId;
    std::string relatedId;
    std::string detail;
};

struct UiTextNode {
    std::string id;
    std::string parentId;
    UiRect bounds{};
    std::string text;
    int scale = 1;
    int padding = 0;
    int lineGap = 18;
    bool wrap = true;
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

class UiScene {
public:
    explicit UiScene(UiRect viewport) : viewport_(viewport) {}

    void clear() { nodes_.clear(); textNodes_.clear(); }
    void add(UiNode node) { nodes_.push_back(std::move(node)); }
    void addText(UiTextNode node) { textNodes_.push_back(std::move(node)); }
    const std::vector<UiNode>& nodes() const { return nodes_; }
    const std::vector<UiTextNode>& textNodes() const { return textNodes_; }

    std::vector<UiValidationIssue> validate() const {
        std::vector<UiValidationIssue> issues;
        for (std::size_t index = 0; index < nodes_.size(); ++index) {
            const UiNode& node = nodes_[index];
            if (node.id.empty()) issues.push_back({"EMPTY_ID", node.id, {}, "every UI node needs a stable ID"});
            if (node.bounds.width <= 0 || node.bounds.height <= 0) {
                issues.push_back({"INVALID_BOUNDS", node.id, {}, "UI node has a non-positive size"});
            }
            if (node.parentId.empty()) {
                if (!containsRect(viewport_, node.bounds)) issues.push_back({"VIEWPORT_ESCAPE", node.id, {}, "root node is outside the logical viewport"});
            } else {
                const UiNode* parent = find(node.parentId);
                if (parent == nullptr) issues.push_back({"MISSING_PARENT", node.id, node.parentId, "parent node does not exist"});
                else {
                    if (parent->role != LayoutRole::Container) issues.push_back({"INVALID_PARENT", node.id, parent->id, "only containers may own child nodes"});
                    if (!containsRect(parent->bounds, node.bounds)) issues.push_back({"PARENT_ESCAPE", node.id, parent->id, "child node escapes its declared parent"});
                }
            }
            if (node.role == LayoutRole::Interactive && node.actionId.empty()) {
                issues.push_back({"MISSING_ACTION", node.id, {}, "interactive node has no semantic action ID"});
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                const UiNode& other = nodes_[previous];
                if (node.id == other.id) issues.push_back({"DUPLICATE_ID", node.id, other.id, "UI node IDs must be unique"});
                if (node.role == LayoutRole::Interactive && other.role == LayoutRole::Interactive &&
                    node.actionId == other.actionId && !node.actionId.empty() && node.enabled && other.enabled) {
                    issues.push_back({"DUPLICATE_ACTION", node.id, other.id, "enabled interactive nodes share an action ID"});
                }
                if (node.parentId != other.parentId || !intersects(node.bounds, other.bounds)) continue;
                if (node.role == LayoutRole::Decoration || other.role == LayoutRole::Decoration) continue;
                if (node.role == LayoutRole::Container || other.role == LayoutRole::Container) continue;
                issues.push_back({"SIBLING_OVERLAP", node.id, other.id, "non-decoration sibling nodes overlap"});
            }
        }
        for (std::size_t index = 0; index < textNodes_.size(); ++index) {
            const UiTextNode& text = textNodes_[index];
            if (text.id.empty()) issues.push_back({"EMPTY_ID", text.id, {}, "every UI text node needs a stable ID"});
            for (const UiNode& node : nodes_) if (text.id == node.id) issues.push_back({"DUPLICATE_ID", text.id, node.id, "UI node IDs must be unique"});
            if (!text.parentId.empty()) {
                const UiNode* parent = find(text.parentId);
                if (parent == nullptr) issues.push_back({"MISSING_PARENT", text.id, text.parentId, "text parent node does not exist"});
                else if (parent->role != LayoutRole::Container) issues.push_back({"INVALID_PARENT", text.id, parent->id, "only containers may own text nodes"});
                else if (!containsRect(parent->bounds, text.bounds)) issues.push_back({"PARENT_ESCAPE", text.id, parent->id, "text node escapes its declared parent"});
            } else if (!containsRect(viewport_, text.bounds)) {
                issues.push_back({"VIEWPORT_ESCAPE", text.id, {}, "root text node is outside the logical viewport"});
            }
            const int innerWidth = std::max(1, text.bounds.width - text.padding * 2);
            const int innerHeight = std::max(1, text.bounds.height - text.padding * 2);
            const TextMetrics metrics = text.wrap ? measureWrappedText(text.text, innerWidth, text.scale, text.lineGap) : measureText(text.text, text.scale);
            if (!fitsWithin(metrics, innerWidth, innerHeight)) issues.push_back({"TEXT_OVERFLOW", text.id, {}, "text does not fit its declared box"});
            for (const UiNode& node : nodes_) {
                if (text.parentId != node.parentId || !intersects(text.bounds, node.bounds)) continue;
                if (node.role != LayoutRole::Decoration && node.role != LayoutRole::Container) issues.push_back({"TEXT_CONTROL_OVERLAP", text.id, node.id, "text overlaps a sibling interactive node"});
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                const UiTextNode& other = textNodes_[previous];
                if (text.id == other.id) issues.push_back({"DUPLICATE_ID", text.id, other.id, "UI node IDs must be unique"});
                if (text.parentId == other.parentId && intersects(text.bounds, other.bounds)) issues.push_back({"TEXT_OVERLAP", text.id, other.id, "sibling text nodes overlap"});
            }
        }
        return issues;
    }

    const UiNode* hitTest(int x, int y) const {
        const UiNode* result = nullptr;
        for (const UiNode& node : nodes_) {
            if (!node.enabled || node.role != LayoutRole::Interactive || !node.bounds.contains(x, y)) continue;
            if (result == nullptr || node.zOrder > result->zOrder ||
                (node.zOrder == result->zOrder && node.id > result->id)) result = &node;
        }
        return result;
    }

private:
    const UiNode* find(const std::string& id) const {
        for (const UiNode& node : nodes_) if (node.id == id) return &node;
        return nullptr;
    }

    UiRect viewport_{};
    std::vector<UiNode> nodes_;
    std::vector<UiTextNode> textNodes_;
};

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
inline constexpr UiRect loadoutDoctrineButton{190, 154, 900, 14};
inline constexpr UiRect loadoutPassiveIdentityStrip{190, 170, 630, 14};
inline constexpr UiRect loadoutStartButton{460, 640, 360, 38};
inline constexpr UiRect loadoutDailyButton{850, 640, 240, 38};
inline constexpr UiRect loadoutFrame{96, 54, 1088, 646};
inline constexpr UiRect loadoutPanel{140, 90, 1000, 610};
inline constexpr UiRect loadoutDailyHeaderRegion{610, 90, 200, 64};
inline constexpr UiRect loadoutSkinHeaderRegion{840, 90, 280, 92};
inline constexpr UiRect workshopConfirmCancelButton{400, 510, 260, 52};
inline constexpr UiRect workshopConfirmAcceptButton{700, 510, 260, 52};
inline constexpr UiRect workshopClassSummaryPanel{700, 545, 390, 110};
inline constexpr UiRect workshopClassOverviewButton{700, 545, 390, 28};
inline constexpr UiRect workshopClassOverviewPanel{120, 104, 1040, 592};
inline constexpr UiRect workshopClassOverviewPrevious{150, 620, 180, 38};
inline constexpr UiRect workshopClassOverviewNext{950, 620, 180, 38};
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
// The equipped bar stays fixed; the catalog is independently scrollable so a
// larger authored skill set never changes the hitboxes of the run controls.
inline constexpr UiRect skillBrowserOverlay{112, 74, 1056, 624};
inline constexpr UiRect skillBrowserSearch{170, 132, 520, 32};
inline constexpr UiRect skillBrowserClassFilter{700, 132, 210, 32};
inline constexpr UiRect skillBrowserClose{974, 92, 156, 32};
inline constexpr UiRect skillBrowserViewport{160, 178, 700, 476};
inline constexpr UiRect skillBrowserDetails{884, 178, 246, 476};
inline constexpr UiRect skillBrowserEquip{884, 610, 246, 32};
inline constexpr UiRect skillBrowserScrollTrack{844, 178, 10, 476};
inline constexpr int skillBrowserColumns = 2;
inline constexpr int skillBrowserCardWidth = 336;
inline constexpr int skillBrowserCardHeight = 82;
inline constexpr int skillBrowserCardGap = 12;
inline constexpr int skillBrowserVisibleRows = 5;

inline UiRect skillBrowserCard(int visibleIndex, int scrollRow = 0) {
    const int row = visibleIndex / skillBrowserColumns + scrollRow;
    const int column = visibleIndex % skillBrowserColumns;
    return {skillBrowserViewport.x + column * (skillBrowserCardWidth + skillBrowserCardGap),
            skillBrowserViewport.y + (row - scrollRow) * (skillBrowserCardHeight + skillBrowserCardGap),
            skillBrowserCardWidth, skillBrowserCardHeight};
}

inline int skillBrowserMaxScrollRows(int resultCount) {
    const int rows = (std::max(0, resultCount) + skillBrowserColumns - 1) / skillBrowserColumns;
    return std::max(0, rows - skillBrowserVisibleRows);
}

inline UiRect skillBrowserScrollbarThumb(int resultCount, int scrollRow) {
    const int maxScroll = skillBrowserMaxScrollRows(resultCount);
    if (maxScroll == 0) return skillBrowserScrollTrack;
    const int thumbHeight = std::max(36, skillBrowserScrollTrack.height * skillBrowserVisibleRows /
        std::max(skillBrowserVisibleRows, maxScroll + skillBrowserVisibleRows));
    const int travel = skillBrowserScrollTrack.height - thumbHeight;
    return {skillBrowserScrollTrack.x, skillBrowserScrollTrack.y + travel * std::clamp(scrollRow, 0, maxScroll) / maxScroll,
            skillBrowserScrollTrack.width, thumbHeight};
}

inline int skillBrowserScrollFromPointer(int resultCount, int pointerY, int grabOffset = 0) {
    const int maximum = skillBrowserMaxScrollRows(resultCount);
    if (maximum == 0) return 0;
    const UiRect thumb = skillBrowserScrollbarThumb(resultCount, 0);
    const int travel = skillBrowserScrollTrack.height - thumb.height;
    if (travel <= 0) return 0;
    const int top = std::clamp(pointerY - grabOffset, skillBrowserScrollTrack.y, skillBrowserScrollTrack.y + travel);
    return std::clamp(static_cast<int>(std::lround(static_cast<double>(top - skillBrowserScrollTrack.y) * maximum / travel)), 0, maximum);
}

inline std::string skillBrowserNormalize(std::string value) {
    std::string normalized;
    for (const unsigned char character : value) {
        if (std::isalnum(character)) normalized.push_back(static_cast<char>(std::tolower(character)));
        else if (!normalized.empty() && normalized.back() != ' ') normalized.push_back(' ');
    }
    while (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
    return normalized;
}

inline bool skillBrowserMatches(const std::vector<std::string>& searchableFields, const std::string& query) {
    const std::string normalizedQuery = skillBrowserNormalize(query);
    if (normalizedQuery.empty()) return true;
    std::vector<std::string> terms;
    std::string term;
    for (const char character : normalizedQuery) {
        if (character == ' ') {
            if (!term.empty()) { terms.push_back(term); term.clear(); }
        } else term.push_back(character);
    }
    if (!term.empty()) terms.push_back(term);
    for (const std::string& required : terms) {
        bool matched = false;
        for (const std::string& field : searchableFields) {
            if (skillBrowserNormalize(field).find(required) != std::string::npos) { matched = true; break; }
        }
        if (!matched) return false;
    }
    return true;
}

inline UiRect loadoutSkinCard(int index) { return {850 + index * 52, 118, 44, 28}; }
inline UiRect upgradeChoiceButton(int index) { return {220 + index * 290, 245, 220, 235}; }
inline UiRect skillSlotButton(int index) { return {270 + index * 124, 620, 108, 72}; }
inline constexpr UiRect ultimateSkillButton{910, 610, 150, 82};
inline constexpr UiRect skillTargetCancelButton{1080, 620, 130, 34};
inline constexpr UiRect hudContextualStrip{24, 596, 1200, 16};
inline constexpr UiRect hudStatusStrip{24, 700, 1200, 14};
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
