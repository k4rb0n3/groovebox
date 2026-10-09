#include "PluginEditor.h"

using namespace cg;

namespace
{
const juce::Colour kBg      { 0xff131417 };
const juce::Colour kPanel   { 0xff1b1d21 };
const juce::Colour kRow     { 0xff1c1e23 };
const juce::Colour kCtrl    { 0xff2a2d33 };
const juce::Colour kText    { 0xffd8d8d8 };
const juce::Colour kDim     { 0xff8a8d93 };
const juce::Colour kAccent  { 0xfff0b44c };

struct Clip
{
    bool valid = false;
    std::array<Step, kMaxSteps> steps {};
    int length = 16, rate = kRate16;
};
Clip& clipboard() { static Clip c; return c; }

juce::String pct (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }
} // namespace

//==============================================================================
Look::Look()
{
    setColour (juce::ResizableWindow::backgroundColourId, kBg);
    setColour (juce::TextButton::buttonColourId, kCtrl);
    setColour (juce::TextButton::buttonOnColourId, kAccent);
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ComboBox::backgroundColourId, kCtrl);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::arrowColourId, kDim);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff202227));
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::Label::textColourId, kText);
    setColour (juce::Slider::backgroundColourId, kCtrl);
    setColour (juce::Slider::trackColourId, kAccent.withAlpha (0.55f));
    setColour (juce::Slider::thumbColourId, kAccent);
    setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    setColour (juce::Slider::rotarySliderOutlineColourId, kCtrl);
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, kAccent.withAlpha (0.4f));
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff2c2f36));
    setColour (juce::TooltipWindow::textColourId, kText);
    setColour (juce::ScrollBar::thumbColourId, kCtrl.brighter (0.3f));
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xff2c2f36));
    setColour (juce::BubbleComponent::outlineColourId, kCtrl);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float lw = juce::jmax (2.0f, r * 0.22f);
    const float angle = start + pos * (end - start);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const float from = bipolar ? (start + end) * 0.5f : start;

    juce::Path bg;
    bg.addCentredArc (c.x, c.y, r - lw * 0.5f, r - lw * 0.5f, 0.0f, start, end, true);
    g.setColour (findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (bg, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (c.x, c.y, r - lw * 0.5f, r - lw * 0.5f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (val, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Point<float> tip (c.x + (r - lw) * std::sin (angle), c.y - (r - lw) * std::cos (angle));
    g.setColour (kText);
    g.drawLine ({ c, tip }, 1.4f);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto col = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : base;
    if (down) col = col.brighter (0.15f);
    else if (over) col = col.brighter (0.07f);
    g.setColour (col);
    g.fillRoundedRectangle (r, 3.0f);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    juce::Path p;
    const float ax = (float) w - 10.0f, ay = (float) h * 0.5f;
    p.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 2.5f);
    g.setColour (kDim);
    g.fillPath (p);
}

//==============================================================================
int StepGrid::stepAt (float x) const
{
    const int len = juce::jlimit (1, kMaxSteps, proc.lanes[(size_t) lane].length);
    return juce::jlimit (0, len - 1, (int) (x / ((float) getWidth() / (float) len)));
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto& L = proc.lanes[(size_t) lane];
    const int len = juce::jlimit (1, kMaxSteps, L.length);
    const float cw = (float) getWidth() / (float) len;
    const float h = (float) getHeight();
    const auto col = roleColour (L.role);
    const int ps = proc.playStep[(size_t) lane].load();
    const float pad = cw > 7.0f ? 1.0f : 0.5f;

    for (int i = 0; i < len; ++i)
    {
        auto r = juce::Rectangle<float> ((float) i * cw, 0.0f, cw, h).reduced (pad, 2.0f);
        g.setColour (((i / 4) % 2 == 0) ? juce::Colour (0xff272a30) : juce::Colour (0xff202227));
        g.fillRoundedRectangle (r, 2.0f);

        const auto& st = L.steps[(size_t) i];
        if (st.on)
        {
            const float alpha = 0.35f + 0.65f * st.prob;
            switch (editMode)
            {
                case EditMode::Steps:
                case EditMode::Velocity:
                {
                    const float bh = r.getHeight() * (0.2f + 0.8f * st.vel);
                    g.setColour (col.withAlpha (alpha));
                    g.fillRoundedRectangle (r.withTop (r.getBottom() - bh), 2.0f);
                    break;
                }
                case EditMode::Probability:
                {
                    g.setColour (col.withAlpha (0.18f));
                    g.fillRoundedRectangle (r, 2.0f);
                    g.setColour (col.withAlpha (0.95f));
                    g.fillRoundedRectangle (r.withTop (r.getBottom() - r.getHeight() * st.prob), 2.0f);
                    break;
                }
                case EditMode::Nudge:
                {
                    g.setColour (col.withAlpha (0.25f));
                    g.fillRoundedRectangle (r, 2.0f);
                    const float cy = r.getCentreY();
                    const float top = cy - st.nudge * r.getHeight();
                    g.setColour (col);
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), juce::jmin (cy, top), r.getRight(), juce::jmax (cy, top) + 1.0f));
                    break;
                }
                case EditMode::Ratchet:
                {
                    g.setColour (col.withAlpha (0.2f));
                    g.fillRoundedRectangle (r, 2.0f);
                    const int n = juce::jlimit (1, 4, st.ratchet);
                    const float bh = r.getHeight() * (float) n / 4.0f;
                    g.setColour (col);
                    g.fillRoundedRectangle (r.withTop (r.getBottom() - bh), 2.0f);
                    break;
                }
            }

            if (editMode == EditMode::Steps)
            {
                if (st.ratchet > 1 && cw > 6.0f)
                {
                    g.setColour (kText.withAlpha (0.85f));
                    for (int k = 0; k < st.ratchet; ++k)
                        g.fillEllipse (r.getX() + 2.0f + (float) k * 3.5f, r.getY() + 2.0f, 2.0f, 2.0f);
                }
                if (std::abs (st.nudge) > 0.01f)
                {
                    g.setColour (kText.withAlpha (0.8f));
                    const float nx = r.getCentreX() + st.nudge * cw;
                    g.fillRect (nx - 0.75f, r.getBottom() - 4.0f, 1.5f, 4.0f);
                }
            }
        }

        if (i == ps)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawRoundedRectangle (r, 2.0f, 1.2f);
        }
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    applyAt (e, true);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    applyAt (e, false);
}

