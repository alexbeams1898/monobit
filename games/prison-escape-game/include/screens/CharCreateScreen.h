#pragma once

#include "FontManager.h"

#include <string>
#include <unordered_map>

class EntityManager;

namespace CharCreateScreen
{

void init(FontHandle body_font, FontHandle title_font);

// New game mode: empty name, default appearance.
void reset();

// Edit mode: pre-fill name + appearance from saved profile.
void resetForEdit(const std::string& name,
                  const std::unordered_map<std::string, std::string>& appearance);

enum class Action
{
    None,
    Start, // New game confirmed
    Apply, // Edit mode confirmed
    Back
};

Action render(EntityManager& em, int window_w, int window_h);
const char* getName();

// Current appearance selections (category_id -> option_id).
const std::unordered_map<std::string, std::string>& getSelections();

// True when screen was opened via resetForEdit (edit existing character).
bool isEditMode();

} // namespace CharCreateScreen
