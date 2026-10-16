#include "PluginEditor.h"
#include "GrooveAnalysis.h"

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

void StepGrid::playStepChanged (int ps)
{
    const int old = lastDrawnPlayStep;
    lastDrawnPlayStep = ps;
    const auto& L = proc.lanes[(size_t) lane];
    if (L.isBed() || L.follow >= 0) return; // no playhead drawn
    const auto [start, count] = viewWindow (L, view.page);
    const float cw = (float) (getWidth() - labelW) / (float) juce::jmax (1, count);
    for (int s : { old, ps })
        if (s >= start && s < start + count)
            repaint (juce::Rectangle<float> ((float) labelW + (float) (s - start) * cw, 0.0f, cw, (float) getHeight())
                         .getSmallestIntegerContainer().expanded (2, 0));
}

void StepGrid::paint (juce::Graphics& g)
{
    g.fillAll (background);
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

    // Beds and followers don't use steps: show what they do instead.
    const int nlNow = proc.numLanes.load();
    if (L.isBed() || (L.follow >= 0 && L.follow < nlNow))
    {
        auto area = juce::Rectangle<float> ((float) labelW, 0.0f, (float) (getWidth() - labelW), h).reduced (1.0f, 2.0f);
        g.setColour (juce::Colour (0xff25282d));
        g.fillRoundedRectangle (area, 3.0f);
        g.setFont (juce::FontOptions (juce::jmin (12.0f, h * 0.45f)));
        if (L.isBed())
        {
            juce::Path wave;
            for (int x = 0; x <= (int) area.getWidth(); x += 3)
            {
                const float y = area.getCentreY() + std::sin ((float) x * 0.045f) * area.getHeight() * 0.18f
                                                  + std::sin ((float) x * 0.13f) * area.getHeight() * 0.06f;
                if (x == 0) wave.startNewSubPath (area.getX(), y); else wave.lineTo (area.getX() + (float) x, y);
            }
            g.setColour (col.withAlpha (0.45f));
            g.strokePath (wave, juce::PathStrokeType (1.5f));
            g.setColour (kText.withAlpha (0.85f));
            g.drawText ("Bed: runs underneath", area.reduced (8.0f, 0.0f).toNearestInt(),
                        juce::Justification::centredRight, false);
            return;
        }

        const auto& leader = proc.lanes[(size_t) L.follow];
        const auto [ls, lc] = viewWindow (leader, view.page);
        const float lw = area.getWidth() / (float) lc;
        const double delaySteps = (double) L.followDelay / 1000.0 * proc.lastBpm.load() / 60.0 / leader.stepBeats();
        for (int i = 0; i < lc; ++i)
        {
            const auto& st = leader.steps[(size_t) (ls + i)];
            if (! st.on) continue;
            const float x = area.getX() + (float) i * lw;
            g.setColour (roleColour (leader.role).withAlpha (0.25f));
            g.fillRect (x + 1.0f, area.getBottom() - area.getHeight() * 0.35f, juce::jmax (1.5f, lw - 2.0f), area.getHeight() * 0.35f);
            const float fx = x + (float) (delaySteps * lw);
            g.setColour (col.withAlpha (0.25f + 0.75f * L.followChance));
            g.fillEllipse (fx - 2.5f + lw * 0.5f, area.getY() + area.getHeight() * 0.3f, 5.0f, 5.0f);
        }
        g.setColour (kText.withAlpha (0.85f));
        g.drawText ("after " + (leader.name.isNotEmpty() ? leader.name : juce::String (roleName (leader.role))) + ": "
                        + juce::String (juce::roundToInt (L.followChance * 100.0f)) + "%, +" + juce::String (juce::roundToInt (L.followDelay)) + " ms",
                    area.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centredRight, false);
        return;
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
    {
        const auto& L = proc.lanes[(size_t) lane];
        if (L.isBed() || L.follow >= 0) return; // no steps to edit
    }
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    applyAt (e, true);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (proc.lanes[(size_t) lane].isBed() || proc.lanes[(size_t) lane].follow >= 0) return;
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

    auto right = inner.removeFromRight (86);
    g.setFont (juce::FontOptions (10.5f));
    g.setColour (kDim);
    auto topRight = right.removeFromTop (right.getHeight() / 2);
    if (count > 0)
    {
        const auto countText = juce::String (L.fileIndex + 1) + "/" + juce::String (count);
        g.drawText (countText, topRight, juce::Justification::bottomRight, false);
        topRight.removeFromRight (juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), countText) + 6);
    }
    if (L.sample != nullptr)
    {
        // Sample length, so loops stand out: amber when it's long for the instrument, red when it looks like a loop.
        const double secs = analyzed ? (double) feat.durSec
                                     : (double) L.sample->buffer.getNumSamples() / juce::jmax (1.0, L.sample->sampleRate);
        const bool loopName = nameLooksLikeLoop (L.sample->file);
        const bool oneShotLane = wantsOneShots (L.role);
        const bool tooLong = oneShotLane && secs > (double) maxOneShotSeconds (L.role) * 1.5;
        juce::String len = secs < 1.0 ? juce::String (juce::roundToInt (secs * 1000.0)) + " ms" : juce::String (secs, secs < 10.0 ? 1 : 0) + " s";
        if (oneShotLane && loopName) len = "loop " + len;
        g.setColour (oneShotLane && loopName ? juce::Colour (0xffe06a5a) : tooLong ? juce::Colour (0xffe0b04a) : kDim);
        g.drawText (len, topRight, juce::Justification::bottomRight, false);
    }
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
    if (! e.mouseWasClicked()) return;
    if (! e.mods.isPopupMenu())
    {
        proc.audition (lane);
        return;
    }

    const auto& L = proc.lanes[(size_t) lane];
    const auto file = L.sample != nullptr ? L.sample->file : juce::File();
    const auto folder = L.folder;
    juce::PopupMenu m;
    m.addItem (1, "Open in Explorer", file.existsAsFile() || folder.isDirectory());
    m.addItem (2, "Copy file path", file.existsAsFile());
    m.addSeparator();
    m.addItem (3, "Audition");
    juce::Component::SafePointer<SampleSlot> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self, file, folder] (int r)
    {
        if (self == nullptr) return;
        if (r == 1) { if (file.existsAsFile()) file.revealToUser(); else folder.revealToUser(); }
        if (r == 2) juce::SystemClipboard::copyTextToClipboard (file.getFullPathName());
        if (r == 3) self->proc.audition (self->lane);
    });
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
    for (auto* b : { &muteBtn, &soloBtn, &prevBtn, &nextBtn, &randBtn, &folderBtn, &genBtn, &mutBtn, &removeBtn, &dupBtn })
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
    lockSampleBtn.setTooltip ("Lock this lane: Shuffle, ?, library scans, Generate and Mutate leave its sample and pattern alone. "
                              "Right-click to lock only the sample or only the pattern");
    lockPatternBtn.setTooltip ("Lock this lane: Shuffle, ?, library scans, Generate and Mutate leave its sample and pattern alone. "
                               "Right-click to lock only the sample or only the pattern");
    prevBtn.setTooltip ("Previous sample (or scroll the mouse wheel over the name)");
    nextBtn.setTooltip ("Next sample");
    randBtn.setTooltip ("Random sample, favouring ones that fit this role and genre");
    folderBtn.setTooltip ("Choose a sample folder for this lane");
    genBtn.setTooltip ("Generate a new pattern for this lane only");
    mutBtn.setTooltip ("Mutate this lane's pattern");
    removeBtn.setTooltip ("Remove lane");
    dupBtn.setTooltip ("Duplicate lane: a copy with the same sample, pattern and settings right below (Ctrl+D)");
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
    // One click locks the whole lane (sample and pattern); right-click for just one of them.
    auto toggleBoth = [this]
    {
        edit ([] (Lane& l) { const bool on = ! (l.lockSample && l.lockPattern); l.lockSample = on; l.lockPattern = on; });
        refresh();
    };
    auto lockMenu = [this]
    {
        const auto& l = proc.lanes[(size_t) lane];
        juce::PopupMenu m;
        m.addItem (1, "Lock sample and pattern", true, l.lockSample && l.lockPattern);
        m.addItem (2, "Lock only the sample", true, l.lockSample && ! l.lockPattern);
        m.addItem (3, "Lock only the pattern", true, l.lockPattern && ! l.lockSample);
        m.addItem (4, "Unlock", l.lockSample || l.lockPattern);
        juce::Component::SafePointer<LaneRow> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [self] (int r)
        {
            if (self == nullptr || r == 0) return;
            self->edit ([r] (Lane& ln) { ln.lockSample = r == 1 || r == 2; ln.lockPattern = r == 1 || r == 3; });
            self->refresh();
        });
    };
    lockSampleBtn.onClick = toggleBoth;
    lockPatternBtn.onClick = toggleBoth;
    lockSampleBtn.onRightClick = lockMenu;
    lockPatternBtn.onRightClick = lockMenu;
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

    dupBtn.onClick = [this]
    {
        juce::Component::SafePointer<LaneRow> self (this);
        juce::MessageManager::callAsync ([self] { if (self != nullptr && self->onDuplicate) self->onDuplicate (self->lane); });
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
        grid.playStepChanged (ps);

    const int bar = proc.displayBar();
    const bool nowResting = ! proc.pianoRollMode.load() && proc.songMode.load() && bar >= 0 && ! proc.laneActiveAtBar (lane, bar);
    const bool nowPiano = proc.pianoRollMode.load();
    if (nowResting != resting || nowPiano != pianoRoll)
    {
        resting = nowResting;
        pianoRoll = nowPiano;
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
    if (pianoRoll)
    {
        // The pattern is now played from FL's piano roll; show which note row this lane is.
        g.setColour (kBg.withAlpha (0.3f));
        g.fillRoundedRectangle (grid.getBounds().toFloat(), 3.0f);
        auto badge = grid.getBounds().removeFromRight (120).reduced (4, 8);
        g.setColour (kPanel.withAlpha (0.92f));
        g.fillRoundedRectangle (badge.toFloat(), 3.0f);
        g.setColour (kAccent);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ("piano roll: " + juce::MidiMessage::getMidiNoteName (kBaseNote + lane, true, true, 5), badge,
                    juce::Justification::centred, false);
        return;
    }
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
    r.removeFromRight (2);
    dupBtn.setBounds (r.removeFromRight (20));
    r.removeFromRight (6);
    r.removeFromLeft (6);
    grid.setBounds (r.expanded (0, 4));
}

//==============================================================================
LaneEditor::LaneEditor (GrooveboxProcessor& p, ViewState& v) : proc (p), view (v), steps (p, v)
{
    for (auto* c : std::initializer_list<juce::Component*> { &title, &fitLabel, &hint, &filterBox, &outBox, &chokeBox,
                                                             &reso, &attack, &reverseBtn, &steps, &mixerHelpBtn,
                                                             &sampleDrag, &notesDrag, &allDrag, &separateBtn, &dupLaneBtn,
                                                             &playsBox, &fChance, &fDelay, &fDrift, &fVel, &modeInfo })
        addAndMakeVisible (c);

    playsBox.setTooltip ("How this lane plays: on its own steps, right after another lane's hits (a follower: foley that "
                         "completes another sound, off the grid), or as a bed running underneath (crackle, rain, room tone)");
    playsBox.onChange = [this]
    {
        const int id = playsBox.getSelectedId();
        edit ([id, this] (Lane& l)
        {
            if (id == 1) { l.follow = -1; if (l.role == Role::Bed) l.role = Role::Foley; }
            else if (id == 2) { l.follow = -1; l.role = Role::Bed; }
            else if (id >= 100 && id - 100 != lane) { l.follow = id - 100; if (l.role == Role::Bed) l.role = Role::Foley; }
        });
        refresh();
        if (onEdited) onEdited();
    };
    auto bar = [this] (juce::Slider& s, double lo, double hi, double def, std::function<juce::String (double)> text,
                       std::function<void (Lane&, float)> set, const char* tip)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setTextBoxStyle (juce::Slider::TextBoxLeft, true, 160, 20);
        s.setRange (lo, hi, 0.0);
        s.textFromValueFunction = text;
        s.setDoubleClickReturnValue (true, def);
        s.setTooltip (tip);
        s.onValueChange = [this, &s, set]
        {
            const float v = (float) s.getValue();
            edit ([&] (Lane& l) { set (l, v); });
            steps.repaint();
            if (onEdited) onEdited();
        };
    };
    bar (fChance, 0.0, 1.0, 0.35, [] (double v) { return "Answers " + juce::String (juce::roundToInt (v * 100.0)) + "% of the hits"; },
         [] (Lane& l, float v) { l.followChance = v; }, "How often this lane answers its leader's hits");
    bar (fDelay, 5.0, 400.0, 40.0, [] (double v) { return "Comes " + juce::String (juce::roundToInt (v)) + " ms after"; },
         [] (Lane& l, float v) { l.followDelay = v; }, "How long after the leader's hit it plays. 10 to 60 ms glues it on, 100+ ms becomes an echo-like answer");
    fDelay.setSkewFactorFromMidPoint (60.0);
    bar (fDrift, 0.0, 1.0, 0.35, [] (double v) { return "Drift " + juce::String (juce::roundToInt (v * 100.0)) + "%"; },
         [] (Lane& l, float v) { l.followDrift = v; }, "Random spread of the timing, sample start, pitch and pan, so it never repeats exactly");
    bar (fVel, 0.0, 1.0, 0.5, [] (double v) { return "Level " + juce::String (juce::roundToInt (v * 100.0)) + "% of the leader"; },
         [] (Lane& l, float v) { l.followVel = v; }, "How loud it is compared to the hit it follows");
    modeInfo.setFont (juce::FontOptions (12.0f));
    modeInfo.setColour (juce::Label::textColourId, kDim);
    modeInfo.setJustificationType (juce::Justification::topLeft);

    for (int i = 0; i < (int) Param::Count; ++i)
    {
        strips.push_back (std::make_unique<ParamStrip> (p, v, (Param) i));
        strips.back()->onEdited = [this] { steps.repaint(); if (onEdited) onEdited(); };
        addAndMakeVisible (*strips.back());
    }
    steps.setLabel ("Steps", ParamStrip::kLabelW);
    steps.background = kPanel;
    steps.onEdited = [this] { for (auto& s : strips) s->repaint(); if (onEdited) onEdited(); };

    title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    fitLabel.setFont (juce::FontOptions (11.5f));
    fitLabel.setColour (juce::Label::textColourId, kDim);
    fitLabel.setJustificationType (juce::Justification::topLeft);
    fitLabel.setMinimumHorizontalScale (0.85f);
    hint.setFont (juce::FontOptions (11.0f));
    hint.setColour (juce::Label::textColourId, kDim.withAlpha (0.8f));
    hint.setJustificationType (juce::Justification::topLeft);
    hint.setText ("Into FL: drag the sample onto the Channel Rack (FL makes a new channel), then drag the notes into that channel's piano roll.",
                  juce::dontSendNotification);

    sampleDrag.setTooltip ("Drag onto FL's Channel Rack: FL makes a new channel with this lane's sample, with its pitch, filter, "
                           "decay, attack, reverse, level and pan baked in");
    notesDrag.setTooltip ("Drag into the piano roll of the channel you just made: this lane's pattern as notes, with velocity, "
                          "step pitch, timing and groove. Length = your loop (or the whole song in Song mode)");
    allDrag.setTooltip ("Drag onto FL's Channel Rack: one new channel per lane (muted lanes are skipped). Then drag each lane's notes into its channel");
    sampleDrag.makeFiles = [this]
    {
        const auto f = proc.bakeLaneSample (lane);
        if (! f.existsAsFile())
        {
            if (onStatus) onStatus ("This lane has no sample yet");
            return juce::StringArray();
        }
        return juce::StringArray (f.getFullPathName());
    };
    sampleDrag.onDragged = [this] (const juce::StringArray&)
    {
        if (onStatus) onStatus ("Drop it on FL's Channel Rack. Next: open the new channel's piano roll and drag \"Notes > Piano roll\" into it.");
    };
    notesDrag.makeFiles = [this]
    {
        const auto f = proc.exportLaneMidi (lane);
        return f.existsAsFile() ? juce::StringArray (f.getFullPathName()) : juce::StringArray();
    };
    notesDrag.onDragged = [this] (const juce::StringArray&)
    {
        if (onStatus) onStatus ("Drop the notes in the piano roll of this lane's FL channel (the notes sit on C5, the sample's own pitch).");
    };
    allDrag.makeFiles = [this]
    {
        juce::StringArray out;
        for (const auto& f : proc.bakeAllLaneSamples())
            out.add (f.getFullPathName());
        if (out.isEmpty() && onStatus) onStatus ("No lanes with samples to send");
        return out;
    };
    allDrag.onDragged = [this] (const juce::StringArray& f)
    {
        if (onStatus) onStatus ("Drop on FL's Channel Rack: " + juce::String (f.size()) + " new channels. Then click each lane here and drag its notes into its channel's piano roll.");
    };

    separateBtn.setClickingTogglesState (true);
    separateBtn.setTooltip ("Off: all lanes play on Groovebox's one mixer track (safe in any project). "
                            "On: each lane plays on its own output; then click Auto map outputs in FL once (click How?)");
    separateBtn.onClick = [this]
    {
        proc.separateOuts = separateBtn.getToggleState();
        if (onStatus) onStatus (proc.separateOuts.load() ? "Separate mixer tracks on. In FL: plugin options (top left) > Processing > Auto map outputs, once."
                                                         : "All lanes on Groovebox's main mixer track again.");
        refresh();
    };
    dupLaneBtn.setTooltip ("Copy this lane with its sample, pattern and every setting, right below it (Ctrl+D)");
    dupLaneBtn.onClick = [this] { if (onDuplicate) onDuplicate (lane); };
    mixerHelpBtn.setTooltip ("How to give every lane its own FL mixer track");
    mixerHelpBtn.onClick = [this] { showMixerHelp(); };

    filterBox.addItem ("Filter off", 1);
    filterBox.addItem ("Low-pass", 2);
    filterBox.addItem ("High-pass", 3);
    filterBox.addItem ("Band-pass", 4);
    filterBox.setTooltip ("Lane filter. Its cutoff is the Cut knob on the lane (automatable); the Cutoff row darkens single hits");

    outBox.setTooltip ("Which mixer track this lane plays on while it plays inside Groovebox. Needs Auto map outputs once (click How?)");

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
    outBox.onChange    = [this]
    {
        edit ([this] (Lane& l) { l.out = outBox.getSelectedId() - 2; });
        if (! proc.separateOuts.load())
        {
            proc.separateOuts = true; // picking a mixer track only means something with separate outputs on
            separateBtn.setToggleState (true, juce::dontSendNotification);
        }
        if (onStatus) onStatus ("Separate mixer tracks is on. In FL: plugin options (top left) > Processing > Auto map outputs, "
                                "and make sure each output's circle is lit. Click How? for the steps.");
    };
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

void LaneEditor::showMixerHelp()
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Each lane on its own mixer track",
        "While lanes play inside Groovebox:\n"
        "0. Turn on Separate mixer tracks here (off by default, so Groovebox never disturbs an existing project's mixer).\n"
        "1. Click the small gear/arrow at the top left of Groovebox's window (the FL plugin wrapper) and open the Processing tab.\n"
        "2. Click \"Auto map outputs\". FL gives each Groovebox output its own mixer track. In the list of outputs there, make sure the circle in front of each one is lit (click it if not).\n"
        "3. Here, Mixer track \"Own\" means lane 1 goes to the first of those tracks, lane 2 to the second, and so on. "
        "Pick another track to send several lanes to the same one.\n\n"
        "Lanes you sent to FL as channels (Sample > Channel Rack): set the mixer track in FL, in the small number box "
        "on the channel in the Channel Rack (or the channel's settings).");
}

