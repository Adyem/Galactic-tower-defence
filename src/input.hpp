#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace ta {

// SDL keycodes for the small default keyboard map. Keeping the values in the
// platform-neutral profile layer lets the client persist remaps without
// pulling SDL into the deterministic simulation or persistence tests.
enum class InputAction : std::size_t {
    Confirm = 0,
    Daily,
    Pause,
    Ultimate,
    Restart,
    AutoUltimate,
    Upgrade1,
    Upgrade2,
    Upgrade3,
    ReducedFlashes,
    HighContrast,
    Captions,
    Vibration,
    Palette,
    UiScaleDown,
    UiScaleUp,
    MasterVolumeDown,
    MasterVolumeUp,
    SfxVolumeDown,
    SfxVolumeUp,
    UiVolumeDown,
    UiVolumeUp,
    Weapon1,
    Weapon2,
    Weapon3,
    Weapon4,
    Weapon5,
    Skull1,
    Skull2,
    Skull3,
    Skull4,
    Ultimate1,
    Ultimate2,
    Ultimate3,
    Ultimate4,
    Ultimate5,
    Skin1,
    Skin2,
    Skin3,
    Skin4,
    Skin5,
    UnlockSkin,
    Count
};

struct InputBindings {
    std::array<int, static_cast<std::size_t>(InputAction::Count)> keyboard{};

    int& key(InputAction action) { return keyboard[static_cast<std::size_t>(action)]; }
    int key(InputAction action) const { return keyboard[static_cast<std::size_t>(action)]; }
};

InputBindings defaultInputBindings();
bool validInputKey(int keycode);
const char* inputActionName(InputAction action);
std::string inputKeyName(int keycode);

} // namespace ta
