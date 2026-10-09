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
const juce::Colour kGood    { 0xff7bd88f };
const juce::Colour kOk      { 0xffe8c94a };
const juce::Colour kPoor    { 0xffe06c6c };

struct Clip
{
    bool valid = false;
    std::array<Step, kMaxSteps> steps {};
    int length = 16, rate = kRate16;
};
Clip& clipboard() { static Clip c; return c; }

juce::String pct (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }

juce::Colour fitColour (int score)
{
    if (score < 0)  return kDim.withAlpha (0.5f);
    if (score >= 75) return kGood;
    if (score >= 50) return kOk;
    return kPoor;
}

juce::String noteLabel (int midi) { return juce::MidiMessage::getMidiNoteName (midi, true, true, 5); }
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
    if (! b.isEnabled()) col = col.withAlpha (0.4f);
    g.setColour (col);
    g.fillRoundedRectangle (r, 3.0f);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (r, 3.0f);
    juce::Path p;
    const float ax = (float) w - 10.0f, ay = (float) h * 0.5f;
    p.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 2.5f);
    g.setColour (kDim);
    g.fillPath (p);
}

//==============================================================================
void LockButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool locked = getToggleState();
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    auto box = juce::Rectangle<float> (s, s).withCentre (r.getCentre());

    const auto col = locked ? kAccent : (over ? kText : kDim.withAlpha (0.7f));
    auto body = box.withTrimmedTop (box.getHeight() * 0.45f);
    const float shackleW = body.getWidth() * 0.6f;
    juce::Path shackle;
    const float sx = body.getCentreX() - shackleW * 0.5f + (locked ? 0.0f : shackleW * 0.35f);
    shackle.addRoundedRectangle (sx, box.getY() + 1.0f, shackleW, box.getHeight() * 0.62f, shackleW * 0.5f);

    g.setColour (col);
    g.strokePath (shackle, juce::PathStrokeType (1.6f));
    g.setColour (kRow);
    g.fillRect (body.expanded (1.0f, 0.0f));
    g.setColour (col);
    if (locked) g.fillRoundedRectangle (body, 2.0f);
    else        g.drawRoundedRectangle (body.reduced (0.5f), 2.0f, 1.3f);
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
            auto fillFromBottom = [&] (float frac, float a)
            {
                g.setColour (col.withAlpha (a));
                g.fillRoundedRectangle (r.withTop (r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, frac)), 2.0f);
            };
            auto bipolar = [&] (float frac) // frac in -1..1
            {
                g.setColour (col.withAlpha (0.2f));
                g.fillRoundedRectangle (r, 2.0f);
                const float cy = r.getCentreY();
                const float y = cy - frac * r.getHeight() * 0.5f;
                g.setColour (col);
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), juce::jmin (cy, y), r.getRight(), juce::jmax (cy, y) + 1.0f));
            };

            switch (editMode)
            {
                case EditMode::Steps:
                case EditMode::Velocity:    fillFromBottom (0.2f + 0.8f * st.vel, alpha); break;
                case EditMode::Probability: g.setColour (col.withAlpha (0.18f)); g.fillRoundedRectangle (r, 2.0f);
                                            fillFromBottom (st.prob, 0.95f); break;
                case EditMode::Nudge:       bipolar (st.nudge * 2.0f); break;
                case EditMode::Ratchet:     g.setColour (col.withAlpha (0.2f)); g.fillRoundedRectangle (r, 2.0f);
                                            fillFromBottom ((float) juce::jlimit (1, 4, st.ratchet) / 4.0f, 1.0f); break;
                case EditMode::Pitch:       bipolar (st.pitch / 12.0f); break;
                case EditMode::Pan:         bipolar (st.pan); break;
                case EditMode::Cutoff:      g.setColour (col.withAlpha (0.18f)); g.fillRoundedRectangle (r, 2.0f);
                                            fillFromBottom (st.cutoff, 0.95f); break;
                case EditMode::Start:       g.setColour (col.withAlpha (0.18f)); g.fillRoundedRectangle (r, 2.0f);
                                            fillFromBottom (juce::jmax (0.04f, st.start), 0.95f); break;
            }

            if (editMode == EditMode::Pitch && cw > 16.0f && std::abs (st.pitch) > 0.01f)
            {
                g.setColour (kText);
                g.setFont (juce::FontOptions (9.5f));
                g.drawText ((st.pitch > 0 ? "+" : "") + juce::String (juce::roundToInt (st.pitch)), r.toNearestInt(),
                            juce::Justification::centredTop, false);
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
                // A small corner tick marks steps with pitch, pan, cutoff or start set.
                if ((std::abs (st.pitch) > 0.01f || std::abs (st.pan) > 0.01f || st.cutoff < 0.99f || st.start > 0.01f) && cw > 6.0f)
                {
                    g.setColour (kText.withAlpha (0.7f));
                    g.fillRect (r.getRight() - 4.0f, r.getY() + 2.0f, 2.0f, 2.0f);
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
    if (onTouched) onTouched();
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
            case EditMode::Pitch:       if (st.on) st.pitch = (float) juce::jlimit (-12, 12, juce::roundToInt ((v - 0.5f) * 24.0f)); break;
            case EditMode::Pan:         if (st.on) st.pan = juce::jlimit (-1.0f, 1.0f, (v - 0.5f) * 2.0f); break;
            case EditMode::Cutoff:      if (st.on) st.cutoff = v; break;
            case EditMode::Start:       if (st.on) st.start = v < 0.03f ? 0.0f : v; break;
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
    m.addItem (11, "Randomize pitch (+/-2 st)");
    m.addItem (12, "Randomize pan");
    m.addItem (7, "Reset step settings (nudge, probability, ratchet, pitch, pan, cutoff, start)");
    m.addItem (8, "Double length (copy pattern)", proc.lanes[(size_t) lane].length <= kMaxSteps / 2);
    m.addSeparator();
    m.addItem (9, "Copy lane pattern");
    m.addItem (10, "Paste lane pattern", clipboard().valid);

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
                case 11: for (auto it = first; it != last; ++it) it->pitch = (float) (rnd.nextInt (5) - 2); break;
                case 12: for (auto it = first; it != last; ++it) it->pan = (rnd.nextFloat() - 0.5f) * 1.2f; break;
                case 7:
                    for (auto it = first; it != last; ++it)
                    {
                        it->nudge = 0.0f; it->prob = 1.0f; it->ratchet = 1;
                        it->pitch = 0.0f; it->pan = 0.0f; it->cutoff = 1.0f; it->start = 0.0f;
                    }
                    break;
                case 8:
                    for (int i = 0; i < len && len * 2 <= kMaxSteps; ++i) L.steps[(size_t) (i + len)] = L.steps[(size_t) i];
                    L.length = juce::jmin (kMaxSteps, len * 2);
                    break;
                case 9: clipboard() = { true, L.steps, L.length, L.rate }; break;
                case 10: if (clipboard().valid) { L.steps = clipboard().steps; L.length = clipboard().length; L.rate = clipboard().rate; } break;
                default: break;
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
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    g.setColour (kCtrl.withAlpha (alpha));
    g.fillRoundedRectangle (r, 3.0f);
    const float frac = (float) (value - lo) / (float) juce::jmax (1, hi - lo);
    g.setColour (accent.withAlpha (0.35f * alpha));
    g.fillRoundedRectangle (r.withWidth (juce::jmax (3.0f, r.getWidth() * frac)), 3.0f);
    g.setColour (kText.withAlpha (alpha));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (format ? format (value) : juce::String (value), getLocalBounds(), juce::Justification::centred, false);
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

    if (L.mode == LaneMode::FLChannel)
    {
        g.setFont (juce::FontOptions (10.5f));
        g.setColour (kDim);
        g.drawText ("FL channel mode", inner.removeFromTop (inner.getHeight() / 2), juce::Justification::bottomLeft, true);
        g.setFont (juce::FontOptions (12.5f));
        g.setColour (kText);
        g.drawText ("MIDI ch " + juce::String (L.midiChannel) + ", note " + noteLabel (L.midiNote), inner,
                    juce::Justification::topLeft, true);
        return;
    }

    const int count = L.files.size();
    const auto fit = proc.laneFit (lane);
    Features feat;
    const bool analyzed = L.sample != nullptr && proc.fileFeatures (L.sample->file, feat);

    // Right column: fit dot, counter, detected note.
    auto right = inner.removeFromRight (56);
    g.setFont (juce::FontOptions (10.5f));
    g.setColour (kDim);
    if (count > 0)
        g.drawText (juce::String (L.fileIndex + 1) + "/" + juce::String (count), right.removeFromTop (right.getHeight() / 2),
                    juce::Justification::bottomRight, false);
    if (L.sample != nullptr)
    {
        auto dotRow = right;
        auto dot = dotRow.removeFromRight (9).withSizeKeepingCentre (8, 8).toFloat();
        g.setColour (fitColour (fit.score));
        if (fit.score >= 0) g.fillEllipse (dot);
        else                g.drawEllipse (dot.reduced (0.5f), 1.0f);
        if (analyzed && feat.pitchHz > 0.0f)
        {
            g.setColour (kDim);
            g.drawText (noteName (feat.pitchHz), dotRow.withTrimmedRight (3), juce::Justification::centredRight, false);
        }
    }

    g.setColour (kDim);
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

juce::String SampleSlot::getTooltip()
{
    const auto& L = proc.lanes[(size_t) lane];
    if (L.mode == LaneMode::FLChannel)
        return "This lane sends notes to an FL channel instead of playing a sample. Change it in the lane panel below.";
    if (L.sample == nullptr)
        return "Drop a folder (or a sample) here, or use the ... button.";

    const auto fit = proc.laneFit (lane);
    juce::String t = L.sample->file.getFileName() + "\n";
    if (fit.score < 0)
        t << "Still listening to this sample...";
    else
        t << "Fit " << fit.score << "/100: " << fit.reason;

    Features f;
    if (proc.fileFeatures (L.sample->file, f) && f.pitchHz > 0.0f)
        t << "\nPitch: " << noteName (f.pitchHz) << " (" << juce::String (f.pitchHz, 1) << " Hz)";
    t << "\nScroll to dig, click to audition" << (L.lockSample ? ". Sample is locked." : ".");
    return t;
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
    if (onChange) onChange();
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
LaneRow::LaneRow (GrooveboxProcessor& p, const EditMode& mode, const int& selectedLane)
    : proc (p), selected (selectedLane), slot (p), grid (p, mode)
{
    for (auto* b : { &muteBtn, &soloBtn, &prevBtn, &nextBtn, &randBtn, &folderBtn, &genBtn, &mutBtn, &removeBtn })
        addAndMakeVisible (b);
    for (auto* c : { &roleBox, &rateBox })
        addAndMakeVisible (c);
    addAndMakeVisible (lockSampleBtn);
    addAndMakeVisible (lockPatternBtn);
    addAndMakeVisible (lenSlider);
    addAndMakeVisible (slot);
    addAndMakeVisible (grid);

    // Any click inside the row selects the lane for the panel below.
    addMouseListener (this, true);

    muteBtn.setClickingTogglesState (true);
    soloBtn.setClickingTogglesState (true);
    muteBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd35a5a));
    soloBtn.setColour (juce::TextButton::buttonOnColourId, kAccent);
    muteBtn.setTooltip ("Mute lane");
    soloBtn.setTooltip ("Solo lane");
    lockSampleBtn.setTooltip ("Lock this sample: Shuffle, ? and library scans won't change it");
    lockPatternBtn.setTooltip ("Lock this pattern: Generate and Mutate won't change it");
    prevBtn.setTooltip ("Previous sample (or scroll the mouse wheel over the name)");
    nextBtn.setTooltip ("Next sample");
    randBtn.setTooltip ("Random sample, favouring ones that fit this role and genre");
    folderBtn.setTooltip ("Choose a sample folder for this lane");
    genBtn.setTooltip ("Generate a new pattern for this lane only");
    mutBtn.setTooltip ("Mutate this lane's pattern");
    removeBtn.setTooltip ("Remove lane");
    roleBox.setTooltip ("Role: tells the generator and the fit rating what this lane is");
    rateBox.setTooltip ("Step rate. Mix rates across lanes for polyrhythm");
    lenSlider.setTooltip ("Lane length in steps (1 to 64): drag up/down or scroll. Different lengths drift against each other");

    for (int i = 0; i < kNumRoles; ++i)
        roleBox.addItem (roleName ((Role) i), i + 1);
    for (int i = 0; i < (int) rates().size(); ++i)
        rateBox.addItem (rates()[(size_t) i].name, i + 1);

    muteBtn.onClick = [this] { edit ([this] (Lane& l) { l.mute = muteBtn.getToggleState(); }); };
    soloBtn.onClick = [this] { edit ([this] (Lane& l) { l.solo = soloBtn.getToggleState(); }); };
    lockSampleBtn.onClick = [this] { edit ([this] (Lane& l) { l.lockSample = lockSampleBtn.getToggleState(); }); slot.repaint(); };
    lockPatternBtn.onClick = [this] { edit ([this] (Lane& l) { l.lockPattern = lockPatternBtn.getToggleState(); }); refresh(); };
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
        juce::Component::SafePointer<LaneRow> self (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [self] (const juce::FileChooser& fc)
                              {
                                  if (self == nullptr) return;
                                  const auto f = fc.getResult();
                                  auto& pr = self->proc;
                                  if (f.isDirectory())
                                      pr.setLaneFolder (self->lane, f, true);
                                  else if (f.existsAsFile())
                                  {
                                      pr.setLaneFolder (self->lane, f.getParentDirectory(), true);
                                      const int idx = pr.lanes[(size_t) self->lane].files.indexOf (f);
                                      if (idx >= 0) pr.loadSampleIndex (self->lane, idx);
                                  }
                                  self->refresh();
                              });
    };

    removeBtn.onClick = [this]
    {
        juce::Component::SafePointer<LaneRow> self (this);
        juce::MessageManager::callAsync ([self] { if (self != nullptr) self->proc.removeLane (self->lane); });
    };

    // Automatable knobs (attached to parameters in setLane).
    for (auto* k : { &level, &pan, &pitch, &cutoff })
    {
        addAndMakeVisible (k);
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        k->setPopupDisplayEnabled (true, false, nullptr);
        k->setMouseDragSensitivity (160);
    }
    pan.getProperties().set ("bipolar", true);
    pitch.getProperties().set ("bipolar", true);
    const std::pair<juce::Slider*, const char*> autoNames[] = { { &level, "Level" }, { &pan, "Pan" }, { &pitch, "Pitch" }, { &cutoff, "Cutoff" } };
    for (auto [k, nm] : autoNames)
    {
        const juce::String name (nm);
        k->onValueChange = [k, name] { k->setTooltip (name + ": " + k->getTextFromValue (k->getValue()) + " (automatable)"); };
    }

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

    grid.onTouched = [this] { if (onSelect) onSelect (lane); };
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

    levelAtt.reset(); panAtt.reset(); pitchAtt.reset(); cutoffAtt.reset();
    levelAtt  = std::make_unique<SliderAtt> (proc.apvts, GrooveboxProcessor::paramId (li, "level"), level);
    panAtt    = std::make_unique<SliderAtt> (proc.apvts, GrooveboxProcessor::paramId (li, "pan"), pan);
    pitchAtt  = std::make_unique<SliderAtt> (proc.apvts, GrooveboxProcessor::paramId (li, "pitch"), pitch);
    cutoffAtt = std::make_unique<SliderAtt> (proc.apvts, GrooveboxProcessor::paramId (li, "cutoff"), cutoff);
    for (auto* k : { &level, &pan, &pitch, &cutoff })
        if (k->onValueChange) k->onValueChange();

    refresh();
}

