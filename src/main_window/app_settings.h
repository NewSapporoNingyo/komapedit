/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "kme.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

std::string narrow_path(const std::filesystem::path& path);
double round_to_100(double value);
std::string sanitize_filename(std::string text);
std::string normalized_storage_path(const std::string& path);
std::string normalized_path_key(const std::string& path);
const std::array<ImVec4, 12>& ui_theme_palette();
std::filesystem::path default_history_path();
std::filesystem::path default_imgui_ini_path();
UserSettings load_user_settings();
bool save_user_settings(const UserSettings& settings);
bool load_imgui_layout(const std::filesystem::path& path);
bool save_imgui_layout(const std::filesystem::path& path);
void save_imgui_layout_if_requested(const std::filesystem::path& path);
bool imgui_layout_save_pending();
std::vector<RecentMapEntry> load_history_entries(const std::filesystem::path& path);
bool save_history_entries(const std::filesystem::path& path, const std::vector<RecentMapEntry>& entries);
void apply_ui_settings(float font_size, float component_size, ImVec4 theme_color, float dpi_scale, bool viewports_enabled);
int normalize_view_2d_mode(int value);
int normalize_grid_mode(int value);