void StepGrid::applyAt (const juce::MouseEvent& e, bool isDown)
{
    const int idx = stepAt ((float) e.x);
    const float v = 1.0f - juce::jlimit (0.0f, 1.0f, (float) e.y / (float) juce::jmax (1, getHeight()));

    {
        const juce::SpinLock::ScopedLockType sl (proc.lock);
        auto& st = proc.lanes[(size_t) lane].steps[(size_t) idx];

        switch (editMode)
        {
            case EditMode::Steps:
                if (isDown)
                {
                    paintState = ! st.on;
                    st.on = paintState;
                    if (paintState && st.vel < 0.05f) st.vel = 0.8f;
                }
                else if (idx != lastIdx)
                {
                    st.on = paintState;
                }
                break;
            case EditMode::Velocity:    if (st.on) st.vel = juce::jmax (0.02f, v); break;
            case EditMode::Probability: if (st.on) st.prob = juce::jmax (0.05f, v); break;
            case EditMode::Nudge:       if (st.on) st.nudge = juce::jlimit (-0.5f, 0.5f, v - 0.5f); break;
            case EditMode::Ratchet:     if (st.on) st.ratchet = 1 + juce::jlimit (0, 3, (int) (v * 4.0f)); break;
        }
    }

    lastIdx = idx;
    repaint();
}

void StepGrid::showMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Clear");
    m.addItem (2, "Fill all steps");
    m.addItem (3, "Shift left");
    m.addItem (4, "Shift right");
    m.addItem (5, "Reverse");
    m.addItem (6, "Randomize velocities");
    m.addItem (7, "Reset nudge, probability, ratchets");
    m.addItem (8, "Double length (copy pattern)", proc.lanes[(size_t) lane].length <= kMaxSteps / 2);
    m.addSeparator();
    m.addItem (9, "Copy lane pattern");
    m.addItem (10, "Paste lane pattern", clipboard().valid);
    m.addSeparator();

    juce::PopupMenu choke;
    const int curChoke = proc.lanes[(size_t) lane].choke;
    choke.addItem (100, "None", true, curChoke == 0);
    for (int i = 1; i <= 4; ++i)
        choke.addItem (100 + i, "Group " + juce::String (i), true, curChoke == i);
    m.addSubMenu ("Choke group", choke);

    juce::PopupMenu outs;
    const int curOut = proc.lanes[(size_t) lane].out;
    for (int i = 0; i < kNumOuts; ++i)
        outs.addItem (200 + i, i == 0 ? juce::String ("Main") : "Out " + juce::String (i + 1), true, curOut == i);
    m.addSubMenu ("Output", outs);

    juce::Component::SafePointer<StepGrid> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self] (int r)
    {
        if (self == nullptr || r == 0) return;
        auto& p = self->proc;
        auto& rnd = juce::Random::getSystemRandom();
        {
            const juce::SpinLock::ScopedLockType sl (p.lock);
            auto& L = p.lanes[(size_t) self->lane];
            const int len = juce::jlimit (1, kMaxSteps, L.length);
            auto first = L.steps.begin(), last = L.steps.begin() + len;

            switch (r)
            {
                case 1: for (auto it = first; it != last; ++it) it->on = false; break;
                case 2: for (auto it = first; it != last; ++it) { it->on = true; if (it->vel < 0.05f) it->vel = 0.8f; } break;
                case 3: std::rotate (first, first + 1, last); break;
                case 4: std::rotate (first, last - 1, last); break;
                case 5: std::reverse (first, last); break;
                case 6: for (auto it = first; it != last; ++it) it->vel = 0.25f + rnd.nextFloat() * 0.75f; break;
                case 7: for (auto it = first; it != last; ++it) { it->nudge = 0.0f; it->prob = 1.0f; it->ratchet = 1; } break;
                case 8:
                    for (int i = 0; i < len && len * 2 <= kMaxSteps; ++i) L.steps[(size_t) (i + len)] = L.steps[(size_t) i];
                    L.length = juce::jmin (kMaxSteps, len * 2);
                    break;
                case 9: clipboard() = { true, L.steps, L.length, L.rate }; break;
                case 10: if (clipboard().valid) { L.steps = clipboard().steps; L.length = clipboard().length; L.rate = clipboard().rate; } break;
                default:
                    if (r >= 100 && r <= 104) L.choke = r - 100;
                    if (r >= 200 && r < 200 + kNumOuts) L.out = r - 200;
                    break;
            }
        }
        self->repaint();
        if (auto* row = self->findParentComponentOfClass<LaneRow>())
            row->refresh();
    });
}

