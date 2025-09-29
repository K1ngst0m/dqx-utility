#include "ITranslator.hpp"
#include "OpenAITranslator.hpp"
#include "GoogleTranslator.hpp"

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
        default:
            return nullptr;
        }
    }
}