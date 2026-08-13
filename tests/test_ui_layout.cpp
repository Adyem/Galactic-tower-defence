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
    return first.x < second.x + second.width && second.x < first.x + first.width &&
           first.y < second.y + second.height && second.y < first.y + first.height;
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
