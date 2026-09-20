#include "assets/mesh_data.h"

#include "core/log.h"

// Meme principe que stb_image : cgltf tient dans un en-tete, dont la definition est
// generee ici, avec les avertissements du code tiers coupes.
#define CGLTF_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <cgltf.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cmath>
#include <cstring>
#include <filesystem>

#include <glm/gtc/type_ptr.hpp>

namespace assets {
namespace {

// Lit un accessor de vecteurs flottants. cgltf_accessor_read_float masque la disposition
// reelle du fichier (type, pas entre elements, normalisation) : on demande des flottants,
// il s'occupe du reste.
template <typename VectorType, int ComponentCount>
bool readVectors(const cgltf_accessor* accessor, std::vector<VectorType>& out) {
    if (accessor == nullptr) {
        return false;
    }
    const std::size_t first = out.size();
    out.resize(first + accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        if (cgltf_accessor_read_float(accessor, i, glm::value_ptr(out[first + i]),
                                      ComponentCount) == 0) {
            return false;
        }
    }
    return true;
}

// Un chemin de texture dans un glTF est relatif AU FICHIER, pas au repertoire courant.
// Sans cette resolution, un modele range dans un sous-dossier ne trouverait aucune de ses
// images des qu'on lance le jeu depuis ailleurs.
std::string resolveTexturePath(const char* gltfPath, const char* uri) {
    if (uri == nullptr || uri[0] == 0) {
        return std::string();
    }
    // Une URI embarquee (data:...) n'est pas un fichier : on ne sait pas encore la traiter.
    if (std::strncmp(uri, "data:", 5) == 0) {
        return std::string();
    }
    std::filesystem::path base(gltfPath);
    base.remove_filename();
    std::filesystem::path full = base / uri;
    return full.make_preferred().string();
}

const char* textureUri(const cgltf_texture_view& view) {
    if (view.texture == nullptr || view.texture->image == nullptr) {
        return nullptr;
    }
    return view.texture->image->uri;
}

MaterialData readMaterial(const cgltf_material& material, const char* gltfPath) {
    MaterialData out;
    out.name = material.name != nullptr ? material.name : "";

    // On ne gere que le modele metallique/rugueux, celui que le format designe comme
    // standard. Les extensions (specular/glossiness, transmission...) attendront un besoin.
    if (material.has_pbr_metallic_roughness != 0) {
        const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
        out.baseColorFactor = core::Vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
                                         pbr.base_color_factor[2], pbr.base_color_factor[3]);
        out.metallicFactor = pbr.metallic_factor;
        out.roughnessFactor = pbr.roughness_factor;
        out.baseColorTexture = resolveTexturePath(gltfPath, textureUri(pbr.base_color_texture));
        out.metallicRoughnessTexture =
            resolveTexturePath(gltfPath, textureUri(pbr.metallic_roughness_texture));
    }

    out.normalScale = material.normal_texture.scale;
    out.normalTexture = resolveTexturePath(gltfPath, textureUri(material.normal_texture));
    return out;
}

const cgltf_accessor* findAttribute(const cgltf_primitive& primitive, cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        if (primitive.attributes[i].type == type && primitive.attributes[i].index == 0) {
            return primitive.attributes[i].data;
        }
    }
    return nullptr;
}