void LaneEditor::rebuildPlaysBox()
{
    const auto& l = proc.lanes[(size_t) lane];
    playsBox.clear (juce::dontSendNotification);
    playsBox.addItem ("Plays its own steps", 1);
    playsBox.addItem ("Bed: runs underneath everything", 2);
    playsBox.addSeparator();
    const int nl = proc.numLanes.load();
    for (int j = 0; j < nl; ++j)
    {
        const auto& o = proc.lanes[(size_t) j];
        if (j == lane || o.follow >= 0 || o.isBed()) continue; // follow lanes that play their own steps
        playsBox.addItem ("Right after lane " + juce::String (j + 1) + ": " + (o.name.isNotEmpty() ? o.name : juce::String (roleName (o.role))), 100 + j);
    }
    playsBox.setSelectedId (l.isBed() ? 2 : (l.follow >= 0 ? 100 + l.follow : 1), juce::dontSendNotification);
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
    outBox.clear (n);
    outBox.addItem ("Own: output " + juce::String (lane + 1), 1);
    outBox.addItem ("Main output (1)", 2);
    for (int o = 2; o <= kNumOuts; ++o)
    {
        juce::String users;
        for (int j = 0; j < proc.numLanes.load(); ++j)
            if (j != lane && (proc.lanes[(size_t) j].out < 0 ? j : proc.lanes[(size_t) j].out) == o - 1)
                users << (users.isEmpty() ? " (with " : ", ") << roleName (proc.lanes[(size_t) j].role);
        if (users.isNotEmpty()) users << ")";
        outBox.addItem ("Output " + juce::String (o) + users, o + 1);
    }
    outBox.setSelectedId (l.out + 2, n);
    separateBtn.setToggleState (proc.separateOuts.load(), n);
    sampleDrag.setEnabled (l.sample != nullptr);

    rebuildPlaysBox();
    fChance.setValue (l.followChance, n);
    fDelay.setValue (l.followDelay, n);
    fDrift.setValue (l.followDrift, n);
    fVel.setValue (l.followVel, n);
    for (auto* s : { &fChance, &fDelay, &fDrift, &fVel }) s->updateText();
    const int mode = l.isBed() ? 2 : (l.follow >= 0 ? 1 : 0);
    modeInfo.setText (mode == 2 ? "This lane is a bed: its sample loops underneath everything (with a soft crossfade) while the music plays, "
                                  "and fades in and out with song sections. Use the lane's Lvl, Pan, Pitch and Cut knobs and the filter to sit it "
                                  "in the mix. Good for vinyl crackle, rain, room tone, hiss, field recordings."
                                : "A follower plays right after its leader's hits, off the grid, like a small sound completing another one. "
                                  "Shown above: the leader's hits (bars) and where this lane answers (dots).",
                      juce::dontSendNotification);
    if (mode != shownMode) { shownMode = mode; resized(); }
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
        steps.playStepChanged (ps);
}

