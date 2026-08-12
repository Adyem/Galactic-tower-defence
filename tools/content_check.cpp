#include "game.hpp"

#include <filesystem>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <regex>
#include <set>
#include <vector>

namespace {
struct Requirement { const char* file; std::vector<const char*> tokens; };

std::uint32_t rotateRight(std::uint32_t value, unsigned int amount) {
    return (value >> amount) | (value << (32u - amount));
}

std::string sha256File(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    static constexpr std::array<std::uint32_t, 64> constants{{
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    }};
    std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8u;
    bytes.push_back(0x80u);
    while ((bytes.size() % 64u) != 56u) bytes.push_back(0u);
    for (int shift = 56; shift >= 0; shift -= 8) bytes.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xffu));

    std::array<std::uint32_t, 8> state{{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u}};
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t base = offset + i * 4;
            words[i] = (static_cast<std::uint32_t>(bytes[base]) << 24u) | (static_cast<std::uint32_t>(bytes[base + 1]) << 16u) |
                       (static_cast<std::uint32_t>(bytes[base + 2]) << 8u) | static_cast<std::uint32_t>(bytes[base + 3]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotateRight(words[i - 15], 7) ^ rotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3u);
            const std::uint32_t s1 = rotateRight(words[i - 2], 17) ^ rotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10u);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6], h = state[7];
        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const std::uint32_t choose = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + s1 + choose + constants[i] + words[i];
            const std::uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + majority;
            h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (std::uint32_t word : state) result << std::setw(8) << word;
    return result.str();
}

bool validateIds(const std::filesystem::path& path, const std::vector<const char*>& expected) {
    std::ifstream input(path);
    if (!input) return false;
    std::ostringstream buffer; buffer << input.rdbuf();
    const std::string text = buffer.str();
    const std::regex idPattern("\\\"id\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    std::set<std::string> ids;
    bool ok = true;
    for (std::sregex_iterator it(text.begin(), text.end(), idPattern), end; it != end; ++it) {
        const std::string id = (*it)[1].str();
        if (!ids.insert(id).second) { std::cerr << "duplicate content id '" << id << "' in " << path << '\n'; ok = false; }
    }
    for (const char* id : expected) if (ids.find(id) == ids.end()) { std::cerr << "missing content id '" << id << "' in " << path << '\n'; ok = false; }
    if (ids.size() != expected.size()) { std::cerr << "unexpected content id count in " << path << '\n'; ok = false; }
    return ok;
}

bool validateUpgradeEffects(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return false;
    std::ostringstream buffer; buffer << input.rdbuf();
    const std::string text = buffer.str();
    const std::set<std::string> allowed{"pierce", "bounce", "fire_rate", "splash", "stun", "burn_area", "chain", "slow", "burn", "pull", "heal", "damage_currency", "tornado_reaction", "poison", "displace"};
    const std::regex effectPattern("\\\"effect\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    bool ok = true;
    int count = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), effectPattern), end; it != end; ++it) {
        ++count;
        if (allowed.find((*it)[1].str()) == allowed.end()) { std::cerr << "invalid upgrade effect reference '" << (*it)[1].str() << "'\n"; ok = false; }
    }
    if (count != 15) { std::cerr << "upgrades content must define fifteen effect references\n"; ok = false; }
    return ok;
}

