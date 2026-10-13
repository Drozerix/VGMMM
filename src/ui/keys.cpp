// Keyboard layout parser and key-binding state (original 0x411c68..0x412540).
#include "keys.h"

#include <QKeyEvent>
#include <cstdio>
#include <cstdlib>
#include <cstring>

KeyActions K;
unsigned char g_keyState[256];

namespace {

struct Binding {
    unsigned combo[3];      // VK | 0x2000 shift | 0x4000 ctrl | 0x8000 alt; up to three alternatives
    int state;
    unsigned char isModifier;       // matched in the second pass, then its keys are hidden
    unsigned char afterModifier;    // matched in the third pass, without the modifier keys
};

Binding s_bind[256];
int s_bindCount;
char *s_text;               // layout text: comments stripped, lines zero-terminated
int s_textSize;
char s_layoutName[0x400];

struct KeyName {
    const char *name;
    int vk;
};
const KeyName s_keyNames[] = {
    {"esc", 0x1b}, {"escape", 0x1b}, {"f1", 0x70}, {"f2", 0x71}, {"f3", 0x72}, {"f4", 0x73}, {"f5", 0x74},
    {"f6", 0x75}, {"f7", 0x76}, {"f8", 0x77}, {"f9", 0x78}, {"f10", 0x79}, {"f11", 0x7a}, {"f12", 0x7b},
    {"f13", 0x7c}, {"f14", 0x7d}, {"f15", 0x7e}, {"f16", 0x7f}, {"f17", 0x80}, {"f18", 0x81}, {"f19", 0x82},
    {"f20", 0x83}, {"f21", 0x84}, {"f22", 0x85}, {"f23", 0x86}, {"f24", 0x87}, {"prnscr", 0x2c},
    {"printscreen", 0x2c}, {"snapshot", 0x2c}, {"scroll", 0x91}, {"scrolllock", 0x91}, {"pause", 0x13},
    {"break", 0x13}, {"~", 0xc0}, {"tilda", 0xc0}, {"oem3", 0xc0}, {"0", 0x30}, {"1", 0x31}, {"2", 0x32},
    {"3", 0x33}, {"4", 0x34}, {"5", 0x35}, {"6", 0x36}, {"7", 0x37}, {"8", 0x38}, {"9", 0x39}, {"minus", 0xbd},
    {"plus", 0xbb}, {"backslash", 0xdc}, {"oem5", 0xdc}, {"\\", 0xdc}, {"back", 0x8}, {"backspace", 0x8},
    {"tab", 0x9}, {"q", 0x51}, {"w", 0x57}, {"e", 0x45}, {"r", 0x52}, {"t", 0x54}, {"y", 0x59}, {"u", 0x55},
    {"i", 0x49}, {"o", 0x4f}, {"p", 0x50}, {"[", 0xdb}, {"{", 0xdb}, {"]", 0xdd}, {"}", 0xdd}, {"enter", 0xd},
    {"capslock", 0x14}, {"a", 0x41}, {"s", 0x53}, {"d", 0x44}, {"f", 0x46}, {"g", 0x47}, {"h", 0x48},
    {"j", 0x4a}, {"k", 0x4b}, {"l", 0x4c}, {":", 0xba}, {";", 0xba}, {"\"", 0xde}, {"quote", 0xde},
    {"shift", 0x10}, {"z", 0x5a}, {"x", 0x58}, {"c", 0x43}, {"v", 0x56}, {"b", 0x42}, {"n", 0x4e}, {"m", 0x4d},
    {"comma", 0xbc}, {"<", 0xbc}, {"period", 0xbe}, {">", 0xbe}, {"slash", 0xbf}, {"?", 0xbf}, {"/", 0xbf},
    {"ctrl", 0x11}, {"control", 0x11}, {"lwin", 0x5b}, {"alt", 0x12}, {"menu", 0x12}, {"space", 0x20},
    {"rwin", 0x5c}, {"apps", 0x5d}, {"ins", 0x2d}, {"insert", 0x2d}, {"home", 0x24}, {"pageup", 0x21},
    {"pgup", 0x21}, {"del", 0x2e}, {"delete", 0x2e}, {"end", 0x23}, {"pagedown", 0x22}, {"pgdown", 0x22},
    {"left", 0x25}, {"right", 0x27}, {"up", 0x26}, {"down", 0x28}, {"numlock", 0x90}, {"numdiv", 0x6f},
    {"nummul", 0x6a}, {"numsub", 0x6d}, {"numadd", 0x6b}, {"num0", 0x60}, {"num1", 0x61}, {"num2", 0x62},
    {"num3", 0x63}, {"num4", 0x64}, {"num5", 0x65}, {"num6", 0x66}, {"num7", 0x67}, {"num8", 0x68},
    {"num9", 0x69}, {"decimal", 0x6e}, {"numdec", 0x6e},
};

// 0x411ca0: case-insensitive compare of n characters; 0 if equal
int ci_diff(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        int x = a[i], y = b[i];
        if (x > 0x40 && x < 0x5b)
            x += 0x20;
        if (y > 0x40 && y < 0x5b)
            y += 0x20;
        if (x != y)
            return 1;
    }
    return 0;
}

