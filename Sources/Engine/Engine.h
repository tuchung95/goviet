//
//  Engine.h
//  OpenKey
//
//  Created by Tuyen on 1/18/19.
//  Copyright © 2019 Tuyen Mai. All rights reserved.
//

#ifndef Engine_h
#define Engine_h

#include "DataType.h"
#include "Vietnamese.h"
#include "Macro.h"
#include "SmartSwitchKey.h"
#include "ConvertTool.h"

#ifndef LOBYTE
#define LOBYTE(data) (data & 0xFF)
#endif // !LOBYTE
#ifndef HIBYTE
#define HIBYTE(data) ((data>>8) & 0xFF)
#endif // !HIBYTE

#define GET_SWITCH_KEY(data) (data & 0xFF)
#define HAS_CONTROL(data) ((data & 0x100) ? 1 : 0)
#define HAS_OPTION(data) ((data & 0x200) ? 1 : 0)
#define HAS_COMMAND(data) ((data & 0x400) ? 1 : 0)
#define HAS_SHIFT(data) ((data & 0x800) ? 1 : 0)
#define GET_BOOL(data) (data ? 1 : 0)
#define HAS_BEEP(data) (data & 0x8000)
#define SET_SWITCH_KEY(data, key) data = (data & 0xFF) | key
#define SET_CONTROL_KEY(data, val) data|=val<<8;
#define SET_OPTION_KEY(data, val) data|=val<<9;
#define SET_COMMAND_KEY(data, val) data|=val<<10;

//define these variable in your application
//API
/*
 * 0: English
 * 1: Vietnamese
 */
extern int vLanguage;

/*
 * 0: Telex
 * 1: VNI
 */
extern int vInputType;

/**
 * 0: No
 * 1: Yes
 */
extern int vFreeMark;

/*
 * 0: Unicode
 * 1: TCVN3 (ABC)
 * 2: VNI-Windows
 */
extern int vCodeTable;

/*
 * first 8 bit: keycode
 * bit 8: Control on/off
 * bit 9: Option on/off
 * bit 10: Command on/off
 */
extern int vSwitchKeyStatus;

/**
 * 0: No
 * 1: Yes
 */
extern int vCheckSpelling;

/*
 * 0: òa, úy
 * 1: oà uý
*/
extern int vUseModernOrthography;

/**
 * 0: No
 * 1: Yes
 * (cc=ch, gg=gi, kk=kh, nn=ng, qq=qu, pp=ph, tt=th, uu=ươ)
 */
extern int vQuickTelex;

/**
 * Work together with vCheckSpelling
 * 0: No
 * 1: Yes
 *
 */
extern int vRestoreIfWrongSpelling;

/*
 * Fix recommend browser's address, excel,...
 * 0: No
 * 1: Yes
 */
extern int vFixRecommendBrowser;

/**
 * Macro on or off
 */
extern int vUseMacro;

/**
 * Still use macro if you are in english mode
 */
extern int vUseMacroInEnglishMode;

/**
 * Ex: define: btw -> by the way
 * Type: `Btw` -> `By the way`
 * Type: `BTW` -> `BY THE WAY`
 */
extern int vAutoCapsMacro;

/**
 * auto switch language when switch app
 * 0: No
 * 1: Yes
 */
extern int vUseSmartSwitchKey;

/**
 * Auto write upper case character for first letter.
 * 0: No
 * 1: Yes
 */
extern int vUpperCaseFirstChar;

/**
 * Temporarily turn off spell checking with Ctrl key
 * 0: No
 * 1: Yes
 */
extern int vTempOffSpelling;

/**
 * Allow write word with consonant Z, F, W, J
 * 0: No
 * 1: Yes
 */
extern int vAllowConsonantZFWJ;

/**
 * Telex: a "w" that doesn't modify a vowel becomes "ư" (w -> ư, tw -> tư)
 * 0: No, keep "w"
 * 1: Yes
 */
extern int vAllowStandaloneW;

/**
 * 0: No; 1: Yes
 * f -> ph: fanh -> phanh,...
 * j ->gi: jang -> giang,...
 * w ->qu: wen -> quen,...
 */
extern int vQuickStartConsonant;

/**
 * 0: No; 1: Yes
 * g -> ng: hag -> hang,...
 * h -> nh: vih -> vinh,...
 * k -> ch: bak -> bach,...
 */
