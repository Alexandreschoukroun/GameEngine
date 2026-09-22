#include <doctest/doctest.h>

#include "renderer/light.h"

#include <cmath>

// L'exposition s'exprime en DIAPHRAGMES, et pas en facteur brut, pour la meme raison
// qu'en photographie : la perception de la luminosite est logarithmique. Entre 1 et 0,5
// il y a 0,5 ; entre 0,5 et 0,25 il n'y a que 0,25 ; et pourtant l'oeil voit le meme pas.
// Un curseur en facteur serait donc inutilisable dans sa moitie basse, qui est justement
// celle qui nous interesse.
//
// La fonction est constexpr, donc elle ne peut pas appeler std::exp2, qui ne l'est pas
// avant C++26. Elle passe par un developpement limite - et c'est precisement ce que ces
// tests verifient : que le raccourci reste exact au niveau ou l'oeil regarde.

TEST_CASE("Un diaphragme de moins divise la lumiere par deux") {
    CHECK(renderer::exposureFromStops(0.0f) == doctest::Approx(1.0f));
    CHECK(renderer::exposureFromStops(-1.0f) == doctest::Approx(0.5f));
    CHECK(renderer::exposureFromStops(-2.0f) == doctest::Approx(0.25f));
    CHECK(renderer::exposureFromStops(-6.0f) == doctest::Approx(1.0f / 64.0f));
    CHECK(renderer::exposureFromStops(1.0f) == doctest::Approx(2.0f));
    CHECK(renderer::exposureFromStops(3.0f) == doctest::Approx(8.0f));
}

TEST_CASE("Les valeurs fractionnaires suivent la vraie puissance de deux") {
    // La tolerance vaut un millieme, soit un centieme de diaphragme. C'est cent fois plus
    // fin que le plus petit ecart qu'un oeil distingue sur un ecran.
    for (core::f32 stops = -6.0f; stops <= 3.0f; stops += 0.25f) {
        CAPTURE(stops);
        CHECK(renderer::exposureFromStops(stops)
              == doctest::Approx(std::exp2(stops)).epsilon(0.001));
    }
}

TEST_CASE("Elle est utilisable a la compilation") {
    // Le reglage par defaut du niveau. Le verifier a la compilation garantit que la
    // fonction reste constexpr : si quelqu'un y glissait un appel a std::exp2, ce test
    // ne compilerait plus - ce qui est exactement le rappel qu'on veut.
    constexpr core::f32 defaultExposure = renderer::exposureFromStops(-1.0f);
    static_assert(defaultExposure > 0.499f && defaultExposure < 0.501f);
    CHECK(defaultExposure == doctest::Approx(0.5f));
}

TEST_CASE("Le reglage sombre du niveau fait bien ce qu'on attend de lui") {
    // On refait ici le calcul qui a decide des valeurs, pour qu'il reste verifiable
    // plutot que rapporte dans un commentaire.
    //
    // Un mur de platre peint (albedo 0,5) qu'AUCUNE lampe n'atteint ne recoit que
    // l'ambiante. A l'intensite d'origine il s'affichait en gris moyen ; le but du
    // reglage est qu'il devienne noir sans que les lampes cessent d'etre des lampes.
    const auto screen = [](core::f32 linear) {
        // La meme courbe ACES que le shader, suivie du reencodage sRGB.
        const core::f32 a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
        const core::f32 mapped = (linear * (a * linear + b)) / (linear * (c * linear + d) + e);
        return std::pow(mapped < 0.0f ? 0.0f : (mapped > 1.0f ? 1.0f : mapped), 1.0f / 2.2f);
    };

    constexpr core::f32 kSky = 0.05f;
    constexpr core::f32 kAlbedo = 0.5f;

    const core::f32 before = screen(kSky * 1.00f * kAlbedo * renderer::exposureFromStops(0.0f));
    const core::f32 after = screen(kSky * 0.15f * kAlbedo * renderer::exposureFromStops(-1.0f));

    // Avant : du gris lisible. Apres : du noir.
    CHECK(before > 0.12f);
    CHECK(after < 0.05f);
    // Et la lampe du hall, a deux metres, reste franchement eclairee - sinon on aurait
    // seulement rendu le jeu illisible, ce qui n'est pas la meme chose qu'obscur.
    const core::f32 direct = (kAlbedo / 3.14159f) * 14.0f / (1.0f + 4.0f);
    const core::f32 lit = screen((direct + kSky * 0.15f * kAlbedo)
                                 * renderer::exposureFromStops(-1.0f));
    CHECK(lit > 0.5f);
    // Le rapport entre les deux est ce qui compte dans ce genre, pas la valeur absolue.
    CHECK(lit / after > 15.0f);
}
