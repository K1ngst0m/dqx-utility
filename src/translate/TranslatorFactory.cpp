#include "ITranslator.hpp"
#include "OpenAITranslator.hpp"
#include "GoogleTranslator.hpp"
#include "ZhipuGLMTranslator.hpp"
#include "QwenMTTranslator.hpp"
#include "NiutransTranslator.hpp"
#include "YoudaoTranslator.hpp"

#include <memory>

namespace translate
{
std::unique_ptr<ITranslator> createTranslator(Backend backend)
{
    switch (backend)
    {
    case Backend::OpenAI:
        return std::make_unique<OpenAITranslator>();
    case Backend::Google:
        return std::make_unique<GoogleTranslator>();
    case Backend::ZhipuGLM:
        return std::make_unique<ZhipuGLMTranslator>();
    case Backend::QwenMT:
        return std::make_unique<QwenMTTranslator>();
    case Backend::Niutrans:
        return std::make_unique<NiutransTranslator>();
    case Backend::Youdao:
        return std::make_unique<YoudaoTranslator>();
    default:
        return nullptr;
    }
}
} // namespace translate