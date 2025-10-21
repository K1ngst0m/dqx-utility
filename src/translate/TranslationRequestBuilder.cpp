#include "TranslationRequestBuilder.hpp"
#include <algorithm>

namespace translate
{

static std::string escape_double_quotes(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (c == '\"')
        {
            out.push_back('\\');
            out.push_back('\"');
        }
        else
        {
            out.push_back(c);
        }
    }
    return out;
}

text_processing::TranslationRequest build_translation_request(const std::string& text, const std::string& source_lang,
                                                              const std::string& target_lang, int backend_id)
{
    text_processing::TranslationRequest req;
    // Light normalization: escape double quotes to avoid downstream JSON/string issues
    req.translatable_text = escape_double_quotes(text);
    req.source_lang = source_lang.empty() ? "auto" : source_lang;
    req.target_lang = target_lang;
    req.backend_id = backend_id;
    req.requested_at = std::chrono::system_clock::now();
    return req;
}

} // namespace translate