extern int vQuickEndConsonant;

/**
 * 0: No; 1: Yes
 * Auto remember table code for each application
 */
extern int vRememberCode;

/**
 * 0: No; 1: Yes
 * Turn off Vietnamese when typing in another language.
 */
extern int vOtherLanguage;

/**
 * 0: No; 1: Yes
 * Temporarily turn off OpenKey  by hot key (Command on mac, Alt on Windows and Linux)
 */
extern int vTempOffOpenKey;

/**
 * Auto switch English/Vietnamese from typing behavior (ported from Vikey, GPL v3).
 * Works with Telex-family input types and the Unicode code table only.
 * 0: No; 1: Yes
 */
extern int vAutoSwitchByTyping;

/**
 * Set by the platform while auto switch is held off (right after a manual switch).
 * 0: No; 1: Yes
 */
extern int vAutoSwitchPaused;

/**
 * Set by the engine when a keystroke should switch the language:
 * -1: no request, 0: switch to English, 1: switch to Vietnamese.
 * The platform layer applies it after sending the key's output, then resets it to -1.
 */
extern int vAutoSwitchRequest;

/**
 * What was on screen before the last automatic correction (engine character data),
 * so the platform can put it back when the user presses Backspace right after it.
 */
extern Uint32 vAutoSwitchUndoData[MAX_BUFF];
extern int vAutoSwitchUndoCount;
/** The keys of the last word auto switch put back to English (so the next word can look back on it). */
extern Uint32 vAutoSwitchRestoredKeys[];
extern int vAutoSwitchRestoredCount;
/** Set by the platform: the previous word passed to vAutoSwitchCheckEnglishWord was put back to English by auto switch. */
extern bool vAutoSwitchPrevRestored;

/**
 * Whether auto switch runs right now: turned on, not paused, Telex family, Unicode code table.
 */
bool vAutoSwitchEnabled();

/**
 * English mode, at a word break: if the raw keys of the finished word are clearly a
 * Vietnamese word typed in Telex, fills the hook state (code vRestore) with that word and
 * returns true. The platform then replaces the word and switches to Vietnamese.
 * keys: key codes with CAPS_MASK, count: number of keys.
 * previous: the words typed just before it (most recent first, separated by single spaces):
 * ambiguous ones are converted together with it (look-back).
 */
bool vAutoSwitchCheckEnglishWord(const Uint32* keys, const int& count,
                                 const Uint32* const* previous, const int* previousCounts, const int& previousN);

/**
 * Forget the words remembered for look-back (language switched by hand, app changed...).
 */
void vAutoSwitchForget();

/**
 * Call at the end of the first English word after a doubled tone key switched to English.
 * keys: the letters typed since the switch. When the word with both presses of the key is not
 * English but the word with one is ("mixx" -> "mix"), fills the hook state (code vRestore) with
 * the fixed word and returns true. Always forgets the pending switch.
 */
bool vAutoSwitchFixDoubledKey(const Uint32* keys, const int& count);

/**
 * Call this function first to receive data pointer
 */
void* vKeyInit();

/**
 * Convert engine character to real character
 */
Uint32 getCharacterCode(const Uint32& data);

/**
 * MAIN entry point for each key
 * event: mouse or keyboard event
 * state: additional state for event
 * data: key code
 * isCaps: caplock is on or shift key is pressing
 * otherControlKey: ctrl, option,... is pressing
 */
void vKeyHandleEvent(const vKeyEvent& event,
                     const vKeyEventState& state,
                     const Uint16& data,
                     const Uint8& capsStatus=0,
                     const bool& otherControlKey=false);

/**
 * Start a new word
 */
void startNewSession();

/**
 * do some task in english mode (use for macro)
 */
void vEnglishMode(const vKeyEventState& state, const Uint16& data, const bool& isCaps, const bool& otherControlKey);

/**
 * temporarily turn off spell checking
 */
void vTempOffSpellChecking();

/**
 * reset spelling value
 */
void vSetCheckSpelling();

/**
 * temporarily turn off OpenKey engine
 */
void vTempOffEngine(const bool& off=true);

/**
 * some utils function
 */
std::wstring utf8ToWideString(const std::string& str);
std::string wideStringToUtf8(const std::wstring& str);

#endif /* Engine_h */