void LaneEditor::paint (juce::Graphics& g)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (10.5f));
    const std::pair<juce::Component*, const char*> caps[] = { { &filterBox, "Filter" }, { &reso, "Res" }, { &attack, "Attack" },
                                                             { &outBox, "Mixer track (inside Groovebox)" }, { &chokeBox, "Choke" },
                                                             { &sampleDrag, "Send this lane to FL as its own channel" } };
    for (auto [c, label] : caps)
        g.drawText (label, c->getX(), c->getY() - 14, c->getWidth() + 30, 13, juce::Justification::centredLeft, false);
}

void LaneEditor::resized()
{
    auto r = getLocalBounds().reduced (10, 8);

    auto left = r.removeFromLeft (330);
    r.removeFromLeft (14);
    title.setBounds (left.removeFromTop (22));
    fitLabel.setBounds (left.removeFromTop (32));
    left.removeFromTop (14);

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
    outBox.setBounds (row2.removeFromLeft (170));
    row2.removeFromLeft (4);
    mixerHelpBtn.setBounds (row2.removeFromLeft (44));
    row2.removeFromLeft (8);
    chokeBox.setBounds (row2);
    left.removeFromTop (4);
    auto rowS = left.removeFromTop (24);
    separateBtn.setBounds (rowS.removeFromLeft (170));
    rowS.removeFromLeft (8);
    dupLaneBtn.setBounds (rowS);
    left.removeFromTop (18);
    auto row3 = left.removeFromTop (28);
    sampleDrag.setBounds (row3.removeFromLeft (row3.getWidth() / 2 - 3));
    row3.removeFromLeft (6);
    notesDrag.setBounds (row3);
    left.removeFromTop (6);
    allDrag.setBounds (left.removeFromTop (26));
    left.removeFromTop (4);
    hint.setBounds (left);

    auto head = r.removeFromTop (24);
    head.removeFromLeft (ParamStrip::kLabelW);
    playsBox.setBounds (head.removeFromLeft (juce::jmin (380, head.getWidth())));
    r.removeFromTop (4);

    const int mode = juce::jmax (0, shownMode);
    for (auto& s : strips) s->setVisible (mode == 0);
    for (auto* s : { &fChance, &fDelay, &fDrift, &fVel }) s->setVisible (mode == 1);
    modeInfo.setVisible (mode != 0);

    steps.setBounds (r.removeFromTop (40));
    r.removeFromTop (6);
    if (mode == 0)
    {
        const int stripCount = (int) strips.size();
        const int sh = juce::jmax (14, r.getHeight() / juce::jmax (1, stripCount));
        for (auto& s : strips)
            s->setBounds (r.removeFromTop (sh));
        return;
    }
    r.removeFromLeft (ParamStrip::kLabelW);
    if (mode == 1)
    {
        auto cols = r.removeFromTop (64);
        auto c1 = cols.removeFromLeft (cols.getWidth() / 2 - 6);
        cols.removeFromLeft (12);
        fChance.setBounds (c1.removeFromTop (28)); c1.removeFromTop (8); fDelay.setBounds (c1.removeFromTop (28));
        fDrift.setBounds (cols.removeFromTop (28)); cols.removeFromTop (8); fVel.setBounds (cols.removeFromTop (28));
        r.removeFromTop (10);
    }
    modeInfo.setBounds (r.removeFromTop (60));
}