// 0x411cf4: offset just behind "name=" or -1. The name is searched as a substring anywhere in
// the text, so "octave1" finds "setOctave1=" (and "genMuteA" runs into "genMuteAll").
int find_entry(const char *name)
{
    int len = (int)strlen(name);
    int i = 0;
    for (;; i++) {
        if (s_textSize - len <= i)
            return -1;
        if (ci_diff(s_text + i, name, len) == 0)
            break;
    }
    i += len;
    if (s_text[i] == '=')
        return i + 1;
    return -1;
}

// 0x411dd4
bool load_text(const char *filename, const char *text, unsigned size)
{
    free(s_text);
    s_text = nullptr;
    char *src;
    if (text == nullptr) {
        FILE *f = fopen(filename, "rb");
        if (f == nullptr)
            return false;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        src = (char *)malloc(n > 0 ? (size_t)n : 1);
        size = (unsigned)fread(src, 1, n > 0 ? (size_t)n : 0, f);
        fclose(f);
    } else {
        src = const_cast<char *>(text);
    }
    s_text = (char *)malloc(size * 2 + 2);
    int out = 0;
    unsigned prev = 0xffffffff;
    bool comment = false;
    for (unsigned i = 0; i < size; i++) {
        unsigned c = (unsigned char)src[i];
        if (c == 0x20 && prev == 0x20)
            continue;
        if (!comment) {
            if (c == 0x3b)
                comment = true;
            if (c > 0x1f && c < 0x80 && c != 0x3b)
                s_text[out++] = (char)c;
        }
        prev = c;
        if (c == 10) {
            comment = false;
            s_text[out++] = 0;
        }
    }
    s_text[out] = 0;
    s_text[out + 1] = 0;
    if (text == nullptr)
        free(src);
    s_bindCount = 0;
    s_textSize = out + 2;
    memset(s_layoutName, 0, sizeof(s_layoutName));
    int p = find_entry("layoutName");
    if (p >= 0)
        for (int n = 0; p < s_textSize && (unsigned char)s_text[p] > 0x1f && n < 0x3ff; n++)
            s_layoutName[n] = s_text[p++];
    return true;
}

// 0x411fd0: register the binding called `name`; returns its index or -1 if absent/empty
int add_binding(const char *name)
{
    if (s_text == nullptr || s_bindCount > 0xff)
        return -1;
    int pos = find_entry(name);
    if (pos < 0)
        return -1;
    Binding *b = &s_bind[s_bindCount];
    memset(b, 0, sizeof(*b));
    int alt = 0;
    while (pos < s_textSize - 1 && s_text[pos] != 0) {
        char c = s_text[pos];
        if (c == ' ' || c == '+') {
            pos++;
            continue;
        }
        if (c == ',') {
            if (++alt > 2)
                break;
            pos++;
            continue;
        }
        char before = pos > 0 ? s_text[pos - 1] : 0;
        if (pos > 0 && (before == ' ' || before == '+' || before == ',' || before == '=')) {
            int code = -1;
            for (const KeyName &kn : s_keyNames) {
                int len = (int)strlen(kn.name);
                if (len + pos < s_textSize - 1 && ci_diff(s_text + pos, kn.name, len) == 0) {
                    char after = s_text[pos + len];
                    if (after == 0 || after == ' ' || after == '+' || after == ',') {
                        code = kn.vk;
                        pos += len - 1;
                        break;
                    }
                }
            }
            if (code >= 0) {
                if (code == 0x10)
                    b->combo[alt] |= 0x2000;
                else if (code == 0x11)
                    b->combo[alt] |= 0x4000;
                else if (code == 0x12)
                    b->combo[alt] |= 0x8000;
                else
                    b->combo[alt] |= (unsigned)code & 0xff;
            }
        }
        pos++;
    }
    for (int i = 0; i < 3; i++)
        if (b->combo[i] != 0)
            return s_bindCount++;
    return -1;
}

