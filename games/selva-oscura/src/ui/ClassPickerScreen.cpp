#include "ui/ClassPickerScreen.h"

#include "AppStateGlobal.h"
#include "gameplay/RomanNumeral.h"
#include "ui/UIComponents.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace selva::ui
{

namespace
{

// Two-tab layout for the Signing modal:
//   - Accept tab: the three signed-class options (YIELD / WITHHOLD /
//     ENDURE). Player selects one with arrow keys + Enter, or mouse
//     hover + click.
//   - Refuse tab: the single Refuse option. Same selection model;
//     the option list happens to have one row.
// Player switches tabs with Q / E (or mouse click on the tab header).
// Within the active tab the option cursor moves with Up / Down / W /
// S; selection is committed with Enter or mouse click on the row.
//
// Selection visual matches the dialog screen (see DialogScreen.cpp):
//   - Header / HeaderHovered / HeaderActive style colors all
//     transparent -- Selectable's default blue fill is suppressed.
//   - A "> " marker prefix on the cursor-highlighted row is the ONLY
//     visual cue for selection.
//   - Last-input-source-wins: keyboard arrow keys claim cursor
//     ownership; real mouse movement releases it back to mouse-hover
//     driving the cursor. Prevents the keyboard-selected row from
//     visibly flickering to wherever a stationary mouse happens to
//     rest.
//
// Confirm phase is the same modal panel reused with different
// content; backdrop stays up across both phases so the world reads
// as suspended for the duration of the ritual.
enum class Phase : std::uint8_t
{
    Inactive = 0,
    Picking,
    Confirming,
};

enum class Tab : std::uint8_t
{
    Accept,
    Refuse,
};

struct State
{
    Phase phase = Phase::Inactive;
    Tab tab = Tab::Accept;
    PlayerClass cursor = PlayerClass::Penitent;
    PlayerClass confirming = PlayerClass::None;
    bool keyboard_owns_cursor = true;
};

State& state()
{
    static State s;
    return s;
}

struct Option
{
    PlayerClass cls;
    const char* label;       // posture verb (YIELD / WITHHOLD / ENDURE / REFUSE)
    const char* description; // one sensory line, tier-0 (no class names, no cosmology)
    std::uint32_t str;       // starting STR (rendered as roman numeral)
    std::uint32_t dex;       // starting DEX
    std::uint32_t end_;      // starting END (end_ to dodge any <ctype.h> ::end macro)
    std::uint32_t lck;       // starting LCK
    const char* trade_off;   // stub: real balance copy TBD
    const char* confirm_phrase;
};

// Starting stats per class -- locked initial values, real balance
// pass will tune. SHAPE is the lore commitment:
//   - YIELD     (Penitent)   IV/IV/V/III  - balanced, slight END lean
//   - WITHHOLD  (Heretic)    III/V/III/IV - DEX/LCK specialist
//   - ENDURE    (Wretched)   V/III/V/II   - STR/END heavyweight
//   - REFUSE    (Unburdened) I/I/I/I      - locked at the floor forever
//     (classes.md *Unburdened* doctrine).
constexpr std::array<Option, 3> kAcceptOptions = {{
    {PlayerClass::Penitent, "YIELD", "A soul that opens fully to what is given to it.", 4u, 4u, 5u,
     3u, "(TBD - balanced, full installation; no exceptional edge)", "Thou wilt yield."},
    {PlayerClass::Heretic, "WITHHOLD", "A soul that takes the gift but holds part of itself back.",
     3u, 5u, 3u, 4u, "(TBD - sharp specialization; lower total ceilings)", "Thou wilt withhold."},
    {PlayerClass::Wretched, "ENDURE", "A soul that takes the gift knowing it will not save it.", 5u,
     3u, 5u, 2u, "(TBD - heaviest weight class; commits cost more)", "Thou wilt endure."},
}};

constexpr std::array<Option, 1> kRefuseOptions = {{
    {PlayerClass::Unburdened, "REFUSE", "A soul that turns aside from what is offered.", 1u, 1u, 1u,
     1u, "(TBD - stats locked at 1; power from technique, not numbers)", "Thou wilt refuse."},
}};

const Option* activeOptions(Tab t, std::size_t& count)
{
    if (t == Tab::Accept)
    {
        count = kAcceptOptions.size();
        return kAcceptOptions.data();
    }
    count = kRefuseOptions.size();
    return kRefuseOptions.data();
}

const Option* findOption(PlayerClass c)
{
    for (const auto& o : kAcceptOptions)
        if (o.cls == c)
            return &o;
    for (const auto& o : kRefuseOptions)
        if (o.cls == c)
            return &o;
    return nullptr;
}

bool isAcceptClass(PlayerClass c)
{
    return c == PlayerClass::Penitent || c == PlayerClass::Heretic || c == PlayerClass::Wretched;
}

void snapCursorToTabDefault(State& s)
{
    if (s.tab == Tab::Accept)
        s.cursor = kAcceptOptions.front().cls;
    else
        s.cursor = kRefuseOptions.front().cls;
}

void switchTab(Tab to)
{
    auto& s = state();
    if (s.tab == to)
        return;
    s.tab = to;
    snapCursorToTabDefault(s);
    s.keyboard_owns_cursor = true;
}

void commitClass(PlayerClass c)
{
    auto* profile = activePlayerProfile();
    if (profile != nullptr)
        profile->player_class = c;
    // signing_committed gates the dialog layer's post-Signing entry
    // topics. Path flag mirrors the cosmological identity (class-
    // pickers install the Crucible; Unburdened installs the Censer).
    setFlag("signing_committed");
    if (c == PlayerClass::Unburdened)
    {
        setFlag("signing_refused");
        setFlag("path_unburdened");
        clearFlag("signing_accepted");
        clearFlag("path_class_picker");
    }
    else if (c != PlayerClass::None)
    {
        setFlag("signing_accepted");
        setFlag("path_class_picker");
        clearFlag("signing_refused");
        clearFlag("path_unburdened");
    }
    std::fprintf(stderr, "[class-picker] committed class=%s\n", playerClassName(c));
    std::fflush(stderr);
    // No special finalize step. The active profile is already a
    // real saveData entry (the unnamed-but-real character pattern):
    // we just mutated its player_class. The next pause-open
    // autosave persists everything to disk.
}

// True if the mouse moved this frame. Used to flip cursor ownership
// back to mouse so keyboard-driven cursor doesn't flicker to a
// stationary mouse's row.
bool mouseMovedThisFrame()
{
    const ImVec2 d = ImGui::GetIO().MouseDelta;
    return (d.x != 0.0f) || (d.y != 0.0f);
}

void drawStatRoman(std::uint32_t value, ImU32 color)
{
    const auto r = selva::gameplay::encodeRoman(value);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float advance = selva::ui::drawRomanGlyphs(dl, pos, r, color);
    // Reserve the space so subsequent SameLine calls flow correctly.
    ImGui::Dummy(ImVec2(advance, ImGui::GetFontSize()));
}

void drawStatRow(const Option& opt, ImU32 color)
{
    constexpr float kStatSpacing = 14.0f;
    ImGui::TextDisabled("STR");
    ImGui::SameLine(0.0f, 4.0f);
    drawStatRoman(opt.str, color);
    ImGui::SameLine(0.0f, kStatSpacing);
    ImGui::TextDisabled("DEX");
    ImGui::SameLine(0.0f, 4.0f);
    drawStatRoman(opt.dex, color);
    ImGui::SameLine(0.0f, kStatSpacing);
    ImGui::TextDisabled("END");
    ImGui::SameLine(0.0f, 4.0f);
    drawStatRoman(opt.end_, color);
    ImGui::SameLine(0.0f, kStatSpacing);
    ImGui::TextDisabled("LCK");
    ImGui::SameLine(0.0f, 4.0f);
    drawStatRoman(opt.lck, color);
}

// Render the active tab's option list. Selection cue is the dialog-
// screen pattern: transparent Header/HeaderHovered/HeaderActive (no
// blue fill); a "> " marker on the cursor row. Returns true if a
// click committed -- caller transitions to Confirming.
bool drawOptionList()
{
    auto& s = state();
    std::size_t count = 0;
    const Option* opts = activeOptions(s.tab, count);
    if (count == 0)
        return false;
    // Suppress Selectable's default blue fill on hover/active -- the
    // "> " marker is the entire selection cue. Match DialogScreen.
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    bool clicked = false;
    PlayerClass hovered_cls = s.cursor;
    for (std::size_t i = 0; i < count; ++i)
    {
        const Option& opt = opts[i];
        const bool selected = (opt.cls == s.cursor);
        ImGui::PushID(static_cast<int>(opt.cls));

        // Posture verb row. "> " marker prefixes the cursor row.
        const std::string marker = selected ? "> " : "  ";
        const std::string label = marker + opt.label;
        const ImVec4 row_color =
            selected ? ImVec4(0.95f, 0.86f, 0.55f, 1.0f) : ImVec4(0.65f, 0.58f, 0.48f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, row_color);
        ImGui::SetWindowFontScale(selected ? 1.20f : 1.10f);
        const bool row_clicked = ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None,
                                                   ImVec2(0.0f, ImGui::GetFontSize() * 1.4f));
        const bool row_hovered = ImGui::IsItemHovered();
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        if (row_hovered && !s.keyboard_owns_cursor)
            hovered_cls = opt.cls;
        if (row_clicked)
        {
            // Click on a row commits the option. Snap cursor to it
            // first so the Confirming phase reads the right class.
            hovered_cls = opt.cls;
            clicked = true;
        }

        // Description + stat row + trade-off stub, indented under the
        // posture verb. Always rendered (not gated on selection) so
        // the player sees all options legibly.
        ImGui::Indent(28.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.58f, 0.48f, 0.95f));
        ImGui::TextWrapped("%s", opt.description);
        ImGui::PopStyleColor();
        const ImU32 stat_color = ImGui::ColorConvertFloat4ToU32(
            selected ? ImVec4(0.95f, 0.86f, 0.55f, 1.0f) : ImVec4(0.70f, 0.62f, 0.52f, 1.0f));
        drawStatRow(opt, stat_color);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.45f, 0.38f, 0.80f));
        ImGui::TextWrapped("%s", opt.trade_off);
        ImGui::PopStyleColor();
        ImGui::Unindent(28.0f);
        ImGui::Spacing();
        ImGui::PopID();
    }
    s.cursor = hovered_cls;
    ImGui::PopStyleColor(3);
    return clicked;
}