//==============================================================================
//==============================================================================
void GrooveSlot::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (kRow);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (dragOver ? kAccent : juce::Colour (0xff3a3f47));
    g.drawRoundedRectangle (r, 3.0f, dragOver ? 1.5f : 1.0f);

    Groove gr;
    {
        const juce::SpinLock::ScopedLockType sl (proc.lock);
        gr = proc.groove;
    }

    auto inner = getLocalBounds().reduced (6, 2);
    g.setFont (juce::FontOptions (11.0f));
    if (proc.grooveBusy.load())
    {
        g.setColour (kDim);
        g.drawText ("Listening to the reference...", inner, juce::Justification::centredLeft, true);
        return;
    }
    if (! gr.valid)
    {
        g.setColour (kDim);
        g.drawText ("Reference groove: drop a loop or track here", inner, juce::Justification::centredLeft, true);
        return;
    }

    // 16 columns, three bands: bar height = how often, tick position = early/late.
    auto viz = inner.removeFromRight (16 * 7).toFloat();
    const float rowH = viz.getHeight() / 3.0f;
    static const juce::uint32 bandCol[] = { 0xffd06a5a, 0xffd0a35a, 0xff6ab4d0 };
    for (int b = 0; b < 3; ++b)
        for (int i = 0; i < 16; ++i)
        {
            const auto cell = juce::Rectangle<float> (viz.getX() + (float) i * 7.0f, viz.getY() + (float) b * rowH, 6.0f, rowH - 1.0f);
            g.setColour (juce::Colour (i % 4 == 0 ? 0xff30343b : 0xff2a2d33));
            g.fillRect (cell);
            const float p = gr.prob[(size_t) b][(size_t) i];
            if (p <= 0.0f) continue;
            const float x = cell.getCentreX() + gr.offset[(size_t) b][(size_t) i] * 7.0f;
            g.setColour (juce::Colour (bandCol[b]).withAlpha (0.35f + 0.65f * juce::jmin (1.0f, p)));
            g.fillRect (juce::Rectangle<float> (x - 1.5f, cell.getBottom() - cell.getHeight() * juce::jmax (0.25f, juce::jmin (1.0f, p)),
                                                3.0f, cell.getHeight() * juce::jmax (0.25f, juce::jmin (1.0f, p))));
        }

    inner.removeFromRight (6);
    g.setColour (kText);
    g.drawText ("Groove: " + gr.name, inner.removeFromTop (inner.getHeight() / 2), juce::Justification::bottomLeft, true);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (juce::String (juce::roundToInt (gr.bpm)) + " BPM, " + juce::String (gr.bars) + " bars.  Click to open, copy or adjust", inner,
                juce::Justification::topLeft, true);
}

void GrooveSlot::load (const juce::File& f)
{
    if (! f.existsAsFile()) return;
    if (onOpen) onOpen();
    proc.loadGroove (f, forcedBpm ? forcedBpm() : 0.0);
    if (onStatus) onStatus ("Listening to " + f.getFileName() + "...");
    repaint();
}

void GrooveSlot::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    if (! files.isEmpty()) load (juce::File (files[0]));
    repaint();
}

void GrooveSlot::choose()
{
    chooser = std::make_unique<juce::FileChooser> ("Reference loop or track", proc.grooveFile.existsAsFile() ? proc.grooveFile : juce::File(),
                                                   proc.formats.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) { load (fc.getResult()); });
}

void GrooveSlot::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
    {
        if (onOpen) onOpen();
        if (! proc.groove.valid && ! proc.grooveBusy.load()) choose();
        return;
    }
    juce::PopupMenu m;
    m.addItem (1, "Load reference...");
    m.addItem (2, "Analyze again", proc.grooveFile.existsAsFile());
    m.addItem (3, "Clear groove", proc.groove.valid);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (r == 1) choose();
        if (r == 2) load (proc.grooveFile);
        if (r == 3)
        {
            proc.clearGroove();
            if (onStatus) onStatus ("Groove cleared");
            repaint();
        }
    });
}

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

    const int bar = proc.displayBar();
    if (bar >= 0 && bar < proc.songBars)
    {
        const float x = r.getX() + r.getWidth() * ((float) bar + 0.5f) / total;
        g.setColour (proc.currentBar.load() >= 0 ? juce::Colours::white : kAccent);
        g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
        g.fillEllipse (x - 4.0f, r.getY() - 1.0f, 8.0f, 8.0f);
    }
}

void SongTimeline::seekFrom (const juce::MouseEvent& e)
{
    if (! proc.songMode.load() || proc.songSections.empty()) return;
    const int bars = juce::jmax (1, proc.songBars);
    const int bar = juce::jlimit (0, bars - 1, (int) ((float) e.x / (float) juce::jmax (1, getWidth()) * (float) bars));
    proc.seekTo (bar * 4.0);
    juce::String sec;
    for (const auto& s : proc.songSections)
        if (bar >= s.startBar && bar < s.startBar + s.bars) sec = s.name;
    if (onStatus)
        onStatus (proc.hostIsPlaying.load()
                      ? juce::String ("FL is playing, so FL's song position leads. Stop FL to scrub here, or move FL's playhead.")
                      : "Bar " + juce::String (bar + 1) + " (" + sec + "). Play starts here; lanes that rest here are dimmed.");
    repaint();
}

//==============================================================================
SettingsPanel::SettingsPanel()
{
    addAndMakeVisible (doneBtn);
    doneBtn.onClick = [this] { setVisible (false); };
}

void SettingsPanel::addRow (juce::Component& c, const juce::String& caption)
{
    rows.push_back ({ &c, caption });
    addAndMakeVisible (c);
}

void SettingsPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 6.0f);
    g.setColour (kPanel.brighter (0.06f));
    g.fillRoundedRectangle (r.reduced (1.0f), 6.0f);
    g.setColour (kAccent.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 1.0f);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Settings", 14, 10, 200, 22, juce::Justification::centredLeft, false);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    for (const auto& [c, caption] : rows)
        g.drawFittedText (caption, c->getRight() + 12, c->getY(), getWidth() - c->getRight() - 24, c->getHeight(),
                          juce::Justification::centredLeft, 2);
}