//==============================================================================
void NumberBox::setValue (int v, bool notify)
{
    v = juce::jlimit (lo, hi, v);
    if (v == value) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

void NumberBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (kCtrl);
    g.fillRoundedRectangle (r, 3.0f);
    const float frac = (float) (value - lo) / (float) juce::jmax (1, hi - lo);
    g.setColour (accent.withAlpha (0.35f));
    g.fillRoundedRectangle (r.withWidth (juce::jmax (3.0f, r.getWidth() * frac)), 3.0f);
    g.setColour (kText);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (juce::String (value), getLocalBounds(), juce::Justification::centred, false);
}

void NumberBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    wheelAcc += w.isReversed ? -w.deltaY : w.deltaY;
    if (std::abs (wheelAcc) < 0.1f) return;
    setValue (value + (wheelAcc > 0 ? 1 : -1), true);
    wheelAcc = 0.0f;
}

//==============================================================================
void SampleSlot::paint (juce::Graphics& g)
{
    const auto& L = proc.lanes[(size_t) lane];
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff24272c));
    g.fillRoundedRectangle (r, 3.0f);
    if (dragOver)
    {
        g.setColour (kAccent);
        g.drawRoundedRectangle (r, 3.0f, 1.5f);
    }

    auto inner = getLocalBounds().reduced (6, 2);
    const int count = L.files.size();

    juce::String counter = count > 0 ? juce::String (L.fileIndex + 1) + "/" + juce::String (count) : juce::String();
    g.setFont (juce::FontOptions (10.5f));
    g.setColour (kDim);
    g.drawText (counter, inner.removeFromRight (52), juce::Justification::centredRight, false);

    g.drawText (L.folder == juce::File() ? juce::String ("no folder") : L.name, inner.removeFromTop (inner.getHeight() / 2),
                juce::Justification::bottomLeft, true);

    g.setFont (juce::FontOptions (12.5f));
    if (L.sample != nullptr)
    {
        g.setColour (kText);
        g.drawText (L.sample->file.getFileNameWithoutExtension(), inner, juce::Justification::topLeft, true);
    }
    else
    {
        g.setColour (kDim.withAlpha (0.7f));
        g.drawText ("drop a folder here", inner, juce::Justification::topLeft, true);
    }
}

void SampleSlot::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && ! e.mods.isPopupMenu())
        proc.audition (lane);
}

void SampleSlot::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    wheelAcc += w.isReversed ? -w.deltaY : w.deltaY;
    int delta = 0;
    if (wheelAcc >= 0.1f)       delta = -1;
    else if (wheelAcc <= -0.1f) delta = 1;
    if (delta == 0) return;

    wheelAcc = 0.0f;
    proc.stepSample (lane, delta);
    repaint();
}

void SampleSlot::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    if (files.isEmpty()) return;

    juce::File f (files[0]);
    if (f.isDirectory())
    {
        proc.setLaneFolder (lane, f, true);
    }
    else if (f.existsAsFile())
    {
        proc.setLaneFolder (lane, f.getParentDirectory(), true);
        const int idx = proc.lanes[(size_t) lane].files.indexOf (f);
        if (idx >= 0) proc.loadSampleIndex (lane, idx);
    }
    proc.audition (lane);
    repaint();
    if (onChange) onChange();
}

