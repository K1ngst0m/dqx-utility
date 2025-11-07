#include "EntityAnnotation.hpp"
#include "../monster/MonsterManager.hpp"
#include "../processing/TextUtils.hpp"
#include <plog/Log.h>

namespace ui
{
namespace entity
{

using namespace processing;

std::vector<Span> parseAnnotatedText(const std::string& text)
{
    std::vector<Span> spans;
    std::u32string u32text = utf8ToUtf32(text);
    
    std::u32string current_plain;
    size_t i = 0;
    int broken_markers = 0;
    
    while (i < u32text.size())
    {
        if (u32text[i] == MARKER_START)
        {
            if (!current_plain.empty())
            {
                Span plain_span;
                plain_span.type = SpanType::Plain;
                plain_span.text = utf32ToUtf8(current_plain);
                spans.push_back(std::move(plain_span));
                current_plain.clear();
            }
            
            size_t sep_pos = i + 1;
            while (sep_pos < u32text.size() && u32text[sep_pos] != MARKER_SEP && u32text[sep_pos] != MARKER_END)
                ++sep_pos;
            
            if (sep_pos < u32text.size() && u32text[sep_pos] == MARKER_SEP)
            {
                std::u32string id_part(u32text.begin() + i + 1, u32text.begin() + sep_pos);
                
                size_t end_pos = sep_pos + 1;
                while (end_pos < u32text.size() && u32text[end_pos] != MARKER_END)
                    ++end_pos;
                
                if (end_pos < u32text.size() && u32text[end_pos] == MARKER_END)
                {
                    std::u32string display_part(u32text.begin() + sep_pos + 1, u32text.begin() + end_pos);
                    
                    Span link_span;
                    link_span.type = SpanType::MonsterLink;
                    link_span.entity_id = utf32ToUtf8(id_part);
                    link_span.text = utf32ToUtf8(display_part);
                    spans.push_back(std::move(link_span));
                    
                    i = end_pos + 1;
                    continue;
                }
                else
                {
                    broken_markers++;
                    PLOG_WARNING << "EntityAnnotation: Broken marker sequence - missing MARKER_END after MARKER_SEP";
                }
            }
            else
            {
                broken_markers++;
                PLOG_WARNING << "EntityAnnotation: Broken marker sequence - missing MARKER_SEP after MARKER_START";
            }
            
            current_plain.push_back(u32text[i]);
            ++i;
        }
        else
        {
            current_plain.push_back(u32text[i]);
            ++i;
        }
    }
    
    if (!current_plain.empty())
    {
        Span plain_span;
        plain_span.type = SpanType::Plain;
        plain_span.text = utf32ToUtf8(current_plain);
        spans.push_back(std::move(plain_span));
    }
    
    if (broken_markers > 0)
    {
        PLOG_WARNING << "EntityAnnotation: Found " << broken_markers << " broken marker sequence(s) in text";
    }
    
    return spans;
}

std::string annotateMonsters(const std::string& text, MonsterManager* monster_mgr)
{
    if (!monster_mgr)
        return text;
    
    return monster_mgr->annotateText(text);
}

} // namespace entity
} // namespace ui
