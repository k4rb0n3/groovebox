#include "PluginEditor.h"

#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "dev"
#endif

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

juce::String lengthText (int steps)
{
    if (steps > kViewSteps && steps % 16 == 0) return juce::String (steps / 16) + " bars";
    return juce::String (steps);
}

void cellBackground (juce::Graphics& g, juce::Rectangle<float> r, int absStep)
{
    g.setColour (((absStep / 4) % 2 == 0) ? juce::Colour (0xff272a30) : juce::Colour (0xff202227));
    g.fillRoundedRectangle (r, 2.0f);
    if (absStep % 16 == 0 && absStep > 0)
    {
        g.setColour (kDim.withAlpha (0.35f));
        g.fillRect (r.getX() - 1.0f, r.getY(), 1.0f, r.getHeight());
    }
}
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
    setColour (juce::TextEditor::backgroundColourId, kPanel);
    setColour (juce::TextEditor::textColourId, kText);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
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
    const auto [start, count] = viewWindow (proc.lanes[(size_t) lane], view.page);
    if (x < (float) labelW) return -1;
    const float cw = (float) (getWidth() - labelW) / (float) count;
    return start + juce::jlimit (0, count - 1, (int) ((x - (float) labelW) / cw));
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto& L = proc.lanes[(size_t) lane];
    const auto [start, count] = viewWindow (L, view.page);
    const float cw = (float) (getWidth() - labelW) / (float) count;
    const float h = (float) getHeight();
    const auto col = roleColour (L.role);
    const int ps = proc.playStep[(size_t) lane].load();
    const float pad = cw > 7.0f ? 1.0f : 0.5f;

    if (labelW > 0)
    {
        g.setColour (kText);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (label, 0, 0, labelW - 6, (int) h, juce::Justification::centredLeft, false);
    }

    for (int i = 0; i < count; ++i)
    {
        const int idx = start + i;
        auto r = juce::Rectangle<float> ((float) labelW + (float) i * cw, 0.0f, cw, h).reduced (pad, 2.0f);
        cellBackground (g, r, idx);

        const auto& st = L.steps[(size_t) idx];
        if (st.on)
        {
            g.setColour (col.withAlpha (0.35f + 0.65f * st.prob));
            g.fillRoundedRectangle (r.withTop (r.getBottom() - r.getHeight() * (0.2f + 0.8f * st.vel)), 2.0f);

            if (st.ratchet > 1 && cw > 6.0f)
            {
                g.setColour (kText.withAlpha (0.85f));
                for (int k = 0; k < st.ratchet; ++k)
                    g.fillEllipse (r.getX() + 2.0f + (float) k * 3.5f, r.getY() + 2.0f, 2.0f, 2.0f);
            }
            if (std::abs (st.nudge) > 0.01f)
            {
                g.setColour (kText.withAlpha (0.8f));
                g.fillRect (r.getCentreX() + st.nudge * cw - 0.75f, r.getBottom() - 4.0f, 1.5f, 4.0f);
            }
            if ((std::abs (st.pitch) > 0.01f || std::abs (st.pan) > 0.01f || st.cutoff < 0.99f || st.start > 0.01f) && cw > 6.0f)
            {
                g.setColour (kText.withAlpha (0.7f));
                g.fillRect (r.getRight() - 4.0f, r.getY() + 2.0f, 2.0f, 2.0f);
            }
            if (labelW > 0 && cw >= 22.0f)
            {
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (juce::String (juce::roundToInt (st.vel * 100.0f)), r.toNearestInt().withTrimmedBottom (2),
                            juce::Justification::centredBottom, false);
            }
        }

        if (idx == ps)
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
    if (! e.mods.isPopupMenu())
        applyAt (e, false);
}

void StepGrid::applyAt (const juce::MouseEvent& e, bool isDown)
{
    const int idx = stepAt ((float) e.x);
    if (idx < 0) return;

    {
        const juce::SpinLock::ScopedLockType sl (proc.lock);
        auto& st = proc.lanes[(size_t) lane].steps[(size_t) idx];
        if (isDown)
        {
            paintState = ! st.on;
            st.on = paintState;
        }
        else if (idx != lastIdx)
        {
            st.on = paintState;
        }
        if (st.on && st.vel < 0.05f) st.vel = 0.8f;
    }

    lastIdx = idx;
    repaint();
    if (onEdited) onEdited();
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
                case 9: clipboard().valid = true; clipboard().steps = L.steps; clipboard().length = L.length; clipboard().rate = L.rate; break;
                case 10: if (clipboard().valid) { L.steps = clipboard().steps; L.length = clipboard().length; L.rate = clipboard().rate; } break;
                default: break;
            }
        }
        self->repaint();
        if (self->onEdited) self->onEdited();
        ++p.modelVersion; // lengths may have changed
    });
}

//==============================================================================
ParamStrip::ParamStrip (GrooveboxProcessor& p, const ViewState& v, Param prm) : proc (p), view (v), param (prm)
{
    static const char* tips[] = {
        "Velocity: how hard each step hits",
        "Probability: how often each step plays (re-rolled every time around)",
        "Pitch: semitones up or down for each hit",
        "Pan: left or right for each hit",
        "Cutoff: darkens single hits (works with or without the lane filter)",
        "Sample start: skips into the sample for each hit",
        "Nudge: pushes a hit early or late (micro timing)",
        "Ratchet: repeats a hit 2 to 4 times inside its step"
    };
    setTooltip (juce::String (tips[(int) param]) + ". Drag to draw, double-click resets a step, right-click for row tools.");
}

int ParamStrip::stepAt (float x) const
{
    const auto [start, count] = viewWindow (proc.lanes[(size_t) lane], view.page);
    if (x < (float) kLabelW) return -1;
    const float cw = (float) (getWidth() - kLabelW) / (float) count;
    return start + juce::jlimit (0, count - 1, (int) ((x - (float) kLabelW) / cw));
}

float ParamStrip::valueFromY (float y) const
{
    return 1.0f - juce::jlimit (0.0f, 1.0f, (y - 2.0f) / (float) juce::jmax (1, getHeight() - 4));
}

void ParamStrip::setFrom01 (Step& st, float v) const
{
    switch (param)
    {
        case Param::Velocity:    st.vel = juce::jmax (0.02f, v); break;
        case Param::Probability: st.prob = juce::jmax (0.05f, v); break;
        case Param::Pitch:       st.pitch = (float) juce::jlimit (-12, 12, juce::roundToInt ((v - 0.5f) * 24.0f)); break;
        case Param::Pan:         st.pan = juce::jlimit (-1.0f, 1.0f, (v - 0.5f) * 2.0f); if (std::abs (st.pan) < 0.04f) st.pan = 0.0f; break;
        case Param::Cutoff:      st.cutoff = v; break;
        case Param::Start:       st.start = v < 0.03f ? 0.0f : v; break;
        case Param::Nudge:       st.nudge = juce::jlimit (-0.5f, 0.5f, v - 0.5f); if (std::abs (st.nudge) < 0.02f) st.nudge = 0.0f; break;
        case Param::Ratchet:     st.ratchet = 1 + juce::jlimit (0, 3, (int) (v * 4.0f)); break;
        case Param::Count:       break;
    }
}

