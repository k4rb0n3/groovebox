#pragma once

#include "PluginProcessor.h"

namespace cg
{
enum class EditMode { Steps, Velocity, Probability, Nudge, Ratchet };

class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (12.5f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (12.5f); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (13.0f); }
};

class StepGrid : public juce::Component
{
public:
    StepGrid (GrooveboxProcessor& p, const EditMode& mode) : proc (p), editMode (mode) {}
    void setLane (int li) { lane = li; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    int lastDrawnPlayStep = -2;

private:
    GrooveboxProcessor& proc;
    const EditMode& editMode;
    int lane = 0;
    bool paintState = true;
    int lastIdx = -1;

    int stepAt (float x) const;
    void applyAt (const juce::MouseEvent&, bool isDown);
    void showMenu();
};

// Drag up/down or scroll to change, double-click to reset.
class NumberBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    NumberBox (int minV, int maxV, int defV) : lo (minV), hi (maxV), def (defV), value (defV) {}
    void setValue (int v, bool notify);
    int getValue() const { return value; }
    std::function<void (int)> onChange;
    juce::Colour accent { 0xfff0b44c };

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragStart = value; }
    void mouseDrag (const juce::MouseEvent& e) override { setValue (dragStart - e.getDistanceFromDragStartY() / 6, true); }
    void mouseDoubleClick (const juce::MouseEvent&) override { setValue (def, true); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    int lo, hi, def, value, dragStart = 0;
    float wheelAcc = 0.0f;
};

class SampleSlot : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit SampleSlot (GrooveboxProcessor& p) : proc (p) {}
    void setLane (int li) { lane = li; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;
    std::function<void()> onChange;

private:
    GrooveboxProcessor& proc;
    int lane = 0;
    float wheelAcc = 0.0f;
    bool dragOver = false;
};

class LaneRow : public juce::Component
{
public:
    LaneRow (GrooveboxProcessor& p, const EditMode& mode);
    void setLane (int li);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick(); // called by the editor timer

    static constexpr int kHeight = 40;

private:
    GrooveboxProcessor& proc;
    int lane = 0;

    juce::TextButton muteBtn { "M" }, soloBtn { "S" }, prevBtn { "<" }, nextBtn { ">" },
                     randBtn { "?" }, folderBtn { "..." }, genBtn { "Gen" }, mutBtn { "Mut" }, removeBtn { "x" };
    juce::ComboBox roleBox, rateBox;
    NumberBox lenSlider { 1, kMaxSteps, 16 };
    juce::Slider level, pan, pitch, decay, swing, hum;
    SampleSlot slot;
    StepGrid grid;
    std::unique_ptr<juce::FileChooser> chooser;
    float meter = 0.0f;

    void setupKnob (juce::Slider&, double min, double max, double def, const juce::String& tip,
                    std::function<void (float)> setter, std::function<juce::String (double)> fmt);
    template <typename Fn> void edit (Fn&& fn);
};

class MidiDragButton : public juce::TextButton
{
public:
    MidiDragButton (GrooveboxProcessor& p) : juce::TextButton ("Drag MIDI"), proc (p) {}
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override { dragging = false; juce::TextButton::mouseUp (e); }

private:
    GrooveboxProcessor& proc;
    bool dragging = false;
};
} // namespace cg

class GrooveboxEditor : public juce::AudioProcessorEditor,
                          public juce::DragAndDropContainer,
                          private juce::Timer
{
public:
    explicit GrooveboxEditor (GrooveboxProcessor&);
    ~GrooveboxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    GrooveboxProcessor& proc;
    cg::Look look;
    cg::EditMode editMode = cg::EditMode::Steps;

    juce::Label title, byline, status, bpmLabel;
    juce::TextButton libraryBtn { "Library..." }, shuffleBtn { "Shuffle samples" },
                     generateBtn { "Generate" }, mutateBtn { "Mutate" }, previewBtn { "Play" },
                     seqBtn { "Sequencer" }, saveMidiBtn { "Save MIDI" }, addLaneBtn { "+ Add lane" };
    cg::MidiDragButton dragMidiBtn;
    juce::ComboBox genreBox, modeBox, barsBox;
    juce::Slider density, variation, irregular, mutateAmt, swingScale, humScale, master;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAtt> swingAtt, humAtt, masterAtt;
    std::unique_ptr<ButtonAtt> seqAtt;

    juce::Viewport viewport;
    juce::Component laneList;
    std::vector<std::unique_ptr<cg::LaneRow>> rows;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 500 };

    int seenVersion = -1;

    void syncRows();
    void layoutLaneList();
    void setupBar (juce::Slider&, const juce::String& name, double def, std::function<void (float)> setter);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrooveboxEditor)
};