void set_modifier(int idx)
{
    if (idx >= 0 && idx < s_bindCount) {
        s_bind[idx].isModifier = 1;
        s_bind[idx].afterModifier = 0;
    }
}

void set_after_modifier(int idx)
{
    if (idx >= 0 && idx < s_bindCount) {
        s_bind[idx].afterModifier = 1;
        s_bind[idx].isModifier = 0;
    }
}

bool matches(const Binding *b, const unsigned char *keys)
{
    unsigned mods = (keys[0x11] ? 0x4000u : 0) | (keys[0x10] ? 0x2000u : 0) | (keys[0x12] ? 0x8000u : 0);
    for (int i = 0; i < 3; i++) {
        unsigned c = b->combo[i];
        if (c != 0 && mods == (c & 0xffffff00) && ((c & 0xff) == 0 || keys[c & 0xff] != 0))
            return true;
    }
    return false;
}

void advance(Binding *b, bool hit)
{
    if (hit) {
        if (b->state < 2)
            b->state++;
    } else {
        b->state = 0;
    }
}

} // namespace

const char *kbd_layout_name(void)
{
    return s_layoutName;
}

// 0x412264
void kbd_update(void)
{
    unsigned char keys[256];
    memcpy(keys, g_keyState, 256);
    for (int i = 0; i < s_bindCount; i++)
        if (!s_bind[i].isModifier && !s_bind[i].afterModifier)
            advance(&s_bind[i], matches(&s_bind[i], keys));
    for (int i = 0; i < s_bindCount; i++) {
        Binding *b = &s_bind[i];
        if (!(b->isModifier && !b->afterModifier))
            continue;
        advance(b, matches(b, keys));
        for (int k = 0; k < 3; k++) {
            unsigned c = b->combo[k];
            if (c == 0)
                continue;
            keys[c & 0xff] = 0;
            if (c & 0x4000)
                keys[0x11] = 0;
            if (c & 0x2000)
                keys[0x10] = 0;
            if (c & 0x8000)
                keys[0x12] = 0;
        }
    }
    for (int i = 0; i < s_bindCount; i++)
        if (!s_bind[i].isModifier && s_bind[i].afterModifier)
            advance(&s_bind[i], matches(&s_bind[i], keys));
}

// 0x412510
void kbd_clear(void)
{
    memset(g_keyState, 0, sizeof(g_keyState));
    for (int i = 0; i < s_bindCount; i++)
        s_bind[i].state = 0;
}

bool kbd_pressed(int action)
{
    return action >= 0 && action < s_bindCount && s_bind[action].state == 1;
}

bool kbd_held(int action)
{
    return action >= 0 && action < s_bindCount && s_bind[action].state != 0;
}