void LaneRow::refresh()
{
    const auto& l = proc.lanes[(size_t) lane];
    const auto n = juce::dontSendNotification;

    muteBtn.setToggleState (l.mute, n);
    soloBtn.setToggleState (l.solo, n);
    lockSampleBtn.setToggleState (l.lockSample, n);
    lockPatternBtn.setToggleState (l.lockPattern, n);
    roleBox.setSelectedId ((int) l.role + 1, n);
    rateBox.setSelectedId (l.rate + 1, n);
    lenSlider.setValue (l.length, false);
    lenSlider.accent = roleColour (l.role);
    decay.setValue (l.decay, n);
    swing.setValue (l.swing, n);
    hum.setValue (l.humanize, n);

    genBtn.setEnabled (! l.lockPattern);
    mutBtn.setEnabled (! l.lockPattern);
    const bool sampleMode = l.mode == LaneMode::Sample;
    for (auto* b : { &prevBtn, &nextBtn, &randBtn, &folderBtn })
        b->setEnabled (sampleMode);
    randBtn.setEnabled (sampleMode && ! l.lockSample);

    const auto col = roleColour (l.role);
    for (auto* k : { &level, &pan, &pitch, &cutoff, &decay, &swing, &hum })
        k->setColour (juce::Slider::rotarySliderFillColourId, col);

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

void LaneRow::mouseDown (const juce::MouseEvent&)
{
    if (onSelect) onSelect (lane);
}

void LaneRow::paint (juce::Graphics& g)
{
    g.setColour (kRow);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
    if (selected == lane)
    {
        g.setColour (kAccent.withAlpha (0.55f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
    }
    const auto col = roleColour (proc.lanes[(size_t) lane].role);
    g.setColour (col.withAlpha (0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, meter)));
    g.fillRoundedRectangle (0.0f, 2.0f, 4.0f, (float) getHeight() - 4.0f, 2.0f);
}

std::vector<std::pair<juce::String, juce::Rectangle<int>>> LaneRow::captions() const
{
    return { { "Lvl", level.getBounds() }, { "Pan", pan.getBounds() }, { "Pitch", pitch.getBounds() },
             { "Cut", cutoff.getBounds() }, { "Decay", decay.getBounds() }, { "Swing", swing.getBounds() },
             { "Hum", hum.getBounds() }, { "Role", roleBox.getBounds() }, { "Sample (scroll to dig)", slot.getBounds() },
             { "Steps / rate", lenSlider.getBounds().getUnion (rateBox.getBounds()) } };
}

void LaneRow::resized()
{
    auto r = getLocalBounds().reduced (0, 6).withTrimmedLeft (8).withTrimmedRight (6);
    auto take = [&r] (int w, int gap = 3) { auto a = r.removeFromLeft (w); r.removeFromLeft (gap); return a; };

    muteBtn.setBounds (take (22));
    soloBtn.setBounds (take (22, 6));
    roleBox.setBounds (take (90, 4));
    lockSampleBtn.setBounds (take (18, 2));
    prevBtn.setBounds (take (18, 2));
    slot.setBounds (take (184, 2).expanded (0, 3));
    nextBtn.setBounds (take (18, 3));
    randBtn.setBounds (take (20, 3));
    folderBtn.setBounds (take (24, 8));
    lenSlider.setBounds (take (40, 3));
    rateBox.setBounds (take (62, 6));
    lockPatternBtn.setBounds (take (18, 2));
    genBtn.setBounds (take (34, 2));
    mutBtn.setBounds (take (34, 8));

    for (auto* k : { &level, &pan, &pitch, &cutoff, &decay, &swing, &hum })
        k->setBounds (take (29, 1).expanded (0, 4));

    removeBtn.setBounds (r.removeFromRight (20));
    r.removeFromRight (6);
    r.removeFromLeft (6);
    grid.setBounds (r.expanded (0, 4));
}

//==============================================================================
LaneDetail::LaneDetail (GrooveboxProcessor& p) : proc (p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &title, &fitLabel, &modeBox, &midiChBox, &filterBox, &outBox,
                                                             &chokeBox, &noteBox, &reso, &attack, &reverseBtn })
        addAndMakeVisible (c);

    title.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    fitLabel.setFont (juce::FontOptions (11.5f));
    fitLabel.setColour (juce::Label::textColourId, kDim);
    fitLabel.setMinimumHorizontalScale (0.8f);

    modeBox.addItem ("Plays its sample", 1);
    modeBox.addItem ("Sends notes to an FL channel", 2);
    modeBox.setTooltip ("FL channel mode: this lane's groove plays one of your FL channels (Sampler, Slicex, any synth) via MIDI out");
    for (int c = 1; c <= 16; ++c) midiChBox.addItem ("Ch " + juce::String (c), c);
    midiChBox.setTooltip ("MIDI channel this lane sends on");
    noteBox.format = [] (int v) { return noteLabel (v); };
    noteBox.setTooltip ("Note this lane sends (drag or scroll). C5 is FL's default root note");

    filterBox.addItem ("Filter off", 1);
    filterBox.addItem ("Low-pass", 2);
    filterBox.addItem ("High-pass", 3);
    filterBox.addItem ("Band-pass", 4);
    filterBox.setTooltip ("Lane filter. Cutoff is the Cut knob on the lane (automatable); per-step cutoff darkens individual hits");

    outBox.addItem ("Own output", 1);
    outBox.addItem ("Main", 2);
    for (int o = 2; o <= kNumOuts; ++o) outBox.addItem ("Out " + juce::String (o), o + 1);
    outBox.setTooltip ("Own output = this lane's own mixer track when FL's Auto map outputs is on");

    chokeBox.addItem ("No choke", 1);
    for (int c = 1; c <= 4; ++c) chokeBox.addItem ("Choke group " + juce::String (c), c + 1);
    chokeBox.setTooltip ("Lanes in the same choke group cut each other off (open/closed hats)");

    for (auto* k : { &reso, &attack })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        k->setRange (0.0, 1.0, 0.001);
        k->setPopupDisplayEnabled (true, false, nullptr);
        k->setMouseDragSensitivity (160);
    }
    reso.setDoubleClickReturnValue (true, 0.2);
    attack.setDoubleClickReturnValue (true, 0.0);
    reso.textFromValueFunction = [] (double v) { return "Resonance " + pct (v); };
    attack.textFromValueFunction = [] (double v) { return "Attack " + juce::String (juce::roundToInt (v * v * 200.0)) + " ms"; };
    reso.setTooltip ("Filter resonance");
    attack.setTooltip ("Fade-in at the start of each hit (softens clicks, makes swells)");
    reverseBtn.setClickingTogglesState (true);
    reverseBtn.setTooltip ("Play the sample backwards");

    modeBox.onChange   = [this]
    {
        edit ([this] (Lane& l) { l.mode = modeBox.getSelectedId() == 2 ? LaneMode::FLChannel : LaneMode::Sample; });
        refresh();
        ++proc.modelVersion; // the lane row shows the mode too
    };
    midiChBox.onChange = [this] { edit ([this] (Lane& l) { l.midiChannel = midiChBox.getSelectedId(); }); ++proc.modelVersion; };
    noteBox.onChange   = [this] (int v) { edit ([v] (Lane& l) { l.midiNote = v; }); ++proc.modelVersion; };
    filterBox.onChange = [this] { edit ([this] (Lane& l) { l.filter = (FilterType) (filterBox.getSelectedId() - 1); }); };
    outBox.onChange    = [this] { edit ([this] (Lane& l) { l.out = outBox.getSelectedId() - 2; }); };
    chokeBox.onChange  = [this] { edit ([this] (Lane& l) { l.choke = chokeBox.getSelectedId() - 1; }); };
    reso.onValueChange   = [this] { edit ([this] (Lane& l) { l.reso = (float) reso.getValue(); }); };
    attack.onValueChange = [this] { edit ([this] (Lane& l) { l.attack = (float) attack.getValue(); }); };
    reverseBtn.onClick   = [this] { edit ([this] (Lane& l) { l.reverse = reverseBtn.getToggleState(); }); };
}