//==============================================================================
LaneRow::LaneRow (GrooveboxProcessor& p, const EditMode& mode)
    : proc (p), slot (p), grid (p, mode)
{
    for (auto* b : { &muteBtn, &soloBtn, &prevBtn, &nextBtn, &randBtn, &folderBtn, &genBtn, &mutBtn, &removeBtn })
        addAndMakeVisible (b);
    for (auto* c : { &roleBox, &rateBox })
        addAndMakeVisible (c);
    addAndMakeVisible (lenSlider);
    addAndMakeVisible (slot);
    addAndMakeVisible (grid);

    muteBtn.setClickingTogglesState (true);
    soloBtn.setClickingTogglesState (true);
    muteBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd35a5a));
    soloBtn.setColour (juce::TextButton::buttonOnColourId, kAccent);
    muteBtn.setTooltip ("Mute lane");
    soloBtn.setTooltip ("Solo lane");
    prevBtn.setTooltip ("Previous sample (or scroll the mouse wheel over the name)");
    nextBtn.setTooltip ("Next sample");
    randBtn.setTooltip ("Random sample from this folder");
    folderBtn.setTooltip ("Choose a sample folder for this lane");
    genBtn.setTooltip ("Generate a new pattern for this lane only");
    mutBtn.setTooltip ("Mutate this lane's pattern");
    removeBtn.setTooltip ("Remove lane");
    roleBox.setTooltip ("Role: tells the generator how this lane should behave");
    rateBox.setTooltip ("Step rate. Mix rates across lanes for polyrhythm");
    lenSlider.setTooltip ("Lane length in steps (1 to 64): drag up/down or scroll. Different lengths drift against each other");

    for (int i = 0; i < kNumRoles; ++i)
        roleBox.addItem (roleName ((Role) i), i + 1);
    for (int i = 0; i < (int) rates().size(); ++i)
        rateBox.addItem (rates()[(size_t) i].name, i + 1);

    muteBtn.onClick = [this] { edit ([this] (Lane& l) { l.mute = muteBtn.getToggleState(); }); };
    soloBtn.onClick = [this] { edit ([this] (Lane& l) { l.solo = soloBtn.getToggleState(); }); };
    roleBox.onChange = [this]
    {
        edit ([this] (Lane& l) { l.role = (Role) (roleBox.getSelectedId() - 1); });
        refresh();
    };
    rateBox.onChange = [this] { edit ([this] (Lane& l) { l.rate = rateBox.getSelectedId() - 1; }); };
    lenSlider.onChange = [this] (int v)
    {
        edit ([v] (Lane& l) { l.length = v; });
        grid.repaint();
    };

    prevBtn.onClick = [this] { proc.stepSample (lane, -1); slot.repaint(); };
    nextBtn.onClick = [this] { proc.stepSample (lane, 1); slot.repaint(); };
    randBtn.onClick = [this] { proc.randomSample (lane); slot.repaint(); };
    genBtn.onClick  = [this] { proc.generateLane (lane); };
    mutBtn.onClick  = [this] { proc.mutateOneLane (lane); };
    slot.onChange   = [this] { refresh(); };

    folderBtn.onClick = [this]
    {
        auto start = proc.lanes[(size_t) lane].folder;
        if (! start.isDirectory()) start = proc.libraryRoot;
        if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

        chooser = std::make_unique<juce::FileChooser> ("Choose a sample folder for this lane", start);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f.isDirectory())
                                      proc.setLaneFolder (lane, f, true);
                                  else if (f.existsAsFile())
                                  {
                                      proc.setLaneFolder (lane, f.getParentDirectory(), true);
                                      const int idx = proc.lanes[(size_t) lane].files.indexOf (f);
                                      if (idx >= 0) proc.loadSampleIndex (lane, idx);
                                  }
                                  refresh();
                              });
    };

    removeBtn.onClick = [this]
    {
        juce::Component::SafePointer<LaneRow> self (this);
        juce::MessageManager::callAsync ([self] { if (self != nullptr) self->proc.removeLane (self->lane); });
    };

    setupKnob (level, -48.0, 6.0, -6.0, "Level", [this] (float v) { edit ([v] (Lane& l) { l.levelDb = v; }); },
               [] (double v) { return juce::String (v, 1) + " dB"; });
    setupKnob (pan, -1.0, 1.0, 0.0, "Pan", [this] (float v) { edit ([v] (Lane& l) { l.pan = v; }); },
               [] (double v)
               {
                   const int p = juce::roundToInt (v * 100.0);
                   return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
               });
    setupKnob (pitch, -24.0, 24.0, 0.0, "Pitch", [this] (float v) { edit ([v] (Lane& l) { l.pitch = v; }); },
               [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 2) + " st"; });
    setupKnob (decay, 0.0, 1.0, 1.0, "Decay", [this] (float v) { edit ([v] (Lane& l) { l.decay = v; }); },
               [] (double v)
               {
                   if (v >= 0.999) return juce::String ("full");
                   return juce::String (juce::roundToInt (5.0 * std::pow (400.0, v))) + " ms";
               });
    setupKnob (swing, 0.0, 0.75, 0.0, "Swing", [this] (float v) { edit ([v] (Lane& l) { l.swing = v; }); },
               [] (double v) { return pct (v); });
    setupKnob (hum, 0.0, 1.0, 0.15, "Humanize", [this] (float v) { edit ([v] (Lane& l) { l.humanize = v; }); },
               [] (double v) { return pct (v); });

    pan.getProperties().set ("bipolar", true);
    pitch.getProperties().set ("bipolar", true);
    pitch.setRange (-24.0, 24.0, 0.01);
}