// 0x412540
bool kbd_load(const char *filename, const char *text, unsigned size)
{
    static const char *const noteNames[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    char name[64];
    memset(s_bind, 0, sizeof(s_bind));
    memset(g_keyState, 0, sizeof(g_keyState));
    s_bindCount = 0;
    s_layoutName[0] = 0;
    if (!load_text(filename, text, size))
        return false;
    for (int i = 0; i < 36; i++) {
        snprintf(name, sizeof(name), "note%s%1.1i", noteNames[i % 12], i / 12);
        K.note[i] = add_binding(name);
    }
    for (int i = 0; i < 8; i++) {
        snprintf(name, sizeof(name), "octave%1.1i", i + 1);
        K.octave[i] = add_binding(name);
    }
#define BIND(x) K.x = add_binding(#x)
    BIND(noteOff);
    BIND(genOpenMod); BIND(genSaveMod); BIND(genPlayAll); BIND(genPlayCur); BIND(genPlayPtn);
    BIND(genStop); BIND(genReset);
    for (int i = 0; i < 10; i++) {
        snprintf(name, sizeof(name), "genMute%c", i + 'A');
        K.genMute[i] = add_binding(name);
        // The stock layouts call these genMuteFM1..6 / genMutePSG1..4, names the original never
        // looks up (so the mute keys were dead); accept them as well.
        if (K.genMute[i] < 0) {
            if (i < 6)
                snprintf(name, sizeof(name), "genMuteFM%c", i + '1');
            else
                snprintf(name, sizeof(name), "genMutePSG%c", i - 6 + '1');
            K.genMute[i] = add_binding(name);
        }
    }
    BIND(genMuteAll); BIND(genSoloMode); BIND(genToPtnEdit); BIND(genToInsEdit); BIND(genToSampleEdit);
    BIND(genToInfo); BIND(genUndo); BIND(genRedo);
    BIND(ptnSelectAll); BIND(ptnSelectChn); BIND(ptnUnselect); BIND(ptnCut); BIND(ptnCopy); BIND(ptnPaste);
    BIND(ptnPasteOver); BIND(ptnDelete); BIND(ptnToOrder); BIND(ptnTransSemiUp); BIND(ptnTransSemiDown);
    BIND(ptnTransOctUp); BIND(ptnTransOctDown); BIND(ptnAssignInstr); BIND(ptnAutoStepInc); BIND(ptnAutoStepDec);
    for (int i = 0; i < 10; i++) {
        snprintf(name, sizeof(name), "ptnAutoStep%1.1i", i);
        K.ptnAutoStep[i] = add_binding(name);
    }
    BIND(ptnCurUp); BIND(ptnCurDown); BIND(ptnCurLeft); BIND(ptnCurRight); BIND(ptnPageUp); BIND(ptnPageDown);
    BIND(ptnHome); BIND(ptnEnd); BIND(ptnOrdPrev); BIND(ptnOrdNext); BIND(ptnNextChan); BIND(ptnPrevChan);
    set_after_modifier(K.ptnCurUp);
    set_after_modifier(K.ptnCurDown);
    set_after_modifier(K.ptnCurLeft);
    set_after_modifier(K.ptnCurRight);
    set_after_modifier(K.ptnPageUp);
    set_after_modifier(K.ptnPageDown);
    set_after_modifier(K.ptnHome);
    set_after_modifier(K.ptnEnd);
    set_after_modifier(K.ptnNextChan);
    set_after_modifier(K.ptnPrevChan);
    BIND(ptnDelNote); BIND(ptnDelLine); BIND(ptnInsNote); BIND(ptnInsLine); BIND(ptnPlay); BIND(ptnPlayLoop);
    BIND(ptnClrField); BIND(ptnClrNote); BIND(ptnClrLine); BIND(ptnSelectKey);
    set_modifier(K.ptnSelectKey);
    BIND(ptnPatternNext); BIND(ptnPatternPrev); BIND(ptnInstrNext); BIND(ptnInstrPrev); BIND(ptnInpRepeat);
    BIND(ptnPlayNote); BIND(ptnPlayLine); BIND(ptnPrevFxColumn); BIND(ptnNextFxColumn); BIND(ptnEditSong);
    BIND(ordInsertSame); BIND(ordInsertCopy); BIND(ordInsertNew); BIND(ordDelete); BIND(ordLoopPosition);
    BIND(ordPatternInc); BIND(ordPatternDec); BIND(ordToPattern); BIND(ordMovePrev); BIND(ordMoveNext);
#undef BIND
    free(s_text);
    s_text = nullptr;
    for (int i = 0; i < s_bindCount; i++)
        s_bind[i].state = 0;
    return true;
}

// Qt key event -> Windows virtual-key code (0 if there is no equivalent)
int qtKeyToVk(const QKeyEvent *e)
{
    int k = e->key();
    bool keypad = e->modifiers() & Qt::KeypadModifier;
    if (keypad) {
        if (k >= Qt::Key_0 && k <= Qt::Key_9)
            return 0x60 + (k - Qt::Key_0);
        switch (k) {
        case Qt::Key_Asterisk: return 0x6a;
        case Qt::Key_Plus: return 0x6b;
        case Qt::Key_Minus: return 0x6d;
        case Qt::Key_Period: case Qt::Key_Comma: return 0x6e;
        case Qt::Key_Slash: return 0x6f;
        // without Num Lock the pad sends navigation keys; on Windows Ctrl+NumN still reports
        // VK_NUMPADn only with Num Lock on, so these stay navigation keys
        default: break;
        }
    }
    if (k >= Qt::Key_A && k <= Qt::Key_Z)
        return 0x41 + (k - Qt::Key_A);
    if (k >= Qt::Key_0 && k <= Qt::Key_9)
        return 0x30 + (k - Qt::Key_0);
    if (k >= Qt::Key_F1 && k <= Qt::Key_F24)
        return 0x70 + (k - Qt::Key_F1);
    switch (k) {
    case Qt::Key_Escape: return 0x1b;
    case Qt::Key_Tab: case Qt::Key_Backtab: return 0x09;
    case Qt::Key_Backspace: return 0x08;
    case Qt::Key_Return: case Qt::Key_Enter: return 0x0d;
    case Qt::Key_Insert: return 0x2d;
    case Qt::Key_Delete: return 0x2e;
    case Qt::Key_Pause: return 0x13;
    case Qt::Key_Print: case Qt::Key_SysReq: return 0x2c;
    case Qt::Key_Home: return 0x24;
    case Qt::Key_End: return 0x23;
    case Qt::Key_Left: return 0x25;
    case Qt::Key_Up: return 0x26;
    case Qt::Key_Right: return 0x27;
    case Qt::Key_Down: return 0x28;
    case Qt::Key_PageUp: return 0x21;
    case Qt::Key_PageDown: return 0x22;
    case Qt::Key_Shift: return 0x10;
    case Qt::Key_Control: return 0x11;
    case Qt::Key_Alt: case Qt::Key_AltGr: return 0x12;
    case Qt::Key_Meta: case Qt::Key_Super_L: return 0x5b;
    case Qt::Key_Super_R: return 0x5c;
    case Qt::Key_Menu: return 0x5d;
    case Qt::Key_CapsLock: return 0x14;
    case Qt::Key_NumLock: return 0x90;
    case Qt::Key_ScrollLock: return 0x91;
    case Qt::Key_Space: return 0x20;
    // US-layout punctuation keys, shifted or not
    case Qt::Key_QuoteLeft: case Qt::Key_AsciiTilde: return 0xc0;
    case Qt::Key_Minus: case Qt::Key_Underscore: return 0xbd;
    case Qt::Key_Equal: case Qt::Key_Plus: return 0xbb;
    case Qt::Key_Backslash: case Qt::Key_Bar: return 0xdc;
    case Qt::Key_BracketLeft: case Qt::Key_BraceLeft: return 0xdb;
    case Qt::Key_BracketRight: case Qt::Key_BraceRight: return 0xdd;
    case Qt::Key_Semicolon: case Qt::Key_Colon: return 0xba;
    case Qt::Key_Apostrophe: case Qt::Key_QuoteDbl: return 0xde;
    case Qt::Key_Comma: case Qt::Key_Less: return 0xbc;
    case Qt::Key_Period: case Qt::Key_Greater: return 0xbe;
    case Qt::Key_Slash: case Qt::Key_Question: return 0xbf;
    case Qt::Key_ParenRight: return 0x30;
    case Qt::Key_Exclam: return 0x31;
    case Qt::Key_At: return 0x32;
    case Qt::Key_NumberSign: return 0x33;
    case Qt::Key_Dollar: return 0x34;
    case Qt::Key_Percent: return 0x35;
    case Qt::Key_AsciiCircum: return 0x36;
    case Qt::Key_Ampersand: return 0x37;
    case Qt::Key_Asterisk: return 0x38;
    case Qt::Key_ParenLeft: return 0x39;
    default: return 0;
    }
}