void SettingsPanel::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    auto top = r.removeFromTop (26);
    doneBtn.setBounds (top.removeFromRight (64));
    r.removeFromTop (10);
    for (auto& [c, caption] : rows)
    {
        c->setBounds (r.removeFromTop (26).withWidth (190));
        r.removeFromTop (8);
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
    text.setSize (1000, 700); // lay the text out at a real width: at width 0 every insert re-wraps everything (slow)

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

    h ("Shortcuts");
    p ("Ctrl+Z undo, Ctrl+Y (or Ctrl+Shift+Z) redo. Ctrl+S save setup. Ctrl+D duplicate the selected lane.\n"
       "Space is still FL's play/stop.\n"
       "G generate all (Shift+G: only the selected lane). M mutate all (Shift+M: selected lane).\n"
       "Up / Down: pick a lane. Left / Right: previous / next sample in that lane. A: hear it.\n"
       "R: random good-fit sample in the selected lane (Shift+R: shuffle all lanes). P: Groovebox's own Play.\n"
       "H or F1: this help. Esc closes it.\n"
       "Shortcuts work after you click anywhere in Groovebox's window.");

    h ("Groovebox in 30 seconds");
    p ("1. Click Library... and pick the folder with your drum samples. Groovebox finds the kick, snare, hat... folders and gives each its own lane.\n"
       "2. Pick a genre and press Generate. Press Play in FL (or Play here).\n"
       "3. Hover a sample name and scroll the mouse wheel to try other samples while it plays.\n"
       "4. Click a lane to edit it in the big editor at the bottom.\n"
       "5. Happy with it? In that editor, drag the lane's sample onto FL's Channel Rack and its notes into that channel's piano roll.");

    h ("Each lane");
    p ("M / S: mute / solo.\n"
       "Role: what the lane is (kick, hat...). It guides the generator and the fit rating.\n"
       "Padlocks: click either one to lock the lane (Shuffle, ?, Library, Generate and Mutate leave its sample and pattern alone). Right-click a padlock to lock only the sample or only the pattern. Right-click a sample name to open it in Explorer.\n"
       "< name >: previous / next sample. Scrolling on the name does the same. Click the name to hear it.\n"
       "?: random sample that fits the role. ...: choose a folder.\n"
       "Coloured dot: how well the sample fits this role in this genre. Green good, yellow usable, red poor, hollow still listening. "
       "Hover the name for the reason and the detected note.\n"
       "Length: steps in the loop. Up to 64 steps it moves by 1, beyond that by whole bars, up to 64 bars. Lanes with different lengths drift against each other.\n"
       "Rate: how long a step is (1/16 is normal; T = triplet, D = dotted).\n"
       "Gen / Mut: new pattern / evolve the pattern for this lane only. + duplicates the lane (sample, pattern, every setting), x removes it.\n"
       "Lvl, Pan, Pitch, Cut: level, stereo position, tuning, filter cutoff. These four can be automated in FL.\n"
       "Decay: how long the sample rings. Swing: shuffle feel. Hum: humanize (tiny timing and velocity changes).\n"
       "Step grid: click to turn steps on or off, drag to paint. Right-click for clear, shift, reverse, copy and paste.");

    h ("Lane editor (bottom)");
    p ("Shows the selected lane big. Drag up and down in each row to shape every step: Velocity, Probability (how often it plays), "
       "Pitch, Pan, Cutoff, Sample start, Nudge (early or late) and Ratchet (repeats inside the step). Double-click resets a step; "
       "right-click a row to reset, randomize or ramp it.\n"
       "Filter + Res: low-pass, high-pass or band-pass for the whole lane. Attack: fade-in on each hit. Reverse: play backwards.\n"
       "The Lane editor button bottom right hides or shows this panel.");

    h ("Mixer tracks");
    p ("Each lane can have its own FL mixer track. In the lane editor (bottom), Mixer track picks where the lane goes; the How? button repeats these steps.\n"
       "Lanes playing inside Groovebox:\n"
       "  0. Turn on Separate mixer tracks in the lane editor. It's off by default: then every lane plays on Groovebox's one mixer track, "
       "so adding Groovebox to an existing project never touches your mixer.\n"
       "  1. Click the small plugin options icon at the top left of Groovebox's window (FL's plugin wrapper).\n"
       "  2. Open the Processing tab and click Auto map outputs.\n"
       "  3. Done: lane 1 lands on the first new mixer track, lane 2 on the next, and so on (Mixer track: Own).\n"
       "Pick \"Same as lane N\" to share a track, or \"Main\" to stay on Groovebox's main track. Until step 2, everything plays on one track.\n"
       "Lanes you sent to FL as channels (see below): set the mixer track on the FL channel itself, in the number box on the channel in the Channel Rack.");

    h ("Choke");
    p ("Lanes in the same choke group cut each other off, like a real hi-hat: the open hat stops the moment the closed hat plays. "
       "Closed and open hat lanes are in group 1 by default. 'No choke' lets sounds ring over each other.");

    h ("Foliage: foley, followers and beds");
    p ("Good tracks in microhouse, deep and dusty house or 2-step live on quiet detail around the drums. Three lane types for that:\n"
       "Followers: in the lane editor, set Plays to \"Right after lane N\". The lane then answers that lane's hits a few ms later, off the grid, "
       "like a rustle or tick completing the snare. Set how often it answers, how many ms after, how much it drifts (timing, sample start, pitch) "
       "and how loud it is compared to the hit.\n"
       "Foley lanes (role Foley): sparse, quiet hits on 1/32 steps nudged off the grid, each from a different part of the sample, different every bar.\n"
       "Beds (role Bed, or Plays: Bed): the sample loops underneath everything while the music runs, with a soft crossfade, and fades in and out "
       "with song sections. Vinyl crackle, rain, room tone, hiss, field recordings.\n"
       "Starter kit (top bar) sets up the lanes a genre needs, with followers and beds, keeps the samples you already have for those instruments "
       "and fills the rest from your library (folders named foley, vinyl, crackle, rain, vocal... are recognised).");

    h ("Generating");
    p ("Genre sets the style and the typical tempo (shown in the BPM box). Microhouse, Deep & dusty and 2-step / future garage are built "
       "for foliage: they add more quiet foley. Hybrid gives every lane a different genre; Free ignores genre rules.\n"
       "Randomize all: new instruments from your library, new samples and new patterns at once (locked lanes stay).\n"
       "Settings (top bar) holds the finer knobs:\n"
       "Density: how busy. Variation: ghost notes, softer and louder hits, steps that only sometimes play, small timing shifts. "
       "Irregular: odd lengths, triplets and Euclidean patterns.\n"
       "Loop: how many bars Generate writes (1 to 64). Long loops evolve bar by bar, with small fills every 4 and 8 bars. "
       "Long lanes are shown 4 bars at a time: use < > next to the bar numbers, or Follow to track the playhead.\n"
       "Mutate: changes the current groove a little (amount next to it) instead of replacing it.");

    h ("Song mode: a full track");
    p ("Turn on Song, pick a length and press Make song. Groovebox arranges intro, build, main, break and outro sections and decides "
       "which lanes play in each (the kick drops out in the last bar of a build). It follows FL's song position, so press Play from the start "
       "of the song. Lanes that sit out the current section are dimmed. Click or drag on the song strip to scrub: the marker shows where "
       "Groovebox's own Play starts, and which lanes play there. The notes you send to FL cover the whole song.");

    h ("Groove from a reference");
    p ("Drop a drum loop, or a whole track you love, on the Reference groove box (top right, above the lanes), or click it. "
       "Groovebox listens to it, finds its tempo and bar, and measures for every 16th how early or late and how loud its "
       "lows (kick, bass), mids (snare, clap, perc) and highs (hats, shakers, ride) play. Click the box to open the big Groove panel at the bottom: "
       "it shows every 16th, how loud (bar height), how often (%) and how early or late (ms).\n"
       "Your lanes then move towards that feel while they play, each lane following its own band. Groove amount sets how far (0 = off). "
       "Or make it permanent: Copy groove to lanes writes the reference's hits, accents and timing into your lanes; "
       "Copy timing only keeps your patterns and gives them its push and drag (Nudge and Velocity rows). Ctrl+Z undoes either.\n"
       "Global swing is set to 0 when a groove loads, because the reference brings its own swing.\n"
       "Pick the genre \"From reference groove\" and press Generate to also write patterns from where the reference hits.\n"
       "Tempo is detected automatically; if it picks half or double, drag the Tempo box in the Groove panel to the real tempo and it listens again. "
       "Clean drum loops work best. The feel goes along when you send lanes to FL.");

    h ("Tempo");
    p ("When FL plays, Groovebox follows FL's tempo, so set FL's tempo to suit the genre. The BPM box sets the tempo for Groovebox's own Play button.");

    h ("Setups");
    p ("Setups > Save setup as... stores everything (folders, samples, patterns, knobs). A new Groovebox opens with your last session, "
       "which is saved automatically every few seconds. Setups > Load last session brings it back.");

    h ("Send lanes to FL as their own channels");
    p ("Click a lane, then use the dashed drag buttons in the lane editor (bottom):\n"
       "  1. Drag \"Sample > Channel Rack\" onto FL's Channel Rack. FL makes a new channel with that lane's sample. "
       "The lane's pitch, filter, decay, attack, reverse, level and pan are baked into it, so it sounds like it does here.\n"
       "  2. Open that new channel's piano roll and drag \"Notes > Piano roll\" into it. The lane's pattern lands as notes "
       "on C5 (the sample's own pitch), with velocity, step pitch, ratchets, swing and the reference groove. "
       "Length = your loop, or the whole song when Song is on.\n"
       "\"All lanes > Channel Rack\" makes every channel in one go; then click each lane and drag its notes into its channel.\n"
       "From then on they're normal FL channels: edit them however you like. Per-step pan, cutoff and sample start stay in Groovebox "
       "(FL channels can't take them per note). The files are kept in Documents\\Groovebox\\To FL (Setups > Open the To FL folder).\n"
       "Setups > Save all lanes as one MIDI file writes every lane into one .mid (C3 = lane 1, C#3 = lane 2...).");

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
void FileDragButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto accent = findColour (juce::TextButton::buttonOnColourId);
    g.setColour (juce::Colour (0xff2a2e35).brighter (over ? 0.15f : 0.0f).darker (down ? 0.2f : 0.0f));
    g.fillRoundedRectangle (r, 4.0f);
    juce::Path border;
    border.addRoundedRectangle (r, 4.0f);
    juce::Path dashed;
    const float dash[] = { 4.0f, 3.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, border, dash, 2);
    g.setColour (isEnabled() ? accent.withAlpha (over ? 1.0f : 0.7f) : kDim.withAlpha (0.4f));
    g.fillPath (dashed);

    // Grip dots: "pick me up".
    auto grip = r.removeFromLeft (16.0f).reduced (5.0f, r.getHeight() * 0.25f);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 2; ++x)
            g.fillEllipse (grip.getX() + (float) x * 4.0f, grip.getY() + (float) y * grip.getHeight() / 2.5f, 2.2f, 2.2f);

    g.setColour (isEnabled() ? kText : kDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (getButtonText(), r.toNearestInt(), juce::Justification::centred, true);
}

void FileDragButton::mouseDrag (const juce::MouseEvent& e)
{
    juce::TextButton::mouseDrag (e);
    if (dragging || e.getDistanceFromDragStart() < 6 || ! makeFiles)
        return;

    dragging = true;
    const auto files = makeFiles();
    if (files.isEmpty())
        return;
    if (onDragged) onDragged (files);
    juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
}