float ParamStrip::get01 (const Step& st) const
{
    switch (param)
    {
        case Param::Velocity:    return st.vel;
        case Param::Probability: return st.prob;
        case Param::Pitch:       return 0.5f + st.pitch / 24.0f;
        case Param::Pan:         return 0.5f + st.pan * 0.5f;
        case Param::Cutoff:      return st.cutoff;
        case Param::Start:       return st.start;
        case Param::Nudge:       return 0.5f + st.nudge;
        case Param::Ratchet:     return (float) juce::jlimit (1, 4, st.ratchet) / 4.0f;
        case Param::Count:       break;
    }
    return 0.0f;
}

juce::String ParamStrip::text (const Step& st) const
{
    switch (param)
    {
        case Param::Velocity:    return juce::String (juce::roundToInt (st.vel * 100.0f));
        case Param::Probability: return juce::String (juce::roundToInt (st.prob * 100.0f));
        case Param::Pitch:
        {
            const int p = juce::roundToInt (st.pitch);
            return p == 0 ? juce::String ("0") : (p > 0 ? "+" : "") + juce::String (p);
        }
        case Param::Pan:
        {
            const int p = juce::roundToInt (st.pan * 100.0f);
            return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
        }
        case Param::Cutoff:      return juce::String (juce::roundToInt (st.cutoff * 100.0f));
        case Param::Start:       return juce::String (juce::roundToInt (st.start * 100.0f));
        case Param::Nudge:       return juce::String (juce::roundToInt (st.nudge * 100.0f));
        case Param::Ratchet:     return "x" + juce::String (st.ratchet);
        case Param::Count:       break;
    }
    return {};
}

void ParamStrip::paint (juce::Graphics& g)
{
    static const char* names[] = { "Velocity", "Probability", "Pitch", "Pan", "Cutoff", "Sample start", "Nudge", "Ratchet" };
    const auto& L = proc.lanes[(size_t) lane];
    const auto [start, count] = viewWindow (L, view.page);
    const float cw = (float) (getWidth() - kLabelW) / (float) count;
    const float h = (float) getHeight();
    const auto col = roleColour (L.role);
    const float pad = cw > 7.0f ? 1.0f : 0.5f;

    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (names[(int) param], 0, 0, kLabelW - 6, (int) h, juce::Justification::centredLeft, false);

    for (int i = 0; i < count; ++i)
    {
        const int idx = start + i;
        auto r = juce::Rectangle<float> ((float) kLabelW + (float) i * cw, 0.0f, cw, h).reduced (pad, 1.0f);
        cellBackground (g, r, idx);

        const auto& st = L.steps[(size_t) idx];
        if (! st.on) continue;

        const float v = juce::jlimit (0.0f, 1.0f, get01 (st));
        g.setColour (col.withAlpha (0.18f));
        g.fillRoundedRectangle (r, 2.0f);
        if (bipolar())
        {
            const float cy = r.getCentreY();
            const float y = r.getBottom() - v * r.getHeight();
            g.setColour (col);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), juce::jmin (cy, y), r.getRight(), juce::jmax (cy, y) + 1.0f));
        }
        else
        {
            g.setColour (col.withAlpha (0.9f));
            g.fillRoundedRectangle (r.withTop (r.getBottom() - juce::jmax (2.0f, v * r.getHeight())), 2.0f);
        }

        if (cw >= 20.0f && h >= 16.0f)
        {
            g.setColour (kText);
            g.setFont (juce::FontOptions (9.5f));
            g.drawText (text (st), r.toNearestInt(), juce::Justification::centredTop, false);
        }
    }
}

void ParamStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    lastIdx = -1;
    mouseDrag (e);
}

void ParamStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const int idx = stepAt ((float) e.x);
    if (idx < 0) return;
    const float v = valueFromY ((float) e.y);

    {
        const juce::SpinLock::ScopedLockType sl (proc.lock);
        auto& L = proc.lanes[(size_t) lane];
        // Fill in between fast mouse moves so lines and ramps are easy to draw.
        const int from = lastIdx < 0 ? idx : lastIdx;
        const int span = std::abs (idx - from);
        for (int k = 0; k <= span; ++k)
        {
            const int i = from + (idx > from ? k : -k);
            const float t = span == 0 ? 1.0f : (float) k / (float) span;
            auto& st = L.steps[(size_t) i];
            if (st.on)
                setFrom01 (st, lastIdx < 0 ? v : lastV + (v - lastV) * t);
        }
    }

    lastIdx = idx;
    lastV = v;
    repaint();
    if (onEdited) onEdited();
}

void ParamStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int idx = stepAt ((float) e.x);
    if (idx < 0) return;
    {
        const juce::SpinLock::ScopedLockType sl (proc.lock);
        auto& st = proc.lanes[(size_t) lane].steps[(size_t) idx];
        const Step d {};
        switch (param)
        {
            case Param::Velocity:    st.vel = 0.8f; break;
            case Param::Probability: st.prob = d.prob; break;
            case Param::Pitch:       st.pitch = d.pitch; break;
            case Param::Pan:         st.pan = d.pan; break;
            case Param::Cutoff:      st.cutoff = d.cutoff; break;
            case Param::Start:       st.start = d.start; break;
            case Param::Nudge:       st.nudge = d.nudge; break;
            case Param::Ratchet:     st.ratchet = d.ratchet; break;
            case Param::Count:       break;
        }
    }
    repaint();
    if (onEdited) onEdited();
}

