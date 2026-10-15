#define CGLTF_IMPLEMENTATION
#include "render/Gltf.h"

#include <memory>
#include <stdexcept>

#include <cgltf.h>
#include <glm/common.hpp>

namespace vw::client {

namespace {

std::string imageBytes(const cgltf_image* image) {
    if (!image || !image->buffer_view || !image->buffer_view->buffer || !image->buffer_view->buffer->data)
        return {};
    const auto* view = image->buffer_view;
    const auto* base = static_cast<const char*>(view->buffer->data) + view->offset;
    return {base, view->size};
}

void readPrimitive(const cgltf_primitive& prim, GltfPart& part) {
    const cgltf_accessor* pos = nullptr;
    const cgltf_accessor* nrm = nullptr;
    const cgltf_accessor* uv = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const auto& a = prim.attributes[i];
        if (a.type == cgltf_attribute_type_position)
            pos = a.data;
        else if (a.type == cgltf_attribute_type_normal)
            nrm = a.data;
        else if (a.type == cgltf_attribute_type_texcoord && a.index == 0)
            uv = a.data;
    }
    if (!pos)
        return;
    const auto base = static_cast<std::uint32_t>(part.vertices.size());
    for (cgltf_size i = 0; i < pos->count; ++i) {
        float p[3] = {};
        float n[3] = {0.0f, 1.0f, 0.0f};
        float t[2] = {};
        cgltf_accessor_read_float(pos, i, p, 3);
        if (nrm)
            cgltf_accessor_read_float(nrm, i, n, 3);
        if (uv)
            cgltf_accessor_read_float(uv, i, t, 2);
        part.vertices.push_back({p[0], p[1], p[2], n[0], n[1], n[2], t[0], t[1]});
    }
    if (prim.indices) {
        for (cgltf_size i = 0; i < prim.indices->count; ++i)
            part.indices.push_back(base + static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i)));
    } else {
        for (cgltf_size i = 0; i < pos->count; ++i)
            part.indices.push_back(base + static_cast<std::uint32_t>(i));
    }
}

}

GltfModel parseGlb(std::string_view bytes) {
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &raw) != cgltf_result_success)
        throw std::runtime_error("bad glb");
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, &cgltf_free);
    if (cgltf_load_buffers(&options, data.get(), nullptr) != cgltf_result_success)
        throw std::runtime_error("glb buffers");

    GltfModel model;
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const auto& image = data->images[i];
        const std::string name = image.name ? image.name : "";
        (name == "mask" ? model.mask : model.color) = imageBytes(&image);
        if (image.uri)
            (name == "mask" ? model.maskUri : model.colorUri) = image.uri;
    }
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const auto& node = data->nodes[i];
        const std::string name = node.name ? node.name : "";
        if (!node.mesh) {
            model.points[name] = {node.translation[0], node.translation[1], node.translation[2]};
            continue;
        }
        auto& part = model.parts[name];
        for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
            readPrimitive(node.mesh->primitives[p], part);
        if (part.vertices.empty())
            continue;
        part.min = part.max = {part.vertices[0].x, part.vertices[0].y, part.vertices[0].z};
        for (const auto& v : part.vertices) {
            part.min = glm::min(part.min, glm::vec3(v.x, v.y, v.z));
            part.max = glm::max(part.max, glm::vec3(v.x, v.y, v.z));
        }
    }
    return model;
}

}
