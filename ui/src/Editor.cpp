// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/Editor.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>
#include <sst/jucegui/accessibility/KeyboardTraverser.h>
#include <sst/jucegui/components/BaseStyles.h>
#include <sst/jucegui/components/ButtonPainter.h>
#include <sst/jucegui/components/GlyphButton.h>
#include <sst/jucegui/components/TextPushButton.h>
#include <sst/jucegui/style/JUCELookAndFeelAdapter.h>
#include <sst/jucegui/style/StyleAndSettingsConsumer.h>
#include <sst/jucegui/style/StyleSheet.h>

#include "AboutBox.h"
#include "GrainCanvas.h"
#include "LoaderStrip.h"
#include "NoticeSink.h"
#include "Palette.h"
#include "gg/core/Version.h"
#include "gg/ui/UndoStack.h"
#include "views/EditView.h"
#include "views/FxView.h"
#include "views/ModView.h"
#include "views/PerformView.h"
#include "views/ViewStack.h"

namespace gg::ui
{
namespace
{
namespace jcmp = sst::jucegui::components;

/// sst-jucegui's built-in sheet for `theme`, for its controls (spec §6.9).
sst::jucegui::style::StyleSheet::ptr_t styleSheetFor(ThemeChoice theme)
{
    // initializeStyleSheets is idempotent (it registers the classes once per process), but the
    // built-in sheets are rebuilt after JUCE's shutdown cleanup deletes them, so ask each time
    // rather than keeping one.
    sst::jucegui::style::StyleSheet::initializeStyleSheets([] {});
    return sst::jucegui::style::StyleSheet::getBuiltInStyleSheet(
        theme == ThemeChoice::Light ? sst::jucegui::style::StyleSheet::LIGHT
                                    : sst::jucegui::style::StyleSheet::DARK);
}

juce::String fromUtf8(const std::string &text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

/// The product's name: a click opens the About box.
struct ProductLabel final : juce::Label
{
    std::function<void()> onClick;
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (onClick && !e.mouseWasDraggedSinceMouseDown())
        {
            onClick();
        }
    }
};

/// Undo or redo: sst-jucegui's glyph button with a curved arrow (sst-jucegui has no undo glyph),
/// and a tooltip naming what it would apply.
struct HistoryButton final : jcmp::GlyphButton, juce::SettableTooltipClient
{
    explicit HistoryButton(bool redo)
        : jcmp::GlyphButton(redo ? jcmp::GlyphPainter::JOG_RIGHT : jcmp::GlyphPainter::JOG_LEFT),
          redo_(redo)
    {
    }

    void paint(juce::Graphics &g) override
    {
        jcmp::paintButtonBG(this, g);
        const float alpha = isEnabled() ? (isHovered ? 1.0f : 0.85f) : 0.35f;
        const auto box = getLocalBounds().toFloat().reduced(6.0f);
        const float side = std::min(box.getWidth(), box.getHeight());
        const auto c = box.getCentre().translated(0.0f, side * 0.12f);
        // An arc over the top from the right to the left, its arrowhead at the left end pointing
        // down (undo); mirrored for redo.
        const float r = side * 0.36f;
        const float sweep = juce::MathConstants<float>::halfPi * 1.3f;
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, r, r, 0.0f, sweep, -sweep, true);
        const auto tip = arc.getCurrentPosition();
        const float h = side * 0.18f;
        juce::Path head;
        head.addTriangle(tip.x - h, tip.y - h * 0.2f, tip.x + h, tip.y - h * 0.2f, tip.x,
                         tip.y + h * 0.9f);
        if (redo_)
        {
            const auto flip = juce::AffineTransform::scale(-1.0f, 1.0f, c.x, c.y);
            arc.applyTransform(flip);
            head.applyTransform(flip);
        }
        g.setColour(getColour(Styles::labelcolor).withAlpha(alpha));
        g.strokePath(arc, juce::PathStrokeType(1.6f));
        g.fillPath(head);
    }