void ParamStrip::showMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Reset this row");
    m.addItem (2, "Randomize this row");
    m.addItem (3, "Ramp up across the page");
    m.addItem (4, "Ramp down across the page");
    juce::Component::SafePointer<ParamStrip> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self] (int r)
    {
        if (self == nullptr || r == 0) return;
        auto& rnd = juce::Random::getSystemRandom();
        {
            const juce::SpinLock::ScopedLockType sl (self->proc.lock);
            auto& L = self->proc.lanes[(size_t) self->lane];
            const auto win = viewWindow (L, self->view.page);
            const bool pageOnly = r >= 3;
            const int a = pageOnly ? win.first : 0;
            const int b = pageOnly ? win.first + win.second : juce::jlimit (1, kMaxSteps, L.length);
            for (int i = a; i < b; ++i)
            {
                auto& st = L.steps[(size_t) i];
                if (! st.on) continue;
                const float t = (float) (i - a) / (float) juce::jmax (1, b - a - 1);
                float v;
                if (r == 1)      v = self->param == Param::Velocity ? 0.8f : self->get01 (Step {});
                else if (r == 2) v = self->bipolar() ? 0.5f + (rnd.nextFloat() - 0.5f) * 0.6f : 0.3f + rnd.nextFloat() * 0.7f;
                else if (r == 3) v = 0.15f + 0.85f * t;
                else             v = 1.0f - 0.85f * t;
                self->setFrom01 (st, v);
            }
        }
        self->repaint();
        if (self->onEdited) self->onEdited();
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

void NumberBox::mouseDrag (const juce::MouseEvent& e)
{
    const int target = -e.getDistanceFromDragStartY() / 6;
    int v = dragStart;
    const int dir = target > 0 ? 1 : -1;
    for (int k = 0; k < std::abs (target); ++k)
        v = juce::jlimit (lo, hi, stepFrom (v, dir));
    setValue (v, true);
}

void NumberBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    g.setColour (kCtrl.withAlpha (alpha));
    g.fillRoundedRectangle (r, 3.0f);
    const float frac = (float) (value - lo) / (float) juce::jmax (1, hi - lo);
    g.setColour (accent.withAlpha (0.35f * alpha));
    g.fillRoundedRectangle (r.withWidth (juce::jmax (3.0f, r.getWidth() * juce::jmin (1.0f, std::sqrt (frac)))), 3.0f);
    g.setColour (kText.withAlpha (alpha));
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (format ? format (value) : juce::String (value), getLocalBounds(), juce::Justification::centred, false);
}

void NumberBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    wheelAcc += w.isReversed ? -w.deltaY : w.deltaY;
    if (std::abs (wheelAcc) < 0.1f) return;
    setValue (stepFrom (value, wheelAcc > 0 ? 1 : -1), true);
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
    const auto fit = proc.laneFit (lane);
    Features feat;
    const bool analyzed = L.sample != nullptr && proc.fileFeatures (L.sample->file, feat);

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
        g.drawText (L.folder != juce::File() && proc.ioBusy.load() > 0 ? "scanning folder..." : "drop a folder here",
                    inner, juce::Justification::topLeft, true);
    }
}

juce::String SampleSlot::getTooltip()
{
    const auto& L = proc.lanes[(size_t) lane];
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
        proc.setLaneFolder (lane, f, true);
    else if (f.existsAsFile())
        proc.setLaneFolder (lane, f.getParentDirectory(), true, f);
    repaint();
    if (onChange) onChange();
}

