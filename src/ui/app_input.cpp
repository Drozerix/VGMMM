// Application-wide input handling: the original hooks Application.OnMessage (0x41c5b0) and
// sees every key message before the focused control does. Here an application event filter
// does the same for key events on their way to the main window.
#include "mainwindow.h"

enum { WM_KEYDOWN = 0x100, WM_KEYUP = 0x101 };

// order in which Left/Right walk through the instrument knobs (VCL tab order)
static const int kKnobOrder[39] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  36, 37, 38, 9,  10, 11, 12, 13, 14, 15, 16,
                                   17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35};

static bool handleAppKey(MainWindow *w, int msg, int vk);

bool MainWindow::eventFilter(QObject *obj, QEvent *ev)
{
    if (closing)
        return false;
    switch (ev->type()) {
    case QEvent::Resize:
        if (obj == TabSheetPtn)
            layoutForm();
        break;
    case QEvent::MouseButtonPress:
        if (obj == ShapeColorCtrl) {
            SpeedButtonColorCtrlClick();
            return true;
        }
        break;
    case QEvent::MouseButtonRelease: {
        QWidget *f = QApplication::focusWidget();
        if (static_cast<QMouseEvent *>(ev)->button() == Qt::RightButton && obj->isWidgetType() &&
            (f == ScrollBoxKeys || f == PaintBoxKeys)) {
            if (previewKeyUp(0xff, -1))
                PaintBoxKeys->update();
        }
        break;
    }
    case QEvent::WindowDeactivate:
    case QEvent::ApplicationDeactivate:
        kbd_clear();
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        QKeyEvent *ke = static_cast<QKeyEvent *>(ev);
        if (obj->isWidgetType()) {
            // snapshots for undo are taken before the edit controls see the key
            if (ev->type() == QEvent::KeyPress && !ke->text().isEmpty()) {
                if (obj == EditInsName)
                    undoPushInstruments();
                else if (obj == EditSampleName)
                    undoPushSamples();
                else if (obj == EditTrkName || obj == EditTrkAuthor || obj == MemoTrkMessage)
                    undoPushHeader();
                // no incremental search in the instrument and sample lists (0x424fb4)
                if ((obj == ListBoxIns || obj == ListBoxSample) && ke->key() != Qt::Key_Tab)
                    return true;
            }
            break;
        }
        if (!obj->isWindowType() || obj != windowHandle())
            break;
        if (QApplication::activeModalWidget() != nullptr || QApplication::activePopupWidget() != nullptr) {
            kbd_clear();
            break;
        }
        if (ev->type() == QEvent::KeyRelease && ke->isAutoRepeat())
            return true;
        int vk = qtKeyToVk(ke);
        if (vk == 0)
            break;
        // keep the modifier state honest if a press or release was missed (focus changes, grabs)
        if (vk != 0x10 && vk != 0x11 && vk != 0x12) {
            Qt::KeyboardModifiers m = ke->modifiers();
            const struct { Qt::KeyboardModifier mod; int vk; } mods[3] = {
                {Qt::ShiftModifier, 0x10}, {Qt::ControlModifier, 0x11}, {Qt::AltModifier, 0x12}};
            bool changed = false;
            for (const auto &e : mods) {
                bool down = (m & e.mod) != 0;
                if (down != (g_keyState[e.vk] != 0)) {
                    g_keyState[e.vk] = down ? 1 : 0;
                    changed = true;
                }
            }
            if (changed)
                kbd_update();
        }
        if (handleAppKey(this, ev->type() == QEvent::KeyPress ? WM_KEYDOWN : WM_KEYUP, vk))
            return true;
        break;
    }
    default:
        break;
    }
    return false;
}

static int knobIndex(MainWindow *w, QWidget *f)
{
    for (int i = 0; i < 39; i++)
        if (f == w->knobs[i])
            return i;
    return -1;
}