template <typename Fn>
void LaneRow::edit (Fn&& fn)
{
    const juce::SpinLock::ScopedLockType sl (proc.lock);
    fn (proc.lanes[(size_t) lane]);
}

void LaneRow::setupKnob (juce::Slider& s, double min, double max, double def, const juce::String& tip,
                         std::function<void (float)> setter, std::function<juce::String (double)> fmt)
{
    addAndMakeVisible (s);
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.setRange (min, max, 0.001);
    s.setDoubleClickReturnValue (true, def);
    s.setPopupDisplayEnabled (true, false, nullptr);
    s.setMouseDragSensitivity (160);
    s.textFromValueFunction = fmt;
    s.setTooltip (tip);
    s.onValueChange = [&s, setter, fmt, tip]
    {
        setter ((float) s.getValue());
        s.setTooltip (tip + ": " + fmt (s.getValue()));
    };
}

void LaneRow::setLane (int li)
{
    lane = li;
    slot.setLane (li);
    grid.setLane (li);
    refresh();
}

void LaneRow::refresh()
{
    const auto& l = proc.lanes[(size_t) lane];
    const auto n = juce::dontSendNotification;

    muteBtn.setToggleState (l.mute, n);
    soloBtn.setToggleState (l.solo, n);
    roleBox.setSelectedId ((int) l.role + 1, n);
    rateBox.setSelectedId (l.rate + 1, n);
    lenSlider.setValue (l.length, false);
    lenSlider.accent = roleColour (l.role);
    level.setValue (l.levelDb, n);
    pan.setValue (l.pan, n);
    pitch.setValue (l.pitch, n);
    decay.setValue (l.decay, n);
    swing.setValue (l.swing, n);
    hum.setValue (l.humanize, n);

    const auto col = roleColour (l.role);
    for (auto* k : { &level, &pan, &pitch, &decay, &swing, &hum })
        k->setColour (juce::Slider::rotarySliderFillColourId, col);

    juce::String extra;
    if (l.choke > 0) extra << "  choke " << l.choke;
    if (l.out > 0)   extra << "  out " << (l.out + 1);
    genBtn.setTooltip ("Generate a new pattern for this lane only" + extra);

    slot.repaint();
    grid.repaint();
    repaint();
}

void LaneRow::tick()
{
    const float m = proc.laneMeter[(size_t) lane].load();
    if (std::abs (m - meter) > 0.02f)
    {
        meter = m;
        repaint (0, 0, 5, getHeight());
    }

    const int ps = proc.playStep[(size_t) lane].load();
    if (ps != grid.lastDrawnPlayStep)
    {
        grid.lastDrawnPlayStep = ps;
        grid.repaint();
    }
}

void LaneRow::paint (juce::Graphics& g)
{
    g.setColour (kRow);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
    const auto col = roleColour (proc.lanes[(size_t) lane].role);
    g.setColour (col.withAlpha (0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, meter)));
    g.fillRoundedRectangle (0.0f, 2.0f, 4.0f, (float) getHeight() - 4.0f, 2.0f);
}

void LaneRow::resized()
{
    auto r = getLocalBounds().reduced (0, 6).withTrimmedLeft (8).withTrimmedRight (6);
    auto take = [&r] (int w, int gap = 3) { auto a = r.removeFromLeft (w); r.removeFromLeft (gap); return a; };

    muteBtn.setBounds (take (22));
    soloBtn.setBounds (take (22, 6));
    roleBox.setBounds (take (92, 6));
    prevBtn.setBounds (take (18, 2));
    slot.setBounds (take (176, 2).expanded (0, 3));
    nextBtn.setBounds (take (18, 3));
    randBtn.setBounds (take (20, 3));
    folderBtn.setBounds (take (24, 8));
    lenSlider.setBounds (take (44, 3));
    rateBox.setBounds (take (64, 6));
    genBtn.setBounds (take (36, 2));
    mutBtn.setBounds (take (36, 8));

    for (auto* k : { &level, &pan, &pitch, &decay, &swing, &hum })
        k->setBounds (take (30, 1).expanded (0, 4));

    removeBtn.setBounds (r.removeFromRight (20));
    r.removeFromRight (6);
    r.removeFromLeft (6);
    grid.setBounds (r.expanded (0, 4));
}