//==============================================================================
LaneRow::LaneRow (GrooveboxProcessor& p, ViewState& v)
    : proc (p), view (v), slot (p), grid (p, v)
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

    // Any click inside the row selects the lane for the editor below.
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
    lenSlider.setTooltip ("Lane length: drag up/down or scroll. Up to 64 steps it moves by 1, beyond that by whole bars (up to 64 bars)");

    lenSlider.format = [] (int val) { return lengthText (val); };
    lenSlider.stepper = [] (int val, int dir)
    {
        if (dir > 0) return val < kViewSteps ? val + 1 : (val / 16 + 1) * 16;
        return val <= kViewSteps ? val - 1 : juce::jmax (kViewSteps, ((val - 1) / 16) * 16);
    };

    for (int i = 0; i < kNumRoles; ++i)
        roleBox.addItem (roleName ((Role) i), i + 1);
    for (int i = 0; i < (int) rates().size(); ++i)
        rateBox.addItem (rates()[(size_t) i].name, i + 1);

    muteBtn.onClick = [this] { edit ([this] (Lane& l) { l.mute = muteBtn.getToggleState(); }); };
    soloBtn.onClick = [this] { edit ([this] (Lane& l) { l.solo = soloBtn.getToggleState(); }); };
    lockSampleBtn.onClick = [this] { edit ([this] (Lane& l) { l.lockSample = lockSampleBtn.getToggleState(); }); refresh(); };
    lockPatternBtn.onClick = [this] { edit ([this] (Lane& l) { l.lockPattern = lockPatternBtn.getToggleState(); }); refresh(); };
    roleBox.onChange = [this]
    {
        edit ([this] (Lane& l) { l.role = (Role) (roleBox.getSelectedId() - 1); });
        refresh();
        if (onEdited) onEdited();
    };
    rateBox.onChange = [this] { edit ([this] (Lane& l) { l.rate = rateBox.getSelectedId() - 1; }); };
    lenSlider.onChange = [this] (int val)
    {
        edit ([val] (Lane& l) { l.length = val; });
        grid.repaint();
        if (onEdited) onEdited();
    };

    prevBtn.onClick = [this] { proc.stepSample (lane, -1); slot.repaint(); };
    nextBtn.onClick = [this] { proc.stepSample (lane, 1); slot.repaint(); };
    randBtn.onClick = [this] { proc.randomSample (lane); slot.repaint(); };
    genBtn.onClick  = [this] { proc.generateLane (lane); };
    mutBtn.onClick  = [this] { proc.mutateOneLane (lane); };
    slot.onChange   = [this] { refresh(); if (onEdited) onEdited(); };
    grid.onEdited   = [this] { if (onEdited) onEdited(); };

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
                                  if (f.isDirectory())
                                      self->proc.setLaneFolder (self->lane, f, true);
                                  else if (f.existsAsFile())
                                      self->proc.setLaneFolder (self->lane, f.getParentDirectory(), true, f);
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

    setupKnob (decay, 0.0, 1.0, 1.0, "Decay", [this] (float val) { edit ([val] (Lane& l) { l.decay = val; }); },
               [] (double val)
               {
                   if (val >= 0.999) return juce::String ("full");
                   return juce::String (juce::roundToInt (5.0 * std::pow (400.0, val))) + " ms";
               });
    setupKnob (swing, 0.0, 0.75, 0.0, "Swing", [this] (float val) { edit ([val] (Lane& l) { l.swing = val; }); },
               [] (double val) { return pct (val); });
    setupKnob (hum, 0.0, 1.0, 0.15, "Humanize", [this] (float val) { edit ([val] (Lane& l) { l.humanize = val; }); },
               [] (double val) { return pct (val); });
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
    lenSlider.repaint();
    decay.setValue (l.decay, n);
    swing.setValue (l.swing, n);
    hum.setValue (l.humanize, n);

    genBtn.setEnabled (! l.lockPattern);
    mutBtn.setEnabled (! l.lockPattern);
    randBtn.setEnabled (! l.lockSample);

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

    const int bar = proc.currentBar.load();
    const bool nowResting = proc.songMode.load() && bar >= 0 && ! proc.laneActiveAtBar (lane, bar);
    if (nowResting != resting)
    {
        resting = nowResting;
        repaint();
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
    if (view.selected == lane)
    {
        g.setColour (kAccent.withAlpha (0.55f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
    }
    const auto col = roleColour (proc.lanes[(size_t) lane].role);
    g.setColour (col.withAlpha (0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, meter)));
    g.fillRoundedRectangle (0.0f, 2.0f, 4.0f, (float) getHeight() - 4.0f, 2.0f);
}

void LaneRow::paintOverChildren (juce::Graphics& g)
{
    if (! resting) return;
    g.setColour (kBg.withAlpha (0.5f));
    g.fillRoundedRectangle (grid.getBounds().toFloat(), 3.0f);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("resting in this section", grid.getBounds(), juce::Justification::centred, false);
}

std::vector<std::pair<juce::String, juce::Rectangle<int>>> LaneRow::captions() const
{
    return { { "Lvl", level.getBounds() }, { "Pan", pan.getBounds() }, { "Pitch", pitch.getBounds() },
             { "Cut", cutoff.getBounds() }, { "Decay", decay.getBounds() }, { "Swing", swing.getBounds() },
             { "Hum", hum.getBounds() }, { "Role", roleBox.getBounds() }, { "Sample (scroll to dig)", slot.getBounds() },
             { "Length / rate", lenSlider.getBounds().getUnion (rateBox.getBounds()) } };
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
    lenSlider.setBounds (take (54, 3));
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
LaneEditor::LaneEditor (GrooveboxProcessor& p, ViewState& v) : proc (p), view (v), steps (p, v)
{
    for (auto* c : std::initializer_list<juce::Component*> { &title, &fitLabel, &hint, &filterBox, &outBox, &chokeBox,
                                                             &reso, &attack, &reverseBtn, &steps })
        addAndMakeVisible (c);

    for (int i = 0; i < (int) Param::Count; ++i)
    {
        strips.push_back (std::make_unique<ParamStrip> (p, v, (Param) i));
        strips.back()->onEdited = [this] { steps.repaint(); if (onEdited) onEdited(); };
        addAndMakeVisible (*strips.back());
    }
    steps.setLabel ("Steps", ParamStrip::kLabelW);
    steps.onEdited = [this] { for (auto& s : strips) s->repaint(); if (onEdited) onEdited(); };

    title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    fitLabel.setFont (juce::FontOptions (11.5f));
    fitLabel.setColour (juce::Label::textColourId, kDim);
    fitLabel.setJustificationType (juce::Justification::topLeft);
    fitLabel.setMinimumHorizontalScale (0.85f);
    hint.setFont (juce::FontOptions (11.0f));
    hint.setColour (juce::Label::textColourId, kDim.withAlpha (0.8f));
    hint.setJustificationType (juce::Justification::topLeft);
    hint.setText ("Drag up and down in the rows to shape each step. Double-click resets a step; right-click a row to reset, randomize or ramp it.",
                  juce::dontSendNotification);

    filterBox.addItem ("Filter off", 1);
    filterBox.addItem ("Low-pass", 2);
    filterBox.addItem ("High-pass", 3);
    filterBox.addItem ("Band-pass", 4);
    filterBox.setTooltip ("Lane filter. Its cutoff is the Cut knob on the lane (automatable); the Cutoff row darkens single hits");

    outBox.addItem ("Own output", 1);
    outBox.addItem ("Main output", 2);
    for (int o = 2; o <= kNumOuts; ++o) outBox.addItem ("Output " + juce::String (o), o + 1);
    outBox.setTooltip ("Which Groovebox output this lane plays on. 'Own output' = its own mixer track after Auto map outputs (see Help)");

    chokeBox.addItem ("No choke", 1);
    for (int c = 1; c <= 4; ++c) chokeBox.addItem ("Choke group " + juce::String (c), c + 1);
    chokeBox.setTooltip ("Lanes in the same choke group cut each other off, like an open hi-hat stopped by the closed one");

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
    reso.textFromValueFunction = [] (double val) { return "Resonance " + pct (val); };
    attack.textFromValueFunction = [] (double val) { return "Attack " + juce::String (juce::roundToInt (val * val * 200.0)) + " ms"; };
    reso.setTooltip ("Filter resonance");
    attack.setTooltip ("Fade-in at the start of each hit (softens clicks, makes swells)");
    reverseBtn.setClickingTogglesState (true);
    reverseBtn.setTooltip ("Play the sample backwards");

    filterBox.onChange = [this] { edit ([this] (Lane& l) { l.filter = (FilterType) (filterBox.getSelectedId() - 1); }); };
    outBox.onChange    = [this] { edit ([this] (Lane& l) { l.out = outBox.getSelectedId() - 2; }); };
    chokeBox.onChange  = [this] { edit ([this] (Lane& l) { l.choke = chokeBox.getSelectedId() - 1; }); };
    reso.onValueChange   = [this] { edit ([this] (Lane& l) { l.reso = (float) reso.getValue(); }); };
    attack.onValueChange = [this] { edit ([this] (Lane& l) { l.attack = (float) attack.getValue(); }); };
    reverseBtn.onClick   = [this] { edit ([this] (Lane& l) { l.reverse = reverseBtn.getToggleState(); }); };
}

template <typename Fn>
void LaneEditor::edit (Fn&& fn)
{
    const juce::SpinLock::ScopedLockType sl (proc.lock);
    fn (proc.lanes[(size_t) lane]);
}

void LaneEditor::setLane (int li)
{
    lane = juce::jlimit (0, kMaxLanes - 1, li);
    steps.setLane (lane);
    for (auto& s : strips) s->setLane (lane);
    refresh();
}

void LaneEditor::refresh()
{
    const auto& l = proc.lanes[(size_t) lane];
    const auto n = juce::dontSendNotification;

    title.setText ("Lane " + juce::String (lane + 1) + ": " + (l.name.isEmpty() ? juce::String (roleName (l.role)) : l.name), n);
    title.setColour (juce::Label::textColourId, roleColour (l.role));

    juce::String info;
    if (l.sample == nullptr)
        info = "No sample yet";
    else
    {
        const auto fit = proc.laneFit (lane);
        info = l.sample->file.getFileNameWithoutExtension() + "\n"
             + (fit.score < 0 ? juce::String ("Listening to this sample...") : "Fit " + juce::String (fit.score) + ": " + fit.reason);
    }
    fitLabel.setText (info, n);

    filterBox.setSelectedId ((int) l.filter + 1, n);
    outBox.setSelectedId (l.out + 2, n);
    chokeBox.setSelectedId (l.choke + 1, n);
    reso.setValue (l.reso, n);
    attack.setValue (l.attack, n);
    reverseBtn.setToggleState (l.reverse, n);

    for (auto* k : { &reso, &attack })
        k->setColour (juce::Slider::rotarySliderFillColourId, roleColour (l.role));

    steps.repaint();
    for (auto& s : strips) s->repaint();
    repaint();
}

void LaneEditor::tick()
{
    const int ps = proc.playStep[(size_t) lane].load();
    if (ps != steps.lastDrawnPlayStep)
    {
        steps.lastDrawnPlayStep = ps;
        steps.repaint();
    }
}

void LaneEditor::paint (juce::Graphics& g)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (10.5f));
    const std::pair<juce::Component*, const char*> caps[] = { { &filterBox, "Filter" }, { &reso, "Res" }, { &attack, "Attack" },
                                                             { &outBox, "Output" }, { &chokeBox, "Choke" } };
    for (auto [c, label] : caps)
        g.drawText (label, c->getX(), c->getY() - 14, c->getWidth() + 30, 13, juce::Justification::centredLeft, false);
}