//==============================================================================
GrooveView::GrooveView (GrooveboxProcessor& p) : proc (p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &amount, &bpmBox, &loadBtn, &clearBtn, &copyBtn, &timingBtn, &backBtn })
        addAndMakeVisible (c);

    amount.setSliderStyle (juce::Slider::LinearBar);
    amount.setTextBoxStyle (juce::Slider::TextBoxLeft, true, 120, 20);
    amount.setRange (0.0, 1.0, 0.01);
    amount.textFromValueFunction = [] (double v) { return "Groove amount " + juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    amount.setDoubleClickReturnValue (true, 0.8);
    amount.setTooltip ("How far your lanes move towards the reference's timing and accents while they play (0 = off)");

    bpmBox.format = [] (int v) { return v <= 0 ? juce::String ("Tempo: auto") : "Tempo: " + juce::String (v) + " BPM"; };
    bpmBox.setTooltip ("Tempo of the reference. Auto detects it; if it picks half or double, drag to the real tempo and it listens again");

    loadBtn.setTooltip ("Pick a loop or a track (or drop one anywhere on this panel)");
    clearBtn.setTooltip ("Forget the reference groove");
    copyBtn.setTooltip ("Write the reference into your lanes: where it hits, how loud, and how early or late. "
                        "Kick from the lows, snare and clap from the loud mids, hats from the highs. Pattern-locked lanes are kept. Ctrl+Z undoes it");
    timingBtn.setTooltip ("Keep your patterns, but give their hits the reference's push and drag and its accents (written into Nudge and Velocity). Ctrl+Z undoes it");
    backBtn.setTooltip ("Back to the lane editor");

    loadBtn.onClick = [this] { if (onLoad) onLoad(); };
    backBtn.onClick = [this] { if (onBack) onBack(); };
    clearBtn.onClick = [this]
    {
        proc.clearGroove();
        if (onStatus) onStatus ("Groove cleared");
        refresh();
    };
    copyBtn.onClick = [this]
    {
        proc.applyGrooveToLanes (true);
        if (onStatus) onStatus ("Copied the reference groove into your lanes (hits, accents and timing). Groove amount set to 0 so it isn't applied twice. Ctrl+Z undoes it.");
        refresh();
    };
    timingBtn.onClick = [this]
    {
        proc.applyGrooveToLanes (false);
        if (onStatus) onStatus ("Your patterns now carry the reference's timing and accents (see the Nudge and Velocity rows). Ctrl+Z undoes it.");
        refresh();
    };
}

void GrooveView::refresh()
{
    amount.setValue (proc.grooveAmount, juce::dontSendNotification);
    amount.updateText();
    bpmBox.setValue (juce::roundToInt (proc.grooveForcedBpm), false);
    const bool ok = proc.groove.valid;
    for (auto* b : { &clearBtn, &copyBtn, &timingBtn })
        b->setEnabled (ok);
    amount.setEnabled (ok);
    repaint();
}

void GrooveView::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty()) return;
    const juce::File f (files[0]);
    if (! f.existsAsFile()) return;
    proc.loadGroove (f, (double) bpmBox.getValue());
    if (onStatus) onStatus ("Listening to " + f.getFileName() + "...");
    repaint();
}

void GrooveView::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto left = r.removeFromLeft (230);
    r.removeFromLeft (14);
    left.removeFromTop (64);
    for (auto* b : std::initializer_list<juce::Component*> { &loadBtn, &bpmBox, &amount })
    {
        b->setBounds (left.removeFromTop (26));
        left.removeFromTop (6);
    }
    left.removeFromTop (6);
    copyBtn.setBounds (left.removeFromTop (28));
    left.removeFromTop (6);
    auto row = left.removeFromTop (26);
    timingBtn.setBounds (row.removeFromLeft (row.getWidth() / 2 + 20));
    row.removeFromLeft (6);
    clearBtn.setBounds (row);
    backBtn.setBounds (left.removeFromBottom (24).removeFromLeft (110));
    gridArea = r;
}

void GrooveView::paint (juce::Graphics& g)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    Groove gr;
    {
        const juce::SpinLock::ScopedLockType sl (proc.lock);
        gr = proc.groove;
    }

    auto head = getLocalBounds().reduced (10, 8).removeFromLeft (230);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Reference groove", head.removeFromTop (22), juce::Justification::centredLeft, false);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText (proc.grooveBusy.load() ? juce::String ("Listening...")
                      : gr.valid ? gr.name + "\n" + describeGroove (gr)
                                 : juce::String ("Drop a drum loop or a track you love on this panel."),
                      head.removeFromTop (40), juce::Justification::topLeft, 3);

    auto r = gridArea;
    if (! gr.valid)
    {
        g.setColour (kRow);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Drop a loop or a track here.\n\nGroovebox finds its tempo and, for every 16th, where its kick, snare and hats hit, "
                          "how loud, and how early or late. You'll see it here, and can copy it into your lanes.",
                          r.reduced (40, 20), juce::Justification::centred, 6);
        return;
    }

    // Header: beat numbers.
    const int labelW = 70;
    auto header = r.removeFromTop (18);
    header.removeFromLeft (labelW);
    const float cw = (float) header.getWidth() / 16.0f;
    static const char* const counts[] = { "1", "e", "+", "a", "2", "e", "+", "a", "3", "e", "+", "a", "4", "e", "+", "a" };
    g.setFont (juce::FontOptions (11.0f));
    for (int i = 0; i < 16; ++i)
    {
        g.setColour (i % 4 == 0 ? kText : kDim);
        g.drawText (counts[i], juce::Rectangle<float> ((float) header.getX() + cw * (float) i, (float) header.getY(), cw, 16.0f),
                    juce::Justification::centred, false);
    }

    static const juce::uint32 bandCol[] = { 0xffd06a5a, 0xffd0a35a, 0xff6ab4d0 };
    static const char* const bandName[] = { "Lows\nkick, bass", "Mids\nsnare, clap", "Highs\nhats, ride" };
    auto foot = r.removeFromBottom (16);
    const float rowH = (float) r.getHeight() / 3.0f;
    const double msPerStep = gr.bpm > 0 ? 60000.0 / gr.bpm / 4.0 : 0.0;

    for (int b = 0; b < 3; ++b)
    {
        auto row = juce::Rectangle<float> ((float) r.getX(), (float) r.getY() + rowH * (float) b, (float) r.getWidth(), rowH).reduced (0.0f, 2.0f);
        g.setColour (juce::Colour (bandCol[b]));
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (bandName[b], row.removeFromLeft ((float) labelW).toNearestInt(), juce::Justification::centredLeft, 2);

        for (int i = 0; i < 16; ++i)
        {
            auto cell = juce::Rectangle<float> (row.getX() + cw * (float) i, row.getY(), cw, row.getHeight()).reduced (1.5f, 0.0f);
            g.setColour (juce::Colour (i % 4 == 0 ? 0xff2e3238 : 0xff25282d));
            g.fillRoundedRectangle (cell, 3.0f);
            const float p = juce::jmin (1.0f, gr.prob[(size_t) b][(size_t) i]);
            if (p <= 0.0f) continue;

            const float acc = gr.accent[(size_t) b][(size_t) i];
            const float off = gr.offset[(size_t) b][(size_t) i];
            // Bar: height = loudness, brightness = how often it plays.
            auto bar = cell.reduced (cell.getWidth() * 0.22f, 3.0f);
            const float h = bar.getHeight() * juce::jmax (0.12f, acc) * 0.8f;
            auto filled = bar.withTrimmedTop (bar.getHeight() - h).withTrimmedBottom (bar.getHeight() * 0.2f - 0.0f).translated (0.0f, 0.0f);
            filled = juce::Rectangle<float> (bar.getX(), bar.getBottom() - bar.getHeight() * 0.2f - h, bar.getWidth(), h);
            g.setColour (juce::Colour (bandCol[b]).withAlpha (0.3f + 0.7f * p));
            g.fillRoundedRectangle (filled.translated (off * cw, 0.0f), 2.0f);

            // Timing line: centre = on the grid.
            g.setColour (kDim.withAlpha (0.5f));
            g.drawVerticalLine ((int) cell.getCentreX(), cell.getY() + 2.0f, cell.getBottom() - 2.0f);

            // Text: early/late in ms (and how often, if not every bar).
            g.setColour (kText.withAlpha (0.85f));
            g.setFont (juce::FontOptions (9.5f));
            const int ms = juce::roundToInt (off * msPerStep);
            juce::String t = ms == 0 ? juce::String ("on") : (ms > 0 ? "+" : "") + juce::String (ms) + "ms";
            g.drawText (t, cell.withTrimmedTop (cell.getHeight() - 14.0f).toNearestInt(), juce::Justification::centred, false);
            if (p < 0.95f)
                g.drawText (juce::String (juce::roundToInt (p * 100.0f)) + "%", cell.withHeight (13.0f).toNearestInt(), juce::Justification::centred, false);
        }
    }

    g.setColour (kDim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("Bar height = how loud. Bar left/right of the line = early/late (ms). % = how often it plays (blank = every bar).",
                foot.withTrimmedLeft (labelW), juce::Justification::centredLeft, true);
}