//==============================================================================
void MidiDragButton::mouseDrag (const juce::MouseEvent& e)
{
    juce::TextButton::mouseDrag (e);
    if (dragging || e.getDistanceFromDragStart() < 6)
        return;

    dragging = true;
    const auto f = proc.exportMidi();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

//==============================================================================
GrooveboxEditor::GrooveboxEditor (GrooveboxProcessor& p)
    : AudioProcessorEditor (&p), proc (p), dragMidiBtn (p)
{
    setLookAndFeel (&look);

    title.setText ("GROOVEBOX", juce::dontSendNotification);
    title.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, kAccent);
    title.setBorderSize ({ 0, 0, 0, 0 });
    byline.setText ("by PJH", juce::dontSendNotification);
    byline.setFont (juce::FontOptions (11.5f));
    byline.setColour (juce::Label::textColourId, kDim);
    byline.setBorderSize ({ 0, 0, 0, 0 });
    addAndMakeVisible (byline);
    status.setColour (juce::Label::textColourId, kDim);
    status.setFont (juce::FontOptions (12.0f));
    status.setText ("Drop a folder on a lane, or use Library... to map a whole sample library. Right-click a step grid for more.",
                    juce::dontSendNotification);
    bpmLabel.setColour (juce::Label::textColourId, kDim);
    bpmLabel.setJustificationType (juce::Justification::centredRight);

    for (auto* c : std::initializer_list<juce::Component*> { &title, &status, &bpmLabel, &libraryBtn, &shuffleBtn, &generateBtn,
                                                             &mutateBtn, &previewBtn, &seqBtn, &saveMidiBtn, &dragMidiBtn,
                                                             &genreBox, &modeBox, &barsBox, &density, &variation, &irregular,
                                                             &mutateAmt, &swingScale, &humScale, &master, &viewport })
        addAndMakeVisible (c);

    generateBtn.setColour (juce::TextButton::buttonColourId, kAccent.withAlpha (0.85f));
    generateBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::black);

    for (int g = 0; g < NumGenres; ++g)
        genreBox.addItem (genreName (g), g + 1);
    genreBox.addSeparator();
    genreBox.onChange = [this] { proc.gen.genre = genreBox.getSelectedId() - 1; };
    genreBox.setTooltip ("Genre for the generator. Hybrid gives every lane a different genre; Free ignores genre rules");

    modeBox.addItem ("Draw: Steps", 1);
    modeBox.addItem ("Draw: Velocity", 2);
    modeBox.addItem ("Draw: Probability", 3);
    modeBox.addItem ("Draw: Nudge", 4);
    modeBox.addItem ("Draw: Ratchet", 5);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.setTooltip ("What clicking and dragging on the step grids edits");
    modeBox.onChange = [this]
    {
        editMode = (EditMode) (modeBox.getSelectedId() - 1);
        for (auto& r : rows) r->refresh();
    };

    for (int b : { 1, 2, 4, 8, 16, 32 })
        barsBox.addItem (juce::String (b) + (b == 1 ? " bar" : " bars"), b);
    barsBox.setTooltip ("Length of the exported MIDI clip. Odd lane lengths make every bar different");
    barsBox.onChange = [this] { proc.exportBars = barsBox.getSelectedId(); };

    setupBar (density, "Density", proc.gen.density, [this] (float v) { proc.gen.density = v; });
    setupBar (variation, "Variation", proc.gen.variation, [this] (float v) { proc.gen.variation = v; });
    setupBar (irregular, "Irregular", proc.gen.irregular, [this] (float v) { proc.gen.irregular = v; });
    setupBar (mutateAmt, "Mutate", proc.mutateAmount, [this] (float v) { proc.mutateAmount = v; });
    density.setTooltip ("How busy the generated patterns are");
    variation.setTooltip ("Ghost notes, velocity spread, steps with less than 100% probability, micro timing");
    irregular.setTooltip ("Chance that a lane gets an odd length, odd rate and Euclidean placement");
    mutateAmt.setTooltip ("How much Mutate changes the current groove");

    swingScale.textFromValueFunction = [] (double v) { return "Swing x" + pct (v); };
    humScale.textFromValueFunction   = [] (double v) { return "Humanize x" + pct (v); };
    for (auto* s : { &swingScale, &humScale })
    {
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, true, 120, 20);
    }
    swingScale.setTooltip ("Scales every lane's swing (automatable)");
    humScale.setTooltip ("Scales every lane's humanize (automatable)");
    swingAtt = std::make_unique<SliderAtt> (proc.apvts, "swing", swingScale);
    humAtt   = std::make_unique<SliderAtt> (proc.apvts, "humanize", humScale);
    // The attachments install the parameter's own text; put ours back.
    swingScale.textFromValueFunction = [] (double v) { return "Swing x" + pct (v); };
    humScale.textFromValueFunction   = [] (double v) { return "Humanize x" + pct (v); };
    swingScale.updateText();
    humScale.updateText();

    master.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    master.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    master.setPopupDisplayEnabled (true, false, nullptr);
    master.setTooltip ("Master level");
    masterAtt = std::make_unique<SliderAtt> (proc.apvts, "master", master);

    seqBtn.setClickingTogglesState (true);
    seqBtn.setTooltip ("Internal sequencer on/off. Turn off to play Groovebox only from MIDI notes (C1 = lane 1)");
    seqAtt = std::make_unique<ButtonAtt> (proc.apvts, "seq", seqBtn);

    previewBtn.setClickingTogglesState (true);
    previewBtn.setTooltip ("Play the groove without starting FL's transport");
    previewBtn.onClick = [this] { proc.previewPlaying = previewBtn.getToggleState(); };

    generateBtn.setTooltip ("New groove for all lanes");
    mutateBtn.setTooltip ("Evolve the current groove instead of replacing it");
    shuffleBtn.setTooltip ("Pick a random sample in every lane");
    libraryBtn.setTooltip ("Pick your sample library root; subfolders are matched to lanes by name (kicks, hats, perc...)");
    dragMidiBtn.setTooltip ("Drag onto FL's playlist, piano roll or a channel to drop the groove as MIDI");
    saveMidiBtn.setTooltip ("Save the groove as a .mid file in Documents/Groovebox/MIDI");

    generateBtn.onClick = [this] { proc.generateAll(); };
    mutateBtn.onClick   = [this] { proc.mutateAll(); };
    shuffleBtn.onClick  = [this] { proc.shuffleAllSamples(); };
    dragMidiBtn.onClick = [this] { status.setText ("Drag the Drag MIDI button onto FL's playlist, a pattern or the piano roll.", juce::dontSendNotification); };
    saveMidiBtn.onClick = [this]
    {
        const auto f = proc.exportMidi();
        status.setText ("Saved " + f.getFileName() + " to Documents/Groovebox/MIDI", juce::dontSendNotification);
        f.revealToUser();
    };
    libraryBtn.onClick = [this]
    {
        auto start = proc.libraryRoot.isDirectory() ? proc.libraryRoot
                                                    : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        chooser = std::make_unique<juce::FileChooser> ("Choose your sample library folder", start);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto dir = fc.getResult();
                                  if (! dir.isDirectory()) return;
                                  status.setText ("Scanning " + dir.getFullPathName() + "...", juce::dontSendNotification);
                                  proc.scanLibrary (dir);
                                  int mapped = 0;
                                  for (int i = 0; i < proc.numLanes; ++i)
                                      if (proc.lanes[(size_t) i].sample != nullptr) ++mapped;
                                  status.setText ("Mapped " + juce::String (mapped) + " lanes from " + dir.getFileName()
                                                      + ". Scroll a sample name to dig, Generate for a groove.",
                                                  juce::dontSendNotification);
                              });
    };

    addLaneBtn.onClick = [this] { proc.addLane(); };
    laneList.addAndMakeVisible (addLaneBtn);
    viewport.setViewedComponent (&laneList, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);

    setResizable (true, true);
    setResizeLimits (1100, 480, 2400, 1600);
    setSize (1400, 800);

    syncRows();
    startTimerHz (30);
}