template <typename Fn>
void LaneDetail::edit (Fn&& fn)
{
    const juce::SpinLock::ScopedLockType sl (proc.lock);
    fn (proc.lanes[(size_t) lane]);
}

void LaneDetail::setLane (int li)
{
    lane = juce::jlimit (0, kMaxLanes - 1, li);
    refresh();
}

void LaneDetail::refresh()
{
    const auto& l = proc.lanes[(size_t) lane];
    const auto n = juce::dontSendNotification;

    title.setText ("Lane " + juce::String (lane + 1) + ": " + (l.name.isEmpty() ? juce::String (roleName (l.role)) : l.name),
                   n);
    title.setColour (juce::Label::textColourId, roleColour (l.role));

    juce::String info;
    if (l.mode == LaneMode::FLChannel)
        info = "Route: Groovebox wrapper MIDI output port = your channel's MIDI input port";
    else if (l.sample == nullptr)
        info = "No sample yet";
    else
    {
        const auto fit = proc.laneFit (lane);
        info = fit.score < 0 ? juce::String ("Listening to this sample...")
                             : "Fit " + juce::String (fit.score) + ": " + fit.reason;
    }
    fitLabel.setText (info, n);

    const bool flMode = l.mode == LaneMode::FLChannel;
    modeBox.setSelectedId (flMode ? 2 : 1, n);
    midiChBox.setSelectedId (l.midiChannel, n);
    noteBox.setValue (l.midiNote, false);
    filterBox.setSelectedId ((int) l.filter + 1, n);
    outBox.setSelectedId (l.out + 2, n);
    chokeBox.setSelectedId (l.choke + 1, n);
    reso.setValue (l.reso, n);
    attack.setValue (l.attack, n);
    reverseBtn.setToggleState (l.reverse, n);

    midiChBox.setEnabled (flMode);
    noteBox.setEnabled (flMode);
    for (auto* c : std::initializer_list<juce::Component*> { &filterBox, &reso, &attack, &reverseBtn, &outBox, &chokeBox })
        c->setEnabled (! flMode);
    noteBox.repaint();

    for (auto* k : { &reso, &attack })
        k->setColour (juce::Slider::rotarySliderFillColourId, roleColour (l.role));
    repaint();
}