//==============================================================================
GrooveboxEditor::GrooveboxEditor (GrooveboxProcessor& p)
    : AudioProcessorEditor (&p), proc (p), laneEditor (p, view), timeline (p), grooveSlot (p), grooveView (p)
{
    setLookAndFeel (&look);
    proc.loadLastSessionIfNew(); // before showing anything (does nothing for a project's Groovebox)
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
    status.setText ("New here? Click Help (or press H). Drop a folder on a lane or use Library..., then Generate (G). Ctrl+Z undoes anything.",
                    juce::dontSendNotification);
    bpmLabel.setColour (juce::Label::textColourId, kDim);
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    pageLabel.setColour (juce::Label::textColourId, kText);
    pageLabel.setJustificationType (juce::Justification::centred);
    pageLabel.setFont (juce::FontOptions (12.0f));

    for (auto* c : std::initializer_list<juce::Component*> {
             &title, &byline, &status, &bpmLabel, &pageLabel, &helpBtn, &setupsBtn, &libraryBtn, &shuffleBtn,
             &generateBtn, &mutateBtn, &previewBtn, &seqBtn, &genreBox, &loopBox, &settingsBtn, &randomBtn, &kitBtn,
             &songLenBox, &songBtn, &makeSongBtn, &pagePrev, &pageNext, &followBtn, &editorBtn,
             &master, &bpmBox, &viewport, &laneEditor, &timeline,
             &grooveSlot, &grooveView })
        addAndMakeVisible (c);

    settings.addRow (density, "How busy Generate makes the patterns");
    settings.addRow (variation, "Ghost notes, velocity spread, steps that don't always play, micro timing");
    settings.addRow (irregular, "Chance of odd lane lengths, odd rates and Euclidean placement (polymeters)");
    settings.addRow (swingScale, "Scales every lane's swing (automatable in FL)");
    settings.addRow (humScale, "Scales every lane's humanize (automatable in FL)");
    settings.addRow (mutateAmt, "How much Mutate changes the groove");
    settings.addRow (fitFilterBtn, "Scrolling skips samples rated a poor fit for the lane");
    settings.addRow (hideLoopsBtn, "Drum and foley lanes skip loops when you scroll (beds and vocals don't)");
    hideLoopsBtn.setClickingTogglesState (true);
    hideLoopsBtn.onClick = [this] { proc.hideLoops = hideLoopsBtn.getToggleState(); };
    addChildComponent (settings);
    settingsBtn.setTooltip ("Density, Variation, Irregular, Swing, Humanize, Mutate amount, Fit filter");
    settingsBtn.onClick = [this] { settings.setVisible (! settings.isVisible()); settings.toFront (false); };
    randomBtn.setTooltip ("Roll the dice: new instruments from your library, new samples and new patterns. Locked lanes stay. Ctrl+Z undoes it");
    randomBtn.onClick = [this]
    {
        proc.randomizeAll();
        status.setText (proc.libraryRoot.isDirectory() ? "Randomized: new instruments, samples and patterns (locked lanes kept). Ctrl+Z undoes it."
                                                       : "Randomized samples and patterns. Pick a library (Library...) so Randomize all can also swap instruments.",
                        juce::dontSendNotification);
    };
    kitBtn.setTooltip ("Set up the lanes this genre needs: core drums, foley that follows other hits, and beds. "
                       "Keeps samples you already have for those instruments, fills the rest from your library. Ctrl+Z undoes it");
    kitBtn.onClick = [this]
    {
        proc.applyStarterKit (proc.gen.genre);
        status.setText (juce::String ("Starter kit for ") + genreName (proc.gen.genre) + ": " + juce::String (proc.numLanes.load())
                            + " lanes. Click a Foley lane to see what it follows. Ctrl+Z undoes it.",
                        juce::dontSendNotification);
    };
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

    grooveSlot.setTooltip ("Drop a drum loop or a track you love here (or click to choose one). Groovebox finds its tempo and, "
                           "for every 16th, how early or late and how loud its lows, mids and highs play, then moves your lanes towards it. "
                           "Pick the genre \"From reference groove\" to also generate patterns from where it hits. Right-click to clear.");
    grooveSlot.forcedBpm = [this] { return (double) grooveView.bpmBox.getValue(); };
    grooveSlot.onStatus = [this] (const juce::String& t) { status.setText (t, juce::dontSendNotification); };
    grooveSlot.onOpen = [this] { setShowGroove (true); };
    grooveView.onStatus = grooveSlot.onStatus;
    grooveView.onBack = [this] { setShowGroove (false); };
    grooveView.onLoad = [this] { grooveSlot.choose(); };
    grooveView.bpmBox.onChange = [this] (int) { grooveReanalyzeIn = 12; };
    grooveView.amount.onValueChange = [this] { proc.setGrooveAmount ((float) grooveView.amount.getValue()); };
    laneEditor.onStatus = grooveSlot.onStatus;
    laneEditor.onDuplicate = [this] (int li) { duplicateSelected (li); };
    timeline.onStatus = grooveSlot.onStatus;
    timeline.setMouseCursor (juce::MouseCursor::IBeamCursor);

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
    setupsBtn.setTooltip ("Save this setup, load a saved one, or go back to your last session");

    setupsBtn.onClick   = [this] { showSetupsMenu(); };
    generateBtn.onClick = [this] { proc.generateAll(); };
    mutateBtn.onClick   = [this] { proc.mutateAll(); };
    shuffleBtn.onClick  = [this] { proc.shuffleAllSamples(); };
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

    // Keyboard: the editor keeps the focus so shortcuts always work; keys it doesn't use
    // (like Space) go back to FL, so FL's play/stop keeps working.
    setWantsKeyboardFocus (true);
    keepFocusOnEditor (*this);
    addMouseListener (this, true);
    proc.checkpoint();
    startTimerHz (30);
}

void GrooveboxEditor::keepFocusOnEditor (juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        if (dynamic_cast<juce::TextEditor*> (child) == nullptr)
        {
            child->setWantsKeyboardFocus (false);
            child->setMouseClickGrabsKeyboardFocus (false);
        }
        keepFocusOnEditor (*child);
    }
}

void GrooveboxEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! help.isParentOf (e.eventComponent))
        grabKeyboardFocus();
}

void GrooveboxEditor::doUndo (bool redo)
{
    const bool ok = redo ? proc.redo() : proc.undo();
    status.setText (ok ? (redo ? "Redo" : "Undo") : (redo ? "Nothing to redo" : "Nothing to undo"), juce::dontSendNotification);
    laneEditor.refresh();
    repaintGrids();
}

bool GrooveboxEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    const int upper = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);

    if (help.isVisible())
    {
        if (code == juce::KeyPress::escapeKey || upper == 'H' || code == juce::KeyPress::F1Key) { help.setVisible (false); return true; }
        return false;
    }

    if (mods.isCommandDown())
    {
        if (upper == 'Z' && ! mods.isShiftDown()) { doUndo (false); return true; }
        if (upper == 'Y' || (upper == 'Z' && mods.isShiftDown())) { doUndo (true); return true; }
        if (upper == 'S') { saveSetupAs(); return true; }
        if (upper == 'D') { duplicateSelected(); return true; }
        return false; // other Ctrl shortcuts belong to FL
    }
    if (mods.isAltDown())
        return false;

    const bool shift = mods.isShiftDown();
    const int sel = view.selected;
    const int nl = proc.numLanes.load();

    if (code == juce::KeyPress::upKey)   { selectLane (juce::jmax (0, sel - 1)); viewport.setViewPosition (0, juce::jmin (viewport.getViewPositionY(), sel * (LaneRow::kHeight + 3))); return true; }
    if (code == juce::KeyPress::downKey) { selectLane (juce::jmin (nl - 1, sel + 1)); return true; }
    if (code == juce::KeyPress::leftKey)  { proc.stepSample (sel, -1); for (auto& r : rows) r->repaintSlot(); laneEditor.refresh(); return true; }
    if (code == juce::KeyPress::rightKey) { proc.stepSample (sel, 1);  for (auto& r : rows) r->repaintSlot(); laneEditor.refresh(); return true; }
    if (code == juce::KeyPress::escapeKey && settings.isVisible()) { settings.setVisible (false); return true; }
    if (code == juce::KeyPress::F1Key || upper == 'H') { help.setVisible (true); help.toFront (true); return true; }

    switch (upper)
    {
        case 'G': if (shift) proc.generateLane (sel); else proc.generateAll(); return true;
        case 'M': if (shift) proc.mutateOneLane (sel); else proc.mutateAll(); return true;
        case 'R': if (shift) proc.shuffleAllSamples(); else proc.randomSample (sel); for (auto& r : rows) r->repaintSlot(); return true;
        case 'A': proc.audition (sel); return true;
        case 'P': previewBtn.setToggleState (! previewBtn.getToggleState(), juce::sendNotification); return true;
        default: break;
    }
    return false; // Space and everything else go to FL
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
    m.addSeparator();
    m.addItem (5, "Save all lanes as one MIDI file (C3 = lane 1...)");
    m.addItem (6, "Open the To FL folder (baked samples and notes)");

    juce::Component::SafePointer<GrooveboxEditor> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setupsBtn), [self, saved] (int r)
    {
        if (self == nullptr || r == 0) return;
        auto& p = self->proc;
        auto say = [self] (const juce::String& t) { if (self != nullptr) self->status.setText (t, juce::dontSendNotification); };

        if (r == 1)
        {
            self->saveSetupAs();
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
        else if (r == 5)
        {
            p.exportBars = p.exportLengthBars();
            const auto f = p.exportMidi();
            say ("Saved " + f.getFileName() + " to Documents/Groovebox/MIDI");
            f.revealToUser();
        }
        else if (r == 6)
        {
            GrooveboxProcessor::flExportFolder().revealToUser();
        }
        else if (r >= 100 && r - 100 < saved.size())
        {
            const auto f = saved[r - 100];
            say (p.loadSetup (f) ? "Loaded setup \"" + f.getFileNameWithoutExtension() + "\"" : juce::String ("Couldn't read that setup"));
        }
    });
}

