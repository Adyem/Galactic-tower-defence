#include "ui_layout.hpp"
#include "ui_text.hpp"
#include "app_state.hpp"

#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool overlaps(const ta::ui::UiRect& first, const ta::ui::UiRect& second) {
    return ta::ui::intersects(first, second);
}

void checkNoUnexpectedOverlaps(const std::vector<ta::ui::LayoutElement>& elements, const char* message) {
    const std::vector<std::string> issues = ta::ui::unexpectedOverlaps(elements);
    check(issues.empty(), message);
}
}

int main() {
    check(ta::ui::mainStartButton.contains(430, 270), "button accepted its top-left edge");
    check(ta::ui::mainStartButton.contains(849, 327), "button accepted its bottom-right interior");
    check(!ta::ui::mainStartButton.contains(850, 327), "button leaked across its right edge");
    check(!ta::ui::mainStartButton.contains(849, 328), "button leaked across its bottom edge");

    for (int index = 0; index < 5; ++index) {
        const ta::ui::UiRect card = ta::ui::loadoutWeaponCard(index);
        check(card.width > 0 && card.height > 0 && card.contains(card.x + 1, card.y + 1), "loadout card was not usable");
        if (index > 0) check(!card.contains(ta::ui::loadoutWeaponCard(index - 1).x + ta::ui::loadoutWeaponCard(index - 1).width - 1, card.y + 1), "adjacent weapon cards overlap");
    }
    check(!ta::ui::loadoutStartButton.contains(ta::ui::loadoutAutoButton.x + 1, ta::ui::loadoutAutoButton.y + 1), "loadout start button overlaps auto toggle");
    check(!ta::ui::workshopConfirmCancelButton.contains(ta::ui::workshopConfirmAcceptButton.x, ta::ui::workshopConfirmAcceptButton.y), "workshop confirmation buttons overlap");
    check(ta::ui::dailyEnemyBriefingRow(0).contains(140, 370), "daily enemy briefing row missed its top-left edge");
    check(!ta::ui::dailyEnemyBriefingRow(0).contains(1180, 370), "daily enemy briefing row leaked across its right edge");
    check(!ta::ui::dailyEnemyBriefingRow(0).contains(140, 391), "daily enemy briefing row leaked across its bottom edge");
    check(!ta::ui::dailyEnemyBriefingRow(0).contains(140, 392), "adjacent daily enemy briefing rows have an invalid gap");
    check(ta::ui::dailySkullBriefingRow(0).contains(140, 470), "daily skull briefing row missed its top-left edge");
    check(!ta::ui::dailySkullBriefingRow(0).contains(1180, 470), "daily skull briefing row leaked across its right edge");
    check(ta::ui::workshopUltimateModuleButton(0).contains(820, 368), "ultimate sidegrade button missed its top-left edge");
    check(!ta::ui::workshopUltimateModuleButton(0).contains(1070, 368), "ultimate sidegrade button leaked across its right edge");
    check(!ta::ui::workshopUltimateModuleButton(0).contains(820, 378), "ultimate sidegrade button leaked across its bottom edge");
    check(ta::ui::settingsToggleButton(-20).contains(230, 270), "settings toggle index was not clamped safely");
    check(ta::ui::settingsToggleButton(99).contains(800, 270), "settings toggle upper index was not clamped safely");
    check(ta::ui::collectionItemCount(11) == 10, "ultimate sidegrade Codex category was incomplete");

    check(ta::ui::runStandardButton.x > ta::ui::runStandardPanel.x && ta::ui::runStandardButton.y > ta::ui::runStandardPanel.y &&
          ta::ui::runStandardButton.x + ta::ui::runStandardButton.width < ta::ui::runStandardPanel.x + ta::ui::runStandardPanel.width &&
          ta::ui::runStandardButton.y + ta::ui::runStandardButton.height < ta::ui::runStandardPanel.y + ta::ui::runStandardPanel.height,
          "standard run button escaped its card panel");
    check(!overlaps(ta::ui::runTypeBackButton, ta::ui::runStandardPanel) &&
          !overlaps(ta::ui::runTypeBackButton, ta::ui::runDailyPanel) &&
          !overlaps(ta::ui::runTypeBackButton, ta::ui::runEndlessPanel) &&
          !overlaps(ta::ui::runTypeBackButton, ta::ui::dailyBriefingCard),
          "run type Back button overlapped content");
    check(!overlaps(ta::ui::runStandardPanel, ta::ui::runDailyPanel) &&
          !overlaps(ta::ui::runStandardPanel, ta::ui::runEndlessPanel) &&
          !overlaps(ta::ui::runDailyPanel, ta::ui::runEndlessPanel) &&
          !overlaps(ta::ui::runEndlessPanel, ta::ui::dailyBriefingCard),
          "run type content panels overlapped");
    check(ta::ui::dailyBriefingCard.y + ta::ui::dailyBriefingCard.height <= 720, "daily briefing escaped the safe area");
    check(!overlaps(ta::ui::workshopBackButton, ta::ui::workshopTowerButton) &&
          !overlaps(ta::ui::workshopBackButton, ta::ui::workshopModuleButton(0)) &&
          !overlaps(ta::ui::workshopBackButton, ta::ui::workshopUltimateButton(0)),
          "workshop Back button overlapped content");
    check(!overlaps(ta::ui::collectionBackButton, ta::ui::collectionCategoryButton(10)) &&
          !overlaps(ta::ui::collectionBackButton, ta::ui::collectionCategoryButton(12)),
          "collection Back button overlapped category cards");

    checkNoUnexpectedOverlaps({
        {"main.start", ta::ui::mainStartButton, ta::ui::LayoutRole::Interactive},
        {"main.workshop", ta::ui::mainWorkshopButton, ta::ui::LayoutRole::Interactive},
        {"main.collection", ta::ui::mainCollectionButton, ta::ui::LayoutRole::Interactive},
        {"main.settings", ta::ui::mainSettingsButton, ta::ui::LayoutRole::Interactive},
        {"main.quit", ta::ui::mainQuitButton, ta::ui::LayoutRole::Interactive}},
        "main-menu controls overlapped");

    checkNoUnexpectedOverlaps({
        {"run.standard.panel", ta::ui::runStandardPanel, ta::ui::LayoutRole::Container},
        {"run.standard.button", ta::ui::runStandardButton, ta::ui::LayoutRole::Interactive},
        {"run.daily.panel", ta::ui::runDailyPanel, ta::ui::LayoutRole::Container},
        {"run.daily.button", ta::ui::runDailyButton, ta::ui::LayoutRole::Interactive},
        {"run.endless.panel", ta::ui::runEndlessPanel, ta::ui::LayoutRole::Container},
        {"run.endless.button", ta::ui::runEndlessButton, ta::ui::LayoutRole::Interactive},
        {"run.back", ta::ui::runTypeBackButton, ta::ui::LayoutRole::Interactive}},
        "run-type controls had an unexpected overlap");
    const ta::ui::UiRect viewport{0, 0, 1248, 720};

    std::vector<ta::ui::LayoutElement> loadoutElements{{"loadout.frame", ta::ui::loadoutFrame, ta::ui::LayoutRole::Container},
        {"loadout.panel", ta::ui::loadoutPanel, ta::ui::LayoutRole::Container},
        {"loadout.start", ta::ui::loadoutStartButton, ta::ui::LayoutRole::Interactive},
        {"loadout.daily", ta::ui::loadoutDailyButton, ta::ui::LayoutRole::Interactive}};
    for (int index = 0; index < 5; ++index) {
        loadoutElements.push_back({"loadout.weapon", ta::ui::loadoutWeaponCard(index), ta::ui::LayoutRole::Interactive});
        loadoutElements.push_back({"loadout.support", ta::ui::loadoutSupportCard(index), ta::ui::LayoutRole::Interactive});
        loadoutElements.push_back({"loadout.ultimate", ta::ui::loadoutUltimateCard(index), ta::ui::LayoutRole::Interactive});
        loadoutElements.push_back({"loadout.skill", ta::ui::loadoutSkillButton(index), ta::ui::LayoutRole::Interactive});
    }
    for (int index = 0; index < 4; ++index) loadoutElements.push_back({"loadout.skull", ta::ui::loadoutSkullCard(index), ta::ui::LayoutRole::Interactive});
    for (int index = 0; index < 3; ++index) {
        loadoutElements.push_back({"loadout.chassis", ta::ui::loadoutChassisCard(index), ta::ui::LayoutRole::Interactive});
        loadoutElements.push_back({"loadout.arena", ta::ui::loadoutArenaCard(index), ta::ui::LayoutRole::Interactive});
    }
    checkNoUnexpectedOverlaps(loadoutElements, "loadout controls overlapped or escaped their intended hierarchy");
    for (const ta::ui::LayoutElement& element : loadoutElements) {
        if (std::string(element.name) == "loadout.frame" || std::string(element.name) == "loadout.panel") continue;
        check(ta::ui::containsRect(ta::ui::loadoutPanel, element.bounds), "loadout control escaped the main panel");
    }
    std::vector<ta::ui::LayoutElement> skinElements{{"loadout.dailyHeader", ta::ui::loadoutDailyHeaderRegion, ta::ui::LayoutRole::Container},
        {"loadout.skinHeader", ta::ui::loadoutSkinHeaderRegion, ta::ui::LayoutRole::Container}};
    for (int index = 0; index < 5; ++index) skinElements.push_back({"loadout.skin", ta::ui::loadoutSkinCard(index), ta::ui::LayoutRole::Interactive});
    checkNoUnexpectedOverlaps({
        {"loadout.dailyHeader", ta::ui::loadoutDailyHeaderRegion, ta::ui::LayoutRole::Container},
        {"loadout.skinHeader", ta::ui::loadoutSkinHeaderRegion, ta::ui::LayoutRole::Container}},
        "loadout header regions overlapped");
    for (int index = 0; index < 5; ++index) {
        check(ta::ui::containsRect(ta::ui::loadoutSkinHeaderRegion, ta::ui::loadoutSkinCard(index)), "skin card escaped its header region");
        check(ta::ui::containsRect(viewport, ta::ui::loadoutSkinCard(index)), "skin card escaped the viewport");
    }
    checkNoUnexpectedOverlaps(skinElements, "skin cards overlapped their header region or each other");

    std::vector<ta::ui::LayoutElement> workshopElements{{"workshop.back", ta::ui::workshopBackButton, ta::ui::LayoutRole::Interactive},
        {"workshop.tower", ta::ui::workshopTowerButton, ta::ui::LayoutRole::Interactive}};
    for (int index = 0; index < 5; ++index) {
        workshopElements.push_back({"workshop.module", ta::ui::workshopModuleButton(index), ta::ui::LayoutRole::Interactive});
        workshopElements.push_back({"workshop.support", ta::ui::workshopSupportButton(index), ta::ui::LayoutRole::Interactive});
    }
    for (int index = 0; index < 3; ++index) {
        workshopElements.push_back({"workshop.preset", ta::ui::workshopPresetButton(index), ta::ui::LayoutRole::Interactive});
        workshopElements.push_back({"workshop.skill", ta::ui::workshopSkillButton(index), ta::ui::LayoutRole::Interactive});
        workshopElements.push_back({"workshop.evolution", ta::ui::workshopUltimateButton(index), ta::ui::LayoutRole::Interactive});
    }
    for (int index = 0; index < 2; ++index) workshopElements.push_back({"workshop.sidegrade", ta::ui::workshopUltimateModuleButton(index), ta::ui::LayoutRole::Interactive});
    checkNoUnexpectedOverlaps(workshopElements, "workshop controls overlapped");
    for (const ta::ui::LayoutElement& element : workshopElements) check(ta::ui::containsRect(viewport, element.bounds), "workshop control escaped the viewport");
    for (int index = 0; index < 13; ++index) check(ta::ui::containsRect(viewport, ta::ui::collectionCategoryButton(index)), "collection category escaped the viewport");
    for (int index = 0; index < 4; ++index) check(ta::ui::containsRect(viewport, ta::ui::settingsToggleButton(index)), "settings control escaped the viewport");

    checkNoUnexpectedOverlaps({
        {"synthetic.panel", {0, 0, 100, 100}, ta::ui::LayoutRole::Container},
        {"synthetic.button", {10, 10, 80, 30}, ta::ui::LayoutRole::Interactive},
        {"synthetic.text", {10, 50, 80, 14}, ta::ui::LayoutRole::Text}},
        "allowed panel containment was incorrectly reported");
    const std::vector<std::string> rejected = ta::ui::unexpectedOverlaps({
        {"synthetic.left", {0, 0, 60, 30}, ta::ui::LayoutRole::Interactive},
        {"synthetic.right", {50, 0, 60, 30}, ta::ui::LayoutRole::Interactive}});
    check(rejected.size() == 1, "sibling interactive overlap was not detected");

    const ta::ui::TextMetrics shortText = ta::ui::measureText("SHORT LABEL", 1);
    check(shortText.width == 66 && shortText.height == 7, "text measurement did not match the bitmap font");
    check(ta::ui::fitsWithin(shortText, 70, 8), "text fit check rejected text that fit");
    check(!ta::ui::fitsWithin(ta::ui::measureText("THIS LABEL IS TOO LONG", 1), 100, 20), "text fit check missed horizontal overflow");
    const ta::ui::TextMetrics wrapped = ta::ui::measureWrappedText("ONE TWO THREE FOUR", 42, 1, 18);
    check(wrapped.lineCount == 3 && wrapped.width <= 42 && wrapped.height == 43, "wrapped text measurement was incorrect");
    check(!ta::ui::fitsWithin(wrapped, 42, 40), "text fit check missed vertical wrapped overflow");
    const std::string fitted = ta::ui::fitTextToWidth("A VERY LONG PANEL LABEL", 60, 1);
    check(ta::ui::measureText(fitted, 1).width <= 60, "bounded text fitting exceeded its panel width");
    check(fitted.size() < std::string("A VERY LONG PANEL LABEL").size(), "bounded text fitting did not shorten an overflowing label");
    check(ta::ui::textLineHeight(2) <= 18, "large text line did not fit the standard title box");

    check(ta::app::mainMenuSelection(0) == ta::app::FrontendScreen::RunType && ta::app::mainMenuSelection(3) == ta::app::FrontendScreen::Settings, "main-menu state transitions were incorrect");
    check(ta::app::runTypeSelection(0) == ta::app::FrontendScreen::Loadout && ta::app::runTypeSelection(3) == ta::app::FrontendScreen::MainMenu, "run-type state transitions were incorrect");
    check(ta::app::backFrom(ta::app::FrontendScreen::Settings) == ta::app::FrontendScreen::MainMenu && ta::app::backFrom(ta::app::FrontendScreen::ModifierSelect) == ta::app::FrontendScreen::Loadout, "Back navigation contract was incorrect");

    if (failures != 0) std::cerr << failures << " UI layout checks failed\n";
    else std::cout << "Tower Ascend UI layout checks passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
