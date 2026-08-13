#pragma once

namespace ta::app {

enum class FrontendScreen { MainMenu, RunType, Loadout, ModifierSelect, Workshop, Collection, Settings };

constexpr FrontendScreen backFrom(FrontendScreen screen) {
    switch (screen) {
        case FrontendScreen::RunType:
        case FrontendScreen::Workshop:
        case FrontendScreen::Collection:
        case FrontendScreen::Settings:
            return FrontendScreen::MainMenu;
        case FrontendScreen::ModifierSelect:
            return FrontendScreen::Loadout;
        case FrontendScreen::Loadout:
            return FrontendScreen::RunType;
        case FrontendScreen::MainMenu:
            return FrontendScreen::MainMenu;
    }
    return FrontendScreen::MainMenu;
}

constexpr FrontendScreen mainMenuSelection(int focus) {
    switch (focus) {
        case 0: return FrontendScreen::RunType;
        case 1: return FrontendScreen::Workshop;
        case 2: return FrontendScreen::Collection;
        case 3: return FrontendScreen::Settings;
        default: return FrontendScreen::MainMenu;
    }
}

constexpr FrontendScreen runTypeSelection(int focus) {
    switch (focus) {
        case 0:
        case 1:
        case 2: return FrontendScreen::Loadout;
        default: return FrontendScreen::MainMenu;
    }
}

} // namespace ta::app
