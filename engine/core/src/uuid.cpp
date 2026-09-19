#include "core/uuid.h"

#include <random>

namespace core {

Uuid generateUuid() {
    // Initialise une seule fois, depuis l'entropie du systeme. static local : construction
    // paresseuse et thread-safe garantie par le langage depuis C++11.
    static std::mt19937_64 generator{std::random_device{}()};
    static std::uniform_int_distribution<Uuid> distribution{1, ~Uuid{0}};
    return distribution(generator);
}

} // namespace core
