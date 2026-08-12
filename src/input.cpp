#include "input.hpp"

#include <algorithm>

namespace ta {
namespace {
constexpr int KeyReturn = 13;
constexpr int KeySpace = 32;
constexpr int KeyEscape = 27;
constexpr int KeyMinus = '-';
constexpr int KeyEquals = '=';
constexpr int KeyComma = ',';
constexpr int KeyPeriod = '.';
constexpr int KeySemicolon = ';';
constexpr int KeyQuote = '\'';

} // namespace

InputBindings defaultInputBindings() {
    InputBindings result;
    result.key(InputAction::Confirm) = KeyReturn;
    result.key(InputAction::Daily) = 'd';
    result.key(InputAction::Pause) = 'p';
    result.key(InputAction::Ultimate) = KeySpace;
    result.key(InputAction::Restart) = 'r';
    result.key(InputAction::AutoUltimate) = 'a';
    result.key(InputAction::Upgrade1) = '1';
    result.key(InputAction::Upgrade2) = '2';
    result.key(InputAction::Upgrade3) = '3';
    result.key(InputAction::ReducedFlashes) = 'f';
    result.key(InputAction::HighContrast) = 'c';
    result.key(InputAction::Captions) = 'v';
    result.key(InputAction::Vibration) = 'b';
    result.key(InputAction::Palette) = 'g';
    result.key(InputAction::UiScaleDown) = KeyMinus;
    result.key(InputAction::UiScaleUp) = KeyEquals;
    result.key(InputAction::MasterVolumeDown) = '[';
    result.key(InputAction::MasterVolumeUp) = ']';
    result.key(InputAction::SfxVolumeDown) = KeyComma;
    result.key(InputAction::SfxVolumeUp) = KeyPeriod;
    result.key(InputAction::UiVolumeDown) = KeySemicolon;
    result.key(InputAction::UiVolumeUp) = KeyQuote;
    result.key(InputAction::Weapon1) = '1';
    result.key(InputAction::Weapon2) = '2';
    result.key(InputAction::Weapon3) = '3';
    result.key(InputAction::Weapon4) = '4';
    result.key(InputAction::Weapon5) = '5';
    result.key(InputAction::Skull1) = 'q';
    result.key(InputAction::Skull2) = 'r';
    result.key(InputAction::Skull3) = 's';
    result.key(InputAction::Skull4) = 't';
    result.key(InputAction::Ultimate1) = 'y';
    result.key(InputAction::Ultimate2) = 'u';
    result.key(InputAction::Ultimate3) = 'i';
    result.key(InputAction::Ultimate4) = 'o';
    result.key(InputAction::Ultimate5) = 'p';
    result.key(InputAction::Skin1) = '6';
    result.key(InputAction::Skin2) = '7';
    result.key(InputAction::Skin3) = '8';
    result.key(InputAction::Skin4) = '9';
    result.key(InputAction::Skin5) = '0';
    result.key(InputAction::UnlockSkin) = 'k';
    return result;
}

bool validInputKey(int keycode) {
    // SDL_Keycode values are positive; reject mouse/controller pseudo-values
    // and values outside the range the profile format can safely persist.
    return keycode > 0 && keycode <= 0x7fffffff;
}

const char* inputActionName(InputAction action) {
    switch (action) {
        case InputAction::Confirm: return "CONFIRM";
        case InputAction::Daily: return "DAILY";
        case InputAction::Pause: return "PAUSE";
        case InputAction::Ultimate: return "ULTIMATE";
        case InputAction::Restart: return "RESTART";
        case InputAction::AutoUltimate: return "AUTO ULTIMATE";
        case InputAction::Upgrade1: return "UPGRADE 1";
        case InputAction::Upgrade2: return "UPGRADE 2";
        case InputAction::Upgrade3: return "UPGRADE 3";
        case InputAction::ReducedFlashes: return "REDUCED FLASHES";
        case InputAction::HighContrast: return "HIGH CONTRAST";
        case InputAction::Captions: return "CAPTIONS";
        case InputAction::Vibration: return "VIBRATION";
        case InputAction::Palette: return "COLOR PALETTE";
        case InputAction::UiScaleDown: return "UI SCALE DOWN";
        case InputAction::UiScaleUp: return "UI SCALE UP";
        case InputAction::MasterVolumeDown: return "MASTER VOLUME DOWN";
        case InputAction::MasterVolumeUp: return "MASTER VOLUME UP";
        case InputAction::SfxVolumeDown: return "SFX VOLUME DOWN";
        case InputAction::SfxVolumeUp: return "SFX VOLUME UP";
        case InputAction::UiVolumeDown: return "UI VOLUME DOWN";
        case InputAction::UiVolumeUp: return "UI VOLUME UP";
        case InputAction::Weapon1: return "WEAPON 1";
        case InputAction::Weapon2: return "WEAPON 2";
        case InputAction::Weapon3: return "WEAPON 3";
        case InputAction::Weapon4: return "WEAPON 4";
        case InputAction::Weapon5: return "WEAPON 5";
        case InputAction::Skull1: return "SKULL 1";
        case InputAction::Skull2: return "SKULL 2";
        case InputAction::Skull3: return "SKULL 3";
        case InputAction::Skull4: return "SKULL 4";
        case InputAction::Ultimate1: return "ULTIMATE 1";
        case InputAction::Ultimate2: return "ULTIMATE 2";
        case InputAction::Ultimate3: return "ULTIMATE 3";
        case InputAction::Ultimate4: return "ULTIMATE 4";
        case InputAction::Ultimate5: return "ULTIMATE 5";
        case InputAction::Skin1: return "SKIN 1";
        case InputAction::Skin2: return "SKIN 2";
        case InputAction::Skin3: return "SKIN 3";
        case InputAction::Skin4: return "SKIN 4";
        case InputAction::Skin5: return "SKIN 5";
        case InputAction::UnlockSkin: return "UNLOCK SKIN";
        case InputAction::Count: break;
    }
    return "UNKNOWN ACTION";
}

std::string inputKeyName(int keycode) {
    switch (keycode) {
        case KeyReturn: return "ENTER";
        case KeySpace: return "SPACE";
        case KeyEscape: return "ESC";
        case KeyMinus: return "-";
        case KeyEquals: return "=";
        case KeyComma: return ",";
        case KeyPeriod: return ".";
        case KeySemicolon: return ";";
        case KeyQuote: return "'";
        default:
            if (keycode >= 'a' && keycode <= 'z') return std::string(1, static_cast<char>(keycode - ('a' - 'A')));
            if (keycode >= '0' && keycode <= '9') return std::string(1, static_cast<char>(keycode));
            return "KEY" + std::to_string(keycode);
    }
}

} // namespace ta
