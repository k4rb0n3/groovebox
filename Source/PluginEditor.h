#pragma once

#include "PluginProcessor.h"

namespace cg
{
// Shared view state: which lane is selected, which page of long lanes is shown.
struct ViewState
{
    int selected = 0;
    int page = 0;
    bool follow = true;
};

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

// Padlock toggle.
class LockButton : public juce::Button
{
public:
    LockButton() : juce::Button ("lock") {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
    std::function<void()> onRightClick;
    void clicked (const juce::ModifierKeys& m) override
    {
        if (m.isPopupMenu()) { if (onRightClick) onRightClick(); }
        else juce::Button::clicked (m);
    }
};

// Step on/off row (velocity shown as bar height). Used small in each lane and large in the lane editor.
class StepGrid : public juce::Component
{
public:
    StepGrid (GrooveboxProcessor& p, const ViewState& v) : proc (p), view (v) {}
    void setLane (int li) { lane = li; repaint(); }
    void setLabel (const juce::String& l, int width) { label = l; labelW = width; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    int lastDrawnPlayStep = -2;
    std::function<void()> onTouched, onEdited;

private:
    GrooveboxProcessor& proc;
    const ViewState& view;
    int lane = 0;
    bool paintState = true;
    int lastIdx = -1;
    juce::String label;
    int labelW = 0;

    int stepAt (float x) const; // absolute step index, -1 outside
    void applyAt (const juce::MouseEvent&, bool isDown);
    void showMenu();
};

// One editable row of step values (velocity, pitch, pan...) in the lane editor.
enum class Param { Velocity, Probability, Pitch, Pan, Cutoff, Start, Nudge, Ratchet, Count };

class ParamStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamStrip (GrooveboxProcessor& p, const ViewState& v, Param prm);
    void setLane (int li) { lane = li; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    static constexpr int kLabelW = 86;
    std::function<void()> onEdited;

private:
    GrooveboxProcessor& proc;
    const ViewState& view;
    Param param;
    int lane = 0, lastIdx = -1;
    float lastV = 0.0f;

    int stepAt (float x) const;
    float valueFromY (float y) const;
    void setFrom01 (Step&, float v) const;
    float get01 (const Step&) const;
    bool bipolar() const { return param == Param::Pitch || param == Param::Pan || param == Param::Nudge; }
    juce::String text (const Step&) const;
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
    std::function<juce::String (int)> format;
    std::function<int (int value, int direction)> stepper; // custom increments (e.g. whole bars)
    juce::Colour accent { 0xfff0b44c };

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragStart = value; dragSteps = 0; }
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { setValue (def, true); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    int lo, hi, def, value, dragStart = 0, dragSteps = 0;
    float wheelAcc = 0.0f;
    int stepFrom (int v, int dir) const { return stepper ? stepper (v, dir) : v + dir; }
};

class SampleSlot : public juce::Component, public juce::FileDragAndDropTarget, public juce::TooltipClient
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
    juce::String getTooltip() override;
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
    LaneRow (GrooveboxProcessor& p, ViewState& v);
    void setLane (int li);
    int getLane() const { return lane; }
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick(); // called by the editor timer
    void repaintSlot() { slot.repaint(); }
    void repaintGrid() { grid.repaint(); }

    std::function<void (int)> onSelect, onDuplicate;
    std::function<void()> onEdited;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> captions() const;

    static constexpr int kHeight = 40;

private:
    GrooveboxProcessor& proc;
    ViewState& view;
    int lane = 0;
    bool resting = false; // song mode: this lane sits out the current section
    bool pianoRoll = false;

    juce::TextButton muteBtn { "M" }, soloBtn { "S" }, prevBtn { "<" }, nextBtn { ">" },
                     randBtn { "?" }, folderBtn { "..." }, genBtn { "Gen" }, mutBtn { "Mut" }, removeBtn { "x" }, dupBtn { "+" };
    LockButton lockSampleBtn, lockPatternBtn;
    juce::ComboBox roleBox, rateBox;
    NumberBox lenSlider { 1, kMaxSteps, 16 };
    juce::Slider level, pan, pitch, cutoff, decay, swing, hum;
    SampleSlot slot;
    StepGrid grid;
    std::unique_ptr<juce::FileChooser> chooser;
    float meter = 0.0f;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAtt> levelAtt, panAtt, pitchAtt, cutoffAtt;

    void setupKnob (juce::Slider&, double min, double max, double def, const juce::String& tip,
                    std::function<void (float)> setter, std::function<juce::String (double)> fmt);
    template <typename Fn> void edit (Fn&& fn);
};

// A button you drag out of Groovebox: on drag it makes files and hands them to FL (or Explorer).
class FileDragButton : public juce::TextButton
{
public:
    explicit FileDragButton (const juce::String& text) : juce::TextButton (text) {}
    std::function<juce::StringArray()> makeFiles;
    std::function<void (const juce::StringArray&)> onDragged;
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override { dragging = false; juce::TextButton::mouseUp (e); }

private:
    bool dragging = false;
};

// Bottom panel: the selected lane's sequence, big, with a row per step parameter, plus lane settings.
class LaneEditor : public juce::Component
{
public:
    LaneEditor (GrooveboxProcessor& p, ViewState& v);
    void setLane (int li);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick();
    std::function<void()> onEdited;
    std::function<void (int)> onDuplicate;
    std::function<void (const juce::String&)> onStatus;

private:
    GrooveboxProcessor& proc;
    ViewState& view;
    int lane = 0;
    void showMixerHelp();

