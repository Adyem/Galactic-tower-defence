#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace ta::ui {

struct TextMetrics {
    int width = 0;
    int height = 0;
    int lineCount = 0;
};

inline int effectiveTextScale(int scale, int uiScalePercent = 100) {
    return std::max(1, static_cast<int>(std::lround(static_cast<float>(std::max(1, scale)) * static_cast<float>(uiScalePercent) / 100.0f)));
}

inline int textLineHeight(int scale, int uiScalePercent = 100) {
    return 7 * effectiveTextScale(scale, uiScalePercent);
}

inline int textAdvance(const std::string& text, int scale, int uiScalePercent = 100) {
    return static_cast<int>(text.size()) * 6 * effectiveTextScale(scale, uiScalePercent);
}

inline TextMetrics measureText(const std::string& text, int scale, int uiScalePercent = 100) {
    const int advance = 6 * effectiveTextScale(scale, uiScalePercent);
    const int lineHeight = textLineHeight(scale, uiScalePercent);
    int currentWidth = 0;
    TextMetrics result{0, lineHeight, 1};
    for (const char raw : text) {
        if (raw == '\n') {
            result.width = std::max(result.width, currentWidth);
            currentWidth = 0;
            ++result.lineCount;
        } else {
            currentWidth += advance;
        }
    }
    result.width = std::max(result.width, currentWidth);
    result.height = std::max(1, result.lineCount) * lineHeight;
    return result;
}

inline std::vector<std::string> wrapText(const std::string& text, int maxWidth, int scale, int uiScalePercent = 100) {
    const int charactersPerLine = std::max(1, maxWidth / (6 * effectiveTextScale(scale, uiScalePercent)));
    std::vector<std::string> lines;
    std::string line;
    const auto flushLine = [&]() {
        if (!line.empty()) {
            lines.push_back(line);
            line.clear();
        }
    };

    std::size_t paragraphStart = 0;
    while (paragraphStart <= text.size()) {
        const std::size_t newline = text.find('\n', paragraphStart);
        const std::size_t paragraphEnd = newline == std::string::npos ? text.size() : newline;
        std::size_t wordStart = paragraphStart;
        while (wordStart < paragraphEnd) {
            while (wordStart < paragraphEnd && text[wordStart] == ' ') ++wordStart;
            if (wordStart >= paragraphEnd) break;
            std::size_t wordEnd = wordStart;
            while (wordEnd < paragraphEnd && text[wordEnd] != ' ') ++wordEnd;
            std::string word = text.substr(wordStart, wordEnd - wordStart);
            while (static_cast<int>(word.size()) > charactersPerLine) {
                if (!line.empty()) flushLine();
                lines.push_back(word.substr(0, static_cast<std::size_t>(charactersPerLine)));
                word.erase(0, static_cast<std::size_t>(charactersPerLine));
            }
            if (line.empty()) line = word;
            else if (static_cast<int>(line.size() + 1u + word.size()) <= charactersPerLine) line += " " + word;
            else {
                flushLine();
                line = word;
            }
            wordStart = wordEnd;
        }
        flushLine();
        if (newline == std::string::npos) break;
        if (newline + 1u == text.size()) lines.emplace_back();
        paragraphStart = newline + 1u;
    }
    if (lines.empty()) lines.emplace_back();
    return lines;
}

inline TextMetrics measureWrappedText(const std::string& text, int maxWidth, int scale, int lineGap = 18, int uiScalePercent = 100) {
    const std::vector<std::string> lines = wrapText(text, maxWidth, scale, uiScalePercent);
    TextMetrics result{0, 0, static_cast<int>(lines.size())};
    for (const std::string& line : lines) result.width = std::max(result.width, textAdvance(line, scale, uiScalePercent));
    result.height = textLineHeight(scale, uiScalePercent);
    if (lines.size() > 1u) result.height += static_cast<int>(lines.size() - 1u) * lineGap;
    return result;
}

inline bool fitsWithin(const TextMetrics& text, int availableWidth, int availableHeight) {
    return text.width <= availableWidth && text.height <= availableHeight;
}

inline std::string fitTextToWidth(const std::string& text, int maxWidth, int scale, int uiScalePercent = 100) {
    const int characterWidth = 6 * effectiveTextScale(scale, uiScalePercent);
    const int maxCharacters = std::max(1, maxWidth / characterWidth);
    if (static_cast<int>(text.size()) <= maxCharacters) return text;
    if (maxCharacters <= 3) return text.substr(0, static_cast<std::size_t>(maxCharacters));
    return text.substr(0, static_cast<std::size_t>(maxCharacters - 3)) + "...";
}

} // namespace ta::ui