// Top tab bar (Accept | Refuse). Mouse click switches the tab; the
// keyboard Q / E hotkeys do the same and are dispatched separately.
// We DON'T use ImGui's BeginTabBar because it forces default visuals
// (rounded tabs, fills) that fight the rest of the modal's flat
// register; a hand-rolled two-Selectable header gives total control
// over the look + matches dialog selection style.
void drawTabHeader()
{
    auto& s = state();
    const float content_w = ImGui::GetContentRegionAvail().x;
    const float tab_w = (content_w - 8.0f) * 0.5f;

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    auto draw_tab = [&](Tab t, const char* label)
    {
        const bool active = (s.tab == t);
        const ImVec4 color =
            active ? ImVec4(0.95f, 0.86f, 0.55f, 1.0f) : ImVec4(0.50f, 0.45f, 0.38f, 0.85f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::SetWindowFontScale(active ? 1.25f : 1.10f);
        const std::string display = active ? std::string("> ") + label : std::string("  ") + label;
        if (ImGui::Selectable(display.c_str(), false, ImGuiSelectableFlags_None,
                              ImVec2(tab_w, ImGui::GetFontSize() * 1.5f)))
        {
            switchTab(t);
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
    };
    draw_tab(Tab::Accept, "Accept the Signing");
    ImGui::SameLine(0.0f, 8.0f);
    draw_tab(Tab::Refuse, "Refuse the Signing");

    ImGui::PopStyleColor(3);
    // Underline under the active tab so the tab visual reads as
    // selected even with the blue fill suppressed.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float y = pos.y - 4.0f;
    const float x0 = pos.x;
    const float x1 = pos.x + content_w;
    const float mid = pos.x + tab_w + 4.0f;
    const ImU32 dim = ImGui::ColorConvertFloat4ToU32(ImVec4(0.40f, 0.35f, 0.28f, 0.6f));
    const ImU32 bright = ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.86f, 0.55f, 1.0f));
    // Dim line under the inactive tab; bright line under the active tab.
    if (s.tab == Tab::Accept)
    {
        dl->AddLine(ImVec2(x0, y), ImVec2(mid - 8.0f, y), bright, 2.0f);
        dl->AddLine(ImVec2(mid, y), ImVec2(x1, y), dim, 1.0f);
    }
    else
    {
        dl->AddLine(ImVec2(x0, y), ImVec2(mid - 8.0f, y), dim, 1.0f);
        dl->AddLine(ImVec2(mid, y), ImVec2(x1, y), bright, 2.0f);
    }
}