GrooveboxEditor::~GrooveboxEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GrooveboxEditor::setupBar (juce::Slider& s, const juce::String& name, double def, std::function<void (float)> setter)
{
    s.setSliderStyle (juce::Slider::LinearBar);
    s.setTextBoxStyle (juce::Slider::TextBoxLeft, true, 120, 20);
    s.setRange (0.0, 1.0, 0.01);
    s.textFromValueFunction = [name] (double v) { return name + " " + pct (v); };
    s.setValue (def, juce::dontSendNotification);
    s.setDoubleClickReturnValue (true, def);
    s.onValueChange = [&s, setter] { setter ((float) s.getValue()); };
    s.updateText();
}

void GrooveboxEditor::syncRows()
{
    const int nl = proc.numLanes.load();
    while ((int) rows.size() < nl)
    {
        rows.push_back (std::make_unique<LaneRow> (proc, editMode));
        laneList.addAndMakeVisible (*rows.back());
    }
    while ((int) rows.size() > nl)
        rows.pop_back();

    for (int i = 0; i < nl; ++i)
        rows[(size_t) i]->setLane (i);

    genreBox.setSelectedId (proc.gen.genre + 1, juce::dontSendNotification);
    barsBox.setSelectedId (proc.exportBars, juce::dontSendNotification);
    density.setValue (proc.gen.density, juce::dontSendNotification);
    variation.setValue (proc.gen.variation, juce::dontSendNotification);
    irregular.setValue (proc.gen.irregular, juce::dontSendNotification);
    mutateAmt.setValue (proc.mutateAmount, juce::dontSendNotification);
    for (auto* s : { &density, &variation, &irregular, &mutateAmt })
        s->updateText();

    layoutLaneList();
}

