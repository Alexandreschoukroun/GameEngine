#include <doctest/doctest.h>

#include "assets/mesh_data.h"
#include "platform/paths.h"

#include <algorithm>
#include <vector>
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

TEST_CASE("The material described in the file is read") {
    assets::MeshData mesh;
    REQUIRE(loadSuzanne(mesh));

    // Suzanne n'a qu'une matiere, donc une seule portion : c'est le cas simple. Un modele
    // telecharge en aura plusieurs - une peau, des yeux, des vetements.
    REQUIRE(mesh.materials.size() == 1);
    REQUIRE(mesh.subMeshes.size() == 1);

    const assets::MaterialData& material = mesh.materials[0];
    // Les facteurs multiplient toujours, meme sans texture : c'est la convention glTF.
    CHECK(material.baseColorFactor.r == doctest::Approx(1.0f));
    CHECK(material.baseColorFactor.a == doctest::Approx(1.0f));
    CHECK(material.metallicFactor >= 0.0f);
    CHECK(material.roughnessFactor >= 0.0f);

    // Les chemins sont resolus par rapport AU FICHIER glTF, pas au repertoire courant :
    // sans cela, un modele range dans un sous-dossier ne trouverait aucune de ses images.
    CHECK(material.baseColorTexture.find("Suzanne_BaseColor") != std::string::npos);
    CHECK(material.metallicRoughnessTexture.find("Suzanne_MetallicRoughness") !=
          std::string::npos);
    CHECK(material.baseColorTexture.find("models") != std::string::npos);
}

TEST_CASE("Sub-meshes cover every index exactly once") {
    assets::MeshData mesh;
    REQUIRE(loadSuzanne(mesh));
    REQUIRE(!mesh.subMeshes.empty());

    // Les portions se suivent sans trou ni recouvrement : un trou laisserait des triangles
    // jamais dessines, un recouvrement les dessinerait deux fois.
    core::u32 expected = 0;
    for (const assets::SubMesh& sub : mesh.subMeshes) {
        CHECK(sub.firstIndex == expected);
        CHECK(sub.indexCount > 0);
        expected += sub.indexCount;
        // Chaque portion designe un materiau existant, ou aucun.
        CHECK((sub.material == assets::kNoMaterial || sub.material < mesh.materials.size()));
    }
    CHECK(expected == mesh.indices.size());
}

TEST_CASE("Generated tangents agree with those the file provides") {
    assets::MeshData mesh;
    REQUIRE(loadSuzanne(mesh));
    REQUIRE(mesh.hasTangents());

    // Les tangentes du fichier sont celles de MikkTSpace, la reference du domaine. On les
    // met de cote, on recalcule les notres, et on compare : c'est la seule validation
    // serieuse possible pour ce genre de calcul.
    const std::vector<core::Vec4> reference = mesh.tangents;
    mesh.tangents.clear();
    REQUIRE(assets::generateTangents(mesh));
    REQUIRE(mesh.tangents.size() == reference.size());

    core::u32 aligned = 0;
    core::u32 sameHandedness = 0;
    for (std::size_t i = 0; i < reference.size(); ++i) {
        const core::Vec3 theirs(reference[i]);
        const core::Vec3 ours(mesh.tangents[i]);
        // Apres orthogonalisation par rapport a la normale, les deux doivent pointer dans
        // la meme direction. On compare ce que le shader utilisera reellement.
        const core::Vec3& normal = mesh.normals[i];
        const core::Vec3 a =
            glm::normalize(theirs - normal * glm::dot(normal, theirs));
        const core::Vec3 b = glm::normalize(ours - normal * glm::dot(normal, ours));
        if (glm::dot(a, b) > 0.9f) {
            ++aligned;
        }
        if (reference[i].w * mesh.tangents[i].w > 0.0f) {
            ++sameHandedness;
        }
    }

    const auto total = static_cast<core::f32>(reference.size());
    // Un desaccord existe sur les coutures de la carte UV, la ou plusieurs directions sont
    // legitimes : on n'exige donc pas l'identite, mais une large majorite.
    CHECK(static_cast<core::f32>(aligned) / total > 0.9f);
    // Le signe, lui, ne souffre pas d'ambiguite : il decide du cote vers lequel le relief
    // ressort. S'y tromper creuserait les bosses.
    CHECK(static_cast<core::f32>(sameHandedness) / total > 0.98f);
}

TEST_CASE("Generated tangents form a usable frame everywhere") {
    assets::MeshData mesh;
    REQUIRE(loadSuzanne(mesh));
    mesh.tangents.clear();
    REQUIRE(assets::generateTangents(mesh));

    // La propriete qui compte vraiment : AUCUN repere degenere. Une tangente nulle ferait
    // calculer normalize(0) au shader, et l'eclairage partirait en NaN - un objet noir ou
    // clignotant, sans aucun message d'erreur.
    for (std::size_t i = 0; i < mesh.tangents.size(); ++i) {
        const core::Vec3 tangent(mesh.tangents[i]);
        CHECK(glm::length(tangent) == doctest::Approx(1.0f).epsilon(0.001));
        CHECK(std::abs(glm::dot(tangent, mesh.normals[i])) < 0.001f);
        CHECK(std::abs(std::abs(mesh.tangents[i].w) - 1.0f) < 0.001f);
    }
}

TEST_CASE("A downloaded model without tangents still gets them") {
    assets::MeshData mesh;
    // Ce modele vient de Poly Haven et declare une carte de normales SANS fournir de
    // tangentes - le cas le plus courant des modeles telecharges, que la specification
    // glTF autorise explicitement.
    REQUIRE(assets::loadGltfMesh(
        platform::assetPath("models/loquet/gate_latch_01_1k.gltf").c_str(), mesh));

    REQUIRE(mesh.isValid());
    CHECK(mesh.hasTangents());
    CHECK(!mesh.materials.empty());
    // Ses trois portions partagent une seule matiere.
    CHECK(mesh.subMeshes.size() == 3);

    core::f32 worstLength = 0.0f;
    for (const core::Vec4& tangent : mesh.tangents) {
        worstLength = std::max(worstLength, std::abs(glm::length(core::Vec3(tangent)) - 1.0f));
    }
    CHECK(worstLength < 0.01f);
}

TEST_CASE("Without texture coordinates there is nothing to deduce") {
    assets::MeshData mesh;
    mesh.positions = {core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{1.0f, 0.0f, 0.0f},
                      core::Vec3{0.0f, 1.0f, 0.0f}};
    mesh.normals = {core::Vec3{0.0f, 0.0f, 1.0f}, core::Vec3{0.0f, 0.0f, 1.0f},
                    core::Vec3{0.0f, 0.0f, 1.0f}};
    mesh.indices = {0, 1, 2};

    // La direction de U se deduit des UV : sans elles, aucune information. On refuse
    // plutot que d'inventer une tangente arbitraire qui donnerait un relief faux.
    CHECK_FALSE(assets::generateTangents(mesh));
    CHECK_FALSE(mesh.hasTangents());
}