void LaneEditor::resized()
{
    auto r = getLocalBounds().reduced (10, 8);

    auto left = r.removeFromLeft (292);
    r.removeFromLeft (14);
    title.setBounds (left.removeFromTop (22));
    fitLabel.setBounds (left.removeFromTop (36));
    left.removeFromTop (16);

    auto row1 = left.removeFromTop (26);
    filterBox.setBounds (row1.removeFromLeft (110));
    row1.removeFromLeft (10);
    reso.setBounds (row1.removeFromLeft (30).expanded (0, 3));
    row1.removeFromLeft (14);
    attack.setBounds (row1.removeFromLeft (30).expanded (0, 3));
    row1.removeFromLeft (12);
    reverseBtn.setBounds (row1.removeFromLeft (72));

    left.removeFromTop (18);
    auto row2 = left.removeFromTop (26);
    outBox.setBounds (row2.removeFromLeft (120));
    row2.removeFromLeft (10);
    chokeBox.setBounds (row2.removeFromLeft (120));
    left.removeFromTop (8);
    hint.setBounds (left);

    const int stripCount = (int) strips.size();
    steps.setBounds (r.removeFromTop (40));
    r.removeFromTop (4);
    const int sh = juce::jmax (14, r.getHeight() / juce::jmax (1, stripCount));
    for (auto& s : strips)
        s->setBounds (r.removeFromTop (sh));
}

//==============================================================================
void SongTimeline::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kRow);
    g.fillRoundedRectangle (r, 3.0f);

    if (! proc.songMode.load() || proc.songSections.empty())
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (proc.songMode.load() ? juce::String ("Click Make song to arrange a full track")
                                         : juce::String ("Loop mode: lanes repeat. Turn on Song to arrange a full track (intro, build, main, break, outro)"),
                    getLocalBounds().reduced (8, 0), juce::Justification::centredLeft, true);
        return;
    }

    static const juce::uint32 cols[] = { 0xff5a7bd0, 0xffd0a35a, 0xffd06a5a, 0xff6ab4a0, 0xff8a7bd0 };
    const float total = (float) juce::jmax (1, proc.songBars);
    for (const auto& s : proc.songSections)
    {
        auto b = juce::Rectangle<float> (r.getX() + r.getWidth() * (float) s.startBar / total, r.getY(),
                                         r.getWidth() * (float) s.bars / total, r.getHeight()).reduced (1.0f, 2.0f);
        g.setColour (juce::Colour (cols[juce::jlimit (0, 4, s.type)]).withAlpha (0.55f));
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (kText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (s.name + " " + juce::String (s.bars), b.toNearestInt().reduced (4, 0), juce::Justification::centredLeft, true);
    }

    const int bar = proc.currentBar.load();
    if (bar >= 0 && bar < proc.songBars)
    {
        const float x = r.getX() + r.getWidth() * ((float) bar + 0.5f) / total;
        g.setColour (juce::Colours::white);
        g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
    }
}

//==============================================================================
HelpOverlay::HelpOverlay()
{
    addAndMakeVisible (text);
    addAndMakeVisible (closeBtn);
    closeBtn.onClick = [this] { setVisible (false); };
    text.setMultiLine (true, true);
    text.setReadOnly (true);
    text.setScrollbarsShown (true);
    text.setCaretVisible (false);

    auto h = [this] (const juce::String& s)
    {
        text.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        text.setColour (juce::TextEditor::textColourId, kAccent);
        text.insertTextAtCaret ("\n" + s + "\n");
    };
    auto p = [this] (const juce::String& s)
    {
        text.setFont (juce::FontOptions (13.5f));
        text.setColour (juce::TextEditor::textColourId, kText);
        text.insertTextAtCaret (s + "\n");
    };

    h ("Groovebox in 30 seconds");
    p ("1. Click Library... and pick the folder with your drum samples. Groovebox finds the kick, snare, hat... folders and gives each its own lane.\n"
       "2. Pick a genre and press Generate. Press Play in FL (or Play here).\n"
       "3. Hover a sample name and scroll the mouse wheel to try other samples while it plays.\n"
       "4. Click a lane to edit it in the big editor at the bottom.");

    h ("Each lane");
    p ("M / S: mute / solo.\n"
       "Role: what the lane is (kick, hat...). It guides the generator and the fit rating.\n"
       "Padlocks: the first locks the sample (Shuffle, ? and Library leave it alone), the second locks the pattern (Generate and Mutate leave it alone).\n"
       "< name >: previous / next sample. Scrolling on the name does the same. Click the name to hear it.\n"
       "?: random sample that fits the role. ...: choose a folder.\n"
       "Coloured dot: how well the sample fits this role in this genre. Green good, yellow usable, red poor, hollow still listening. "
       "Hover the name for the reason and the detected note.\n"
       "Length: steps in the loop. Up to 64 steps it moves by 1, beyond that by whole bars, up to 64 bars. Lanes with different lengths drift against each other.\n"
       "Rate: how long a step is (1/16 is normal; T = triplet, D = dotted).\n"
       "Gen / Mut: new pattern / evolve the pattern for this lane only.\n"
       "Lvl, Pan, Pitch, Cut: level, stereo position, tuning, filter cutoff. These four can be automated in FL.\n"
       "Decay: how long the sample rings. Swing: shuffle feel. Hum: humanize (tiny timing and velocity changes).\n"
       "Step grid: click to turn steps on or off, drag to paint. Right-click for clear, shift, reverse, copy and paste.");

    h ("Lane editor (bottom)");
    p ("Shows the selected lane big. Drag up and down in each row to shape every step: Velocity, Probability (how often it plays), "
       "Pitch, Pan, Cutoff, Sample start, Nudge (early or late) and Ratchet (repeats inside the step). Double-click resets a step; "
       "right-click a row to reset, randomize or ramp it.\n"
       "Filter + Res: low-pass, high-pass or band-pass for the whole lane. Attack: fade-in on each hit. Reverse: play backwards.\n"
       "The Lane editor button bottom right hides or shows this panel.");

    h ("Output: one mixer track per lane");
    p ("Output decides which of Groovebox's audio outputs a lane plays on. 'Own output' means lane 1 uses output 1, lane 2 uses output 2, and so on.\n"
       "To get every lane on its own FL mixer track:\n"
       "  1. Click the small plugin options icon at the top left of Groovebox's window (FL's plugin wrapper).\n"
       "  2. Open the Processing tab.\n"
       "  3. Click Auto map outputs.\n"
       "FL then sends each lane to its own mixer track. Until you do this, everything plays through the main output, which is fine too.\n"
       "'Main output' keeps a lane on the main output even when the others have their own tracks. 'Output 2...32' lets lanes share a track.");

    h ("Choke");
    p ("Lanes in the same choke group cut each other off, like a real hi-hat: the open hat stops the moment the closed hat plays. "
       "Closed and open hat lanes are in group 1 by default. 'No choke' lets sounds ring over each other.");

    h ("Generating");
    p ("Genre sets the style and the typical tempo (shown in the BPM box). Hybrid gives every lane a different genre; Free ignores genre rules.\n"
       "Density: how busy. Variation: ghost notes, softer and louder hits, steps that only sometimes play, small timing shifts. "
       "Irregular: odd lengths, triplets and Euclidean patterns.\n"
       "Loop: how many bars Generate writes (1 to 64). Long loops evolve bar by bar, with small fills every 4 and 8 bars. "
       "Long lanes are shown 4 bars at a time: use < > next to the bar numbers, or Follow to track the playhead.\n"
       "Mutate: changes the current groove a little (amount next to it) instead of replacing it.");

    h ("Song mode: a full track");
    p ("Turn on Song, pick a length and press Make song. Groovebox arranges intro, build, main, break and outro sections and decides "
       "which lanes play in each (the kick drops out in the last bar of a build). It follows FL's song position, so press Play from the start "
       "of the song. Lanes that sit out the current section are dimmed. Save MIDI and Drag MIDI export the whole arrangement.");

    h ("Tempo");
    p ("When FL plays, Groovebox follows FL's tempo, so set FL's tempo to suit the genre. The BPM box sets the tempo for Groovebox's own Play button.");

    h ("Setups");
    p ("Setups > Save setup as... stores everything (folders, samples, patterns, knobs). A new Groovebox opens with your last session, "
       "which is saved automatically every few seconds. Setups > Load last session brings it back.");

    h ("MIDI");
    p ("Drag MIDI: drag this button onto FL's playlist or a pattern to drop the groove as notes. Save MIDI writes a .mid file to Documents\\Groovebox\\MIDI.\n"
       "Sequencer off: Groovebox only plays from incoming MIDI (C3 plays lane 1, C#3 lane 2, and so on).");

    text.moveCaretToTop (false);
}

void HelpOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    auto panel = getLocalBounds().reduced (juce::jmax (40, getWidth() / 6), 40).toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (panel, 8.0f);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("Help", panel.toNearestInt().reduced (24, 14).removeFromTop (28), juce::Justification::centredLeft, false);
}

void HelpOverlay::resized()
{
    auto panel = getLocalBounds().reduced (juce::jmax (40, getWidth() / 6), 40).reduced (24, 14);
    auto top = panel.removeFromTop (30);
    closeBtn.setBounds (top.removeFromRight (80));
    panel.removeFromTop (6);
    text.setBounds (panel);
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
    : AudioProcessorEditor (&p), proc (p), dragMidiBtn (p), laneEditor (p, view), timeline (p)
{
    setLookAndFeel (&look);
    proc.editorEverOpened = true;

    title.setText ("GROOVEBOX", juce::dontSendNotification);
    title.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, kAccent);
    title.setBorderSize ({ 0, 0, 0, 0 });
    byline.setText ("by PJH  v" JucePlugin_VersionString, juce::dontSendNotification);
    byline.setFont (juce::FontOptions (11.5f));
    byline.setColour (juce::Label::textColourId, kDim);
    byline.setBorderSize ({ 0, 0, 0, 0 });
    status.setColour (juce::Label::textColourId, kDim);
    status.setFont (juce::FontOptions (12.0f));
    status.setText ("New here? Click Help. Drop a folder on a lane or use Library..., then Generate.", juce::dontSendNotification);
    bpmLabel.setColour (juce::Label::textColourId, kDim);
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    pageLabel.setColour (juce::Label::textColourId, kText);
    pageLabel.setJustificationType (juce::Justification::centred);
    pageLabel.setFont (juce::FontOptions (12.0f));

    for (auto* c : std::initializer_list<juce::Component*> {
             &title, &byline, &status, &bpmLabel, &pageLabel, &helpBtn, &setupsBtn, &libraryBtn, &shuffleBtn, &fitFilterBtn,
             &generateBtn, &mutateBtn, &previewBtn, &seqBtn, &saveMidiBtn, &dragMidiBtn, &genreBox, &barsBox, &loopBox,
             &songLenBox, &songBtn, &makeSongBtn, &pagePrev, &pageNext, &followBtn, &editorBtn, &density, &variation,
             &irregular, &mutateAmt, &swingScale, &humScale, &master, &bpmBox, &viewport, &laneEditor, &timeline })
        addAndMakeVisible (c);
    addChildComponent (help);

    generateBtn.setColour (juce::TextButton::buttonColourId, kAccent.withAlpha (0.85f));
    generateBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    helpBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a4a6a));

    helpBtn.setTooltip ("What everything does");
    helpBtn.onClick = [this] { help.setVisible (true); help.toFront (true); };

    for (int g = 0; g < NumGenres; ++g)
        genreBox.addItem (genreName (g), g + 1);
    genreBox.onChange = [this]
    {
        proc.gen.genre = genreBox.getSelectedId() - 1;
        const double bpm = genreBpm (proc.gen.genre);
        proc.previewBpm = bpm;
        bpmBox.setValue (juce::roundToInt (bpm), false);
        for (auto& r : rows) r->repaintSlot(); // fit ratings depend on the genre
        laneEditor.refresh();
        status.setText (juce::String (genreName (proc.gen.genre)) + ": typical tempo " + juce::String (juce::roundToInt (bpm))
                            + " BPM. Set FL's tempo to match, then Generate.", juce::dontSendNotification);
    };
    genreBox.setTooltip ("Genre for the generator and the fit ratings. Hybrid gives every lane a different genre; Free ignores genre rules");

    bpmBox.format = [] (int v) { return juce::String (v) + " BPM"; };
    bpmBox.setTooltip ("Tempo for Groovebox's own Play button. When FL plays, FL's tempo is used");
    bpmBox.onChange = [this] (int v) { proc.previewBpm = (double) v; };

    for (int b : { 1, 2, 4, 8, 16, 32, 64 })
        loopBox.addItem ("Loop: " + juce::String (b) + (b == 1 ? " bar" : " bars"), b);
    loopBox.setTooltip ("How many bars Generate writes. Long loops evolve bar by bar, with small fills every 4 and 8 bars");
    loopBox.onChange = [this] { proc.gen.bars = loopBox.getSelectedId(); };

    for (int b : { 1, 2, 4, 8, 16, 32, 64 })
        barsBox.addItem (juce::String (b) + (b == 1 ? " bar" : " bars"), b);
    barsBox.setTooltip ("Length of the exported MIDI clip in loop mode (song mode exports the whole song)");
    barsBox.onChange = [this] { proc.exportBars = barsBox.getSelectedId(); };

    for (int b : { 32, 64, 96, 128 })
        songLenBox.addItem (juce::String (b) + " bars", b);
    songLenBox.setTooltip ("Length of the song Make song arranges");
    songBtn.setClickingTogglesState (true);
    songBtn.setTooltip ("Song mode: lanes come in and out following an arrangement, following FL's song position");
    songBtn.onClick = [this]
    {
        proc.songMode = songBtn.getToggleState();
        if (proc.songMode.load() && proc.songSections.empty())
            proc.makeSong (songLenBox.getSelectedId() > 0 ? songLenBox.getSelectedId() : 64);
        timeline.repaint();
        for (auto& r : rows) r->repaint();
    };
    makeSongBtn.setTooltip ("Arrange a full track: intro, build, main, break, outro (turns Song mode on)");
    makeSongBtn.onClick = [this]
    {
        proc.makeSong (songLenBox.getSelectedId() > 0 ? songLenBox.getSelectedId() : 64);
        proc.songMode = true;
        songBtn.setToggleState (true, juce::dontSendNotification);
        timeline.repaint();
        status.setText ("Song arranged: " + juce::String (proc.songBars) + " bars. Press Play in FL from the start of the song.",
                        juce::dontSendNotification);
    };

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
    previewBtn.setTooltip ("Play the groove without starting FL's transport, at the BPM next to it");
    previewBtn.onClick = [this] { proc.previewPlaying = previewBtn.getToggleState(); };

    fitFilterBtn.setClickingTogglesState (true);
    fitFilterBtn.setTooltip ("When on, scrolling skips samples rated as a poor fit for the lane's role and genre");
    fitFilterBtn.onClick = [this] { proc.fitFilter = fitFilterBtn.getToggleState(); };

    pagePrev.setTooltip ("Previous 4 bars of long lanes");
    pageNext.setTooltip ("Next 4 bars of long lanes");
    followBtn.setClickingTogglesState (true);
    followBtn.setToggleState (view.follow, juce::dontSendNotification);
    followBtn.setTooltip ("Follow the playhead through long lanes");
    pagePrev.onClick = [this] { view.follow = false; followBtn.setToggleState (false, juce::dontSendNotification); setPage (view.page - 1); };
    pageNext.onClick = [this] { view.follow = false; followBtn.setToggleState (false, juce::dontSendNotification); setPage (view.page + 1); };
    followBtn.onClick = [this] { view.follow = followBtn.getToggleState(); };

    editorBtn.setClickingTogglesState (true);
    editorBtn.setToggleState (editorExpanded, juce::dontSendNotification);
    editorBtn.setTooltip ("Show or hide the lane editor at the bottom");
    editorBtn.onClick = [this] { editorExpanded = editorBtn.getToggleState(); resized(); };

    generateBtn.setTooltip ("New groove for all lanes (pattern-locked lanes are kept)");
    mutateBtn.setTooltip ("Evolve the current groove instead of replacing it (pattern-locked lanes are kept)");
    shuffleBtn.setTooltip ("Pick a new sample in every lane, favouring good fits (sample-locked lanes are kept)");
    libraryBtn.setTooltip ("Pick your sample library root; subfolders are matched to lanes by name (kicks, hats, perc...)");
    dragMidiBtn.setTooltip ("Drag onto FL's playlist or a pattern to drop the groove (or the whole song) as MIDI");
    saveMidiBtn.setTooltip ("Save the groove (or the whole song) as a .mid file in Documents/Groovebox/MIDI");
    setupsBtn.setTooltip ("Save this setup, load a saved one, or go back to your last session");

    setupsBtn.onClick   = [this] { showSetupsMenu(); };
    generateBtn.onClick = [this] { proc.generateAll(); };
    mutateBtn.onClick   = [this] { proc.mutateAll(); };
    shuffleBtn.onClick  = [this] { proc.shuffleAllSamples(); };
    dragMidiBtn.onClick = [this] { status.setText ("Drag the Drag MIDI button onto FL's playlist or a pattern.", juce::dontSendNotification); };
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
                                  proc.scanLibrary (dir);
                                  status.setText ("Scanning " + dir.getFileName() + " in the background. Lanes fill in as folders are found; "
                                                  "Groovebox then listens to the samples to rate how well they fit.",
                                                  juce::dontSendNotification);
                              });
    };

    laneEditor.onEdited = [this] { repaintGrids(); };

    addLaneBtn.onClick = [this] { proc.addLane(); };
    laneList.addAndMakeVisible (addLaneBtn);
    viewport.setViewedComponent (&laneList, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);

    setResizable (true, true);
    setResizeLimits (1320, 640, 2600, 1800);
    setSize (1460, 960);

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
    if (li == view.selected) return;
    view.selected = li;
    laneEditor.setLane (li);
    for (auto& r : rows) r->repaint();
    updatePageLabel();
}

