#include "game.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <regex>
#include <set>
#include <vector>

namespace {
struct Requirement { const char* file; std::vector<const char*> tokens; };

bool validate(const std::filesystem::path& path, const std::vector<const char*>& tokens) {
    std::ifstream input(path);
    if (!input) { std::cerr << "missing content file: " << path << '\n'; return false; }
    std::ostringstream buffer; buffer << input.rdbuf();
    const std::string text = buffer.str();
    int braces = 0, brackets = 0;
    for (char c : text) { if (c == '{') ++braces; if (c == '}') --braces; if (c == '[') ++brackets; if (c == ']') --brackets; if (braces < 0 || brackets < 0) break; }
    bool ok = braces == 0 && brackets == 0;
    if (!ok) std::cerr << "unbalanced JSON delimiters: " << path << '\n';
    for (const char* token : tokens) if (text.find(token) == std::string::npos) { std::cerr << "missing token '" << token << "' in " << path << '\n'; ok = false; }
    return ok;
}

bool validateManifest(const std::filesystem::path& contentRoot) {
    const std::filesystem::path manifestPath = contentRoot.parent_path() / "manifest.json";
    std::ifstream input(manifestPath);
    if (!input) { std::cerr << "missing asset manifest: " << manifestPath << '\n'; return false; }
    std::ostringstream buffer; buffer << input.rdbuf();
    const std::string text = buffer.str();
    bool ok = true;
    std::set<std::string> ids;
    const std::regex idPattern("\\\"id\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    for (std::sregex_iterator it(text.begin(), text.end(), idPattern), end; it != end; ++it) {
        const std::string id = (*it)[1].str();
        if (!ids.insert(id).second) { std::cerr << "duplicate manifest id: " << id << '\n'; ok = false; }
    }
    const std::regex runtimePattern("\\\"runtime_path\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    int runtimeCount = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), runtimePattern), end; it != end; ++it) {
        ++runtimeCount;
        const std::filesystem::path runtimePath = contentRoot.parent_path() / (*it)[1].str();
        if (!std::filesystem::is_regular_file(runtimePath)) { std::cerr << "manifest runtime path is missing: " << runtimePath << '\n'; ok = false; }
    }
    const std::regex licensePattern("\\\"license\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    int licenseCount = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), licensePattern), end; it != end; ++it) {
        ++licenseCount;
        if ((*it)[1].str().empty()) { std::cerr << "manifest contains an empty license" << '\n'; ok = false; }
    }
    if (runtimeCount == 0 || licenseCount < runtimeCount) { std::cerr << "manifest records are missing runtime paths or licenses\n"; ok = false; }
    return ok;
}
} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path root = argc > 1 ? argv[1] : "assets/content";
    const Requirement requirements[] = {
        {"weapons.json", {"rapid_fire", "explosive_cannon", "arcane_beam", "frost_blaster", "sniper_railgun"}},
        {"upgrades.json", {"piercing_shots", "ricochet", "overclock", "cluster_bombs", "freezing_blast", "black_hole", "wind_shear", "poison_coil", "teleport_trap", "value_a", "value_b"}},
        {"skulls.json", {"swarm", "glass_cannon", "haste", "greed", "spawn_scale", "speed_scale", "currency_bonus", "boss_currency_bonus"}},
        {"waves.json", {"\"wave\":1", "\"wave\":10", "\"boss\":true", "grunt_weight", "runner_weight", "tank_weight", "shielded_weight", "swarmling_weight", "teleporter_weight", "boss_weight"}},
        {"enemies.json", {"grunt", "runner", "tank", "shielded", "swarmling", "teleporter", "boss", "damage_resistance", "radius", "teleport_cooldown", "\"phases\":2"}},
        {"ultimates.json", {"meteor_rain", "bullet_storm", "absolute_zero", "gravity_shift", "energy_surge", "cooldown_seconds", "damage_scale"}},
        {"skins.json", {"azure", "ember", "nebula", "verdant", "gold", "\"cosmetic_only\":true"}},
        {"arenas.json", {"moonbase", "ember_crater", "neon_ruins", "wide_sine", "deep_sine", "tight_sine"}}
    };
    bool ok = true;
    for (const Requirement& requirement : requirements) ok = validate(root / requirement.file, requirement.tokens) && ok;
    ok = validate(root.parent_path() / "manifest.json", {"content.weapons", "content.upgrades", "content.skulls", "content.waves", "content.enemies", "content.ultimates", "content.skins", "content.arenas", "\"license\":\"project\""}) && ok;
    ok = validateManifest(root) && ok;
    ta::ContentConfig loaded;
    std::string contentError;
    if (!ta::loadContentConfig(root.string(), loaded, &contentError)) { std::cerr << "runtime content loader rejected pack: " << contentError << '\n'; ok = false; }
    if (ok) std::cout << "Tower Ascend content checks passed\n";
    return ok ? 0 : 1;
}
