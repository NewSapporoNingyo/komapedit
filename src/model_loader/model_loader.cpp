/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#define MODEL_LOADER_EXPORTS
#include "model_loader.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

thread_local std::string g_last_error;

#if defined(_WIN32)
std::wstring utf8_to_wide_local(const std::string& text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) {
        n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    }
    if (n <= 0) throw std::runtime_error("UTF-8 path decode failed");
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string wide_to_utf8_local(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::filesystem::path path_from_utf8(const std::string& path) {
    return std::filesystem::path(utf8_to_wide_local(path));
}

std::string path_to_utf8(const std::filesystem::path& path) {
    return wide_to_utf8_local(path.wstring());
}
#else
std::filesystem::path path_from_utf8(const std::string& path) {
    return std::filesystem::path(path);
}

std::string path_to_utf8(const std::filesystem::path& path) {
#if defined(__cpp_char8_t)
    auto s = path.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
#else
    return path.u8string();
#endif
}
#endif

std::string ascii_lower(std::string text) {
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return text;
}

std::vector<unsigned char> read_binary_file(const std::string& path_utf8) {
#if defined(_WIN32)
    FILE* input = _wfopen(utf8_to_wide_local(path_utf8).c_str(), L"rb");
#else
    FILE* input = std::fopen(path_utf8.c_str(), "rb");
#endif
    if (!input) throw std::runtime_error("file open failed: " + path_utf8);

    std::vector<unsigned char> data;
    unsigned char buffer[8192];
    while (true) {
        size_t n = std::fread(buffer, 1, sizeof(buffer), input);
        if (n > 0) data.insert(data.end(), buffer, buffer + n);
        if (n < sizeof(buffer)) {
            if (std::ferror(input)) {
                std::fclose(input);
                throw std::runtime_error("file read failed: " + path_utf8);
            }
            break;
        }
    }
    std::fclose(input);
    if (data.empty()) throw std::runtime_error("file is empty: " + path_utf8);
    return data;
}

std::string extension_hint(const std::string& path_utf8) {
    std::string ext = path_to_utf8(path_from_utf8(path_utf8).extension());
    ext = ascii_lower(ext);
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    return ext;
}

char* copy_c_string(const std::string& text) {
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (!out) throw std::runtime_error("memory allocation failed");
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

std::string resolved_texture_path(const std::filesystem::path& model_dir, const aiString& texture) {
    std::string text = texture.C_Str();
    if (text.empty() || text[0] == '*') return {};
    std::replace(text.begin(), text.end(), '\\', '/');
    std::filesystem::path texture_path = path_from_utf8(text);
    if (texture_path.is_relative()) texture_path = model_dir / texture_path;
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(texture_path, ec);
    if (!ec) texture_path = abs;
    return path_to_utf8(texture_path.lexically_normal());
}

void free_mesh(MlMeshData& model) {
    if (model.materials) {
        for (size_t i = 0; i < model.material_count; ++i) {
            std::free(model.materials[i].texture_path);
            model.materials[i].texture_path = nullptr;
        }
    }
    std::free(model.vertices);
    std::free(model.indices);
    std::free(model.parts);
    std::free(model.materials);
    model = {};
}

void assign_bounds(MlMeshData& out) {
    float mn[3] = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    float mx[3] = {
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max()
    };

    for (size_t i = 0; i < out.vertex_count; ++i) {
        const MlVertex& v = out.vertices[i];
        const float p[3] = {v.px, v.py, v.pz};
        for (int axis = 0; axis < 3; ++axis) {
            mn[axis] = std::min(mn[axis], p[axis]);
            mx[axis] = std::max(mx[axis], p[axis]);
        }
    }

    float radius = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        out.bounds_min[axis] = mn[axis];
        out.bounds_max[axis] = mx[axis];
        out.center[axis] = (mn[axis] + mx[axis]) * 0.5f;
    }
    for (size_t i = 0; i < out.vertex_count; ++i) {
        const MlVertex& v = out.vertices[i];
        const float dx = v.px - out.center[0];
        const float dy = v.py - out.center[1];
        const float dz = v.pz - out.center[2];
        radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    out.radius = std::max(radius, 0.001f);
}

MlMeshData load_with_assimp(const std::string& path_utf8) {
    const std::vector<unsigned char> bytes = read_binary_file(path_utf8);
    const std::string hint = extension_hint(path_utf8);
    std::filesystem::path model_path = path_from_utf8(path_utf8);
    std::error_code ec;
    std::filesystem::path abs_model_path = std::filesystem::absolute(model_path, ec);
    if (!ec) model_path = abs_model_path;
    const std::filesystem::path model_dir = model_path.parent_path();

    Assimp::Importer importer;
    const unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_ImproveCacheLocality |
        aiProcess_PreTransformVertices |
        aiProcess_ConvertToLeftHanded;
    const aiScene* scene = importer.ReadFileFromMemory(bytes.data(), bytes.size(), flags, hint.c_str());
    if (!scene || !scene->HasMeshes()) {
        std::string error = importer.GetErrorString();
        if (error.empty()) error = "Assimp did not return any mesh";
        throw std::runtime_error(error);
    }

    size_t vertex_count = 0;
    size_t index_count = 0;
    size_t part_count = 0;
    for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index) {
        const aiMesh* mesh = scene->mMeshes[mesh_index];
        if (!mesh) continue;
        vertex_count += mesh->mNumVertices;
        size_t mesh_index_count = 0;
        for (unsigned int face_index = 0; face_index < mesh->mNumFaces; ++face_index) {
            const aiFace& face = mesh->mFaces[face_index];
            if (face.mNumIndices == 3) mesh_index_count += 3;
        }
        if (mesh_index_count > 0) ++part_count;
        index_count += mesh_index_count;
    }

    if (vertex_count == 0 || index_count == 0) throw std::runtime_error("model contains no renderable triangles");
    if (vertex_count > std::numeric_limits<unsigned int>::max()) {
        throw std::runtime_error("model has too many vertices for 32-bit indices");
    }
    if (index_count > std::numeric_limits<unsigned int>::max()) {
        throw std::runtime_error("model has too many indices for Direct3D 11");
    }

    MlMeshData out = {};
    out.vertices = static_cast<MlVertex*>(std::malloc(sizeof(MlVertex) * vertex_count));
    out.indices = static_cast<unsigned int*>(std::malloc(sizeof(unsigned int) * index_count));
    out.parts = static_cast<MlMeshPart*>(std::malloc(sizeof(MlMeshPart) * part_count));
    out.material_count = std::max<size_t>(scene->mNumMaterials, 1);
    out.materials = static_cast<MlMaterial*>(std::calloc(out.material_count, sizeof(MlMaterial)));
    if (!out.vertices || !out.indices || !out.parts || !out.materials) {
        free_mesh(out);
        throw std::runtime_error("memory allocation failed");
    }
    out.vertex_count = vertex_count;
    out.index_count = index_count;
    out.part_count = part_count;

    for (size_t i = 0; i < out.material_count; ++i) {
        out.materials[i].diffuse[0] = 1.0f;
        out.materials[i].diffuse[1] = 1.0f;
        out.materials[i].diffuse[2] = 1.0f;
        out.materials[i].diffuse[3] = 1.0f;
    }
    for (unsigned int material_index = 0; material_index < scene->mNumMaterials; ++material_index) {
        const aiMaterial* material = scene->mMaterials[material_index];
        if (!material) continue;

        aiColor4D diffuse(1.0f, 1.0f, 1.0f, 1.0f);
        if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
            out.materials[material_index].diffuse[0] = diffuse.r;
            out.materials[material_index].diffuse[1] = diffuse.g;
            out.materials[material_index].diffuse[2] = diffuse.b;
            out.materials[material_index].diffuse[3] = diffuse.a;
        }
        float opacity = 1.0f;
        if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
            out.materials[material_index].diffuse[3] = std::clamp(opacity, 0.0f, 1.0f);
        }

        aiString texture_path;
        if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texture_path) == AI_SUCCESS) {
            std::string resolved = resolved_texture_path(model_dir, texture_path);
            if (!resolved.empty()) out.materials[material_index].texture_path = copy_c_string(resolved);
        }
    }

    size_t vertex_offset = 0;
    size_t index_offset = 0;
    size_t part_offset = 0;
    for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index) {
        const aiMesh* mesh = scene->mMeshes[mesh_index];
        if (!mesh) continue;

        for (unsigned int vertex_index = 0; vertex_index < mesh->mNumVertices; ++vertex_index) {
            const aiVector3D& p = mesh->mVertices[vertex_index];
            aiVector3D n(0.0f, 1.0f, 0.0f);
            if (mesh->HasNormals()) n = mesh->mNormals[vertex_index];
            aiVector3D uv(0.0f, 0.0f, 0.0f);
            if (mesh->HasTextureCoords(0)) uv = mesh->mTextureCoords[0][vertex_index];
            out.vertices[vertex_offset + vertex_index] = {p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y};
        }

        size_t part_start = index_offset;
        for (unsigned int face_index = 0; face_index < mesh->mNumFaces; ++face_index) {
            const aiFace& face = mesh->mFaces[face_index];
            if (face.mNumIndices != 3) continue;
            out.indices[index_offset++] = static_cast<unsigned int>(vertex_offset + face.mIndices[0]);
            out.indices[index_offset++] = static_cast<unsigned int>(vertex_offset + face.mIndices[1]);
            out.indices[index_offset++] = static_cast<unsigned int>(vertex_offset + face.mIndices[2]);
        }
        size_t mesh_index_count = index_offset - part_start;
        if (mesh_index_count > 0) {
            unsigned int material_index = mesh->mMaterialIndex;
            if (material_index >= out.material_count) material_index = 0;
            out.parts[part_offset++] = {
                static_cast<unsigned int>(part_start),
                static_cast<unsigned int>(mesh_index_count),
                material_index
            };
        }
        vertex_offset += mesh->mNumVertices;
    }

    assign_bounds(out);
    return out;
}

} // namespace

extern "C" {

ML_API unsigned int ml_api_version(void) {
    return 2;
}

ML_API int ml_load_model(const char* path, MlMeshData* out_model) {
    if (!out_model) {
        g_last_error = "output pointer is null";
        return 0;
    }
    *out_model = {};
    try {
        if (!path || !*path) throw std::runtime_error("model path is empty");
        *out_model = load_with_assimp(path);
        g_last_error.clear();
        return 1;
    } catch (const std::exception& e) {
        g_last_error = e.what();
        free_mesh(*out_model);
        return 0;
    }
}

ML_API void ml_free_model(MlMeshData* model) {
    if (!model) return;
    free_mesh(*model);
}

ML_API const char* ml_get_last_error(void) {
    return g_last_error.c_str();
}

}