void GrooveboxEditor::layoutLaneList()
{
    const int w = juce::jmax (100, viewport.getWidth() - viewport.getScrollBarThickness() - 2);
    const int step = LaneRow::kHeight + 3;
    const int nl = (int) rows.size();
    for (int i = 0; i < nl; ++i)
        rows[(size_t) i]->setBounds (0, i * step, w, LaneRow::kHeight);

    addLaneBtn.setVisible (nl < kMaxLanes);
    addLaneBtn.setBounds (8, nl * step + 6, 110, 26);
    laneList.setSize (w, nl * step + 40);
}

void GrooveboxEditor::timerCallback()
{
    for (int i = 0; i < kMaxLanes; ++i)
    {
        auto& m = proc.laneMeter[(size_t) i];
        m = m.load() * 0.8f;
    }

    if (proc.modelVersion.load() != seenVersion)
    {
        seenVersion = proc.modelVersion.load();
        syncRows();
    }

    for (auto& r : rows)
        r->tick();

    static int counter = 0;
    if (++counter % 15 == 0)
        bpmLabel.setText (juce::String (proc.lastBpm.load(), 1) + " BPM", juce::dontSendNotification);
}

void GrooveboxEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRect (0, 0, getWidth(), 72);

    // Column captions aligned to the first lane row.
    if (! rows.empty())
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (10.5f));
        const auto origin = viewport.getPosition();
        auto* row = rows.front().get();
        const char* knobNames[] = { "Lvl", "Pan", "Pitch", "Decay", "Swing", "Hum" };
        int k = 0;
        for (auto* c : row->getChildren())
        {
            if (auto* s = dynamic_cast<juce::Slider*> (c); s != nullptr && s->getSliderStyle() != juce::Slider::LinearBar && k < 6)
            {
                g.drawText (knobNames[k++], s->getX() + origin.x - 6, 74, s->getWidth() + 12, 14, juce::Justification::centred);
            }
        }
        g.drawText ("Lane / role", origin.x + 8, 74, 120, 14, juce::Justification::left);
        g.drawText ("Sample (scroll to dig)", origin.x + 152, 74, 200, 14, juce::Justification::left);
        g.drawText ("Steps / rate", origin.x + 418, 74, 120, 14, juce::Justification::left);
    }
}

void GrooveboxEditor::resized()
{
    auto r = getLocalBounds();

    auto row1 = r.removeFromTop (38).reduced (10, 6);
    title.setBounds (row1.removeFromLeft (102));
    byline.setBounds (row1.removeFromLeft (52).withTrimmedTop (5));
    libraryBtn.setBounds (row1.removeFromLeft (86)); row1.removeFromLeft (4);
    shuffleBtn.setBounds (row1.removeFromLeft (118)); row1.removeFromLeft (16);
    genreBox.setBounds (row1.removeFromLeft (180)); row1.removeFromLeft (4);
    generateBtn.setBounds (row1.removeFromLeft (88)); row1.removeFromLeft (4);
    mutateBtn.setBounds (row1.removeFromLeft (70)); row1.removeFromLeft (4);
    mutateAmt.setBounds (row1.removeFromLeft (120)); row1.removeFromLeft (16);
    master.setBounds (row1.removeFromRight (30)); row1.removeFromRight (8);
    seqBtn.setBounds (row1.removeFromRight (88)); row1.removeFromRight (4);
    previewBtn.setBounds (row1.removeFromRight (60));

    auto row2 = r.removeFromTop (30).reduced (10, 3);
    for (auto* s : { &density, &variation, &irregular })
    {
        s->setBounds (row2.removeFromLeft (128));
        row2.removeFromLeft (4);
    }
    row2.removeFromLeft (12);
    swingScale.setBounds (row2.removeFromLeft (130)); row2.removeFromLeft (4);
    humScale.setBounds (row2.removeFromLeft (150)); row2.removeFromLeft (16);
    modeBox.setBounds (row2.removeFromLeft (150)); row2.removeFromLeft (16);
    barsBox.setBounds (row2.removeFromLeft (86)); row2.removeFromLeft (4);
    dragMidiBtn.setBounds (row2.removeFromLeft (92)); row2.removeFromLeft (4);
    saveMidiBtn.setBounds (row2.removeFromLeft (84));
    bpmLabel.setBounds (row2.removeFromRight (90));

    r.removeFromTop (18); // captions
    status.setBounds (r.removeFromBottom (26).reduced (10, 2));
    viewport.setBounds (r.reduced (8, 2));
    layoutLaneList();
    repaint();
}
