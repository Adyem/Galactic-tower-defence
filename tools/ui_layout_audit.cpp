#include "ui_layout.hpp"
#include "ui_text.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr ta::ui::UiRect viewport{0, 0, 1280, 720};
int failures = 0;

void report(const std::string& message) {
    std::cerr << "UI AUDIT FAIL: " << message << '\n';
    ++failures;
}

void auditScene(const char* name, const ta::ui::UiScene& scene) {
    for (const ta::ui::UiValidationIssue& issue : scene.validate()) {
        report(std::string(name) + " // " + issue.code + " // " + issue.nodeId +
               (issue.relatedId.empty() ? std::string{} : " // " + issue.relatedId) + " // " + issue.detail);
    }
}

void auditText(const char* name, const ta::ui::UiRect& box, const std::string& text, int scale = 1, int padding = 8, int lineGap = 14) {
    const int innerWidth = std::max(1, box.width - padding * 2);
    const int innerHeight = std::max(1, box.height - padding * 2);
    const ta::ui::TextMetrics metrics = ta::ui::measureWrappedText(text, innerWidth, scale, lineGap);
    if (!ta::ui::containsRect(viewport, box)) report(std::string(name) + " // text box escapes viewport");
    if (!ta::ui::fitsWithin(metrics, innerWidth, innerHeight)) report(std::string(name) + " // text exceeds its box");
}
}

int main() {
    ta::ui::UiScene mainMenu(viewport);
    mainMenu.add({"main.start", {}, "main.start", ta::ui::mainStartButton, ta::ui::LayoutRole::Interactive, 10, true});
    mainMenu.add({"main.workshop", {}, "main.workshop", ta::ui::mainWorkshopButton, ta::ui::LayoutRole::Interactive, 10, true});
    mainMenu.add({"main.collection", {}, "main.collection", ta::ui::mainCollectionButton, ta::ui::LayoutRole::Interactive, 10, true});
    mainMenu.add({"main.settings", {}, "main.settings", ta::ui::mainSettingsButton, ta::ui::LayoutRole::Interactive, 10, true});
    mainMenu.add({"main.quit", {}, "main.quit", ta::ui::mainQuitButton, ta::ui::LayoutRole::Interactive, 10, true});
    mainMenu.addText({"main.title", {}, {430, 222, 420, 14}, "WELCOME COMMANDER", 2, 0, 14, false});
    auditScene("MAIN_MENU", mainMenu);

    ta::ui::UiScene runType(viewport);
    runType.add({"run.standard.panel", {}, {}, ta::ui::runStandardPanel, ta::ui::LayoutRole::Container, 0, true});
    runType.add({"run.standard", "run.standard.panel", "run.standard", ta::ui::runStandardButton, ta::ui::LayoutRole::Interactive, 10, true});
    runType.add({"run.daily.panel", {}, {}, ta::ui::runDailyPanel, ta::ui::LayoutRole::Container, 0, true});
    runType.add({"run.daily", "run.daily.panel", "run.daily", ta::ui::runDailyButton, ta::ui::LayoutRole::Interactive, 10, true});
    runType.add({"run.endless.panel", {}, {}, ta::ui::runEndlessPanel, ta::ui::LayoutRole::Container, 0, true});
    runType.add({"run.endless", "run.endless.panel", "run.endless", ta::ui::runEndlessButton, ta::ui::LayoutRole::Interactive, 10, true});
    runType.add({"run.back", {}, "run.back", ta::ui::runTypeBackButton, ta::ui::LayoutRole::Interactive, 10, true});
    runType.addText({"run.title", {}, {420, 110, 440, 21}, "SELECT RUN TYPE", 3, 0, 18, false});
    auditScene("RUN_TYPE", runType);

    auditText("daily.title", {168, 536, 944, 16}, "DAILY // STORM", 1, 0);
    auditText("daily.description", {168, 558, 944, 16}, "SURVIVE 10 WAVES", 1, 0);
    auditText("daily.modifiers", {168, 652, 944, 16}, "SKULLS // ARMORED // NORMALIZED", 1, 0);
    auditText("skill.detail", {898, 230, 218, 56}, "EQUIPPED PASSIVE // CARRION VECTORS // AUTOMATICALLY CLAIMS NEARBY BIOMASS REMAINS", 1, 0, 14);
    auditText("workshop.currency", {160, 496, 930, 16}, "SHARDS 999 // PARTS 999 // CORES 999", 1, 0);

    if (failures != 0) {
        std::cerr << failures << " UI layout audit failures\n";
        return 1;
    }
    std::cout << "Tower Ascend UI layout audit passed\n";
    return 0;
}