void handleKeyboardPicking()
{
    auto& s = state();

    // Last-input-source-wins: real mouse motion releases keyboard
    // ownership so hover steers the cursor. Any arrow key claims it
    // back.
    if (mouseMovedThisFrame())
        s.keyboard_owns_cursor = false;

    // Tab switching: Left/Right arrow keys, A/D. NOT Q/E -- E is the
    // universal interact/commit key everywhere else (dialog advance,
    // world interact, picker confirm) and would collide with the
    // option commit below.
    const bool tab_left = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, /*repeat=*/false) ||
                          ImGui::IsKeyPressed(ImGuiKey_A, /*repeat=*/false);
    const bool tab_right = ImGui::IsKeyPressed(ImGuiKey_RightArrow, /*repeat=*/false) ||
                           ImGui::IsKeyPressed(ImGuiKey_D, /*repeat=*/false);
    if (tab_left)
        switchTab(Tab::Accept);
    else if (tab_right)
        switchTab(Tab::Refuse);

    const bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow, /*repeat=*/true) ||
                    ImGui::IsKeyPressed(ImGuiKey_W, /*repeat=*/true);
    const bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow, /*repeat=*/true) ||
                      ImGui::IsKeyPressed(ImGuiKey_S, /*repeat=*/true);
    const bool commit = ImGui::IsKeyPressed(ImGuiKey_Enter, /*repeat=*/false) ||
                        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, /*repeat=*/false);

    std::size_t count = 0;
    const Option* opts = activeOptions(s.tab, count);
    if (count == 0)
        return;

    // Find current cursor's index within the active tab.
    int cur = 0;
    for (std::size_t i = 0; i < count; ++i)
        if (opts[i].cls == s.cursor)
        {
            cur = static_cast<int>(i);
            break;
        }
    if (up)
    {
        cur = (cur - 1 + static_cast<int>(count)) % static_cast<int>(count);
        s.cursor = opts[cur].cls;
        s.keyboard_owns_cursor = true;
    }
    if (down)
    {
        cur = (cur + 1) % static_cast<int>(count);
        s.cursor = opts[cur].cls;
        s.keyboard_owns_cursor = true;
    }
    if (commit)
    {
        s.phase = Phase::Confirming;
        s.confirming = s.cursor;
    }
}

