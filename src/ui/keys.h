// Keyboard layout (.kbd) handling. The original works with Windows virtual-key codes and its
// own key-state table; both are kept so layout files remain compatible.
#pragma once

class QKeyEvent;

struct KeyActions {
    int note[36];
    int octave[8];
    int noteOff;
    int genOpenMod, genSaveMod, genPlayAll, genPlayCur, genPlayPtn, genStop, genReset;
    int genMute[10];
    int genMuteAll, genSoloMode, genToPtnEdit, genToInsEdit, genToSampleEdit, genToInfo, genUndo, genRedo;
    int ptnSelectAll, ptnSelectChn, ptnUnselect, ptnCut, ptnCopy, ptnPaste, ptnPasteOver, ptnDelete, ptnToOrder;
    int ptnTransSemiUp, ptnTransSemiDown, ptnTransOctUp, ptnTransOctDown, ptnAssignInstr;
    int ptnAutoStepInc, ptnAutoStepDec;
    int ptnAutoStep[10];
    int ptnCurUp, ptnCurDown, ptnCurLeft, ptnCurRight, ptnPageUp, ptnPageDown, ptnHome, ptnEnd;
    int ptnOrdPrev, ptnOrdNext, ptnNextChan, ptnPrevChan;
    int ptnDelNote, ptnDelLine, ptnInsNote, ptnInsLine, ptnPlay, ptnPlayLoop;
    int ptnClrField, ptnClrNote, ptnClrLine, ptnSelectKey;
    int ptnPatternNext, ptnPatternPrev, ptnInstrNext, ptnInstrPrev, ptnInpRepeat, ptnPlayNote, ptnPlayLine;
    int ptnPrevFxColumn, ptnNextFxColumn, ptnEditSong;
    int ordInsertSame, ordInsertCopy, ordInsertNew, ordDelete, ordLoopPosition;
    int ordPatternInc, ordPatternDec, ordToPattern, ordMovePrev, ordMoveNext;
};

extern KeyActions K;
extern unsigned char g_keyState[256];   // 0 up, 1 just pressed, 2 held (auto-repeating)

// text != null: parse the given layout text of `size` bytes, otherwise read the file
bool kbd_load(const char *filename, const char *text, unsigned size);
const char *kbd_layout_name(void);
void kbd_update(void);                  // 0x412264: recompute action states from g_keyState
void kbd_clear(void);                   // 0x412510: all keys up
bool kbd_pressed(int action);           // state == 1
bool kbd_held(int action);              // state != 0
int qtKeyToVk(const QKeyEvent *e);