bool validate(const std::filesystem::path& path, const std::vector<const char*>& tokens) {
    std::ifstream input(path);
    if (!input) { std::cerr << "missing content file: " << path << '\n'; return false; }
    std::ostringstream buffer; buffer << input.rdbuf();
    const std::string text = buffer.str();
    int braces = 0, brackets = 0;
    bool quoted = false;
    bool escaped = false;
    for (char c : text) {
        if (escaped) { escaped = false; continue; }
        if (c == '\\' && quoted) { escaped = true; continue; }
        if (c == '"') { quoted = !quoted; continue; }
        if (quoted) continue;
        if (c == '{') ++braces;
        if (c == '}') --braces;
        if (c == '[') ++brackets;
        if (c == ']') --brackets;
        if (braces < 0 || brackets < 0) break;
    }
    bool ok = !quoted && braces == 0 && brackets == 0;
    if (!ok) std::cerr << "invalid JSON delimiters or unterminated string: " << path << '\n';
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
    std::set<std::string> runtimePaths;
    int runtimeCount = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), runtimePattern), end; it != end; ++it) {
        ++runtimeCount;
        const std::string relative = (*it)[1].str();
        const std::filesystem::path relativePath(relative);
        if (relativePath.is_absolute() || relativePath.lexically_normal().string().find("..") != std::string::npos) {
            std::cerr << "manifest runtime path must stay inside the asset root: " << relative << '\n';
            ok = false;
        }
        if (!runtimePaths.insert(relative).second) { std::cerr << "duplicate manifest runtime path: " << relative << '\n'; ok = false; }
        const std::filesystem::path runtimePath = contentRoot.parent_path() / relativePath;
        if (!std::filesystem::is_regular_file(runtimePath)) { std::cerr << "manifest runtime path is missing: " << runtimePath << '\n'; ok = false; }
    }
    const std::regex licensePattern("\\\"license\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    int licenseCount = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), licensePattern), end; it != end; ++it) {
        ++licenseCount;
        if ((*it)[1].str().empty()) { std::cerr << "manifest contains an empty license" << '\n'; ok = false; }
    }
    const auto countMatches = [&text](const std::regex& pattern) {
        int count = 0;
        for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) ++count;
        return count;
    };
    const int assetCount = static_cast<int>(ids.size());
    const std::regex sourcePattern("\\\"source\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    const std::regex dimensionsPattern("\\\"dimensions\\\"\\s*:\\s*\\[[^\\]]+\\]");
    const std::regex pivotPattern("\\\"pivot\\\"\\s*:\\s*\\[[^\\]]+\\]");
    const std::regex atlasPattern("\\\"atlas\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    const std::regex tagsPattern("\\\"tags\\\"\\s*:\\s*\\[[^\\]]*\\]");
    const std::regex hashPattern("\\\"content_hash\\\"\\s*:\\s*\\\"sha256:[0-9a-fA-F]{64}\\\"");
    if (runtimeCount == 0 || assetCount == 0 || licenseCount != assetCount || runtimeCount != assetCount ||
        countMatches(sourcePattern) != assetCount || countMatches(dimensionsPattern) != assetCount ||
        countMatches(pivotPattern) != assetCount || countMatches(atlasPattern) != assetCount ||
        countMatches(tagsPattern) != assetCount || countMatches(hashPattern) != assetCount) {
        std::cerr << "manifest records are missing required metadata (source, runtime_path, dimensions, pivot, atlas, tags, license, or content_hash)\n";
        ok = false;
    }
    for (std::sregex_iterator it(text.begin(), text.end(), sourcePattern), end; it != end; ++it) {
        const std::filesystem::path source((*it)[1].str());
        if (source.is_absolute() || source.lexically_normal().string().find("..") != std::string::npos ||
            !std::filesystem::is_regular_file(contentRoot.parent_path() / source)) {
            std::cerr << "manifest source path is missing or escapes the asset root: " << source << '\n';
            ok = false;
        }
    }
    const std::regex recordPattern("\\{[^{}]*\\}");
    const std::regex hashFieldPattern("\\\"content_hash\\\"\\s*:\\s*\\\"sha256:([0-9a-fA-F]{64})\\\"");
    for (std::sregex_iterator recordIt(text.begin(), text.end(), recordPattern), end; recordIt != end; ++recordIt) {
        const std::string record = (*recordIt)[0].str();
        if (record.find("\"id\"") == std::string::npos) continue;
        std::smatch sourceMatch;
        std::smatch hashMatch;
        if (!std::regex_search(record, sourceMatch, sourcePattern) || !std::regex_search(record, hashMatch, hashFieldPattern)) continue;
        const std::filesystem::path source(sourceMatch[1].str());
        const std::string actual = sha256File(contentRoot.parent_path() / source);
        if (actual.empty() || actual != hashMatch[1].str()) {
            std::cerr << "manifest content hash mismatch for " << source << '\n';
            ok = false;
        }
    }
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
        {"enemies.json", {"grunt", "runner", "tank", "shielded", "swarmling", "teleporter", "boss", "damage_resistance", "radius", "teleport_cooldown", "\"phases\":2", "attack_cooldown_seconds", "telegraph_ms", "attack_lives"}},
        {"ultimates.json", {"meteor_rain", "bullet_storm", "absolute_zero", "gravity_shift", "energy_surge", "cooldown_seconds", "damage_scale"}},
        {"skins.json", {"azure", "ember", "nebula", "verdant", "gold", "\"cosmetic_only\":true"}},
        {"arenas.json", {"moonbase", "ember_crater", "neon_ruins", "wide_sine", "deep_sine", "tight_sine"}}
    };
    bool ok = true;
    for (const Requirement& requirement : requirements) ok = validate(root / requirement.file, requirement.tokens) && ok;
    const std::pair<const char*, std::vector<const char*>> ids[] = {
        {"weapons.json", {"rapid_fire", "explosive_cannon", "arcane_beam", "frost_blaster", "sniper_railgun"}},
        {"upgrades.json", {"piercing_shots", "ricochet", "overclock", "cluster_bombs", "shockwave", "fireball_shells", "chain_lightning", "freezing_blast", "burning_shot", "black_hole", "emergency_repair", "scavenger", "wind_shear", "poison_coil", "teleport_trap"}},
        {"ultimates.json", {"meteor_rain", "bullet_storm", "absolute_zero", "gravity_shift", "energy_surge"}},
        {"skins.json", {"azure", "ember", "nebula", "verdant", "gold"}},
        {"arenas.json", {"moonbase", "ember_crater", "neon_ruins"}},
        {"enemies.json", {"grunt", "runner", "tank", "shielded", "swarmling", "teleporter", "boss"}}
    };
    for (const auto& requirement : ids) ok = validateIds(root / requirement.first, requirement.second) && ok;
    ok = validateUpgradeEffects(root / "upgrades.json") && ok;
    ok = validate(root.parent_path() / "manifest.json", {"content.weapons", "content.upgrades", "content.skulls", "content.waves", "content.enemies", "content.ultimates", "content.skins", "content.arenas", "\"license\":\"project\""}) && ok;
    ok = validateManifest(root) && ok;
    ta::ContentConfig loaded;
    std::string contentError;
    if (!ta::loadContentConfig(root.string(), loaded, &contentError)) { std::cerr << "runtime content loader rejected pack: " << contentError << '\n'; ok = false; }
    if (ok) std::cout << "Tower Ascend content checks passed\n";
    return ok ? 0 : 1;
}
