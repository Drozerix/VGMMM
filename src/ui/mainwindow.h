// Main form of the editor (original TFormMain). Control and handler names follow the original
// form so the code can be compared with the decompiled unit; addresses are given at the
// definitions.
#pragma once
#include <QtWidgets>

#include "edit.h"
#include "export.h"
#include "gen/ui_forms.h"
#include "keys.h"
#include "player.h"
#include "song.h"
#include "vgmplay.h"
#include "widgets.h"

enum {
    COL_OUT_OF_RANGE, COL_NORMAL_LINE, COL_FOURTH_LINE, COL_NORMAL_TEXT, COL_INVERTED_TEXT, COL_ACTIVE_LINE,
    COL_INACTIVE_LINE, COL_SELECT_ACTIVE, COL_SELECT_NORMAL, COL_SELECT_FOURTH, COL_SEPARATOR, COL_PREV_NEXT,
    COL_COUNT
};

// Editor state that the original keeps in globals (see UI_NOTES.md)
struct UiState {
    int curRow = 0;
    int curCol = 0;             // channel * 8 + field
    int curPattern = 0;
    int curOrder = 0;
    int octave = 4;
    int curIns = 0;
    int curSample = 0;
    int autoStep = 1;
    int soloChan = -1;
    int samplePending = -1;     // sample whose gain/pitch changed; reconverted by Timer2
    int levelPeak[CHANNELS] = {};
    bool noPattern = false;     // order cursor is beyond the end of the order list
    int savedRow = 0, savedPattern = 0, savedOrder = 0;
    int16_t lastNote = NOTE_EMPTY;
    int8_t lastOctave = 0;
    int8_t lastVolume = 0;
    int lastFx = -1;
    uint8_t lastParam = 0;
    int16_t lastIns = 0;
    int selX1 = 0, selY1 = 0, selX2 = 0, selY2 = 0;     // selection anchor / free corner
    int selChan = 0, selRow = 0, selW = 0, selH = 0;    // normalised selection
    bool selecting = false;
    int charW = 8, rowH = 16;
    int fontSize = 9;
    QString fontName;
    int fxColumn[CHANNELS] = {};
    unsigned colors[COL_COUNT] = {};
    int songTicks = 0, posTicks = 0;
    struct { int key, note; } preview[6];
    bool modified = false;
    QString fileTitle;          // name shown in the title bar
    QString kbdLayout = "Default";
    QString pathInstrument = ".", pathSample = ".", pathModule = ".", pathImport = ".", pathExport = ".";
};
extern UiState U;

// Audio device (audio.cpp). The lock must be held around anything that touches the player or
// the chips from the GUI thread.
bool audio_open(int frames);
void audio_close(void);
void audio_lock(void);
void audio_unlock(void);
struct AudioLock {
    AudioLock() { audio_lock(); }
    ~AudioLock() { audio_unlock(); }
};

// MIDI input (midi.cpp)
QStringList midi_list_inputs(void);
bool midi_open(int index, std::function<void(unsigned msg)> handler);
void midi_close(void);

class MainWindow : public QMainWindow, public Ui_FormMain {
public:
    MainWindow();
    ~MainWindow() override;
    bool loadModule(const QString &filename, QObject *Sender = nullptr);     // 0x41e174
    // VGM playback (an addition of this port)
    static bool isVgmFile(const QString &filename);
    bool playVgm(const QString &filename);
    void stopVgm();
    void updateVgmStatus();
    void FilePlayVgmClick(QObject *Sender = nullptr);