    const bool redo_;
};

/// The editor's Tab order: sst-jucegui's traverser over the reading order of what is shown now.
/// sst-jucegui's knobs number themselves per parent, and the rest of the editor's components not
/// at all, so before each move every focusable component shown gets its place in JUCE's reading
/// order (top to bottom, left to right, parent before child) as its traversal ID; the traverser
/// then goes round, the last back to the first.
struct EditorTraverser final : sst::jucegui::accessibility::KeyboardTraverser
{
    /// `modal`, while visible, takes the focus from everything behind it: it is a focus container
    /// of its own, whose traverser keeps Tab inside it.
    EditorTraverser(juce::Component &root, juce::Component &modal) : root_(root), modal_(modal) {}

    juce::Component *getDefaultComponent(juce::Component *parent) override
    {
        if (modal_.isVisible())
        {
            return &modal_;
        }
        number();
        return KeyboardTraverser::getDefaultComponent(parent);
    }
    juce::Component *getNextComponent(juce::Component *current) override
    {
        if (modal_.isVisible())
        {
            return &modal_;
        }
        number();
        return KeyboardTraverser::getNextComponent(current);
    }
    juce::Component *getPreviousComponent(juce::Component *current) override
    {
        if (modal_.isVisible())
        {
            return &modal_;
        }
        number();
        return KeyboardTraverser::getPreviousComponent(current);
    }

  private:
    void number()
    {
        int64_t id = 0;
        for (juce::Component *c : juce::KeyboardFocusTraverser().getAllComponents(&root_))
        {
            namespace adapters = sst::jucegui::component_adapters;
            ++id;
            adapters::setTraversalId(c, id);
            adapters::setFullTraversalId(c, id);
        }
    }

    juce::Component &root_;
    juce::Component &modal_;
};

constexpr std::array<const char *, 4> kViewNames{"Perform", "Edit", "Mod", "FX"};
constexpr std::array<const char *, 4> kViewIds{"header.perform", "header.edit", "header.mod",
                                               "header.fx"};
} // namespace