// 0x41c5b0. Returns true when the message must not reach the focused control.
static bool handleAppKey(MainWindow *w, int msg, int vk)
{
    QWidget *focus = QApplication::focusWidget();
    bool handled = false;
    const bool playKey = kbd_held(K.ptnPlay) || kbd_held(K.ptnPlayLoop);
    const int knob = knobIndex(w, focus);

    if (msg == WM_KEYDOWN) {
        if (vk == 0x20 && w->PageControlMain->currentWidget() == w->TabSheetSample && focus != w->EditSampleName) {
            w->SpeedButtonSampleTestClick();
            handled = true;
        }
        if (vk == 9 && knob >= 0) {
            // Tab moves to the same parameter of the next operator
            int target;
            if (!(QApplication::keyboardModifiers() & Qt::ShiftModifier)) {
                target = knob + 9;
                if (target > 0x23)
                    target = knob - 0x1b;
            } else {
                target = knob - 9;
                if (target < 0)
                    target = knob + 0x1b;
            }
            if (target >= 0 && target < 39)
                w->knobs[target]->setFocus();
            handled = true;
        }
        if ((vk == 0x25 || vk == 0x27) && knob >= 0) {
            int pos = 0;
            for (int i = 0; i < 39; i++)
                if (kKnobOrder[i] == knob)
                    pos = i;
            pos = (pos + (vk == 0x27 ? 1 : 38)) % 39;
            w->knobs[kKnobOrder[pos]]->setFocus();
            handled = true;
        }
    } else if (msg == WM_KEYUP) {
        if (knob >= 0 && w->knobs[knob]->handlesKey(vk))
            if (w->previewReleaseAll())
                w->PaintBoxKeys->update();
    }

    if (msg == WM_KEYDOWN) {
        if (g_keyState[vk & 0xff] < 2)
            g_keyState[vk & 0xff]++;
        kbd_update();
    } else {
        g_keyState[vk & 0xff] = 0;
        kbd_update();
    }

    if (msg == WM_KEYDOWN) {
        for (int i = 0; i < 10; i++) {
            if (kbd_pressed(K.ptnAutoStep[i])) {
                w->SpeedButtonAStep->setChecked(i != 0);
                w->UpDownAStep->setEnabled(true);
                w->UpDownAStep->setValue(i);
                U.autoStep = i;
                w->UpDownAStep->setEnabled(w->SpeedButtonAStep->isChecked());
                handled = true;
            }
        }
        for (int i = 0; i < 10; i++) {
            if (kbd_pressed(K.genMute[i])) {
                w->hdr[i * 4 + 1]->toggle();
                w->headerButtonClick(i * 4 + 1);
                handled = true;
            }
        }
        if (kbd_pressed(K.genSaveMod)) {
            kbd_clear();
            w->SpeedButtonModSaveClick();
            handled = true;
        }
        if (kbd_pressed(K.genOpenMod)) {
            kbd_clear();
            w->SpeedButtonModOpenClick();
            handled = true;
        }
        if (kbd_pressed(K.genMuteAll)) {
            w->hdr[0]->toggle();
            w->headerButtonClick(0);
            handled = true;
        }
        if (kbd_pressed(K.genSoloMode)) {
            w->headerButtonClick((U.curCol / 8) * 4 + 2);
            handled = true;
        }
        if (kbd_pressed(K.genReset)) {
            if (w->SpeedButtonReset->isEnabled())
                w->SpeedButtonResetClick();
            handled = true;
        }
        if (kbd_pressed(K.genStop)) {
            if (w->SpeedButtonStop->isEnabled())
                w->SpeedButtonStopClick();
            handled = true;
        }
        if (kbd_pressed(K.genToPtnEdit)) {
            w->PageControlMain->setCurrentWidget(w->TabSheetPtn);
            handled = true;
        }
        if (kbd_pressed(K.genToInsEdit)) {
            w->PageControlMain->setCurrentWidget(w->TabSheetIns);
            handled = true;
        }
        if (kbd_pressed(K.genToSampleEdit)) {
            w->PageControlMain->setCurrentWidget(w->TabSheetSample);
            handled = true;
        }
        if (kbd_pressed(K.genToInfo)) {
            w->PageControlMain->setCurrentWidget(w->TabSheetInfo);
            handled = true;
        }
        if (kbd_pressed(K.genPlayAll)) {
            if (w->SpeedButtonPlayAll->isEnabled())
                w->SpeedButtonPlayAllClick();
            handled = true;
        }
        if (kbd_pressed(K.genPlayCur)) {
            if (w->SpeedButtonPlayPos->isEnabled())
                w->SpeedButtonPlayPosClick();
            handled = true;
        }
        if (kbd_pressed(K.genPlayPtn)) {
            if (w->SpeedButtonPlayPtn->isEnabled())
                w->SpeedButtonPlayPtnClick();
            handled = true;
        }
        if (kbd_held(K.genUndo)) {
            w->EditUndoClick();
            handled = true;
        }
        if (kbd_held(K.genRedo)) {
            w->EditRedoClick();
            handled = true;
        }
    } else {
        // playing with the "play from cursor" keys lasts only while the key is held
        if (!U.noPattern && playKey && !kbd_held(K.ptnPlay) && !kbd_held(K.ptnPlayLoop)) {
            bool playing;
            {
                AudioLock lock;
                playing = player_is_playing();
            }
            if (playing)
                w->playKeyReleased();
        }
    }
    focus = QApplication::focusWidget();

    if (!handled) {
        const bool inKnobs = knobIndex(w, focus) >= 0;
        bool notes = inKnobs;
        if (focus == w->ListBoxIns || focus == w->ScrollBoxKeys)
            notes = true;
        if (Knob::active != nullptr || w->pickerActive())
            notes = true;
        if (notes) {
            // digits typed into a knob are values, not notes
            bool allow = !(inKnobs && msg == WM_KEYDOWN && vk > 0x2f && vk < 0x3a);
            if (msg == WM_KEYDOWN) {
                for (int i = 0; i < 8; i++) {
                    if (kbd_pressed(K.octave[i])) {
                        w->UpDownOctave->setValue(i + 1);
                        w->PaintBoxKeys->update();
                        handled = true;
                    }
                }
                int n = w->noteKeyPressed(false);
                if (n >= 0 && n < 0x24 && allow) {
                    int note = U.octave * 12 + n - 12;
                    if (note < 0)
                        note = 0;
                    if (note > 0x5f)
                        note = 0x5f;
                    if (w->previewKeyDown(n, note, w->SpeedButtonFMPoly->isChecked()))
                        w->PaintBoxKeys->update();
                    handled = true;
                }
            } else {
                bool released = false;
                for (int i = 0; i < 0x24; i++) {
                    if (!kbd_held(K.note[i])) {
                        if (w->previewKeyUp(i, -1))
                            released = true;
                    }
                }
                if (released) {
                    handled = true;
                    w->PaintBoxKeys->update();
                }
            }
        }
        if (focus == w->ScrollBoxPtn) {
            if (msg == WM_KEYDOWN) {
                if (w->patternKeys()) {
                    w->patternRedraw();
                    handled = true;
                }
            } else {
                if (!kbd_held(K.ptnSelectKey))
                    U.selecting = false;
                if (w->CheckBoxPtnKeyoff->isChecked()) {
                    bool none = true;
                    for (int i = 0; i < 0x24; i++)
                        if (kbd_held(K.note[i])) {
                            none = false;
                            break;
                        }
                    if (none) {
                        // 0x41d414: key off on all FM channels of the playback chip
                        static const uint8_t chanReg[6] = {0, 1, 2, 4, 5, 6};
                        AudioLock lock;
                        for (int i = 0; i < 6; i++) {
                            chips_ym_write(0, 0x28);
                            chips_ym_write(1, chanReg[i]);
                        }
                    }
                }
            }
        }
        if (focus == w->StringGridOrd && msg == WM_KEYDOWN) {
            w->orderKeyDown(vk);
            handled = true;
        }
    }
    return handled;
}