void LaneDetail::paint (juce::Graphics& g)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    g.setColour (kDim);
    g.setFont (juce::FontOptions (10.5f));
    const std::pair<juce::Component*, const char*> caps[] = {
        { &modeBox, "Plays" }, { &midiChBox, "MIDI" }, { &noteBox, "Note" }, { &filterBox, "Filter" },
        { &reso, "Res" }, { &attack, "Attack" }, { &outBox, "Output" }, { &chokeBox, "Choke" }
    };
    for (auto [c, label] : caps)
        g.drawText (label, c->getX() - 4, 4, c->getWidth() + 8, 13, juce::Justification::centredLeft, false);
}

void LaneDetail::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto info = r.removeFromLeft (300);
    title.setBounds (info.removeFromTop (22));
    fitLabel.setBounds (info.removeFromTop (22));
    r.removeFromLeft (10);

    auto controls = r.withTrimmedTop (14);
    auto take = [&controls] (int w, int gap = 8) { auto a = controls.removeFromLeft (w); controls.removeFromLeft (gap); return a.withSizeKeepingCentre (w, 26); };
    modeBox.setBounds (take (210));
    midiChBox.setBounds (take (70, 4));
    noteBox.setBounds (take (52, 16));
    filterBox.setBounds (take (100, 4));
    reso.setBounds (take (34, 2).expanded (0, 4));
    attack.setBounds (take (34, 10).expanded (0, 4));
    reverseBtn.setBounds (take (70, 16));
    outBox.setBounds (take (110, 4));
    chokeBox.setBounds (take (124));
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
    : AudioProcessorEditor (&p), proc (p), dragMidiBtn (p), detail (p)
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
    status.setColour (juce::Label::textColourId, kDim);
    status.setFont (juce::FontOptions (12.0f));
    status.setText ("Drop a folder on a lane, or use Library... to map a whole sample library. Click a lane for its settings; right-click a step grid for more.",
                    juce::dontSendNotification);
    bpmLabel.setColour (juce::Label::textColourId, kDim);
    bpmLabel.setJustificationType (juce::Justification::centredRight);

    proc.editorEverOpened = true;

    for (auto* c : std::initializer_list<juce::Component*> { &title, &byline, &status, &bpmLabel, &setupsBtn, &libraryBtn, &shuffleBtn,
                                                             &fitFilterBtn, &generateBtn, &mutateBtn, &previewBtn, &seqBtn,
                                                             &saveMidiBtn, &dragMidiBtn, &genreBox, &modeBox, &barsBox,
                                                             &density, &variation, &irregular, &mutateAmt, &swingScale,
                                                             &humScale, &master, &viewport, &detail })
        addAndMakeVisible (c);

    generateBtn.setColour (juce::TextButton::buttonColourId, kAccent.withAlpha (0.85f));
    generateBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::black);

    for (int g = 0; g < NumGenres; ++g)
        genreBox.addItem (genreName (g), g + 1);
    genreBox.onChange = [this]
    {
        proc.gen.genre = genreBox.getSelectedId() - 1;
        for (auto& r : rows) r->repaintSlot(); // fit ratings depend on the genre
        detail.refresh();
    };
    genreBox.setTooltip ("Genre for the generator and the fit ratings. Hybrid gives every lane a different genre; Free ignores genre rules");

    const char* modes[] = { "Draw: Steps", "Draw: Velocity", "Draw: Probability", "Draw: Nudge", "Draw: Ratchet",
                            "Draw: Pitch", "Draw: Pan", "Draw: Cutoff", "Draw: Sample start" };
    for (int i = 0; i < 9; ++i)
        modeBox.addItem (modes[i], i + 1);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.setTooltip ("What clicking and dragging on the step grids edits, like FL's graph editor");
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

    for (auto* s : { &swingScale, &humScale })
    {
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, true, 120, 20);
    }
    swingScale.setTooltip ("Scales every lane's swing (automatable)");
    humScale.setTooltip ("Scales every lane's humanize (automatable)");
    swingAtt = std::make_unique<SliderAtt> (proc.apvts, "swing", swingScale);
    humAtt   = std::make_unique<SliderAtt> (proc.apvts, "humanize", humScale);
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
    seqBtn.setTooltip ("Internal sequencer on/off. Turn off to play Groovebox only from MIDI notes (C3 = lane 1 in FL's naming)");
    seqAtt = std::make_unique<ButtonAtt> (proc.apvts, "seq", seqBtn);

    previewBtn.setClickingTogglesState (true);
    previewBtn.setTooltip ("Play the groove without starting FL's transport");
    previewBtn.onClick = [this] { proc.previewPlaying = previewBtn.getToggleState(); };

    fitFilterBtn.setClickingTogglesState (true);
    fitFilterBtn.setToggleState (proc.fitFilter, juce::dontSendNotification);
    fitFilterBtn.setTooltip ("When on, scrolling skips samples rated as a poor fit for the lane's role and genre");
    fitFilterBtn.onClick = [this] { proc.fitFilter = fitFilterBtn.getToggleState(); };

    generateBtn.setTooltip ("New groove for all lanes (pattern-locked lanes are kept)");
    mutateBtn.setTooltip ("Evolve the current groove instead of replacing it (pattern-locked lanes are kept)");
    shuffleBtn.setTooltip ("Pick a new sample in every lane, favouring good fits (sample-locked lanes are kept)");
    libraryBtn.setTooltip ("Pick your sample library root; subfolders are matched to lanes by name (kicks, hats, perc...)");
    dragMidiBtn.setTooltip ("Drag onto FL's playlist, piano roll or a channel to drop the groove as MIDI");
    saveMidiBtn.setTooltip ("Save the groove as a .mid file in Documents/Groovebox/MIDI");

    setupsBtn.setTooltip ("Save this setup, load a saved one, or go back to your last session");
    setupsBtn.onClick = [this] { showSetupsMenu(); };

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
                                                      + ". Groovebox is listening to the samples to rate how well they fit.",
                                                  juce::dontSendNotification);
                              });
    };

    addLaneBtn.onClick = [this] { proc.addLane(); };
    laneList.addAndMakeVisible (addLaneBtn);
    viewport.setViewedComponent (&laneList, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);

    setResizable (true, true);
    setResizeLimits (1150, 560, 2400, 1600);
    setSize (1440, 860);

    syncRows();
    startTimerHz (30);
}