void handleKeyboardConfirming()
{
    auto& s = state();
    const bool commit = ImGui::IsKeyPressed(ImGuiKey_Enter, /*repeat=*/false) ||
                        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, /*repeat=*/false);
    if (commit)
    {
        commitClass(s.confirming);
        s.phase = Phase::Inactive;
        s.confirming = PlayerClass::None;
        return;
    }
    if (selva::ui::wantBack())
    {
        s.phase = Phase::Picking;
        s.confirming = PlayerClass::None;
    }
}

void drawPicking()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float panel_w = vp->Size.x * 0.72f;
    const float panel_h = vp->Size.y * 0.85f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.04f, 0.03f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.60f, 0.48f, 0.32f, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36.0f, 28.0f));
    selva::ui::beginCenteredWindow("##class_picker_panel", ImVec2(panel_w, panel_h));

    // Header question. Tier-0: no ritual name.
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.85f, 0.68f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted("What manner of soul dost thou bring to this?");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawTabHeader();
    ImGui::Spacing();
    ImGui::Spacing();

    if (drawOptionList())
    {
        // Mouse click on a row commits to Confirming.
        auto& s = state();
        s.phase = Phase::Confirming;
        s.confirming = s.cursor;
    }

    // No footer key hints. The selection visual + the modal context
    // teach the player; explicit "[Up] [Down] [Enter]" rows are noise.

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void drawConfirming()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float panel_w = vp->Size.x * 0.50f;
    const float panel_h = vp->Size.y * 0.40f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.03f, 0.02f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.78f, 0.55f, 0.30f, 0.98f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36.0f, 28.0f));
    selva::ui::beginCenteredWindow("##class_picker_confirm", ImVec2(panel_w, panel_h));

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.86f, 0.55f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted("Confirm");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Spacing();

    const PlayerClass to_commit = state().confirming;
    const char* phrase = "?";
    if (const Option* o = findOption(to_commit))
        phrase = o->confirm_phrase;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.85f, 0.68f, 1.0f));
    ImGui::SetWindowFontScale(1.15f);
    ImGui::TextUnformatted(phrase);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.55f, 0.30f, 1.0f));
    ImGui::TextWrapped(
        "This cannot be undone here. Press [Enter] to commit, or [Esc/RMB] to choose again.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Spacing();
    auto& s = state();
    if (selva::ui::centeredButton("Commit", 180.0f))
    {
        commitClass(s.confirming);
        s.phase = Phase::Inactive;
        s.confirming = PlayerClass::None;
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

} // namespace

bool classPickerActive()
{
    return state().phase != Phase::Inactive;
}

void openClassPicker(PlayerClass initial_selection)
{
    auto& s = state();
    s.phase = Phase::Picking;
    if (initial_selection != PlayerClass::None)
    {
        s.tab = isAcceptClass(initial_selection) ? Tab::Accept : Tab::Refuse;
        s.cursor = initial_selection;
    }
    else
    {
        s.tab = Tab::Accept;
        snapCursorToTabDefault(s);
    }
    s.confirming = PlayerClass::None;
    s.keyboard_owns_cursor = true;
}

void renderClassPicker()
{
    auto& s = state();
    if (s.phase == Phase::Inactive)
        return;
    // Full-screen dim sits behind both phases (Picking + Confirming)
    // so the world reads as suspended for the entire ritual.
    selva::ui::drawFullScreenBackdrop(0.85f);
    if (s.phase == Phase::Picking)
    {
        drawPicking();
        if (s.phase == Phase::Picking)
            handleKeyboardPicking();
    }
    else if (s.phase == Phase::Confirming)
    {
        drawConfirming();
        if (s.phase == Phase::Confirming)
            handleKeyboardConfirming();
    }
}

} // namespace selva::ui