// key-up part of 0x41c5b0
void MainWindow::playKeyReleased()
{
    syncFromPlayer();
    if (!Timer1->isActive()) {
        AudioLock lock;
        player_auto_stop();
    } else {
        stopPlayback();
    }
}

// 0x41b4c0: MIDI input. msg = status | data1 << 8 | data2 << 16
void MainWindow::midiMessage(unsigned msg)
{
    unsigned status = msg & 0xf0;
    bool on;
    if (status == 0x80) {
        on = false;
    } else if (status == 0x90) {
        on = true;
    } else if (status == 0xc0) {
        int ins = (int)(msg >> 8 & 0xff);
        if (ins >= MAX_INSTRUMENTS)
            return;
        updating = true;
        ListBoxIns->clearSelection();
        ListBoxIns->setCurrentRow(ins);
        updating = false;
        selectInstrument(ins);
        return;
    } else {
        return;
    }
    int velocity = (int)(msg >> 16 & 0xff);
    if (velocity == 0)
        on = false;
    int n = (int)(msg >> 8 & 0xff) - 12;
    int note = n % 12;
    int octave = n / 12;
    if (PageControlMain->currentWidget() == TabSheetIns) {
        if (on) {
            if (previewKeyDown(n | 0xff00, n, SpeedButtonFMPoly->isChecked()))
                PaintBoxKeys->update();
        } else if (previewKeyUp(n | 0xff00, n)) {
            PaintBoxKeys->update();
        }
    }
    if (QApplication::focusWidget() != ScrollBoxPtn || !on || U.noPattern)
        return;
    if (octave < 1 || octave > 8 || note < 0 || note > 11)
        return;
    if ((U.curCol & 7) != 0)
        return;
    undoPushPattern(U.curPattern);
    Cell *c = &g_song.pattern[U.curPattern].cell[U.curRow][U.curCol / 8];
    const bool hadIns = c->ins != 0;
    {
        AudioLock lock;
        c->note = (int16_t)note;
        c->octave = (int8_t)octave;
        U.lastNote = (int16_t)note;
        U.lastOctave = (int8_t)octave;
        if (c->ins == 0 && SpeedButtonAIns->isChecked())
            c->ins = (int16_t)UpDownAIns->value();
        if (SpeedButtonAVol->isChecked())
            c->volume = (int8_t)UpDownAVol->value();
        if (CheckBoxMidiVelocity->isChecked())
            c->volume = (int8_t)((velocity / 3 + 1) & 0x1f);
        player_start(U.curOrder, U.curRow, 1, 0, 0, -1);
    }
    if (hadIns)
        UpDownAIns->setValue(c->ins);
    cursorMoveRows(U.autoStep, false);
    patternRedraw();
}
