#include "ITranslator.hpp"
#include <memory>

namespace translate {

std::unique_ptr<ITranslator> createTranslator(Backend) {
    return std::unique_ptr<ITranslator>{};
}

}