void GrooveboxEditor::repaintGrids()
{
    for (auto& r : rows) r->repaintGrid();
}

void GrooveboxEditor::setPage (int p)
{
    int maxLen = 1;
    for (int i = 0; i < proc.numLanes; ++i)
        maxLen = juce::jmax (maxLen, proc.lanes[(size_t) i].length);
    const int pages = juce::jmax (1, (maxLen + kViewSteps - 1) / kViewSteps);
    p = ((p % pages) + pages) % pages;
    if (p == view.page) return;
    view.page = p;
    repaintGrids();
    laneEditor.refresh();
    updatePageLabel();
}

void GrooveboxEditor::updatePageLabel()
{
    int maxLen = 1;
    for (int i = 0; i < proc.numLanes; ++i)
        maxLen = juce::jmax (maxLen, proc.lanes[(size_t) i].length);
    const int pages = juce::jmax (1, (maxLen + kViewSteps - 1) / kViewSteps);
    if (view.page >= pages) view.page = 0;
    pageLabel.setText (pages <= 1 ? juce::String ("All steps shown")
                                  : "Bars " + juce::String (view.page * 4 + 1) + "-" + juce::String (view.page * 4 + 4)
                                        + " of " + juce::String (pages * 4),
                       juce::dontSendNotification);
    pagePrev.setEnabled (pages > 1);
    pageNext.setEnabled (pages > 1);
    followBtn.setEnabled (pages > 1);
}

