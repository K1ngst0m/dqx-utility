#pragma once

#include <string>
#include <vector>

namespace ui
{

namespace entity
{

constexpr char32_t MARKER_START = U'\uE100';
constexpr char32_t MARKER_SEP = U'\uE101';
constexpr char32_t MARKER_END = U'\uE102';

enum class SpanType
{
    Plain,
    MonsterLink
};

struct Span
{
    SpanType type = SpanType::Plain;
    std::string text;
    std::string entity_id;
};

std::vector<Span> parseAnnotatedText(const std::string& text);

} // namespace entity
} // namespace ui

class MonsterManager;

namespace ui
{
namespace entity
{

std::string annotateMonsters(const std::string& text, MonsterManager* monster_mgr);

} // namespace entity
} // namespace ui