GrooveboxEditor::~GrooveboxEditor()
{
    stopTimer();
    proc.autosave();
    setLookAndFeel (nullptr);
}

void GrooveboxEditor::showSetupsMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Save setup as...");
    m.addItem (2, "Load last session", GrooveboxProcessor::lastSessionFile().existsAsFile());
    m.addSeparator();

    auto saved = GrooveboxProcessor::setupsFolder().findChildFiles (juce::File::findFiles, false, "*.gbx");
    std::sort (saved.begin(), saved.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    if (saved.size() > 40) saved.removeRange (40, saved.size() - 40);

    if (saved.isEmpty())
        m.addItem (-1, "(no saved setups yet)", false);
    for (int i = 0; i < saved.size(); ++i)
        m.addItem (100 + i, saved[i].getFileNameWithoutExtension());

    m.addSeparator();
    m.addItem (3, "Load setup from file...");
    m.addItem (4, "Open setups folder");

    juce::Component::SafePointer<GrooveboxEditor> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setupsBtn), [self, saved] (int r)
    {
        if (self == nullptr || r == 0) return;
        auto& p = self->proc;
        auto say = [self] (const juce::String& t) { if (self != nullptr) self->status.setText (t, juce::dontSendNotification); };

        if (r == 1)
        {
            self->chooser = std::make_unique<juce::FileChooser> ("Save Groovebox setup",
                                                                 GrooveboxProcessor::setupsFolder().getChildFile ("My setup.gbx"), "*.gbx");
            self->chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                        [self, say] (const juce::FileChooser& fc)
                                        {
                                            if (self == nullptr) return;
                                            auto f = fc.getResult();
                                            if (f == juce::File()) return;
                                            f = f.withFileExtension ("gbx");
                                            say (self->proc.saveSetup (f) ? "Saved setup \"" + f.getFileNameWithoutExtension() + "\""
                                                                          : juce::String ("Couldn't save the setup there"));
                                        });
        }
        else if (r == 2)
        {
            say (p.loadSetup (GrooveboxProcessor::lastSessionFile()) ? "Loaded your last session" : "Couldn't read the last session");
        }
        else if (r == 3)
        {
            self->chooser = std::make_unique<juce::FileChooser> ("Load Groovebox setup", GrooveboxProcessor::setupsFolder(), "*.gbx");
            self->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                        [self, say] (const juce::FileChooser& fc)
                                        {
                                            if (self == nullptr) return;
                                            const auto f = fc.getResult();
                                            if (! f.existsAsFile()) return;
                                            say (self->proc.loadSetup (f) ? "Loaded setup \"" + f.getFileNameWithoutExtension() + "\""
                                                                          : juce::String ("That file isn't a Groovebox setup"));
                                        });
        }
        else if (r == 4)
        {
            GrooveboxProcessor::setupsFolder().revealToUser();
        }
        else if (r >= 100 && r - 100 < saved.size())
        {
            const auto f = saved[r - 100];
            say (p.loadSetup (f) ? "Loaded setup \"" + f.getFileNameWithoutExtension() + "\"" : juce::String ("Couldn't read that setup"));
        }
    });
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