    // ---- handlers named as in the original form
    void FileNewClick(QObject *Sender = nullptr);
    void FileClearSongClick(QObject *Sender = nullptr);
    void FileLoadClick(QObject *Sender = nullptr);
    void FileSaveClick(QObject *Sender = nullptr);
    void FileCloseClick(QObject *Sender = nullptr);
    void Recent0Click(QObject *Sender = nullptr);
    void ImportTFEClick(QObject *Sender = nullptr);
    void ImportMODClick(QObject *Sender = nullptr);
    void ExportVGMGenesisClick(QObject *Sender = nullptr);
    void ExportVGMSMSClick(QObject *Sender = nullptr);
    void ExportRegGGClick(QObject *Sender = nullptr);
    void ExportVGZGenesisClick(QObject *Sender = nullptr);
    void ExportVGZSMSClick(QObject *Sender = nullptr);
    void ExportVGZGGClick(QObject *Sender = nullptr);
    void ExportVGMSMDClick(QObject *Sender = nullptr);
    void ExportTextClick(QObject *Sender = nullptr);
    void ExportWaveAllClick(QObject *Sender = nullptr);
    void EditUndoClick(QObject *Sender = nullptr);
    void EditRedoClick(QObject *Sender = nullptr);
    void EditCutClick(QObject *Sender = nullptr);
    void EditCopyClick(QObject *Sender = nullptr);
    void EditPasteClick(QObject *Sender = nullptr);
    void EditPasteOverClick(QObject *Sender = nullptr);
    void EditDeleteClick(QObject *Sender = nullptr);
    void EditSelectAllClick(QObject *Sender = nullptr);
    void EditUnselectAllClick(QObject *Sender = nullptr);
    void EditCleanupInstrumentsClick(QObject *Sender = nullptr);
    void EditRearrangeInstrumentsClick(QObject *Sender = nullptr);
    void EditRearrangePatternsClick(QObject *Sender = nullptr);
    void BlockExpandClick(QObject *Sender = nullptr);
    void BlockShrinkClick(QObject *Sender = nullptr);
    void BlockVolumeScaleClick(QObject *Sender = nullptr);
    void BlockEffectScaleClick(QObject *Sender = nullptr);
    void BlockVolumeGradientClick(QObject *Sender = nullptr);
    void BlockEffectGradientClick(QObject *Sender = nullptr);
    void BlockDuplicateClick(QObject *Sender = nullptr);
    void BlockAssignInsClick(QObject *Sender = nullptr);
    void BlockTransposeClick(QObject *Sender = nullptr);
    void BlockChanExchangeClick(QObject *Sender = nullptr);
    void PatternExpandClick(QObject *Sender = nullptr);
    void PatternShrinkClick(QObject *Sender = nullptr);
    void InstrumentClearClick(QObject *Sender = nullptr);
    void InstrumentOpenClick(QObject *Sender = nullptr);
    void InstrumentSaveClick(QObject *Sender = nullptr);
    void InstrumentCopyExchangeClick(QObject *Sender = nullptr);
    void InstrumentChangeVolumeClick(QObject *Sender = nullptr);
    void MainMenuHelpClick(QObject *Sender = nullptr);
    void HelpShowClick(QObject *Sender = nullptr);
    void HelpAboutClick(QObject *Sender = nullptr);
    void PopupMenuOrdPopup(QObject *Sender = nullptr);
    void OrderSetLoopPosClick(QObject *Sender = nullptr);
    void OrderMovePrevClick(QObject *Sender = nullptr);
    void OrderMoveNextClick(QObject *Sender = nullptr);
    void OrderInsertSameClick(QObject *Sender = nullptr);
    void OrderInsertNewClick(QObject *Sender = nullptr);
    void OrderInsertCopyClick(QObject *Sender = nullptr);
    void OrderDeletePosClick(QObject *Sender = nullptr);
    void PtnPopupAssignInsClick(QObject *Sender = nullptr);
    void PtnPopupOctaveUpClick(QObject *Sender = nullptr);
    void PtnPopupOctaveDownClick(QObject *Sender = nullptr);
    void PtnPopupSemitoneUpClick(QObject *Sender = nullptr);
    void PtnPopupSemiToneDownClick(QObject *Sender = nullptr);
    void SpeedButtonModNewClick(QObject *Sender = nullptr);
    void SpeedButtonModOpenClick(QObject *Sender = nullptr);
    void SpeedButtonModSaveClick(QObject *Sender = nullptr);
    void SpeedButtonPlayAllClick(QObject *Sender = nullptr);
    void SpeedButtonPlayPosClick(QObject *Sender = nullptr);
    void SpeedButtonPlayPtnClick(QObject *Sender = nullptr);
    void SpeedButtonStopClick(QObject *Sender = nullptr);
    void SpeedButtonResetClick(QObject *Sender = nullptr);
    void SpeedButtonFollowOrderClick(QObject *Sender = nullptr);
    void SpeedButtonSpdEqualClick(QObject *Sender = nullptr);
    void SpeedButtonAStepClick(QObject *Sender = nullptr);
    void SpeedButtonAVolClick(QObject *Sender = nullptr);
    void EditPtnChange(QObject *Sender = nullptr);
    void EditPtnLenChange(QObject *Sender = nullptr);
    void EditSpdEvenChange(QObject *Sender = nullptr);
    void EditSpdOddChange(QObject *Sender = nullptr);
    void EditInterChange(QObject *Sender = nullptr);
    void EditOctaveChange(QObject *Sender = nullptr);
    void EditAStepChange(QObject *Sender = nullptr);
    void EditAInsChange(QObject *Sender = nullptr);
    void EditAVolChange(QObject *Sender = nullptr);
    void SpeedButtonInsNewClick(QObject *Sender = nullptr);
    void SpeedButtonInsOpenClick(QObject *Sender = nullptr);
    void SpeedButtonInsSaveClick(QObject *Sender = nullptr);
    void SpeedButtonInsExchClick(QObject *Sender = nullptr);
    void SpeedButtonOpExchClick(QObject *Sender = nullptr);
    void SpeedButtonResetOp1Click(QObject *Sender = nullptr);
    void EditInsNameChange(QObject *Sender = nullptr);
    void SpeedButtonSampleClearClick(QObject *Sender = nullptr);
    void SpeedButtonSampleLoadClick(QObject *Sender = nullptr);
    void SpeedButtonSampleExchangeClick(QObject *Sender = nullptr);
    void SpeedButtonSampleTestClick(QObject *Sender = nullptr);
    void SpeedButtonRate8000Click(QObject *Sender = nullptr);
    void EditSampleNameChange(QObject *Sender = nullptr);
    void EditTrkNameChange(QObject *Sender = nullptr);
    void EditTrkAuthorChange(QObject *Sender = nullptr);
    void MemoTrkMessageChange(QObject *Sender = nullptr);
    void SpeedButtonClockPALClick(QObject *Sender = nullptr);
    void SpeedButtonClockNTSCClick(QObject *Sender = nullptr);
    void SpeedButtonClockCustomClick(QObject *Sender = nullptr);
    void ComboBoxBufCountChange(QObject *Sender = nullptr);
    void TrackBarGlobalVolChange(QObject *Sender = nullptr);
    void SpeedButtonTestSignalClick(QObject *Sender = nullptr);
    void EditFontSizeChange(QObject *Sender = nullptr);
    void ComboBoxFontChange(QObject *Sender = nullptr);
    void ComboBoxColorCtrlChange(QObject *Sender = nullptr);
    void SpeedButtonColorCtrlClick(QObject *Sender = nullptr);
    void SpeedButtonColorsDefaultClick(QObject *Sender = nullptr);
    void ComboBoxShowPrevNextChange(QObject *Sender = nullptr);
    void CheckBoxPtnNoteDotsClick(QObject *Sender = nullptr);
    void EditRowHighlightChange(QObject *Sender = nullptr);
    void EditPageStepChange(QObject *Sender = nullptr);
    void CheckBoxVertSlidersClick(QObject *Sender = nullptr);
    void ButtonRegVGEClick(QObject *Sender = nullptr);
    void ButtonUnregVGEClick(QObject *Sender = nullptr);
    void CheckBoxOneInstanceClick(QObject *Sender = nullptr);
    void CheckBoxDefaultInsClick(QObject *Sender = nullptr);
    void SpeedButtonKBDLoadClick(QObject *Sender = nullptr);
    void SpeedButtonKBDDefaultClick(QObject *Sender = nullptr);
    void ComboBoxMidiChange(QObject *Sender = nullptr);
    void Timer1Timer(QObject *Sender = nullptr);
    void Timer2Timer(QObject *Sender = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;
    void closeEvent(QCloseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    // ---- setup (mainwindow.cpp)
    void layoutForm();
    void loadSettings();
    void saveSettings();
    void loadColors(bool fromSettings);         // 0x41ebf4
    void fillFontList();                        // 0x41f0b4
    void fillMidiList(int device);              // 0x41b83c
    void loadKeyboardLayout(const QString &name);   // 0x41e480
    void audioOpen(bool chips);                 // 0x41ee48
    void audioClose(bool chips);                // 0x41eec8
    void setTitle(const QString &name = QString());     // 0x41d8d0
    void addRecent(const QString &name);        // 0x41d4fc
    int messageBox(const QString &text, const QString &title, int type);    // 0x41bba0
    void backupModule(const QString &filename); // 0x41f4e4
    void exportVgm(QObject *Sender, int system, bool gzip);     // 0x41f754
    QString exportName(const QString &ext) const;
    void stopAndReset(QObject *Sender);         // 0x41dd50
    void resetUi();                             // 0x41c218
    void refreshAll();                          // 0x41bbc4
    void updateInfoPage();                      // 0x41bc3c
    void updateClockInfo();                     // 0x41c340
    void updateSpeedControls();                 // 0x41c1dc
    void updateBpm();                           // 0x41f394
    void syncFromPlayer();                      // 0x41dbd4
    void stopPlayback();                        // 0x41debc
    void startPlayback(int order, int loopPattern);
    void setNoPattern(bool on);                 // 0x41dd90
    void restorePosition();                     // 0x4164fc
    void midiMessage(unsigned msg);             // 0x41b4c0

    // ---- undo (edit_actions.cpp)
    struct UndoEntry {
        int type = 0;
        QByteArray data;
        int pattern = 0;
        int curRow = 0, curCol = 0, curPattern = -1, curOrder = 0;
        int page = 0;
        QPointer<QWidget> focus;
    };
    enum { UNDO_MAX = 1000 };
    QVector<UndoEntry> undoRing;
    int undoPos = 0, undoCount = 0, redoCount = 0;
    void undoReset();                           // 0x412d2c
    void undoCommit(bool withCursor);           // 0x412e24
    void undoSwap(int idx);                     // 0x412bd4
    void undoRestoreCursor(int idx);            // 0x412f7c
    bool undoStep();                            // 0x413390
    bool redoStep();                            // 0x413448

public:
    void undoPushPattern(int pat);              // 0x413000
    void undoPushInstruments();                 // 0x4130c0
    void undoPushSamples();                     // 0x413154
    void undoPushOrders();                      // 0x4131e8
    void undoPushHeader();                      // 0x41327c
    void undoPushSong();                        // 0x413310

    // ---- pattern editor (ptn_view.cpp, ptn_input.cpp)
    void patternRedraw() { PaintBoxPtn->update(); }
    void paintPattern(QPainter &p);             // 0x41653c
    void paintPatternPreview(QPainter &p);      // 0x424fb8
    void createPatternHeader();                 // 0x41ac1c
    void layoutPatternHeader();                 // 0x41b010
    void headerButtonClick(int idx);            // 0x41b200
    void applyMute();                           // 0x41b090
    void cursorColumnChanged();                 // 0x41b0d4
    void updatePatternHeader();                 // 0x41b150
    void scrollToCursorChannel();               // 0x418fb4
    void updateFont();
    void setCurrentPattern(int pat);            // 0x4164bc
    void savePosition();                        // 0x416498
    void updateOrderList();                     // 0x41bfe4
    void updateStatus(bool calcTotal, bool calcPos);    // 0x41be08
    int songLength();                           // 0x41e848
    bool mouseToCell(int x, int y, int *chan, int *row);    // 0x41d468
    void mouseSetCursor(int x, int y);          // 0x41da90
    void PaintBoxPtnMouseDown(QMouseEvent *e);
    void PaintBoxPtnMouseMove(QMouseEvent *e);
    void PaintBoxPtnMouseUp(QMouseEvent *e);
    int hexKeyPressed(int field);               // 0x418b40
    int noteKeyPressed(bool repeat);            // 0x418cd8
    void selectionSetEnd(int chan, int row);    // 0x418c00
    void selectionFollowCursor();               // 0x418cac
    void cursorMoveRows(int delta, bool clamp); // 0x418db8
    void playFromCursor(bool loopPattern);
    bool patternKeys();                         // 0x419008
    void blockTransposeQuick(int semitones);    // 0x41dde8
    void orderKeyDown(int vk);                  // 0x421754
    void orderMouseDown(int col);               // 0x426c00
    void orderDrop(int from, int to);           // 0x426740
    void audioLock() { audio_lock(); }
    void audioUnlock() { audio_unlock(); }

    // ---- instruments and samples (ins_view.cpp)
    void createKnobs();                         // 0x415224
    void knobChanged(int tag, int value);       // 0x414fec
    void updateInstrumentControls();            // 0x414d00
    void updateInsList(int idx, bool setName);  // 0x414404
    void updateSampleList(int idx, bool setName);   // 0x415b70
    void updateSampleInfo();                    // 0x415d74
    void setInsName(int idx, const QString &s); // 0x415abc
    void setSampleName(int idx, const QString &s);  // 0x41610c
    void applyOperatorCopy();                   // 0x41590c
    void paintEnvelope(QPainter &p, int op, const QSize &size);     // 0x41460c
    void paintKeys(QPainter &p);                // 0x413f24
    void paintSample(QPainter &p);              // 0x4161e4
    void paintLevels(QPainter &p);              // 0x41eee4 (drawing part)
    void updateLevels();
    int keysNoteAt(int x, int y);               // 0x413db0
    void previewStart(int key, int note, int slot);     // 0x4135b4
    bool previewKeyDown(int key, int note, bool poly);  // 0x413a88
    bool previewSlide(int note, bool flag);     // 0x413b74
    bool previewKeyUp(int key, int note);       // 0x413c7c
    bool previewReleaseAll();                   // 0x413d50
    void setAlgorithm(int alg);                 // 0x414f50
    void setSsgEg(int value, int op);           // 0x414f9c
    bool previewFm() const { return SpeedButtonFMPoly->isChecked() || SpeedButtonFMMono->isChecked(); }
    void selectInstrument(int idx);
    void selectSample(int idx);
    void playKeyReleased();
    bool pickerActive() const { return picker != nullptr; }
    // "Follow song" (an addition of this port) on: the editor follows the playing song;
    // off: the cursor stays where the user puts it
    bool following() const { return SpeedButtonFollowSong->isChecked(); }
    QToolButton *SpeedButtonFollowSong = nullptr;
    bool tracking() const { return Timer1->isActive() && following(); }

    Knob *knobs[39] = {};
    Knob *knobGain = nullptr, *knobPitch = nullptr;
    QToolButton *hdr[42] = {};
    QPixmap pixKeys, pixAlgo, pixEG, pixAM, pixKnob;
    QAction *recent[10] = {};
    QByteArray winGeometry;
    bool playKeyStarted = false;
    bool updating = false;      // set while the code itself changes controls
    bool closing = false;
    QString currentFile;        // SaveDialogMod->FileName
    PickerPopup *picker = nullptr;
    int pickerOp = 0;
    int envDragOp = -1;
    Qt::MouseButton envDragButton = Qt::NoButton;
    bool levelsClipped = false;
    float levelNow[CHANNELS] = {};
    int midiDevice = 0;
    int playRow = -1, playOrder = -1;   // song position shown while Follow-mode is off
    bool vgmMode = false;       // a VGM file is playing instead of the module
    QString vgmFile;
    QAction *FilePlayVgm = nullptr;
};

extern MainWindow *g_mainWindow;
