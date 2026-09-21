#include <doctest/doctest.h>

#include "renderer/light_selection.h"

#include <vector>

// Le budget du SPEC est de huit lumieres simultanees, et le shader d'eclairage en boucle
// exactement huit. Un batiment de douze pieces en declare douze.
//
// Le defaut que ces tests verrouillent n'etait pas un plantage mais un SILENCE : le jeu
// gardait les premieres lumieres rencontrees dans le registre, c'est-a-dire un ordre
// arbitraire. Quatre pieces au hasard restaient dans le noir, et lesquelles pouvait
// changer. Garder les premieres n'est pas un choix, c'est une absence de choix.

namespace {

renderer::Light lamp(core::f32 x, core::f32 intensity, core::f32 range = 100.0f) {
    renderer::Light light;
    light.position = {x, 0.0f, 0.0f};
    light.intensity = intensity;
    light.range = range;
    return light;
}

} // namespace

TEST_CASE("Les lumieres retenues sont les plus influentes, pas les premieres venues") {
    // Volontairement dans le DESORDRE, et la plus faible en tete : c'est exactement la
    // situation ou garder les premieres donne le mauvais resultat.
    std::vector<renderer::Light> lights{
        lamp(50.0f, 10.0f), // tres loin
        lamp(1.0f, 10.0f),  // juste a cote
        lamp(20.0f, 10.0f),
        lamp(3.0f, 10.0f),
    };

    const core::u32 kept =
        renderer::selectStrongestLights(lights, core::Vec3{0.0f, 0.0f, 0.0f}, 2);

    REQUIRE(kept == 2);
    CHECK(lights[0].position.x == doctest::Approx(1.0f));
    CHECK(lights[1].position.x == doctest::Approx(3.0f));
}

TEST_CASE("A distance egale, c'est la plus puissante qui gagne") {
    std::vector<renderer::Light> lights{lamp(5.0f, 4.0f), lamp(-5.0f, 40.0f)};

    const core::u32 kept =
        renderer::selectStrongestLights(lights, core::Vec3{0.0f, 0.0f, 0.0f}, 1);

    REQUIRE(kept == 1);
    CHECK(lights[0].intensity == doctest::Approx(40.0f));
}

TEST_CASE("Une lumiere hors de portee ne prend pas d'emplacement") {
    // Sa portee s'arrete a 5 m, l'observateur est a 30 : elle n'eclaire rien ici, et le
    // shader la couperait de toute facon. Lui laisser un des huit emplacements reviendrait
    // a eteindre une piece pour rien.
    std::vector<renderer::Light> lights{lamp(30.0f, 1000.0f, 5.0f), lamp(8.0f, 1.0f)};

    const core::u32 kept =
        renderer::selectStrongestLights(lights, core::Vec3{0.0f, 0.0f, 0.0f}, 8);

    CHECK(kept == 1);
    CHECK(lights[0].position.x == doctest::Approx(8.0f));
}

TEST_CASE("Moins de lumieres que le budget : rien ne bouge") {
    std::vector<renderer::Light> lights{lamp(1.0f, 10.0f), lamp(2.0f, 10.0f)};

    const core::u32 kept =
        renderer::selectStrongestLights(lights, core::Vec3{0.0f, 0.0f, 0.0f}, 8);

    // On ne trie pas ce qu'on garde entierement : c'est du travail pour rien, et surtout
    // l'ordre porte une information - la lampe torche doit rester la ou le jeu l'a mise.
    REQUIRE(kept == 2);
    CHECK(lights[0].position.x == doctest::Approx(1.0f));
    CHECK(lights[1].position.x == doctest::Approx(2.0f));
}

TEST_CASE("Aucune lumiere : aucun emplacement") {
    std::vector<renderer::Light> lights;
    CHECK(renderer::selectStrongestLights(lights, core::Vec3{0.0f, 0.0f, 0.0f}, 8) == 0);
}

TEST_CASE("L'influence suit la meme decroissance que le shader") {
    const renderer::Light light = lamp(0.0f, 100.0f);

    // 1 / (1 + d^2) : a 3 m, l'intensite est deja divisee par dix.
    CHECK(renderer::lightInfluence(light, core::Vec3{0.0f, 0.0f, 0.0f})
          == doctest::Approx(100.0f));
    CHECK(renderer::lightInfluence(light, core::Vec3{3.0f, 0.0f, 0.0f})
          == doctest::Approx(10.0f));
}