void GrooveboxEditor::selectLane (int li)
{
    if (li == selectedLane) return;
    selectedLane = li;
    detail.setLane (li);
    for (auto& r : rows) r->repaint();
}

void GrooveboxEditor::syncRows()
{
    const int nl = proc.numLanes.load();
    while ((int) rows.size() < nl)
    {
        rows.push_back (std::make_unique<LaneRow> (proc, editMode, selectedLane));
        rows.back()->onSelect = [this] (int li) { selectLane (li); };
        laneList.addAndMakeVisible (*rows.back());
    }
    while ((int) rows.size() > nl)
        rows.pop_back();

    for (int i = 0; i < nl; ++i)
        rows[(size_t) i]->setLane (i);

    selectedLane = juce::jlimit (0, nl - 1, selectedLane);
    detail.setLane (selectedLane);

    genreBox.setSelectedId (proc.gen.genre + 1, juce::dontSendNotification);
    barsBox.setSelectedId (proc.exportBars, juce::dontSendNotification);
    fitFilterBtn.setToggleState (proc.fitFilter, juce::dontSendNotification);
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

    if (++tickCount % 15 == 0)
    {
        if (proc.analyzer.version.load() != seenAnalysis)
        {
            seenAnalysis = proc.analyzer.version.load();
            for (auto& r : rows) r->repaintSlot();
            detail.refresh();
        }

        const int pending = proc.analyzer.pending();
        juce::String s = juce::String (proc.lastBpm.load(), 1) + " BPM";
        if (pending > 0) s = "listening to " + juce::String (pending) + " samples  |  " + s;
        if (proc.ioBusy.load() > 0) s = "scanning folders...  |  " + s;
        bpmLabel.setText (s, juce::dontSendNotification);
    }

    if (tickCount % 150 == 0) // every ~5 s: keep the last session safe in case of a crash
        proc.autosave();
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
        for (const auto& [name, b] : rows.front()->captions())
        {
            const bool knob = b.getWidth() < 40;
            g.drawText (name, origin.x + b.getX() - (knob ? 6 : 0), 74, b.getWidth() + (knob ? 12 : 120), 14,
                        knob ? juce::Justification::centred : juce::Justification::left, false);
        }
    }
}