bool appendPrimitive(const cgltf_primitive& primitive, const core::Mat4& worldMatrix,
                     const cgltf_data& data, MeshData& out) {
    if (primitive.type != cgltf_primitive_type_triangles || primitive.indices == nullptr) {
        return false; // on ignore les bandes et eventails : glTF les autorise, on n'en veut pas
    }

    const cgltf_accessor* positions = findAttribute(primitive, cgltf_attribute_type_position);
    const cgltf_accessor* normals = findAttribute(primitive, cgltf_attribute_type_normal);
    const cgltf_accessor* uvs = findAttribute(primitive, cgltf_attribute_type_texcoord);
    if (positions == nullptr || normals == nullptr || uvs == nullptr) {
        return false;
    }
    // Facultative : un maillage sans tangentes s'eclaire par sa seule normale.
    const cgltf_accessor* tangents = findAttribute(primitive, cgltf_attribute_type_tangent);

    // Les indices reperent des sommets a l'interieur de cette primitive : il faut les
    // decaler de ce qui est deja accumule.
    const core::u32 vertexOffset = static_cast<core::u32>(out.positions.size());

    const std::size_t firstVertex = out.positions.size();
    if (!readVectors<core::Vec3, 3>(positions, out.positions) ||
        !readVectors<core::Vec3, 3>(normals, out.normals) ||
        !readVectors<core::Vec2, 2>(uvs, out.uvs)) {
        return false;
    }
    if (tangents != nullptr && !readVectors<core::Vec4, 4>(tangents, out.tangents)) {
        return false;
    }

    // Une normale ne se transforme pas comme un point : avec une mise a l'echelle non
    // uniforme, la matrice monde la ferait sortir de la perpendiculaire a la surface.
    // La transposee de l'inverse corrige exactement ce defaut.
    const core::Mat3 normalMatrix = glm::transpose(glm::inverse(core::Mat3(worldMatrix)));

    // La transformation du noeud amene la geometrie a sa place dans la scene. L'ignorer
    // empilerait tous les objets a l'origine.
    // Une tangente, elle, se transforme comme une DIRECTION ordinaire : elle est couchee
    // dans la surface, pas perpendiculaire a elle. Lui appliquer la matrice des normales
    // la sortirait du plan de la surface, et le repere tangent serait faux.
    const core::Mat3 linear(worldMatrix);

    for (std::size_t i = firstVertex; i < out.positions.size(); ++i) {
        out.positions[i] = core::Vec3(worldMatrix * core::Vec4(out.positions[i], 1.0f));
        out.normals[i] = glm::normalize(normalMatrix * out.normals[i]);
        if (i < out.tangents.size()) {
            const core::Vec3 direction = glm::normalize(linear * core::Vec3(out.tangents[i]));
            // Le signe w survit a la transformation : il decrit l'orientation de la carte
            // UV, que deplacer l'objet ne change pas.
            out.tangents[i] = core::Vec4(direction, out.tangents[i].w);
        }
    }

    // Chaque primitive devient une PORTION : meme tampon de sommets, plage d'indices
    // propre, materiau propre. C'est ce qui permet a un modele d'avoir une peau, des yeux
    // et des vetements differents sans le decouper en plusieurs maillages.
    SubMesh subMesh;
    subMesh.firstIndex = static_cast<core::u32>(out.indices.size());

    for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
        const cgltf_size index = cgltf_accessor_read_index(primitive.indices, i);
        out.indices.push_back(vertexOffset + static_cast<core::u32>(index));
    }

    subMesh.indexCount = static_cast<core::u32>(out.indices.size()) - subMesh.firstIndex;
    // cgltf range les materiaux dans un tableau : la soustraction de pointeurs donne
    // directement l'indice, sans avoir a chercher.
    subMesh.material = primitive.material != nullptr
                           ? static_cast<core::u32>(primitive.material - data.materials)
                           : kNoMaterial;
    out.subMeshes.push_back(subMesh);
    return true;
}

} // namespace

bool MeshData::computeBounds(core::Vec3& outMin, core::Vec3& outMax) const {
    if (positions.empty()) {
        return false;
    }
    outMin = positions[0];
    outMax = positions[0];
    for (const core::Vec3& position : positions) {
        outMin = glm::min(outMin, position);
        outMax = glm::max(outMax, position);
    }
    return true;
}