    juce::Label title, fitLabel, hint;
    juce::ComboBox filterBox, outBox, chokeBox;
    juce::Slider reso, attack;
    juce::TextButton reverseBtn { "Reverse" }, mixerHelpBtn { "How?" }, separateBtn { "Separate mixer tracks" }, dupLaneBtn { "Duplicate lane" };
    FileDragButton sampleDrag { "Sample  >  Channel Rack" }, notesDrag { "Notes  >  Piano roll" }, allDrag { "All lanes  >  Channel Rack" };
    StepGrid steps;
    std::vector<std::unique_ptr<ParamStrip>> strips;

    template <typename Fn> void edit (Fn&& fn);
};

// Song arrangement strip with the playhead.
class SongTimeline : public juce::Component
{
public:
    explicit SongTimeline (GrooveboxProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { seekFrom (e); }
    void mouseDrag (const juce::MouseEvent& e) override { seekFrom (e); }
    std::function<void (const juce::String&)> onStatus;
    int lastBar = -2;

private:
    GrooveboxProcessor& proc;
    void seekFrom (const juce::MouseEvent&);
};

// Drop a reference loop or track here: Groovebox extracts its groove.
class GrooveSlot : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    explicit GrooveSlot (GrooveboxProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;
    void choose();
    std::function<double()> forcedBpm;      // reference BPM box (0 = detect)
    std::function<void (const juce::String&)> onStatus;
    std::function<void()> onOpen;           // show the big groove panel

private:
    GrooveboxProcessor& proc;
    bool dragOver = false;
    std::unique_ptr<juce::FileChooser> chooser;
    void load (const juce::File&);
};

// Bottom panel for the reference groove: big view of what was found, and ways to use it.
class GrooveView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit GrooveView (GrooveboxProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;
    std::function<void()> onBack, onLoad;
    std::function<void (const juce::String&)> onStatus;
    juce::Slider amount;
    NumberBox bpmBox { 0, 300, 0 };
    juce::TextButton loadBtn { "Load reference..." }, clearBtn { "Clear" }, copyBtn { "Copy groove to lanes" },
                     timingBtn { "Copy timing only" }, backBtn { "Back to lane" };

private:
    GrooveboxProcessor& proc;
    juce::Rectangle<int> gridArea;
};

// Full-window help screen.
class HelpOverlay : public juce::Component
{
public:
    HelpOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }

private:
    juce::TextEditor text;
    juce::TextButton closeBtn { "Close" };
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
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    GrooveboxProcessor& proc;
    cg::Look look;
    cg::ViewState view;

    juce::Label title, byline, status, bpmLabel, pageLabel;
    juce::TextButton helpBtn { "Help" }, setupsBtn { "Setups" }, libraryBtn { "Library..." }, shuffleBtn { "Shuffle samples" },
                     fitFilterBtn { "Fit filter" }, generateBtn { "Generate" }, mutateBtn { "Mutate" }, previewBtn { "Play" },
                     seqBtn { "Sequencer" }, addLaneBtn { "+ Add lane" },
                     songBtn { "Song" }, makeSongBtn { "Make song" }, pagePrev { "<" }, pageNext { ">" }, followBtn { "Follow" },
                     editorBtn { "Lane editor" };
    juce::ComboBox genreBox, loopBox, songLenBox;
    juce::Slider density, variation, irregular, mutateAmt, swingScale, humScale, master;
    cg::NumberBox bpmBox { 40, 300, 124 };

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAtt> swingAtt, humAtt, masterAtt;
    std::unique_ptr<ButtonAtt> seqAtt;

    juce::Viewport viewport;
    juce::Component laneList;
    std::vector<std::unique_ptr<cg::LaneRow>> rows;
    cg::LaneEditor laneEditor;
    cg::SongTimeline timeline;
    cg::GrooveSlot grooveSlot;
    cg::GrooveView grooveView;
    bool showGroove = false;
    void setShowGroove (bool);
    int grooveReanalyzeIn = -1; // ticks until the reference is analyzed again at the new BPM
    cg::HelpOverlay help;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 500 };
    bool editorExpanded = true;

    int seenVersion = -1, seenAnalysis = -1, tickCount = 0;

    void syncRows();
    void layoutLaneList();
    void selectLane (int li);
    void duplicateSelected (int lane = -1);
    void setPage (int p);
    void updatePageLabel();
    void repaintGrids();
    void showSetupsMenu();
    void saveSetupAs();
    void doUndo (bool redo);
    static void keepFocusOnEditor (juce::Component&);
    void setupBar (juce::Slider&, const juce::String& name, double def, std::function<void (float)> setter);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrooveboxEditor)
};