void GrooveboxEditor::resized()
{
    auto r = getLocalBounds();

    auto row1 = r.removeFromTop (38).reduced (10, 6);
    title.setBounds (row1.removeFromLeft (102));
    byline.setBounds (row1.removeFromLeft (52).withTrimmedTop (5));
    setupsBtn.setBounds (row1.removeFromLeft (66)); row1.removeFromLeft (4);
    libraryBtn.setBounds (row1.removeFromLeft (86)); row1.removeFromLeft (4);
    shuffleBtn.setBounds (row1.removeFromLeft (118)); row1.removeFromLeft (4);
    fitFilterBtn.setBounds (row1.removeFromLeft (76)); row1.removeFromLeft (16);
    genreBox.setBounds (row1.removeFromLeft (170)); row1.removeFromLeft (4);
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
    modeBox.setBounds (row2.removeFromLeft (160)); row2.removeFromLeft (16);
    barsBox.setBounds (row2.removeFromLeft (86)); row2.removeFromLeft (4);
    dragMidiBtn.setBounds (row2.removeFromLeft (92)); row2.removeFromLeft (4);
    saveMidiBtn.setBounds (row2.removeFromLeft (84));
    bpmLabel.setBounds (row2.removeFromRight (260));

    r.removeFromTop (18); // captions
    status.setBounds (r.removeFromBottom (26).reduced (10, 2));
    detail.setBounds (r.removeFromBottom (70).reduced (8, 4));
    viewport.setBounds (r.reduced (8, 2));
    layoutLaneList();
    repaint();
}
