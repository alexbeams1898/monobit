#include "ui/AppearanceEditor.h"

#include "gameplay/Appearance.h"
#include "gameplay/AppearanceRegistry.h"
#include "hair/HairRegistry.h"

#include <imgui.h>

#include <cmath>
#include <cstring>
#include <string>
#include <utility>

namespace selva::ui
{

namespace
{

// Sculpting-flow ordered: identity first (the discrete body-type pick
// that gates everything else), then face-down-to-mouth in viewing
// order, then whole-body proportions, then hue last (it's a tweak,
// not a sculpt).
constexpr AppearanceCategory kCategories[] = {
    {"identity", "Identity"},
    {"face_structure", "Face"},
    {"eyes", "Eyes"},
    {"brow", "Brow"},
    {"nose", "Nose"},
    {"mouth", "Mouth"},
    {"cheek", "Cheek"},
    {"chin", "Chin & jaw"},
    {"ears", "Ears"},
    {"hair", "Hair"},
    {"proportions", "Proportions"},
    {"hue", "Hue"},
    {"misc", "Other"},
};

// Two framings cover everything: face close-up for any head feature
// being edited, full-body for proportions / overall identity. Per-
// feature tightening adds complexity without UX value.
constexpr float kFaceYaw = 0.0f, kFacePitch = 0.0f, kFaceZoom = 0.25f, kFaceLook = 0.92f;
constexpr float kBodyYaw = 0.0f, kBodyPitch = 0.0f, kBodyZoom = 1.00f, kBodyLook = 0.55f;
constexpr AppearanceCategoryFraming kCategoryFramings[] = {
    {"identity", kBodyYaw, kBodyPitch, kBodyZoom, kBodyLook},
    {"face_structure", kFaceYaw, kFacePitch, kFaceZoom, kFaceLook},
    {"eyes", kFaceYaw, kFacePitch, kFaceZoom, kFaceLook},
    {"brow", kFaceYaw, kFacePitch, kFaceZoom, kFaceLook},
    {"nose", kFaceYaw, kFacePitch, kFaceZoom, kFaceLook},
    {"mouth", kFaceYaw, kFacePitch, kFaceZoom, kFaceLook},
    {"cheek", kFaceYaw, kFacePitch, kFaceZoom, kFaceLook},
    {"chin", kFaceYaw, kFacePitch, kFaceZoom, kFaceLook},
    {"ears", kFaceYaw, kFacePitch, kFaceZoom, kFaceLook},
    {"hair", kFaceYaw, kFacePitch, kFaceZoom, kFaceLook},
    {"proportions", kBodyYaw, kBodyPitch, kBodyZoom, kBodyLook},
    {"hue", kBodyYaw, kBodyPitch, kBodyZoom, kBodyLook},
    {"misc", kBodyYaw, kBodyPitch, kBodyZoom, kBodyLook},
};

// Split "l-foo,r-foo" into ("l-foo", "r-foo"). Comma-less input
// returns (input, "").
std::pair<std::string, std::string> splitPairParam(const std::string& s)
{
    const auto comma = s.find(',');
    if (comma == std::string::npos)
        return {s, std::string{}};
    return {s.substr(0, comma), s.substr(comma + 1)};
}

// Wrap ImGui::SliderFloat so callers can detect user-committed edits
// (mouse release after drag). Undo commits happen on release, not on
// every pixel of movement.
bool sliderFloatTracked(const char* label, float* v, float vmin, float vmax,
                        bool* edit_committed_out)
{
    const bool changed = ImGui::SliderFloat(label, v, vmin, vmax, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit() && edit_committed_out != nullptr)
        *edit_committed_out = true;
    return changed;
}

// Right-click "Reset to default" popup for a slider. popup_id must be
// unique per site (use the slider's ##suffix label).
template <typename ResetFn>
void drawSliderResetContextMenu(const char* popup_id, ResetFn on_reset, bool* edit_committed_out)
{
    if (ImGui::BeginPopupContextItem(popup_id))
    {
        if (ImGui::MenuItem("Reset to default"))
        {
            on_reset();
            if (edit_committed_out != nullptr)
                *edit_committed_out = true;
        }
        ImGui::EndPopup();
    }
}

// Bipolar morph-pair slider: display [-1, +1], write to the two
// underlying MPFB2 shape-key weights (decr = negative, incr = positive).
// morph_pair_mirrored uses a comma to name the L/R sibling pair; both
// halves stay in lockstep.
void drawMorphPairSlider(selva::gameplay::Appearance& app,
                         const selva::gameplay::AppearanceSliderDef& def, const std::string& label,
                         bool* edit_committed_out)
{
    if (def.param_decr.empty() || def.param_incr.empty())
        return;
    const auto decr_pair = splitPairParam(def.param_decr);
    const auto incr_pair = splitPairParam(def.param_incr);
    const bool has_second_target = !decr_pair.second.empty();
    const float decr_w = selva::gameplay::appearanceMorphWeight(
        static_cast<const selva::gameplay::Appearance&>(app), decr_pair.first);
    const float incr_w = selva::gameplay::appearanceMorphWeight(
        static_cast<const selva::gameplay::Appearance&>(app), incr_pair.first);
    float bipolar = (incr_w >= decr_w) ? incr_w : -decr_w;
    const float vmin = (def.min < 0.0f) ? def.min : -1.0f;
    const float vmax = (def.max > 0.0f) ? def.max : 1.0f;
    if (sliderFloatTracked(label.c_str(), &bipolar, vmin, vmax, edit_committed_out))
    {
        const float pos = (bipolar > 0.0f) ? bipolar : 0.0f;
        const float neg = (bipolar < 0.0f) ? -bipolar : 0.0f;
        *selva::gameplay::appearanceMorphWeight(app, decr_pair.first) = neg;
        *selva::gameplay::appearanceMorphWeight(app, incr_pair.first) = pos;
        if (has_second_target)
        {
            *selva::gameplay::appearanceMorphWeight(app, decr_pair.second) = neg;
            *selva::gameplay::appearanceMorphWeight(app, incr_pair.second) = pos;
        }
    }
    drawSliderResetContextMenu(
        label.c_str(),
        [&]()
        {
            *selva::gameplay::appearanceMorphWeight(app, decr_pair.first) = 0.0f;
            *selva::gameplay::appearanceMorphWeight(app, incr_pair.first) = 0.0f;
            if (has_second_target)
            {
                *selva::gameplay::appearanceMorphWeight(app, decr_pair.second) = 0.0f;
                *selva::gameplay::appearanceMorphWeight(app, incr_pair.second) = 0.0f;
            }
        },
        edit_committed_out);
}

// Render every registry slider whose category matches cat_id.
// include_designer_only=true surfaces sliders with player_visible=false
// (Effigie's designer-only toggle). Runtime creator passes false.
void drawSlidersInCategory(selva::gameplay::Appearance& app, const char* cat_id,
                           bool include_designer_only, bool* edit_committed_out)
{
    for (const auto& def : selva::gameplay::AppearanceRegistry::instance().all())
    {
        if (!include_designer_only && !def.player_visible)
            continue;
        if (def.category != cat_id)
            continue;
        const std::string label = def.label + "##appearance-" + def.id;
        if (def.applies_to == "morph_pair" || def.applies_to == "morph_pair_mirrored")
        {
            drawMorphPairSlider(app, def, label, edit_committed_out);
            continue;
        }
        float* field = nullptr;
        // For a comma-separated morph_target param (used by one-way
        // mirrored sliders like ear_pointed = "l-ear-shape-pointed,
        // r-ear-shape-pointed"), the visible slider drives the FIRST
        // morph directly and mirrors the value onto the second below.
        std::string mirror_target;
        if (def.applies_to == "morph_target")
        {
            if (def.param.empty())
                continue;
            const auto pair = splitPairParam(def.param);
            field = selva::gameplay::appearanceMorphWeight(app, pair.first);
            mirror_target = pair.second;
        }
        else
        {
            field = selva::gameplay::appearanceFieldByName(app, def.id);
        }
        if (field == nullptr)
            continue;
        sliderFloatTracked(label.c_str(), field, def.min, def.max, edit_committed_out);
        if (!mirror_target.empty())
        {
            // Comma-separated morph_target: drive the second target
            // in lockstep with the visible slider so both sides
            // stay symmetric without a second UI knob.
            *selva::gameplay::appearanceMorphWeight(app, mirror_target) = *field;
        }
        const float default_value = def.default_value;
        drawSliderResetContextMenu(
            label.c_str(),
            [field, default_value, &app, mirror_target]()
            {
                *field = default_value;
                if (!mirror_target.empty())
                    *selva::gameplay::appearanceMorphWeight(app, mirror_target) = default_value;
            },
            edit_committed_out);
    }
}

// Identity: body-type radio + registry sliders in the identity category.
void drawIdentityCategory(selva::gameplay::Appearance& app, bool include_designer_only,
                          bool* edit_committed_out)
{
    int body_type_idx = (app.body_type == selva::gameplay::BodyType::Type2) ? 1 : 0;
    const int prev_idx = body_type_idx;
    if (ImGui::RadioButton("Type 1##appearance-body-type", body_type_idx == 0))
        body_type_idx = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Type 2##appearance-body-type", body_type_idx == 1))
        body_type_idx = 1;
    if (body_type_idx != prev_idx)
    {
        app.body_type = (body_type_idx == 1) ? selva::gameplay::BodyType::Type2
                                             : selva::gameplay::BodyType::Type1;
        if (edit_committed_out != nullptr)
            *edit_committed_out = true;
    }
    ImGui::Spacing();
    drawSlidersInCategory(app, "identity", include_designer_only, edit_committed_out);
}

// Hue: RGB sliders on app.color + swatch preview.
void drawHueCategory(selva::gameplay::Appearance& app, bool include_designer_only,
                     bool* edit_committed_out)
{
    sliderFloatTracked("R##appearance-color", &app.color.x, 0.0f, 1.0f, edit_committed_out);
    drawSliderResetContextMenu(
        "R##appearance-color", [&]() { app.color.x = 1.0f; }, edit_committed_out);
    sliderFloatTracked("G##appearance-color", &app.color.y, 0.0f, 1.0f, edit_committed_out);
    drawSliderResetContextMenu(
        "G##appearance-color", [&]() { app.color.y = 1.0f; }, edit_committed_out);
    sliderFloatTracked("B##appearance-color", &app.color.z, 0.0f, 1.0f, edit_committed_out);
    drawSliderResetContextMenu(
        "B##appearance-color", [&]() { app.color.z = 1.0f; }, edit_committed_out);
    ImGui::ColorButton("##appearance-swatch", ImVec4(app.color.x, app.color.y, app.color.z, 1.0f),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(60.0f, 20.0f));
    ImGui::Spacing();
    drawSlidersInCategory(app, "hue", include_designer_only, edit_committed_out);
}

// Hair: length-grouped style listbox (+ Bald) + optional RGB tint.
// Three RGB channel sliders with per-channel right-click reset + a
// swatch preview. Shared between hair and eye tint blocks; the id
// prefix keeps the sliders unique inside ImGui's ID stack.
void drawRgbTintBlock(const char* id_prefix, glm::vec3& tint, bool* edit_committed_out)
{
    struct Channel
    {
        const char* letter;
        float glm::vec3::*ptr;
    };
    constexpr Channel kChannels[] = {
        {"R", &glm::vec3::x},
        {"G", &glm::vec3::y},
        {"B", &glm::vec3::z},
    };
    for (const auto& ch : kChannels)
    {
        const std::string label = std::string(ch.letter) + "##" + id_prefix;
        sliderFloatTracked(label.c_str(), &(tint.*(ch.ptr)), 0.0f, 1.0f, edit_committed_out);
        drawSliderResetContextMenu(
            label.c_str(), [&, ptr = ch.ptr]() { tint.*ptr = 1.0f; }, edit_committed_out);
    }
    const std::string swatch_id = std::string("##") + id_prefix + "-swatch";
    ImGui::ColorButton(swatch_id.c_str(), ImVec4(tint.x, tint.y, tint.z, 1.0f),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(60.0f, 20.0f));
}

// One length-bucket ("Short" / "Long") of the hair-style list. Header
// prints lazily on the first matching style so an empty bucket is
// invisible.
void drawHairStyleBucket(const char* header, const char* bucket_key,
                         const selva::hair::HairRegistry& reg, std::string& current_id,
                         bool* edit_committed_out)
{
    bool printed_header = false;
    for (const auto& style : reg.styles)
    {
        if (style.length != bucket_key)
            continue;
        if (!printed_header)
        {
            ImGui::Separator();
            ImGui::TextDisabled("%s", header);
            printed_header = true;
        }
        const bool selected = (current_id == style.id);
        const std::string label = style.display_name + "##appearance-hair-" + style.id;
        if (ImGui::Selectable(label.c_str(), selected) && !selected)
        {
            current_id = style.id;
            if (edit_committed_out != nullptr)
                *edit_committed_out = true;
        }
    }
}

void drawHairStyleList(selva::gameplay::Appearance& app, bool* edit_committed_out)
{
    const auto& reg = selva::hair::hairRegistry();
    if (!ImGui::BeginListBox("##appearance-hair-style", ImVec2(-FLT_MIN, 260.0f)))
        return;
    const bool bald_selected = app.hair_style_id.empty();
    if (ImGui::Selectable("(Bald)", bald_selected) && !bald_selected)
    {
        app.hair_style_id.clear();
        if (edit_committed_out != nullptr)
            *edit_committed_out = true;
    }
    drawHairStyleBucket("Short", "short", reg, app.hair_style_id, edit_committed_out);
    drawHairStyleBucket("Long", "long", reg, app.hair_style_id, edit_committed_out);
    ImGui::EndListBox();
}

void drawHairCategory(selva::gameplay::Appearance& app, bool /*include_designer_only*/,
                      bool* edit_committed_out)
{
    ImGui::TextUnformatted("Style");
    drawHairStyleList(app, edit_committed_out);
    if (!app.hair_style_id.empty())
    {
        ImGui::Spacing();
        ImGui::TextUnformatted("Tint");
        drawRgbTintBlock("appearance-hair-color", app.hair_tint, edit_committed_out);
    }
}

// Eyes: iris RGB tint + registry sliders in the eyes category.
void drawEyesCategory(selva::gameplay::Appearance& app, bool include_designer_only,
                      bool* edit_committed_out)
{
    ImGui::TextUnformatted("Iris colour");
    drawRgbTintBlock("appearance-eye-color", app.eye_tint, edit_committed_out);
    ImGui::Spacing();
    drawSlidersInCategory(app, "eyes", include_designer_only, edit_committed_out);
}

int countSlidersInCategory(const char* cat_id, bool include_designer_only)
{
    int n = 0;
    for (const auto& def : selva::gameplay::AppearanceRegistry::instance().all())
    {
        if (!include_designer_only && !def.player_visible)
            continue;
        if (def.category == cat_id)
            ++n;
    }
    return n;
}

} // namespace

const AppearanceCategory* appearanceCategories()
{
    return kCategories;
}

std::size_t appearanceCategoryCount()
{
    return sizeof(kCategories) / sizeof(kCategories[0]);
}

const AppearanceCategoryFraming* findAppearanceCategoryFraming(const std::string& cat_id)
{
    for (const auto& f : kCategoryFramings)
        if (cat_id == f.id)
            return &f;
    return nullptr;
}

bool appearanceCategoryHasContent(const char* cat_id, bool include_designer_only)
{
    if (std::strcmp(cat_id, "identity") == 0 || std::strcmp(cat_id, "hue") == 0 ||
        std::strcmp(cat_id, "hair") == 0 || std::strcmp(cat_id, "eyes") == 0)
        return true;
    return countSlidersInCategory(cat_id, include_designer_only) > 0;
}

void drawAppearanceCategoryContent(selva::gameplay::Appearance& app, const std::string& cat_id,
                                   bool include_designer_only, bool* edit_committed_out)
{
    if (cat_id == "identity")
    {
        drawIdentityCategory(app, include_designer_only, edit_committed_out);
        return;
    }
    if (cat_id == "hue")
    {
        drawHueCategory(app, include_designer_only, edit_committed_out);
        return;
    }
    if (cat_id == "hair")
    {
        drawHairCategory(app, include_designer_only, edit_committed_out);
        return;
    }
    if (cat_id == "eyes")
    {
        drawEyesCategory(app, include_designer_only, edit_committed_out);
        return;
    }
    drawSlidersInCategory(app, cat_id.c_str(), include_designer_only, edit_committed_out);
}

} // namespace selva::ui
