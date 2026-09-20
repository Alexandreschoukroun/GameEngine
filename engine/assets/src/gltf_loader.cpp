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
        core::logWarn("glTF : tangentes partielles, relief desactive pour ce maillage");
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
