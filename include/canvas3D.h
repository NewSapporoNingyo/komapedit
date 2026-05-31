/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "imgui.h"

#include <memory>
#include <string>

struct ID3D11Device;

class Canvas3D {
public:
    explicit Canvas3D(ID3D11Device* device);
    ~Canvas3D();

    Canvas3D(const Canvas3D&) = delete;
    Canvas3D& operator=(const Canvas3D&) = delete;

    bool load_model(const std::string& path, std::string& error);
    bool reload_model(std::string& error);
    void clear_model();
    bool has_model() const;
    const std::string& model_path() const;
    void set_background_color(ImVec4 color);
    ImVec4 background_color() const;

    void render(ImVec2 size);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
