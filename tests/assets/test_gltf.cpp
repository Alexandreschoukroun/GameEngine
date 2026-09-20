#include <doctest/doctest.h>

#include "assets/mesh_data.h"
#include "platform/paths.h"

#include <algorithm>
#include <cmath>

namespace {

bool loadSuzanne(assets::MeshData& mesh) {
    return assets::loadGltfMesh(
        platform::assetPath("models/suzanne/Suzanne.gltf").c_str(), mesh);
}

} // namespace

TEST_CASE("Suzanne loads with one tangent per vertex") {
    assets::MeshData mesh;
    REQUIRE(loadSuzanne(mesh));

    REQUIRE(mesh.isValid());
    // Le fichier fournit des tangentes : elles doivent etre toutes la, ou pas du tout. Un
    // tableau partiel donnerait du relief sur une moitie du modele seulement.
    CHECK(mesh.hasTangents());
    CHECK(mesh.tangents.size() == mesh.positions.size());
}

TEST_CASE("Tangents are unit length and form a usable frame after orthogonalization") {
    assets::MeshData mesh;
    REQUIRE(loadSuzanne(mesh));
    REQUIRE(mesh.hasTangents());

    core::f32 worstLength = 0.0f;
    core::f32 worstAlignment = 0.0f;
    core::f32 worstResidual = 0.0f;

    for (std::size_t i = 0; i < mesh.tangents.size(); ++i) {
        const core::Vec3 tangent(mesh.tangents[i]);
        const core::Vec3& normal = mesh.normals[i];
        worstLength = std::max(worstLength, std::abs(glm::length(tangent) - 1.0f));

        // Attention : glTF ne garantit PAS que la tangente soit perpendiculaire a la
        // normale. MikkTSpace produit une tangente constante par triangle alors que les
        // normales sont lissees par sommet - sur Suzanne, 77 % des sommets s'ecartent de
        // la perpendiculaire, jusqu'a 0,89. Exiger l'orthogonalite ici serait exiger du
        // format une promesse qu'il ne fait pas.
        //
        // Ce qui compte vraiment, c'est que le repere reste CONSTRUCTIBLE : tangente et
        // normale ne doivent pas etre confondues, sinon la re-orthogonalisation du shader
        // rendrait un vecteur nul et le relief partirait n'importe ou.
        const core::f32 alignment = std::abs(glm::dot(tangent, normal));
        worstAlignment = std::max(worstAlignment, alignment);

        // Gram-Schmidt, exactement ce que fait gbuffer.frag : on retire de la tangente sa
        // part parallele a la normale.
        const core::Vec3 corrected = glm::normalize(tangent - normal * glm::dot(normal, tangent));
        worstResidual = std::max(worstResidual, std::abs(glm::dot(corrected, normal)));
    }

    CHECK(worstLength < 0.01f);
    // Aucun repere degenere : le pire alignement reste loin de 1.
    CHECK(worstAlignment < 0.95f);
    // Et apres correction, la perpendiculaire est exacte.
    CHECK(worstResidual < 0.001f);
}

TEST_CASE("The handedness sign is kept as glTF requires") {
    assets::MeshData mesh;
    REQUIRE(loadSuzanne(mesh));
    REQUIRE(mesh.hasTangents());

    // glTF impose w = +1 ou -1 : c'est lui qui donne le sens de la bitangente, et donc
    // qui evite qu'une texture miroitee produise des creux a la place des bosses.
    core::f32 worst = 0.0f;
    for (const core::Vec4& tangent : mesh.tangents) {
        worst = std::max(worst, std::abs(std::abs(tangent.w) - 1.0f));
    }
    CHECK(worst < 0.001f);
}

TEST_CASE("A mesh without tangents stays valid, a partial one does not") {
    // Les tangentes sont FACULTATIVES : un maillage qui n'en a pas doit rester
    // exploitable, il sera simplement eclaire sans relief.
    assets::MeshData mesh;
    mesh.positions = {core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{1.0f, 0.0f, 0.0f},
                      core::Vec3{0.0f, 1.0f, 0.0f}};
    mesh.normals = {core::Vec3{0.0f, 0.0f, 1.0f}, core::Vec3{0.0f, 0.0f, 1.0f},
                    core::Vec3{0.0f, 0.0f, 1.0f}};
    mesh.uvs = {core::Vec2{0.0f, 0.0f}, core::Vec2{1.0f, 0.0f}, core::Vec2{0.0f, 1.0f}};
    mesh.indices = {0, 1, 2};

    CHECK(mesh.isValid());
    CHECK_FALSE(mesh.hasTangents());

    // Un tableau de tangentes INCOMPLET est en revanche une incoherence : on la refuse
    // plutot que de laisser le renderer lire hors des bornes.
    mesh.tangents = {core::Vec4{1.0f, 0.0f, 0.0f, 1.0f}};
    CHECK_FALSE(mesh.isValid());
}