void GrooveboxEditor::syncRows()
{
    const int nl = proc.numLanes.load();
    while ((int) rows.size() < nl)
    {
        rows.push_back (std::make_unique<LaneRow> (proc, view));
        rows.back()->onSelect = [this] (int li) { selectLane (li); };
        rows.back()->onEdited = [this] { laneEditor.refresh(); updatePageLabel(); };
        laneList.addAndMakeVisible (*rows.back());
    }
    while ((int) rows.size() > nl)
        rows.pop_back();

    for (int i = 0; i < nl; ++i)
        rows[(size_t) i]->setLane (i);

    view.selected = juce::jlimit (0, nl - 1, view.selected);
    laneEditor.setLane (view.selected);

    genreBox.setSelectedId (proc.gen.genre + 1, juce::dontSendNotification);
    barsBox.setSelectedId (proc.exportBars, juce::dontSendNotification);
    loopBox.setSelectedId (proc.gen.bars, juce::dontSendNotification);
    const int sl = proc.songBars <= 32 ? 32 : proc.songBars <= 64 ? 64 : proc.songBars <= 96 ? 96 : 128;
    songLenBox.setSelectedId (sl, juce::dontSendNotification);
    songBtn.setToggleState (proc.songMode.load(), juce::dontSendNotification);
    bpmBox.setValue (juce::roundToInt (proc.previewBpm.load()), false);
    fitFilterBtn.setToggleState (proc.fitFilter, juce::dontSendNotification);
    density.setValue (proc.gen.density, juce::dontSendNotification);
    variation.setValue (proc.gen.variation, juce::dontSendNotification);
    irregular.setValue (proc.gen.irregular, juce::dontSendNotification);
    mutateAmt.setValue (proc.mutateAmount, juce::dontSendNotification);
    for (auto* s : { &density, &variation, &irregular, &mutateAmt })
        s->updateText();

    timeline.repaint();
    updatePageLabel();
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

    // Follow the playhead through long lanes (page of the selected lane).
    if (view.follow)
    {
        const int ps = proc.playStep[(size_t) view.selected].load();
        if (ps >= 0 && proc.lanes[(size_t) view.selected].length > kViewSteps)
            setPage (ps / kViewSteps);
    }

    for (auto& r : rows)
        r->tick();
    laneEditor.tick();

    const int bar = proc.currentBar.load();
    if (bar != timeline.lastBar)
    {
        timeline.lastBar = bar;
        timeline.repaint();
    }

    if (++tickCount % 15 == 0)
    {
        if (proc.analyzer.version.load() != seenAnalysis)
        {
            seenAnalysis = proc.analyzer.version.load();
            for (auto& r : rows) r->repaintSlot();
            laneEditor.refresh();
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
    g.fillRect (0, 0, getWidth(), 98);

    // Column captions aligned to the first lane row.
    if (! rows.empty())
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (10.5f));
        const auto origin = viewport.getPosition();
        const int y = viewport.getY() - 16;
        for (const auto& [name, b] : rows.front()->captions())
        {
            const bool knob = b.getWidth() < 40;
            g.drawText (name, origin.x + b.getX() - (knob ? 6 : 0), y, b.getWidth() + (knob ? 12 : 120), 14,
                        knob ? juce::Justification::centred : juce::Justification::left, false);
        }
    }
}

void GrooveboxEditor::resized()
{
    auto r = getLocalBounds();
    help.setBounds (r);

    auto row1 = r.removeFromTop (38).reduced (10, 6);
    title.setBounds (row1.removeFromLeft (102));
    byline.setBounds (row1.removeFromLeft (92).withTrimmedTop (5));
    helpBtn.setBounds (row1.removeFromLeft (52)); row1.removeFromLeft (4);
    setupsBtn.setBounds (row1.removeFromLeft (66)); row1.removeFromLeft (4);
    libraryBtn.setBounds (row1.removeFromLeft (80)); row1.removeFromLeft (4);
    shuffleBtn.setBounds (row1.removeFromLeft (112)); row1.removeFromLeft (4);
    fitFilterBtn.setBounds (row1.removeFromLeft (72)); row1.removeFromLeft (14);
    genreBox.setBounds (row1.removeFromLeft (170)); row1.removeFromLeft (4);
    generateBtn.setBounds (row1.removeFromLeft (84)); row1.removeFromLeft (4);
    mutateBtn.setBounds (row1.removeFromLeft (64)); row1.removeFromLeft (4);
    mutateAmt.setBounds (row1.removeFromLeft (110));
    master.setBounds (row1.removeFromRight (30)); row1.removeFromRight (8);
    seqBtn.setBounds (row1.removeFromRight (84)); row1.removeFromRight (4);
    previewBtn.setBounds (row1.removeFromRight (52)); row1.removeFromRight (4);
    bpmBox.setBounds (row1.removeFromRight (74));

    auto row2 = r.removeFromTop (30).reduced (10, 3);
    for (auto* s : { &density, &variation, &irregular })
    {
        s->setBounds (row2.removeFromLeft (116));
        row2.removeFromLeft (4);
    }
    row2.removeFromLeft (8);
    swingScale.setBounds (row2.removeFromLeft (120)); row2.removeFromLeft (4);
    humScale.setBounds (row2.removeFromLeft (140)); row2.removeFromLeft (12);
    loopBox.setBounds (row2.removeFromLeft (112)); row2.removeFromLeft (12);
    songBtn.setBounds (row2.removeFromLeft (56)); row2.removeFromLeft (4);
    songLenBox.setBounds (row2.removeFromLeft (88)); row2.removeFromLeft (4);
    makeSongBtn.setBounds (row2.removeFromLeft (84)); row2.removeFromLeft (12);
    barsBox.setBounds (row2.removeFromLeft (80)); row2.removeFromLeft (4);
    dragMidiBtn.setBounds (row2.removeFromLeft (84)); row2.removeFromLeft (4);
    saveMidiBtn.setBounds (row2.removeFromLeft (78));
    bpmLabel.setBounds (row2);

    auto row3 = r.removeFromTop (30).reduced (10, 4);
    pagePrev.setBounds (row3.removeFromLeft (24)); row3.removeFromLeft (2);
    pageLabel.setBounds (row3.removeFromLeft (140)); row3.removeFromLeft (2);
    pageNext.setBounds (row3.removeFromLeft (24)); row3.removeFromLeft (4);
    followBtn.setBounds (row3.removeFromLeft (60)); row3.removeFromLeft (12);
    timeline.setBounds (row3);

    r.removeFromTop (18); // column captions

    auto statusRow = r.removeFromBottom (26).reduced (10, 2);
    editorBtn.setBounds (statusRow.removeFromRight (96).reduced (0, 1));
    statusRow.removeFromRight (8);
    status.setBounds (statusRow);

    laneEditor.setVisible (editorExpanded);
    if (editorExpanded)
        laneEditor.setBounds (r.removeFromBottom (300).reduced (8, 4));

    viewport.setBounds (r.reduced (8, 2));
    layoutLaneList();
    repaint();
}
