#include "renderer/light_selection.h"

#include <algorithm>

namespace renderer {

core::f32 lightInfluence(const Light& light, const core::Vec3& point) {
    const core::f32 distance = glm::length(light.position - point);
    // Au-dela de sa portee, une lumiere ne contribue a rien : elle ne doit donc pas
    // occuper un des huit emplacements. Le shader la couperait de toute facon.
    if (distance > light.range) {
        return 0.0f;
    }
    // Meme decroissance que le shader. Le 1 + evite la division par zero quand
    // l'observateur est sur la lumiere - ce qui est le cas de la lampe torche.
    return light.intensity / (1.0f + distance * distance);
}

core::u32 selectStrongestLights(std::span<Light> lights, const core::Vec3& viewer,
                                core::u32 keep) {
    // Premier temps : ECARTER ce qui n'eclaire rien d'ici. Une lumiere hors de portee ne
    // contribue a aucun pixel - le shader la coupe - donc lui laisser un des huit
    // emplacements reviendrait a eteindre une piece pour rien.
    //
    // Le tri est STABLE : tant qu'on ne doit rien choisir, l'ordre d'origine est conserve.
    // C'est ce qui evite de brasser la liste a chaque frame dans le cas courant.
    const auto useless = std::stable_partition(
        lights.begin(), lights.end(),
        [&viewer](const Light& light) { return lightInfluence(light, viewer) > 0.0f; });
    const core::u32 usable = static_cast<core::u32>(useless - lights.begin());

    if (keep >= usable) {
        return usable;
    }

    // Second temps : il y en a plus que le budget, il faut donc choisir. Un tri PARTIEL
    // suffit - on ne veut que les `keep` premieres, l'ordre des autres n'a aucune
    // importance. Sur douze lumieres la difference est theorique, mais la fonction doit
    // rester honnete sur un niveau qui en aurait deux cents.
    std::partial_sort(lights.begin(), lights.begin() + keep, lights.begin() + usable,
                      [&viewer](const Light& a, const Light& b) {
                          return lightInfluence(a, viewer) > lightInfluence(b, viewer);
                      });
    return keep;
}

} // namespace renderer