bool generateTangents(MeshData& mesh) {
    if (mesh.positions.empty() || mesh.uvs.size() != mesh.positions.size() ||
        mesh.indices.size() < 3) {
        return false;
    }

    // On accumule dans deux tableaux : la direction de U et celle de V. La seconde ne
    // finit pas dans le resultat, mais elle decide du SIGNE - donc du sens de la
    // bitangente, donc du cote vers lequel le relief ressort.
    std::vector<core::Vec3> tangentSum(mesh.positions.size(), core::Vec3(0.0f));
    std::vector<core::Vec3> bitangentSum(mesh.positions.size(), core::Vec3(0.0f));

    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const core::u32 i0 = mesh.indices[i];
        const core::u32 i1 = mesh.indices[i + 1];
        const core::u32 i2 = mesh.indices[i + 2];
        if (i0 >= mesh.positions.size() || i1 >= mesh.positions.size() ||
            i2 >= mesh.positions.size()) {
            return false;
        }

        const core::Vec3 edge1 = mesh.positions[i1] - mesh.positions[i0];
        const core::Vec3 edge2 = mesh.positions[i2] - mesh.positions[i0];
        const core::Vec2 deltaUv1 = mesh.uvs[i1] - mesh.uvs[i0];
        const core::Vec2 deltaUv2 = mesh.uvs[i2] - mesh.uvs[i0];

        // Determinant de la matrice des UV. Nul quand les trois sommets partagent la meme
        // coordonnee de texture : le triangle est degenere dans l'espace UV et ne dit
        // rien sur la direction de U. On le saute plutot que de diviser par zero.
        const core::f32 determinant = deltaUv1.x * deltaUv2.y - deltaUv2.x * deltaUv1.y;
        if (std::abs(determinant) < 1e-12f) {
            continue;
        }
        const core::f32 inverse = 1.0f / determinant;

        const core::Vec3 tangent = (edge1 * deltaUv2.y - edge2 * deltaUv1.y) * inverse;
        const core::Vec3 bitangent = (edge2 * deltaUv1.x - edge1 * deltaUv2.x) * inverse;

        for (const core::u32 index : {i0, i1, i2}) {
            tangentSum[index] += tangent;
            bitangentSum[index] += bitangent;
        }
    }

    mesh.tangents.assign(mesh.positions.size(), core::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        const core::Vec3& normal = mesh.normals[i];
        core::Vec3 tangent = tangentSum[i];

        // Un sommet qu'aucun triangle exploitable n'a touche : on fabrique une tangente
        // quelconque mais VALIDE, perpendiculaire a la normale. Le relief y sera faux,
        // mais le repere ne degenerera pas.
        if (glm::dot(tangent, tangent) < 1e-16f) {
            const core::Vec3 reference =
                std::abs(normal.y) < 0.9f ? core::Vec3{0.0f, 1.0f, 0.0f}
                                          : core::Vec3{1.0f, 0.0f, 0.0f};
            tangent = glm::cross(reference, normal);
        }

        // Gram-Schmidt : on retire la part parallele a la normale, exactement ce que fait
        // le shader. Le faire ici aussi evite de lui donner un repere deja tordu.
        tangent = tangent - normal * glm::dot(normal, tangent);
        if (glm::dot(tangent, tangent) < 1e-16f) {
            tangent = glm::cross(core::Vec3{0.0f, 0.0f, 1.0f}, normal);
        }
        tangent = glm::normalize(tangent);

        // Le signe : si la bitangente reconstruite pointe a l'oppose de celle que les
        // UV decrivent, c'est que la carte est miroitee a cet endroit.
        const core::f32 handedness =
            glm::dot(glm::cross(normal, tangent), bitangentSum[i]) < 0.0f ? -1.0f : 1.0f;
        mesh.tangents[i] = core::Vec4(tangent, handedness);
    }
    return true;
}

bool loadGltfMesh(const char* path, MeshData& out) {
    cgltf_options options{};
    cgltf_data* data = nullptr;

    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        core::logError("fichier glTF illisible");
        core::logError(path);
        return false;
    }

    // Le .gltf ne contient que la structure : les donnees brutes sont dans les .bin et
    // doivent etre chargees explicitement.
    bool ok = cgltf_load_buffers(&options, data, path) == cgltf_result_success;
    if (!ok) {
        core::logError("buffers du fichier glTF introuvables");
    }

    if (ok) {
        // Les materiaux sont lus en bloc, avant la geometrie : les portions y renverront
        // par indice.
        out.materials.reserve(data->materials_count);
        for (cgltf_size m = 0; m < data->materials_count; ++m) {
            out.materials.push_back(readMaterial(data->materials[m], path));
        }

        for (cgltf_size n = 0; ok && n < data->nodes_count; ++n) {
            const cgltf_node& node = data->nodes[n];
            if (node.mesh == nullptr) {
                continue;
            }

            cgltf_float matrix[16];
            cgltf_node_transform_world(&node, matrix);
            const core::Mat4 worldMatrix = glm::make_mat4(matrix);

            for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p) {
                ok = appendPrimitive(node.mesh->primitives[p], worldMatrix, *data, out) && ok;
            }
        }
    }

    cgltf_free(data);

    // Toutes les primitives sont fusionnees en un seul maillage : si l'une d'elles n'avait
    // pas de tangentes, le tableau est incomplet. On le vide plutot que de le completer au
    // hasard - un relief invente sur une moitie du modele se verrait davantage qu'une
    // absence de relief partout.
    if (!out.tangents.empty() && out.tangents.size() != out.positions.size()) {
        core::logWarn("glTF : tangentes partielles, elles seront recalculees");
        out.tangents.clear();
    }

    // glTF n'oblige pas un fichier a fournir ses tangentes, meme avec une carte de
    // normales : la plupart des modeles telecharges n'en ont pas. On les calcule, sans
    // quoi le repere tangent serait nul et l'eclairage casse.
    if (out.tangents.empty() && !out.positions.empty() && !generateTangents(out)) {
        core::logWarn("glTF : tangentes incalculables, relief desactive pour ce maillage");
        out.tangents.clear();
    }

    if (!ok || !out.isValid()) {
        core::logError("geometrie glTF inexploitable (primitives non triangulaires, ou "
                       "position/UV manquantes)");
        return false;
    }
    return true;
}

} // namespace assets