/// The editor (M2 sub-project 6's spec §6.2): the header over the four views, one shown at a
/// time. It starts at kEditorWidth x kEditorHeight and lays out at any size, kEditorMinWidth x
/// kEditorMinHeight being the smallest the plugin asks a host for.
///
/// **The header** (40 px): the product's name (a click opens the About box), the preset, the
/// view buttons (a radio group), the notice (a refused drop, a setting not saved: 4 s), the frame
/// time, and at the right undo and redo (tooltips naming the entry) and the theme switch.
///
/// **Its state** is the view shown (opened on, and kept in, the context's view: the plugin
/// instance's), the selected layer (one value, pushed to the canvas, the strips and the Edit and
/// Mod views) and the theme; none is saved. The undo history is the context's, the plugin's.
///
/// **Keys**, wherever the focus is when the focused component doesn't take them: Command 1 to 4
/// (Ctrl off macOS) the views, Command Shift A to D the layers, Command Z undo, Command Shift Z
/// and Command Y redo, Command I the About box, Escape closes it or ends a canvas drag. Tab
/// moves through every control shown (EditorTraverser). While the About box is open it is
/// modal: Escape closes it, every other key is consumed, and Tab stays inside it.
///
/// **The theme** is the context's per-machine setting: read when the editor is made, polled
/// every 30th timer tick so a change in another editor is followed, written by the switch. A
/// switch restyles in place: sst-jucegui's sheet on the root, the look and feel, the palette
/// (PaletteOwner), a look-and-feel change sent to every component, a repaint.
///
/// Files dropped anywhere on the editor go to the instrument's loader strip unless a lane takes
/// them. Its timer is a public base so tests can drive it: at 30 Hz it polls the sample's status,
/// follows the base values, the undo history and the notice, and shows the frame time.
class Editor final : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     public juce::Timer,
                     public sst::jucegui::style::StyleConsumer,
                     public PaletteOwner,
                     public NoticeSink
{
  public:
    explicit Editor(const EditorContext &context)
        : sst::jucegui::style::StyleConsumer(
              sst::jucegui::components::base_styles::Base::styleClass),
          context_(context), publishing_(context.publishing), lookAndFeel_(this), perform_(context),
          edit_(context, true), mod_(context), fx_(context), views_(perform_, edit_, mod_, fx_),
          undo_(false), redo_(true), about_(context.productName, context.formatName)
    {
        setOpaque(true);
        setWantsKeyboardFocus(true);
        setFocusContainerType(FocusContainerType::keyboardFocusContainer);
        const juce::String product = fromUtf8(context.productName);
        setTitle(product);

        setUpLabel(product_, "header.product", product, juce::FontOptions(18.0f, juce::Font::bold));
        setUpLabel(preset_, "header.preset", "Init", juce::FontOptions(15.0f));
        product_.setTooltip("version " + fromUtf8(std::string(gg::kVersion)) +
                            ": click for About (Command I)");
        product_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        product_.onClick = [this] { openAbout(); };
        preset_.setTooltip("The preset");
        setUpLabel(frameMs_, "header.frameMs", {}, juce::FontOptions(12.0f));
        frameMs_.setJustificationType(juce::Justification::centredRight);
        frameMs_.setTooltip("The canvas's frame time, the mean of the last 60 frames");
        setUpLabel(notice_, "header.notice", {}, juce::FontOptions(12.0f));
        notice_.setMinimumHorizontalScale(0.7f);
        notice_.setVisible(false);

        for (std::size_t i = 0; i < viewButtons_.size(); ++i)
        {
            juce::TextButton &button = viewButtons_[i];
            button.setComponentID(kViewIds[i]);
            button.setButtonText(kViewNames[i]);
            button.setRadioGroupId(1);
            button.setClickingTogglesState(true);
            button.setTooltip(juce::String(kViewNames[i]) + " (Command " +
                              juce::String(static_cast<int>(i) + 1) + ")");
            button.onClick = [this, i] { selectView(static_cast<View>(i)); };
            addAndMakeVisible(button);
        }

        undo_.setComponentID("header.undo");
        undo_.setTitle("Undo");
        undo_.setOnCallback([this] { undoOrRedo(false); });
        addAndMakeVisible(undo_);
        redo_.setComponentID("header.redo");
        redo_.setTitle("Redo");
        redo_.setOnCallback([this] { undoOrRedo(true); });
        addAndMakeVisible(redo_);
        theme_.setComponentID("header.theme");
        theme_.setOnCallback([this] {
            setTheme(themeChoice_ == ThemeChoice::Dark ? ThemeChoice::Light : ThemeChoice::Dark);
        });
        addAndMakeVisible(theme_);

        perform_.onSelectLayer = [this](uint8_t layer) { selectLayer(layer); };
        edit_.onSelectLayer = [this](uint8_t layer) { selectLayer(layer); };
        mod_.onSelectLayer = [this](uint8_t layer) { selectLayer(layer); };
        addAndMakeVisible(views_);

        about_.onClose = [this] {
            if (isShowing())
            {
                grabKeyboardFocus(); // back from the box
            }
        };
        addChildComponent(about_);

        selectView(context.view ? context.view() : View::Perform);
        selectLayer(context.layer ? context.layer() : uint8_t{0});
        lastSetting_ = context.theme ? context.theme() : ThemeChoice::Dark;
        applyTheme(lastSetting_);

        setSize(kEditorWidth, kEditorHeight);
        timerCallback();
        startTimerHz(30);
    }

    ~Editor() override
    {
        stopTimer();
        // The look and feel is a member, gone before juce::Component's destructor runs.
        setLookAndFeel(nullptr);
        // The plugin publishes its grains and snapshots only while an editor is open (spec
        // §6.4); the shim destroys the editor in guiDestroy, with no hook of its own.
        if (publishing_)
        {
            publishing_(false);
        }
    }

    // ---- The editor's state, for the free functions in Editor.h ----

    void selectView(View view)
    {
        views_.show(view);
        for (std::size_t i = 0; i < viewButtons_.size(); ++i)
        {
            viewButtons_[i].setToggleState(i == static_cast<std::size_t>(view),
                                           juce::dontSendNotification);
        }
        refreshShownView();
        if (context_.setView)
        {
            context_.setView(view);
        }
    }

    View currentView() const noexcept { return views_.shown(); }

    void selectLayer(uint8_t layer)
    {
        layer_ = std::min<uint8_t>(layer, gg::core::kLayers - 1);
        perform_.setSelectedLayer(layer_);
        edit_.setLayer(layer_);
        mod_.setLayer(layer_);
        if (context_.setLayer)
        {
            context_.setLayer(layer_);
        }
    }

    /// The header's switch: applies `theme` here, then saves it; a save that fails is said in
    /// the notice, and the setting's poll leaves this editor as it is (lastSetting_ unchanged).
    void setTheme(ThemeChoice theme)
    {
        applyTheme(theme);
        if (!context_.setTheme)
        {
            return;
        }
        if (context_.setTheme(theme))
        {
            lastSetting_ = theme;
        }
        else
        {
            showNotice("can't save the theme: the settings file can't be written");
        }
    }

    // ---- PaletteOwner, NoticeSink ----

    const Palette &palette() const noexcept override
    {
        return themeChoice_ == ThemeChoice::Light ? Palette::light() : Palette::dark();
    }
    ThemeChoice theme() const noexcept override { return themeChoice_; }

    void showNotice(const std::string &utf8) override
    {
        if (context_.notice)
        {
            context_.notice(utf8);
        }
        const juce::String text = fromUtf8(utf8);
        notice_.setText(text, juce::dontSendNotification);
        notice_.setTooltip(text);
        notice_.setVisible(true);
        noticeTicks_ = kNoticeTicks;
        layoutNotice();
    }

    // ---- juce::Component ----

    void paint(juce::Graphics &g) override
    {
        const Palette &pal = palette();
        g.fillAll(pal.background);
        g.setColour(pal.header);
        g.fillRect(getLocalBounds().removeFromTop(kHeaderHeight));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop(kHeaderHeight).reduced(kMargin, 6);
        // The left group is M1's: the product's name and the preset's slot.
        product_.setBounds(header.removeFromLeft(150));
        preset_.setBounds(header.removeFromLeft(90));
        header.removeFromLeft(12);
        // Narrow (the smallest sizes), the buttons are too.
        const bool compact = header.getWidth() < 560;
        const std::array<int, 4> widths =
            compact ? std::array<int, 4>{64, 44, 44, 36} : std::array<int, 4>{76, 56, 56, 48};
        for (std::size_t i = 0; i < viewButtons_.size(); ++i)
        {
            viewButtons_[i].setBounds(header.removeFromLeft(widths[i]));
            header.removeFromLeft(compact ? 2 : 4);
        }
        theme_.setBounds(header.removeFromRight(compact ? 48 : 56));
        header.removeFromRight(compact ? 4 : 8);
        redo_.setBounds(header.removeFromRight(28));
        header.removeFromRight(2);
        undo_.setBounds(header.removeFromRight(28));
        header.removeFromRight(compact ? 4 : 8);
        middle_ = header;
        frameMs_.setBounds(
            middle_.withTrimmedLeft(middle_.getWidth() - std::min(170, middle_.getWidth())));
        layoutNotice();

        area = area.reduced(kMargin, kGap);
        views_.setBounds(area);
        about_.setBounds(getLocalBounds());
    }

    bool keyPressed(const juce::KeyPress &key) override
    {
        const auto mods = key.getModifiers();
        const int code = key.getKeyCode();
        if (about_.isVisible())
        {
            // The box is modal: Escape closes it, and every other key, Command I included, is
            // its own, so nothing changes behind it (the undo history, the view, the layer).
            if (code == juce::KeyPress::escapeKey)
            {
                about_.onEscape();
            }
            return true;
        }
        if (code == juce::KeyPress::escapeKey)
        {
            return perform_.canvas().endDragIfAny();
        }
        if (!mods.isCommandDown() || mods.isAltDown())
        {
            return false;
        }
        const int letter = static_cast<int>(
            juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(code)));
        if (mods.isShiftDown())
        {
            if (letter == 'Z')
            {
                undoOrRedo(true);
                return true;
            }
            if (letter >= 'A' && letter <= 'D')
            {
                selectLayer(static_cast<uint8_t>(letter - 'A'));
                return true;
            }
            return false;
        }
        if (code >= '1' && code <= '4')
        {
            selectView(static_cast<View>(code - '1'));
            return true;
        }
        switch (letter)
        {
        case 'Z':
            undoOrRedo(false);
            return true;
        case 'Y':
            undoOrRedo(true);
            return true;
        case 'I':
            openAbout();
            return true;
        default:
            return false;
        }
    }

    std::unique_ptr<juce::ComponentTraverser> createKeyboardFocusTraverser() override
    {
        return std::make_unique<EditorTraverser>(*this, about_);
    }

    // ---- juce::FileDragAndDropTarget: the loader strip's drop target, the whole editor ----

    bool isInterestedInFileDrag(const juce::StringArray &files) override
    {
        return perform_.loaderStrip() != nullptr && !files.isEmpty();
    }

    void filesDropped(const juce::StringArray &files, int /*x*/, int /*y*/) override
    {
        if (isInterestedInFileDrag(files))
        {
            perform_.loaderStrip()->filesDropped(files);
        }
    }

    // ---- juce::Timer, 30 Hz. Label::setText does nothing when the text is unchanged. ----

    void timerCallback() override
    {
        ++ticks_;
        // The Perform view always: its loader strip polls the sample's status, which applies a
        // finished load (EditorContext::sampleStatus).
        perform_.refresh();
        if (views_.shown() != View::Perform)
        {
            refreshShownView();
        }
        refreshHistory();
        if (noticeTicks_ > 0 && --noticeTicks_ == 0)
        {
            notice_.setVisible(false);
            layoutNotice();
        }
        if (ticks_ % kThemePollTicks == 0 && context_.theme)
        {
            const ThemeChoice setting = context_.theme();
            if (setting != lastSetting_)
            {
                lastSetting_ = setting;
                applyTheme(setting);
            }
        }
        const double ms = perform_.canvas().averageFrameMs();
        frameMs_.setText(frameMs_.getWidth() >= 150 ? "frame ms (avg 60): " + juce::String(ms, 2)
                                                    : juce::String(ms, 2) + " ms",
                         juce::dontSendNotification);
    }

  private:
    static constexpr int kHeaderHeight = 40;
    static constexpr int kMargin = 12;
    static constexpr int kGap = 6;
    static constexpr int kNoticeTicks = 4 * 30; // 4 s at 30 Hz
    static constexpr int kThemePollTicks = 30;  // once a second
    static constexpr int kNoticeMinWidth = 220; // narrower, it shows under the header

    void setUpLabel(juce::Label &label, const char *id, const juce::String &text,
                    const juce::FontOptions &font)
    {
        label.setComponentID(id);
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(font));
        label.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(label);
    }

    /// The notice takes the header's middle, the frame time hidden while it shows, or, where
    /// that is too narrow, a band under the header's right end, over the view.
    void layoutNotice()
    {
        const bool shown = notice_.isVisible();
        frameMs_.setVisible(!shown || middle_.getWidth() < kNoticeMinWidth);
        if (middle_.getWidth() >= kNoticeMinWidth)
        {
            notice_.setBounds(middle_);
        }
        else
        {
            const int width = std::max(0, std::min(420, getWidth() - 2 * kMargin));
            notice_.setBounds(getWidth() - kMargin - width, kHeaderHeight, width, 22);
        }
        if (shown)
        {
            notice_.toFront(false);
        }
    }

    void refreshShownView()
    {
        switch (views_.shown())
        {
        case View::Perform:
            perform_.refresh();
            break;
        case View::Edit:
            edit_.refresh();
            break;
        case View::Mod:
            mod_.refresh();
            break;
        case View::Fx:
            fx_.refresh();
            break;
        }
    }

    void undoOrRedo(bool redo)
    {
        if (UndoStack *stack = context_.undo)
        {
            if (redo)
            {
                stack->redo();
            }
            else
            {
                stack->undo();
            }
        }
        refreshHistory();
    }

    /// The buttons' state and tooltips from the history.
    void refreshHistory()
    {
        const UndoStack *stack = context_.undo;
        // Mid-gesture they wait (UndoStack::busy), so the buttons are off then.
        const bool canUndo = stack != nullptr && stack->canUndo() && !stack->busy();
        const bool canRedo = stack != nullptr && stack->canRedo() && !stack->busy();
        undo_.setEnabled(canUndo);
        redo_.setEnabled(canRedo);
        undo_.setTooltip(canUndo ? "Undo " + fromUtf8(stack->undoLabel())
                                 : juce::String("Nothing to undo"));
        redo_.setTooltip(canRedo ? "Redo " + fromUtf8(stack->redoLabel())
                                 : juce::String("Nothing to redo"));
    }

    void openAbout()
    {
        about_.setBounds(getLocalBounds());
        about_.setVisible(true);
        about_.toFront(false);
        if (about_.isShowing())
        {
            about_.grabKeyboardFocus(); // Escape needs the focus (Task 14)
        }
    }

    void applyTheme(ThemeChoice theme)
    {
        themeChoice_ = theme;
        const auto sheet = styleSheetFor(theme);
        lookAndFeel_.setStyle(sheet);
        setStyle(sheet);
        applyPalette();
        // The components that set colors from the palette take them again (LoaderStrip,
        // AboutBox); the rest read it as they paint.
        sendLookAndFeelChange();
        repaint();
    }

    /// The header's own colors.
    void applyPalette()
    {
        const Palette &pal = palette();
        product_.setColour(juce::Label::textColourId, pal.text);
        preset_.setColour(juce::Label::textColourId, pal.dimText);
        frameMs_.setColour(juce::Label::textColourId, pal.dimText);
        notice_.setColour(juce::Label::textColourId, pal.notice);
        notice_.setColour(juce::Label::backgroundColourId, pal.header);
        for (juce::TextButton &button : viewButtons_)
        {
            button.setColour(juce::TextButton::buttonColourId, pal.background);
            button.setColour(juce::TextButton::buttonOnColourId, pal.accent.withAlpha(0.35f));
            button.setColour(juce::TextButton::textColourOffId, pal.dimText);
            button.setColour(juce::TextButton::textColourOnId, pal.text);
            button.setColour(juce::ComboBox::outlineColourId, pal.outline);
        }
        const bool light = themeChoice_ == ThemeChoice::Light;
        theme_.setLabel(light ? "Light" : "Dark");
        theme_.setTitle("Theme");
        theme_.setTooltip(light ? "The light theme: click for the dark"
                                : "The dark theme: click for the light");
    }

    const EditorContext &context_;
    const std::function<void(bool)> publishing_; // this editor's own (EditorContext)
    sst::jucegui::style::LookAndFeelManager lookAndFeel_;

    // The views before the stack that places them and the header, so they go last.
    PerformView perform_;
    EditView edit_;
    ModView mod_;
    FxView fx_;
    ViewStack views_;

    ProductLabel product_;
    juce::Label preset_, notice_, frameMs_;
    std::array<juce::TextButton, 4> viewButtons_;
    HistoryButton undo_, redo_;
    struct ThemeButton final : jcmp::TextPushButton, juce::SettableTooltipClient
    {
    } theme_;
    AboutBox about_;
    juce::TooltipWindow tooltips_{this, 600};

    juce::Rectangle<int> middle_; // the header between the view buttons and undo
    uint8_t layer_{0};
    ThemeChoice themeChoice_{ThemeChoice::Dark};
    ThemeChoice lastSetting_{ThemeChoice::Dark}; // the setting as last read or written here
    int noticeTicks_{0};
    uint32_t ticks_{0};
};

std::unique_ptr<juce::Component> makeEditor(const EditorContext &context)
{
    return std::make_unique<Editor>(context);
}

bool selectView(juce::Component &editor, View view)
{
    auto *e = dynamic_cast<Editor *>(&editor);
    if (e == nullptr)
    {
        return false;
    }
    e->selectView(view);
    return true;
}

View currentView(const juce::Component &editor)
{
    const auto *e = dynamic_cast<const Editor *>(&editor);
    return e != nullptr ? e->currentView() : View::Perform;
}

bool setTheme(juce::Component &editor, ThemeChoice theme)
{
    auto *e = dynamic_cast<Editor *>(&editor);
    if (e == nullptr)
    {
        return false;
    }
    e->setTheme(theme);
    return true;
}
} // namespace gg::ui