void GrooveboxEditor::saveSetupAs()
{
    juce::Component::SafePointer<GrooveboxEditor> self (this);
    chooser = std::make_unique<juce::FileChooser> ("Save Groovebox setup",
                                                   GrooveboxProcessor::setupsFolder().getChildFile ("My setup.gbx"), "*.gbx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [self] (const juce::FileChooser& fc)
                          {
                              if (self == nullptr) return;
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              f = f.withFileExtension ("gbx");
                              self->status.setText (self->proc.saveSetup (f) ? "Saved setup \"" + f.getFileNameWithoutExtension() + "\""
                                                                             : juce::String ("Couldn't save the setup there"),
                                                    juce::dontSendNotification);
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

void GrooveboxEditor::duplicateSelected (int li)
{
    if (li < 0) li = view.selected;
    const int copy = proc.duplicateLane (li);
    if (copy < 0)
    {
        status.setText ("Can't duplicate: all 32 lanes are in use", juce::dontSendNotification);
        return;
    }
    syncRows();
    view.selected = -1;
    selectLane (copy);
    status.setText ("Duplicated lane " + juce::String (li + 1) + " (sample, pattern and every setting). Ctrl+Z undoes it.", juce::dontSendNotification);
}

void GrooveboxEditor::setShowGroove (bool on)
{
    showGroove = on;
    if (on && ! editorExpanded)
    {
        editorExpanded = true;
        editorBtn.setToggleState (true, juce::dontSendNotification);
    }
    grooveView.refresh();
    resized();
}

void GrooveboxEditor::selectLane (int li)
{
    if (showGroove) setShowGroove (false);
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
        rows.back()->onDuplicate = [this] (int li) { duplicateSelected (li); };
        rows.back()->onEdited = [this] { laneEditor.refresh(); updatePageLabel(); };
        laneList.addAndMakeVisible (*rows.back());
        keepFocusOnEditor (*rows.back());
    }
    while ((int) rows.size() > nl)
        rows.pop_back();

    for (int i = 0; i < nl; ++i)
        rows[(size_t) i]->setLane (i);

    view.selected = juce::jlimit (0, nl - 1, view.selected);
    laneEditor.setLane (view.selected);

    genreBox.setSelectedId (proc.gen.genre + 1, juce::dontSendNotification);
    loopBox.setSelectedId (proc.gen.bars, juce::dontSendNotification);
    const int sl = proc.songBars <= 32 ? 32 : proc.songBars <= 64 ? 64 : proc.songBars <= 96 ? 96 : 128;
    songLenBox.setSelectedId (sl, juce::dontSendNotification);
    songBtn.setToggleState (proc.songMode.load(), juce::dontSendNotification);
    bpmBox.setValue (juce::roundToInt (proc.previewBpm.load()), false);
    fitFilterBtn.setToggleState (proc.fitFilter, juce::dontSendNotification);
    hideLoopsBtn.setToggleState (proc.hideLoops, juce::dontSendNotification);
    density.setValue (proc.gen.density, juce::dontSendNotification);
    variation.setValue (proc.gen.variation, juce::dontSendNotification);
    irregular.setValue (proc.gen.irregular, juce::dontSendNotification);
    mutateAmt.setValue (proc.mutateAmount, juce::dontSendNotification);
    grooveSlot.repaint();
    grooveView.refresh();
    if (proc.grooveMessage.isNotEmpty())
    {
        status.setText (proc.grooveMessage, juce::dontSendNotification);
        proc.grooveMessage.clear();
        bpmBox.setValue (juce::roundToInt (proc.previewBpm.load()), false);
        swingScale.updateText();
    }
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

    const int bar = proc.displayBar();
    if (bar != timeline.lastBar)
    {
        timeline.lastBar = bar;
        timeline.repaint();
    }

    if (grooveReanalyzeIn > 0 && --grooveReanalyzeIn == 0)
    {
        grooveReanalyzeIn = -1;
        if (proc.grooveFile.existsAsFile())
        {
            proc.loadGroove (proc.grooveFile, (double) grooveView.bpmBox.getValue());
            status.setText ("Listening to " + proc.grooveFile.getFileName() + " again...", juce::dontSendNotification);
        }
        grooveSlot.repaint();
    }

    ++tickCount;
    if (tickCount % 15 == 0)
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

    if (tickCount % 8 == 0) // ~4x per second: record changes for undo
        proc.checkpoint();

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
    settingsBtn.setBounds (row1.removeFromLeft (72)); row1.removeFromLeft (14);
    genreBox.setBounds (row1.removeFromLeft (190)); row1.removeFromLeft (4);
    kitBtn.setBounds (row1.removeFromLeft (84)); row1.removeFromLeft (4);
    generateBtn.setBounds (row1.removeFromLeft (84)); row1.removeFromLeft (4);
    mutateBtn.setBounds (row1.removeFromLeft (64)); row1.removeFromLeft (4);
    shuffleBtn.setBounds (row1.removeFromLeft (112)); row1.removeFromLeft (4);
    randomBtn.setBounds (row1.removeFromLeft (104));
    master.setBounds (row1.removeFromRight (30)); row1.removeFromRight (8);
    seqBtn.setBounds (row1.removeFromRight (84)); row1.removeFromRight (4);
    previewBtn.setBounds (row1.removeFromRight (52)); row1.removeFromRight (4);
    bpmBox.setBounds (row1.removeFromRight (74));

    settings.setBounds (settingsBtn.getX(), settingsBtn.getBottom() + 4, 560, settings.preferredHeight());

    auto row2 = r.removeFromTop (32).reduced (10, 4);
    loopBox.setBounds (row2.removeFromLeft (104)); row2.removeFromLeft (8);
    songBtn.setBounds (row2.removeFromLeft (52)); row2.removeFromLeft (4);
    songLenBox.setBounds (row2.removeFromLeft (84)); row2.removeFromLeft (4);
    makeSongBtn.setBounds (row2.removeFromLeft (80)); row2.removeFromLeft (12);
    pagePrev.setBounds (row2.removeFromLeft (24)); row2.removeFromLeft (2);
    pageLabel.setBounds (row2.removeFromLeft (120)); row2.removeFromLeft (2);
    pageNext.setBounds (row2.removeFromLeft (24)); row2.removeFromLeft (4);
    followBtn.setBounds (row2.removeFromLeft (60)); row2.removeFromLeft (12);
    grooveSlot.setBounds (row2.removeFromRight (380)); row2.removeFromRight (12);
    timeline.setBounds (row2);

    r.removeFromTop (18); // column captions

    auto statusRow = r.removeFromBottom (26).reduced (10, 2);
    editorBtn.setBounds (statusRow.removeFromRight (96).reduced (0, 1));
    statusRow.removeFromRight (8);
    bpmLabel.setBounds (statusRow.removeFromRight (280));
    status.setBounds (statusRow);

    laneEditor.setVisible (editorExpanded && ! showGroove);
    grooveView.setVisible (editorExpanded && showGroove);
    if (editorExpanded)
    {
        const auto bottom = r.removeFromBottom (300).reduced (8, 4);
        laneEditor.setBounds (bottom);
        grooveView.setBounds (bottom);
    }

    viewport.setBounds (r.reduced (8, 2));
    layoutLaneList();
    repaint();
}
