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

const cgltf_accessor* findAttribute(const cgltf_primitive& primitive, cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        if (primitive.attributes[i].type == type && primitive.attributes[i].index == 0) {
            return primitive.attributes[i].data;
        }
    }
    return nullptr;
}

bool appendPrimitive(const cgltf_primitive& primitive, const core::Mat4& worldMatrix,
                     MeshData& out) {
    if (primitive.type != cgltf_primitive_type_triangles || primitive.indices == nullptr) {
        return false; // on ignore les bandes et eventails : glTF les autorise, on n'en veut pas
    }

    const cgltf_accessor* positions = findAttribute(primitive, cgltf_attribute_type_position);
    const cgltf_accessor* uvs = findAttribute(primitive, cgltf_attribute_type_texcoord);
    if (positions == nullptr || uvs == nullptr) {
        return false;
    }

    // Les indices reperent des sommets a l'interieur de cette primitive : il faut les
    // decaler de ce qui est deja accumule.
    const core::u32 vertexOffset = static_cast<core::u32>(out.positions.size());

    const std::size_t firstPosition = out.positions.size();
    if (!readVectors<core::Vec3, 3>(positions, out.positions) ||
        !readVectors<core::Vec2, 2>(uvs, out.uvs)) {
        return false;
    }

    // La transformation du noeud amene la geometrie a sa place dans la scene. L'ignorer
    // empilerait tous les objets a l'origine.
    for (std::size_t i = firstPosition; i < out.positions.size(); ++i) {
        out.positions[i] = core::Vec3(worldMatrix * core::Vec4(out.positions[i], 1.0f));
    }

    for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
        const cgltf_size index = cgltf_accessor_read_index(primitive.indices, i);
        out.indices.push_back(vertexOffset + static_cast<core::u32>(index));
    }
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
        for (cgltf_size n = 0; ok && n < data->nodes_count; ++n) {
            const cgltf_node& node = data->nodes[n];
            if (node.mesh == nullptr) {
                continue;
            }

            cgltf_float matrix[16];
            cgltf_node_transform_world(&node, matrix);
            const core::Mat4 worldMatrix = glm::make_mat4(matrix);

            for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p) {
                ok = appendPrimitive(node.mesh->primitives[p], worldMatrix, out) && ok;
            }
        }
    }

    cgltf_free(data);

    if (!ok || !out.isValid()) {
        core::logError("geometrie glTF inexploitable (primitives non triangulaires, ou "
                       "position/UV manquantes)");
        return false;
    }
    return true;
}

} // namespace assets
