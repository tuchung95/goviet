//
//  Engine.cpp
//  OpenKey
//
//  Created by Tuyen on 1/18/19.
//  Copyright © 2019 Tuyen Mai. All rights reserved.
//
#include <algorithm>
#include "Engine.h"
#include <string.h>
#include <cmath>
#include <string>
#include "Macro.h"
#include "AutoSwitchData.h"
#include "BigramData.h"

using namespace std;

struct FixedState {
    Uint32 data[MAX_BUFF * 2];
    size_t count;

    FixedState() : count(0) {}
    
    void clear() { count = 0; }
    void push_back(Uint32 val) {
        if (count < MAX_BUFF * 2) {
            data[count++] = val;
        }
    }
    void pop_back() {
        if (count > 0) count--;
    }
    Uint32 back() const {
        if (count > 0) return data[count - 1];
        return 0;
    }
    size_t size() const { return count; }
    Uint32 operator[](size_t idx) const { return data[idx]; }
    Uint32& operator[](size_t idx) { return data[idx]; }
};

class TypingStatesList {
private:
    FixedState elements[MAX_BUFF * 4];
    size_t totalSize;

public:
    TypingStatesList() : totalSize(0) {}

    void clear() { totalSize = 0; }
    size_t size() const { return totalSize; }
    
    void push_back(const FixedState& state) {
        if (totalSize < MAX_BUFF * 4) {
            elements[totalSize++] = state;
        }
    }
    
    FixedState back() const {
        if (totalSize > 0) {
            return elements[totalSize - 1];
        }
        return FixedState();
    }
    
    void pop_back() {
        if (totalSize > 0) {
            totalSize--;
        }
    }
};

static Uint16 ProcessingChar[][11] = {
    {KEY_S, KEY_F, KEY_R, KEY_X, KEY_J, KEY_A, KEY_O, KEY_E, KEY_W, KEY_D, KEY_Z}, //Telex
    {KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0}, //VNI
    {KEY_S, KEY_F, KEY_R, KEY_X, KEY_J, KEY_A, KEY_O, KEY_E, KEY_W, KEY_D, KEY_Z}, //Simple Telex 1
    {KEY_S, KEY_F, KEY_R, KEY_X, KEY_J, KEY_A, KEY_O, KEY_E, KEY_W, KEY_D, KEY_Z} //Simple Telex 2
};

#define IS_KEY_Z(key) (ProcessingChar[vInputType][10] == key)
#define IS_KEY_D(key) (ProcessingChar[vInputType][9] == key)
#define IS_KEY_W(key) ((vInputType != vVNI) ? ProcessingChar[vInputType][8] == key : \
                                    (vInputType == vVNI ? (ProcessingChar[vInputType][8] == key || ProcessingChar[vInputType][7] == key) : false))
#define IS_KEY_DOUBLE(key) ((vInputType != vVNI) ? (ProcessingChar[vInputType][5] == key || ProcessingChar[vInputType][6] == key || ProcessingChar[vInputType][7] == key) :\
                                        (vInputType == vVNI ? ProcessingChar[vInputType][6] == key : false))
#define IS_KEY_S(key) (ProcessingChar[vInputType][0] == key)
#define IS_KEY_F(key) (ProcessingChar[vInputType][1] == key)
#define IS_KEY_R(key) (ProcessingChar[vInputType][2] == key)
#define IS_KEY_X(key) (ProcessingChar[vInputType][3] == key)
#define IS_KEY_J(key) (ProcessingChar[vInputType][4] == key)

#define IS_MARK_KEY(keyCode) (((vInputType != vVNI) && (keyCode == KEY_S || keyCode == KEY_F || keyCode == KEY_R || keyCode == KEY_J || keyCode == KEY_X)) || \
                                        (vInputType == vVNI && (keyCode == KEY_1 || keyCode == KEY_2 || keyCode == KEY_3 || keyCode == KEY_5 || keyCode == KEY_4)))
#define IS_BRACKET_KEY(key) (key == KEY_LEFT_BRACKET || key == KEY_RIGHT_BRACKET)

#define VSI vowelStartIndex
#define VEI vowelEndIndex
#define VWSM vowelWillSetMark
#define hBPC HookState.backspaceCount
#define hNCC HookState.newCharCount
#define hCode HookState.code
#define hExt HookState.extCode
#define hData HookState.charData
#define GET getCharacterCode
#define hMacroKey HookState.macroKey
#define hMacroData HookState.macroData

//Data to sendback to main program
vKeyHookState HookState;

//private data
/**
 * data structure of each element in TypingWord (Uint64)
 * first 2 byte is character code or key code.
 * bit 16: has caps or not
 * bit 17: has tone ^ or not
 * bit 18: has tone w or not
 * bit 19 - > 23: has mark or not (Sắc, huyền, hỏi, ngã, nặng)
 * bit 24: is standalone key? (w, [, ])
 * bit 25: is character code or keyboard code; 1: character code; 0: keycode
 */
static Uint32 TypingWord[MAX_BUFF];
static Byte _index = 0;
static FixedState _longWordHelper; //save the word when _index >= MAX_BUFF
static TypingStatesList _typingStates; //Aug 28th, 2019: typing helper, save long state of Typing word, can go back and modify the word
static FixedState _typingStatesData;

/**
 * Use for restore key if invalid word
 */
static Uint32 KeyStates[MAX_BUFF];
//auto switch: keys Telex undid and dropped from the key states ("eee" -> "ee", "deepseek"), and where each
//was, oldest first (each position counts the keys kept at that moment)
#define AS_MAX_DROPS 4
static int _asDropN = 0;
static int _asDropPos[AS_MAX_DROPS];
static Uint32 _asDropKey[AS_MAX_DROPS];
/** The keys as typed: the key states with the dropped keys put back. Returns how many. */
static int asStatesWithDrops(Uint32* full, const int& maxN);
static Byte _stateIndex = 0;

static bool tempDisableKey = false;
static int capsElem;
static int key;
static int markElem;
static bool isCorect = false;
static bool isChanged = false;
static Byte vowelCount = 0;
static Byte vowelStartIndex = 0;
static Byte vowelEndIndex = 0;
static Byte vowelWillSetMark = 0;
static int i, ii, iii;
static int j;
static int k, kk;
static int l;
static bool isRestoredW;
static Uint16 keyForAEO;
static bool isCheckedGrammar;
static bool _isCaps = false;
static int _spaceCount = 0; //add: July 30th, 2019
static bool _hasHandledMacro = false; //for macro flag August 9th, 2019
static Byte _upperCaseStatus = 0; //for Write upper case for the first letter; 2: will upper case
static bool _isCharKeyCode;
static FixedState _specialChar;
static bool _useSpellCheckingBefore;
static bool _hasHandleQuickConsonant;
static bool _willTempOffEngine = false;

//function prototype
void findAndCalculateVowel(const bool& forGrammar=false);
void insertMark(const Uint32& markMask, const bool& canModifyFlag=true);

wstring utf8ToWideString(const string& str) {
    wstring result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size();) {
        unsigned char c = str[i];
        wchar_t uni = 0;
        size_t bytes = 0;
        if (c < 0x80) {
            uni = c;
            bytes = 1;
        } else if ((c & 0xE0) == 0xC0) {
            uni = c & 0x1F;
            bytes = 2;
        } else if ((c & 0xF0) == 0xE0) {
            uni = c & 0x0F;
            bytes = 3;
        } else if ((c & 0xF8) == 0xF0) {
            uni = c & 0x07;
            bytes = 4;
        } else {
            i++;
            continue;
        }
        if (i + bytes > str.size()) break;
        for (size_t j = 1; j < bytes; j++) {
            uni = (uni << 6) | (str[i + j] & 0x3F);
        }
        result.push_back(uni);
        i += bytes;
    }
    return result;
}

string wideStringToUtf8(const wstring& str) {
    string result;
    result.reserve(str.size() * 3);
    for (wchar_t uni : str) {
        if (uni < 0x80) {
            result.push_back(static_cast<char>(uni));
        } else if (uni < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (uni >> 6)));
            result.push_back(static_cast<char>(0x80 | (uni & 0x3F)));
        } else if (uni < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (uni >> 12)));
            result.push_back(static_cast<char>(0x80 | ((uni >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (uni & 0x3F)));
        } else if (uni < 0x110000) {
            result.push_back(static_cast<char>(0xF0 | (uni >> 18)));
            result.push_back(static_cast<char>(0x80 | ((uni >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((uni >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (uni & 0x3F)));
        }
    }
    return result;
}

void* vKeyInit() {
    _index = 0;
    _stateIndex = 0;
    _useSpellCheckingBefore = vCheckSpelling;
    _typingStatesData.clear();
    _typingStates.clear();
    _longWordHelper.clear();
    return &HookState;
}

bool isWordBreak(const vKeyEvent& event, const vKeyEventState& state, const Uint16& data) {
    if (event == vKeyEvent::Mouse)
        return true;
    switch (data) {
        case KEY_ESC:
        case KEY_TAB:
        case KEY_ENTER:
        case KEY_RETURN:
        case KEY_LEFT:
        case KEY_RIGHT:
        case KEY_DOWN:
        case KEY_UP:
        case KEY_COMMA:
        case KEY_DOT:
        case KEY_SLASH:
        case KEY_SEMICOLON:
        case KEY_QUOTE:
        case KEY_BACK_SLASH:
        case KEY_MINUS:
        case KEY_EQUALS:
        case KEY_BACKQUOTE:
            return true;
#if _WIN32
        case VK_INSERT:
        case VK_HOME:
        case VK_END:
        case VK_DELETE:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_SNAPSHOT:
        case VK_PRINT:
        case VK_SELECT:
        case VK_HELP:
        case VK_EXECUTE:
        case VK_NUMLOCK:
        case VK_SCROLL:
            return true;
#endif
        default:
            return false;
    }
}

bool isMacroBreakCode(const int& data) {
    switch (data) {
        case KEY_RETURN:
        case KEY_COMMA:
        case KEY_DOT:
        case KEY_SLASH:
        case KEY_SEMICOLON:
        case KEY_QUOTE:
        case KEY_BACK_SLASH:
        case KEY_MINUS:
        case KEY_EQUALS:
            return true;
        default:
            return false;
    }
}

static inline bool isCharKeyCode(const Uint16& data) {
    switch (data) {
        case KEY_BACKQUOTE:
        case KEY_1:
        case KEY_2:
        case KEY_3:
        case KEY_4:
        case KEY_5:
        case KEY_6:
        case KEY_7:
        case KEY_8:
        case KEY_9:
        case KEY_0:
        case KEY_MINUS:
        case KEY_EQUALS:
        case KEY_LEFT_BRACKET:
        case KEY_RIGHT_BRACKET:
        case KEY_BACK_SLASH:
        case KEY_SEMICOLON:
        case KEY_QUOTE:
        case KEY_COMMA:
        case KEY_DOT:
        case KEY_SLASH:
            return true;
        default:
            return false;
    }
}

void setKeyData(const Byte& index, const Uint16& keyCode, const bool& isCaps) {
    if (index < 0 || index >= MAX_BUFF)
        return;
    TypingWord[index] = keyCode | (isCaps ? CAPS_MASK : 0);
}

bool _spellingOK = false;
bool _spellingFlag = false;
bool _spellingVowelOK = false;
Byte _spellingEndIndex = 0;

void checkSpelling(const bool& forceCheckVowel=false) {
    _spellingOK = false;
    _spellingVowelOK = true;
    _spellingEndIndex = _index;
    
    if (_index > 0 && CHR(_index-1) == KEY_RIGHT_BRACKET) {
        _spellingEndIndex = _index-1;
    }
    
    if (_spellingEndIndex > 0) {
        j = 0;
        //Check first consonant
        if (IS_CONSONANT(CHR(0))) {
            for (i = 0; i < _consonantTable.size(); i++) {
                _spellingFlag = false;
                if (_spellingEndIndex < _consonantTable[i].size())
                    _spellingFlag = true;
                for (j = 0; j < _consonantTable[i].size(); j++) {
                    if (_spellingEndIndex > j &&
                        (_consonantTable[i][j] & ~(vQuickStartConsonant ? END_CONSONANT_MASK : 0)) != CHR(j) &&
                        (_consonantTable[i][j] & ~(vAllowConsonantZFWJ ? CONSONANT_ALLOW_MASK : 0)) != CHR(j)) {
                        _spellingFlag = true;
                        break;
                    }
                }
                if (_spellingFlag)
                    continue;
                
                break;
            }
        }
        
        if (j == _spellingEndIndex){ //for "d" case
            _spellingOK = true;
        }
        
        //check next vowel
        k = j;
        VSI = k;
        //August 23rd, 2019: fix case "que't"
        if (CHR(VSI) == KEY_U && k > 0 && k < _spellingEndIndex-1 && CHR(VSI-1) == KEY_Q) {
            k = k + 1;
            j = k;
            VSI = k;
        } else if (_index >= 2 && CHR(0) == KEY_G && CHR(1) == KEY_I && IS_CONSONANT(CHR(2))) {
            VSI = k = j = 1; //Sep 28th: fix gìn
        }
        for (l = 0; l < 3; l++) {
            if (k < _spellingEndIndex && !IS_CONSONANT(CHR(k))) {
                k++;
                VEI = k;
            }
        }
        if (k > j) { //has vowel,
            _spellingVowelOK = false;
            //check correct combined vowel
            if (k - j > 1 && forceCheckVowel) {
                const Array2D32& vowelSet = _vowelCombine[CHR(j)];
                for (l = 0; l < vowelSet.size(); l++) {
                    _spellingFlag = false;
                    for (ii = 1; ii < vowelSet[l].size(); ii++) {
                        if (j + ii - 1 < _spellingEndIndex && vowelSet[l][ii] != ((CHR(j + ii - 1) | (TypingWord[j + ii - 1] & TONEW_MASK) | (TypingWord[j + ii - 1] & TONE_MASK)))) {
                            _spellingFlag = true;
                            break;
                        }
                    }
                    if (_spellingFlag || (k < _spellingEndIndex && !vowelSet[l][0]) || (j + ii - 1 < _spellingEndIndex && !IS_CONSONANT(CHR(j + ii - 1))))
                        continue;
                    
                    _spellingVowelOK = true;
                    break;
                }
            } else if (!IS_CONSONANT(CHR(j))) {
                _spellingVowelOK = true;
            }
            
            //continue check last consonant
            for (ii = 0; ii < _endConsonantTable.size(); ii++) {
                _spellingFlag = false;
   
                for (j = 0; j < _endConsonantTable[ii].size(); j++) {
                    if (_spellingEndIndex > k+j &&
                        (_endConsonantTable[ii][j] & ~(vQuickEndConsonant ? END_CONSONANT_MASK : 0)) != CHR(k + j)) {
                        _spellingFlag = true;
                        break;
                    }
                }
                if (_spellingFlag)
                    continue;
                
                if (k + j >= _spellingEndIndex) {
                    _spellingOK = true;
                    break;
                }
            }
            
            //limit: end consonant "ch", "t" can not use with "~", "`", "?"
            if (_spellingOK) {
                if (_index >= 3 && CHR(_index-1) == KEY_H && CHR(_index-2) == KEY_C && !((TypingWord[_index-3] & MARK1_MASK) || (TypingWord[_index-3] & MARK5_MASK) || !(TypingWord[_index-3] & MARK_MASK))) {
                    _spellingOK = false;
                } else if (_index >= 2 && CHR(_index-1) == KEY_T && !((TypingWord[_index-2] & MARK1_MASK) || (TypingWord[_index-2] & MARK5_MASK) || !(TypingWord[_index-2] & MARK_MASK))) {
                    _spellingOK = false;
                }
            }
        }
    } else {
        _spellingOK = true;
    }
    tempDisableKey = !(_spellingOK && _spellingVowelOK);
    
    //cout<<"spelling vowel: "<<(_spellingVowelOK ? "OK": "Err")<<endl;
    //cout<<"spelling: "<<(_spellingOK ? "OK": "Err")<<endl<<endl;
}

void checkGrammar(const int& deltaBackSpace) {
    if (_index <= 1 || _index >= MAX_BUFF)
        return;
    
    findAndCalculateVowel(true);
    if (vowelCount == 0)
        return;
    
    isCheckedGrammar = false;
    
    l = VSI;
    
    //if N key for case: "thuơn", "ưoi", "ưom", "ưoc"
    if (_index >= 3) {
        for (i = _index-1; i >= 0; i--) {
            if (CHR(i) == KEY_N || CHR(i) == KEY_C || CHR(i) == KEY_I ||
                CHR(i) == KEY_M || CHR(i) == KEY_P || CHR(i) == KEY_T) {
                if (i - 2 >= 0 && CHR(i - 1) == KEY_O && CHR(i - 2) == KEY_U) {
                    if ((TypingWord[i-1] & TONEW_MASK) ^ (TypingWord[i-2] & TONEW_MASK)) {
                        TypingWord[i - 2] |= TONEW_MASK;
                        TypingWord[i - 1] |= TONEW_MASK;
                        isCheckedGrammar = true;
                        break;
                    }
                }
            }
        }
    }
    
    //check mark
    if (_index >= 2) {
        for (i = l; i <= VEI; i++) {
            if (TypingWord[i] & MARK_MASK) {
                Uint32 mark = TypingWord[i] & MARK_MASK;
                TypingWord[i] &= ~MARK_MASK;
                insertMark(mark, false);
                if (i != vowelWillSetMark)
                    isCheckedGrammar = true;
                break;
            }
        }
    }
    
    //re-arrange data to sendback
    if (isCheckedGrammar) {
        if (hCode ==vDoNothing)
            hCode = vWillProcess;
        hBPC = 0;
        
        for (i = _index - 1; i >= l; i--) {
            hBPC++;
            hData[_index - 1 - i] = GET(TypingWord[i]);
        }
        hNCC = hBPC;
        hBPC += deltaBackSpace;
        hExt = 4;
    }
}

void insertKey(const Uint16& keyCode, const bool& isCaps, const bool& isCheckSpelling=true) {
    if (_index >= MAX_BUFF) {
        _longWordHelper.push_back(TypingWord[0]); //save long word
        //left shift
        for (iii = 0; iii < MAX_BUFF - 1; iii++) {
            TypingWord[iii] = TypingWord[iii + 1];
        }
        setKeyData(_index-1, keyCode, isCaps);
    } else {
        setKeyData(_index++, keyCode, isCaps);
    }
    
    if (vCheckSpelling && isCheckSpelling)
        checkSpelling();
    
    //allow d after consonant
    if (keyCode == KEY_D && _index - 2 >= 0 && IS_CONSONANT(CHR(_index - 2)))
        tempDisableKey = false;
}

void insertState(const Uint16& keyCode, const bool& isCaps) {
    if (_stateIndex >= MAX_BUFF) {
        //left shift
        for (iii = 0; iii < MAX_BUFF - 1; iii++) {
            KeyStates[iii] = KeyStates[iii + 1];
        }
        KeyStates[_stateIndex-1] = keyCode | (isCaps ? CAPS_MASK : 0);
    } else {
        KeyStates[_stateIndex++] = keyCode | (isCaps ? CAPS_MASK : 0);
    }
}

void saveWord() {
    //save word history
    if (hCode != vReplaceMaro) {
        if (_index > 0) {
            if (_longWordHelper.size() > 0) { //save long word first
                _typingStatesData.clear();
                for (i = 0; i < _longWordHelper.size(); i++) {
                    if (i != 0 && i % MAX_BUFF == 0) { //save if overflow
                        _typingStates.push_back(_typingStatesData);
                        _typingStatesData.clear();
                    }
                    _typingStatesData.push_back(_longWordHelper[i]);
                }
                _typingStates.push_back(_typingStatesData);
                _longWordHelper.clear();
            }
            
            //save current word
            _typingStatesData.clear();
            for (i = 0; i < _index; i++) {
                _typingStatesData.push_back(TypingWord[i]);
            }
            _typingStates.push_back(_typingStatesData);
        }
    } else { //save macro words
        _typingStatesData.clear();
        for (i = 0; i < hMacroData.size(); i++) {
            if (i != 0 && i % MAX_BUFF == 0) { //break if overflow
                _typingStates.push_back(_typingStatesData);
                _typingStatesData.clear();
            }
            _typingStatesData.push_back(hMacroData[i]);
        }
        _typingStates.push_back(_typingStatesData);
    }
}

void saveWord(const Uint32& keyCode, const int& count) {
    _typingStatesData.clear();
    for (i = 0; i < count; i++) {
        _typingStatesData.push_back(keyCode);
    }
    _typingStates.push_back(_typingStatesData);
}

void saveSpecialChar() {
    _typingStatesData.clear();
    for (i = 0; i < _specialChar.size(); i++) {
        _typingStatesData.push_back(_specialChar[i]);
    }
    _typingStates.push_back(_typingStatesData);
    _specialChar.clear();
}

void restoreLastTypingState() {
    if (_typingStates.size() > 0) {
        _typingStatesData = _typingStates.back();
        _typingStates.pop_back();
        if (_typingStatesData.size() > 0){
            if (_typingStatesData[0] == KEY_SPACE) {
                _spaceCount = (int)_typingStatesData.size();
                _index = 0;
            } else if (isCharKeyCode((Uint16)_typingStatesData[0])) {
                _index = 0;
                _specialChar = _typingStatesData;
                checkSpelling();
            } else {
                for (i = 0; i < _typingStatesData.size(); i++) {
                    TypingWord[i] = _typingStatesData[i];
                }
                _index = (Byte)_typingStatesData.size();
            }
        }
    }
}

void startNewSession() {
    _asDropN = 0;
    _index = 0;
    hBPC = 0;
    hNCC = 0;
    tempDisableKey = false;
    _stateIndex = 0;
    _hasHandledMacro = false;
    _hasHandleQuickConsonant = false;
    _longWordHelper.clear();
}

void checkCorrectVowel(const Array2D16& charset, int& i, int& k, const Uint16& markKey) {
    //ignore "qu" case
    if (_index >= 2 && CHR(_index-1) == KEY_U && CHR(_index-2) == KEY_Q) {
        isCorect = false;
        return;
    }
    k = _index - 1;
    for (j = (int)charset[i].size() - 1; j >= 0; j--) {
        if ((charset[i][j] & ~(vQuickEndConsonant ? END_CONSONANT_MASK : 0)) != CHR(k)) {
            isCorect = false;
            return;
        }
        k--;
        if (k < 0)
            break;
    }
    
    //limit mark for end consonant: "C", "T"
    if (isCorect && charset[i].size() > 1 && (IS_KEY_F(markKey) || IS_KEY_X(markKey) || IS_KEY_R(markKey))) {
        if (charset[i][1] == KEY_C || charset[i][1] == KEY_T) {
            isCorect = false;
        } else if (charset[i].size() > 2 && (charset[i][2] == KEY_T)) {
            isCorect = false;
        }
    }
    
    if (isCorect && k >= 0) {
        if (CHR(k) == CHR(k+1)) {
            isCorect = false;
        }
    }
}

Uint32 getCharacterCode(const Uint32& data) {
    capsElem = (data & CAPS_MASK) ? 0 : 1;
    key = data & CHAR_MASK;
    if (data & MARK_MASK) { //has mark
        markElem = -2;
        switch (data & MARK_MASK) {
            case MARK1_MASK:
                markElem = 0;
                break;
            case MARK2_MASK:
                markElem = 2;
                break;
            case MARK3_MASK:
                markElem = 4;
                break;
            case MARK4_MASK:
                markElem = 6;
                break;
            case MARK5_MASK:
                markElem = 8;
                break;
        }
        markElem += capsElem;
        
        switch (key) {
            case KEY_A:
            case KEY_O:
            case KEY_U:
            case KEY_E:
                if ((data & TONE_MASK) == 0 && (data & TONEW_MASK) == 0)
                    markElem += 4;
                break;
        }
        
        if (data & TONE_MASK) {
            key |= TONE_MASK;
        } else if (data & TONEW_MASK) {
            key |= TONEW_MASK;
        }
        if (_codeTable[vCodeTable].find(key) == _codeTable[vCodeTable].end())
            return data & ~STANDALONE_MASK; //not found (a standalone w that lost its ư is just u, never a raw flagged code)

        return _codeTable[vCodeTable][key][markElem] | CHAR_CODE_MASK;
    } else { //doesn't has mark
        if (_codeTable[vCodeTable].find(key) == _codeTable[vCodeTable].end())
            return data & ~STANDALONE_MASK; //not found (a standalone w that lost its ư is just u, never a raw flagged code)

        if (data & TONE_MASK) {
            return _codeTable[vCodeTable][key][capsElem] | CHAR_CODE_MASK;
        } else if (data & TONEW_MASK) {
            return _codeTable[vCodeTable][key][capsElem + 2] | CHAR_CODE_MASK;
        } else {
            return data & ~STANDALONE_MASK; //not found (a standalone w that lost its ư is just u, never a raw flagged code)
        }
    }
    
    return 0;
}

void findAndCalculateVowel(const bool& forGrammar) {
    vowelCount = 0;
    VSI = VEI = 0;
    for (iii = _index - 1; iii >= 0; iii--) {
        if (IS_CONSONANT(CHR(iii))) {
            if (vowelCount > 0)
                break;
        } else {  //is vowel
            if (vowelCount == 0)
                VEI = iii;
            if (!forGrammar) {
                if ((iii-1 >= 0 && (CHR(iii) == KEY_I && CHR(iii-1) == KEY_G)) ||
                    (iii-1 >= 0 && (CHR(iii) == KEY_U && CHR(iii-1) == KEY_Q))) {
                    break;
                }
            }
            VSI = iii;
            vowelCount++;
        }
    }
    //August 26th, 2019: don't count "u" at "q u" as a vowel
    if (VSI - 1 >= 0 && CHR(VSI) == KEY_U && CHR(VSI-1) == KEY_Q) {
        VSI++;
        vowelCount--;
    }
}

void removeMark() {
    findAndCalculateVowel(true);
    isChanged = false;
    if (_index > 0) {
        for (i = VSI; i <= VEI; i++) {
            if (TypingWord[i] & MARK_MASK) {
                TypingWord[i] &= ~MARK_MASK;
                isChanged = true;
            }
        }
    }
    if (isChanged) {
        hCode = vWillProcess;
        hBPC = 0;
        
        for (i = _index - 1; i >= VSI; i--) {
            hBPC++;
            hData[_index - 1 - i] = GET(TypingWord[i]);
        }
        hNCC = hBPC;
    } else {
        hCode = vDoNothing;
    }
}

bool canHasEndConsonant() {
    const Array2D32& vo = _vowelCombine[CHR(VSI)];
    for (ii = 0; ii < vo.size(); ii++) {
        kk = VSI;
        for (iii = 1; iii < vo[ii].size(); iii++) {
            if (kk > VEI || ((CHR(kk) | (TypingWord[kk] & TONE_MASK) | (TypingWord[kk] & TONEW_MASK)) != vo[ii][iii])) {
                break;
            }
            kk++;
        }
        if (iii >= vo[ii].size()) {
            return vo[ii][0] == 1;
        }
    }
    return false;
}

void handleModernMark() {
    //default
    VWSM = VEI;
    hBPC = (_index - VEI);
    
    //rule 2
    if (vowelCount == 3 && ((CHR(VSI) == KEY_O && CHR(VSI+1) == KEY_A && CHR(VSI+2) == KEY_I) ||
                            (CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_Y && CHR(VSI+2) == KEY_U) ||
                            (CHR(VSI) == KEY_O && CHR(VSI+1) == KEY_E && CHR(VSI+2) == KEY_O) ||
                            (CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_Y && CHR(VSI+2) == KEY_A))) {
        VWSM = VSI + 1;
        hBPC = _index - VWSM;
    } else if ((CHR(VSI) == KEY_O && CHR(VSI+1) == KEY_I) ||
               (CHR(VSI) == KEY_A && CHR(VSI+1) == KEY_I) ||
               (CHR(VSI)== KEY_U && CHR(VSI+1) == KEY_I) ) {
        
        VWSM = VSI;
        hBPC = _index - VWSM;
    } else if (CHR(VEI-1) == KEY_A && CHR(VEI) == KEY_Y) {
        VWSM = VEI - 1;
        hBPC = (_index - VEI) + 1;
    } else if (CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_O) {
        VWSM = VSI + 1;
        hBPC = _index - VWSM;
    } else if (CHR(VSI+1) == KEY_O || CHR(VSI+1) == KEY_U) {
        VWSM = VEI - 1;
        hBPC = (_index - VEI) + 1;
    } else if (CHR(VSI) == KEY_O || CHR(VSI) == KEY_U) {
        VWSM = VEI;
        hBPC = (_index - VEI);
    }
    
    //rule 3.1
    if ((CHR(VSI) == KEY_I && (TypingWord[VSI+1] & (KEY_E | TONE_MASK))) ||
        (CHR(VSI) == KEY_Y && (TypingWord[VSI+1] & (KEY_E | TONE_MASK))) ||
        (CHR(VSI) == KEY_U && (TypingWord[VSI+1] == (KEY_O | TONE_MASK))) ||
        ((TypingWord[VSI] == (KEY_U | TONEW_MASK)) && (TypingWord[VSI+1] == (KEY_O | TONEW_MASK)))){
        
        if (VSI+2 < _index) {
            if (CHR(VSI+2) == KEY_P || CHR(VSI+2) == KEY_T ||
                CHR(VSI+2) == KEY_M || CHR(VSI+2) == KEY_N ||
                CHR(VSI+2) == KEY_O || CHR(VSI+2) == KEY_U ||
                CHR(VSI+2) == KEY_I || CHR(VSI+2) == KEY_C ||
                (VSI+3 < _index && CHR(VSI+2) == KEY_C && CHR(VSI+2) == KEY_H) ||
                (VSI+3 < _index && CHR(VSI+2) == KEY_N && CHR(VSI+2) == KEY_H) ||
                (VSI+3 < _index && CHR(VSI+2) == KEY_N && CHR(VSI+2) == KEY_G)) {
                
                VWSM = VSI + 1;
                hBPC = _index - VWSM;
            } else {
                VWSM = VSI;
                hBPC = _index - VWSM;
            }
        } else {
            VWSM = VSI;
            hBPC = _index - VWSM;
        }
    }
    //rule 3.2
    else if ((CHR(VSI) == KEY_I && (CHR(VSI) == KEY_A)) ||
             (CHR(VSI) == KEY_Y && (CHR(VSI) == KEY_A)) ||
             (CHR(VSI) == KEY_U && (CHR(VSI) == KEY_A)) ||
             (CHR(VSI) == KEY_U && (TypingWord[VSI+1] == (KEY_U | TONEW_MASK)))){
        
        VWSM = VSI;
        hBPC = _index - VWSM;
    }
    
    //rule 4
    if (vowelCount == 2) {
        if (((CHR(VSI) == KEY_I) && (CHR(VSI+1) == KEY_A)) ||
            ((CHR(VSI) == KEY_I) && (CHR(VSI+1) == KEY_U)) ||
            ((CHR(VSI) == KEY_I) && (CHR(VSI+1) == KEY_O))) {
            
            if (VSI == 0 || (CHR(VSI-1) != KEY_G)) { //dont have G
                VWSM = VSI;
                hBPC = _index - VWSM;
            } else {
                VWSM = VSI + 1;
                hBPC = _index - VWSM;
            }
        } else if ((CHR(VSI) == KEY_U) && (CHR(VSI+1) == KEY_A)) {
            if (VSI == 0 || (CHR(VSI-1) != KEY_Q)) { //dont have Q
                if (VEI + 1 >= _index || !canHasEndConsonant()) {
                    VWSM = VSI;
                    hBPC = _index - VWSM;
                }
            } else {
                VWSM = VSI + 1;
                hBPC = _index - VWSM;
            }
        } else if ((CHR(VSI) == KEY_O) && (CHR(VSI+1) == KEY_O)) { //thoong
            VWSM = VEI;
            hBPC = _index - VWSM;
        }
    }
}

void handleOldMark() {
    //default
    if (vowelCount == 0 && CHR(VEI) == KEY_I)
        VWSM = VEI;
    else
        VWSM = VSI;
    hBPC = (_index - VWSM);
    
    //rule 2
    if (vowelCount == 3 || (VEI + 1 < _index && IS_CONSONANT(CHR(VEI + 1)) && canHasEndConsonant())) {
        VWSM = VSI + 1;
        hBPC = _index - VWSM;
    }
    
    //rule 3
    for (ii = VSI; ii <= VEI; ii++) {
        if ((CHR(ii) == KEY_E && TypingWord[ii] & TONE_MASK) || (CHR(ii) == KEY_O && TypingWord[ii] & TONEW_MASK)) {
            VWSM = ii;
            hBPC = _index - VWSM;
            break;
        }
    }
    
    hNCC = hBPC;
}

void insertMark(const Uint32& markMask, const bool& canModifyFlag) {
    vowelCount = 0;
    
    if (canModifyFlag)
        hCode = vWillProcess;
    hBPC = hNCC = 0;
    
    findAndCalculateVowel();
    VWSM = 0;
    
    //detect mark position
    if (vowelCount == 1) {
        VWSM = VEI;
        hBPC = (_index - VEI);
    } else { //vowel = 2 or 3
        if (vUseModernOrthography == 0)
            handleOldMark();
        else
            handleModernMark();
        if (TypingWord[VEI] & TONE_MASK || TypingWord[VEI] & TONEW_MASK)
            vowelWillSetMark = VEI;
    }
    
    //send data
    kk = _index - 1 - VSI;
    //if duplicate same mark -> restore
    if (TypingWord[VWSM] & markMask) {
        
        TypingWord[VWSM] &= ~MARK_MASK;
        if (canModifyFlag)
            hCode = vRestore;
        for (ii = VSI; ii < _index; ii++) {
            TypingWord[ii] &= ~MARK_MASK;
            hData[kk--] = GET(TypingWord[ii]);
        }
        //_index = 0;
        tempDisableKey = true;
    } else {
        //remove other mark
        TypingWord[VWSM] &= ~MARK_MASK;
        
        //add mark
        TypingWord[VWSM] |= markMask;
        for (ii = VSI; ii < _index; ii++) {
            if (ii != VWSM) { //remove mark for other vowel
                TypingWord[ii] &= ~MARK_MASK;
            }
            hData[kk--] = GET(TypingWord[ii]);
        }
        
        hBPC = _index - VSI;
    }
    hNCC = hBPC;
}

void insertD(const Uint16& data, const bool& isCaps) {
    hCode = vWillProcess;
    hBPC = 0;
    for (ii = _index - 1; ii >= 0; ii--) {
        hBPC++;
        if (CHR(ii) == KEY_D) { //reverse unicode char
            if (TypingWord[ii] & TONE_MASK) {
                //restore and disable temporary
                hCode = vRestore;
                TypingWord[ii] &= ~TONE_MASK;
                hData[_index - 1 - ii] = TypingWord[ii];
                tempDisableKey = true;
                break;
            } else {
                TypingWord[ii] |= TONE_MASK;
                hData[_index - 1 - ii] = GET(TypingWord[ii]);
            }
            break;
        } else { //preresent old char
            hData[_index - 1 - ii] = GET(TypingWord[ii]);
        }
    }
    hNCC = hBPC;
}

void insertAOE(const Uint16& data, const bool& isCaps) {
    findAndCalculateVowel();
    
    //remove W tone
    for (ii = VSI; ii <= VEI; ii++) {
        TypingWord[ii] &= ~TONEW_MASK;
    }
    
    hCode = vWillProcess;
    hBPC = 0;
    
    for (ii = _index - 1; ii >= 0; ii--) {
        hBPC++;
        if (CHR(ii) == data) { //reverse unicode char
            if (TypingWord[ii] & TONE_MASK) {
                //restore and disable temporary
                hCode = vRestore;
                TypingWord[ii] &= ~TONE_MASK;
                hData[_index - 1 - ii] = TypingWord[ii];
                //_index = 0;
                if (data != KEY_O) //case thoòng
                    tempDisableKey = true;
                break;
            } else {
                TypingWord[ii] |= TONE_MASK;
                if (!IS_KEY_D(data))
                    TypingWord[ii] &= ~TONEW_MASK;
                hData[_index - 1 - ii] = GET(TypingWord[ii]);
                
            }
            break;
        } else { //preresent old char
            hData[_index - 1 - ii] = GET(TypingWord[ii]);
        }
    }
    hNCC = hBPC;
}

void insertW(const Uint16& data, const bool& isCaps) {
    isRestoredW = false;
    
    findAndCalculateVowel();
    
    //remove ^ tone
    for (ii = VSI; ii <= VEI; ii++) {
        TypingWord[ii] &= ~TONE_MASK;
    }
    
    if (vowelCount > 1) {
        hBPC = _index - VSI;
        hNCC = hBPC;
        
        if (((TypingWord[VSI] & TONEW_MASK) && (TypingWord[VSI+1] & TONEW_MASK)) ||
            ((TypingWord[VSI] & TONEW_MASK) && CHR(VSI+1) == KEY_I) ||
            ((TypingWord[VSI] & TONEW_MASK) && CHR(VSI+1) == KEY_A)){
            //restore and disable temporary
            hCode = vRestore;
            
            for (ii = VSI; ii < _index; ii++) {
                TypingWord[ii] &= ~TONEW_MASK;
                hData[_index - 1 - ii] = GET(TypingWord[ii]) & ~STANDALONE_MASK;
            }
            isRestoredW = true;
            tempDisableKey = true;
        } else {
            hCode = vWillProcess;
            
            if ((CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_O)) {
                if (VSI - 2 >= 0 && TypingWord[VSI - 2] == KEY_T && TypingWord[VSI - 1] == KEY_H) {
                    TypingWord[VSI+1] |= TONEW_MASK;
                    if (VSI + 2 < _index && CHR(VSI+2) == KEY_N) {
                        TypingWord[VSI] |= TONEW_MASK;
                    }
                } else if (VSI - 1 >= 0 && TypingWord[VSI - 1] == KEY_Q) {
                    TypingWord[VSI+1] |= TONEW_MASK;
                } else {
                    TypingWord[VSI] |= TONEW_MASK;
                    TypingWord[VSI+1] |= TONEW_MASK;
                }
            } else if ((CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_A) ||
                       (CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_I) ||
                       (CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_U) ||
                       (CHR(VSI) == KEY_O && CHR(VSI+1) == KEY_I)) {
                TypingWord[VSI] |= TONEW_MASK;
            } else if ((CHR(VSI) == KEY_I && CHR(VSI+1) == KEY_O) ||
                       (CHR(VSI) == KEY_O && CHR(VSI+1) == KEY_A)) {
                TypingWord[VSI+1] |= TONEW_MASK;
            } else {
                //don't do anything
                tempDisableKey = true;
                isChanged = false;
                hCode = vDoNothing;
            }
            
            for (ii = VSI; ii < _index; ii++) {
                hData[_index - 1 - ii] = GET(TypingWord[ii]);
            }
        }
        
        return;
    }
    
    hCode = vWillProcess;
    hBPC = 0;
    
    for (ii = _index - 1; ii >= 0; ii--) {
        if (ii < VSI)
            break;
        hBPC++;
        switch (CHR(ii)) {
            case KEY_A:
            case KEY_U:
            case KEY_O:
                if (TypingWord[ii] & TONEW_MASK) {
                    //restore and disable temporary
                    if (TypingWord[ii] & STANDALONE_MASK) {
                        hCode = vWillProcess;
                        if (CHR(ii) == KEY_U){
                            TypingWord[ii] = KEY_W | ((TypingWord[ii] & CAPS_MASK) ? CAPS_MASK : 0);
                        } else if (CHR(ii) == KEY_O) {
                            hCode = vRestore;
                            TypingWord[ii] = KEY_O | ((TypingWord[ii] & CAPS_MASK) ? CAPS_MASK : 0);
                            isRestoredW = true;
                        }
                        hData[_index - 1 - ii] = TypingWord[ii];
                    } else {
                        hCode = vRestore;
                        TypingWord[ii] &= ~TONEW_MASK;
                        hData[_index - 1 - ii] = TypingWord[ii];
                        isRestoredW = true;
                        //_index++;
                    }
                    
                    tempDisableKey = true;
                } else {
                    TypingWord[ii] |= TONEW_MASK;
                    TypingWord[ii] &= ~TONE_MASK;
                    hData[_index - 1 - ii] = GET(TypingWord[ii]);
                }
                break;
                
            default:
                hData[_index - 1 - ii] = GET(TypingWord[ii]);
                break;
        }
    }
    hNCC = hBPC;
    
    if (isRestoredW) {
        //_index = 0;
    }
}

void reverseLastStandaloneChar(const Uint32& keyCode, const bool& isCaps) {
    hCode = vWillProcess;
    hBPC = 0;
    hNCC = 1;
    hExt = 4;
    TypingWord[_index - 1] = (keyCode | TONEW_MASK | STANDALONE_MASK | (isCaps ? CAPS_MASK : 0));
    hData[0] = GET(TypingWord[_index - 1]);
}

static bool _afterDigit = false;   //the word started right after a digit: a lone w is a unit (100W), not ư
void checkForStandaloneChar(const Uint16& data, const bool& isCaps, const Uint32& keyWillReverse) {
    if (CHR(_index - 1) == keyWillReverse && TypingWord[_index - 1] & TONEW_MASK) {
        hCode = vWillProcess;
        hBPC = 1;
        hNCC = 1;
        TypingWord[_index - 1] = data | (isCaps ? CAPS_MASK : 0);
        hData[0] = GET(TypingWord[_index - 1]);
        return;
    }
    
    //check standalone w -> ư

    if (_index > 0 && CHR(_index-1) == KEY_U && keyWillReverse == KEY_O) {
        insertKey(keyWillReverse, isCaps);
        reverseLastStandaloneChar(keyWillReverse, isCaps);
        return;
    }

    if (_index == 0 && _afterDigit && data == KEY_W) { insertKey(data, isCaps); return; }
    if (_index == 0) { //zero char
        insertKey(data, isCaps, false);
        reverseLastStandaloneChar(keyWillReverse, isCaps);
        return;
    } else if (_index == 1) { //1 char
        for (i = 0; i < _standaloneWbad.size(); i++) {
            if (CHR(0) == _standaloneWbad[i]) {
                insertKey(data, isCaps);
                return;
            }
        }
        insertKey(data, isCaps, false);
        reverseLastStandaloneChar(keyWillReverse, isCaps);
        return;
    } else if (_index == 2) {
        for (i = 0; i < _doubleWAllowed.size(); i++) {
            if (CHR(0) == _doubleWAllowed[i][0] && CHR(1) == _doubleWAllowed[i][1]) {
                insertKey(data, isCaps, false);
                reverseLastStandaloneChar(keyWillReverse, isCaps);
                return;
            }
        }
        insertKey(data, isCaps);
        return;
    }
    
    insertKey(data, isCaps);
}

void upperCaseFirstCharacter() {
    if (!(TypingWord[0] & CAPS_MASK)) {
        hCode = vWillProcess;
        hBPC = 0;
        hNCC = 1;
        TypingWord[0] |= CAPS_MASK;
        hData[0] = GET(TypingWord[0]);
        _upperCaseStatus = 0;
        if (vUseMacro)
            hMacroKey[0] |= CAPS_MASK;
    }
}

void handleMainKey(const Uint16& data, const bool& isCaps) {
    //if is Z key, remove mark
    if (IS_KEY_Z(data)) {
        removeMark();
        if (!isChanged) {
            insertKey(data, isCaps);
        }
        return;
    }
    
    if (data == KEY_LEFT_BRACKET) { //standalone key [
        checkForStandaloneChar(data, isCaps, KEY_O);
        return;
    }
    
    if (data == KEY_RIGHT_BRACKET) { //standalone key }
        checkForStandaloneChar(data, isCaps, KEY_U);
        return;
    }
    
    //if is D key
    if (IS_KEY_D(data)) {
        isCorect = false;
        isChanged = false;
        k = _index;
        for (i = 0; i < _consonantD.size(); i++) {
            if (_index < _consonantD[i].size())
                continue;
            isCorect = true;
            checkCorrectVowel(_consonantD, i, k, data);
            
            //allow d after consonant
            if (!isCorect && _index - 2 >= 0 && CHR(_index-1) == KEY_D && IS_CONSONANT(CHR(_index-2))) {
                isCorect = true;
            }
            if (isCorect) {
                isChanged = true;
                insertD(data, isCaps);
                break;
            }
        }
    
        if (!isChanged) {
            insertKey(data, isCaps);
        }
        return;
    }
    
    //if is mark key
    if (IS_MARK_KEY(data)) {
        for (i = 0; i < 6; i++) {
            const Array2D16& charset = _vowelForMarkStatic[i].vowelSet;
            isCorect = false;
            isChanged = false;
            k = _index;
            for (l = 0; l < charset.size(); l++) {
                if (_index < charset[l].size())
                    continue;
                isCorect = true;
                checkCorrectVowel(charset, l, k, data);
                
                if (isCorect) {
                    isChanged = true;
                    if (IS_KEY_S(data))
                        insertMark(MARK1_MASK);
                    else if (IS_KEY_F(data))
                        insertMark(MARK2_MASK);
                    else if (IS_KEY_R(data))
                        insertMark(MARK3_MASK);
                    else if (IS_KEY_X(data))
                        insertMark(MARK4_MASK);
                    else if (IS_KEY_J(data))
                        insertMark(MARK5_MASK);
                    break;
                }
            }

            if (isCorect) {
                break;
            }
        }
        
        if (!isChanged) {
            insertKey(data, isCaps);
        }
        
        return;
    }
    
    //check Vowel
    if (vInputType == vVNI) {
        for (i = _index-1; i >= 0; i--) {
            if (CHR(i) == KEY_O || CHR(i) == KEY_A || CHR(i) == KEY_E) {
                VEI = i;
                break;
            }
        }
    }
    
    keyForAEO = ((vInputType != vVNI) ? data : ((data == KEY_7 || data == KEY_8 ? KEY_W : (data == KEY_6 ? TypingWord[VEI] : data))));
    const Array2D16& charset = _vowel[keyForAEO];
    isCorect = false;
    isChanged = false;
    k = _index;
    for (i = 0; i < charset.size(); i++) {
        if (_index < charset[i].size())
            continue;
        isCorect = true;
        checkCorrectVowel(charset, i, k, data);
        
        if (isCorect) {
            isChanged = true;
            if (IS_KEY_DOUBLE(data)) {
                insertAOE(keyForAEO, isCaps);
            } else if (IS_KEY_W(data)) {
                if (vInputType == vVNI) {
                    for (j = _index-1; j >= 0; j--) {
                        if (CHR(j) == KEY_O || CHR(j) == KEY_U ||CHR(j) == KEY_A || CHR(j) == KEY_E) {
                            VEI = j;
                            break;
                        }
                    }
                    if ((data == KEY_7 && CHR(VEI) == KEY_A && (VEI-1>=0 ? CHR(VEI-1) != KEY_U : true)) || (data == KEY_8 && (CHR(VEI) == KEY_O || CHR(VEI) == KEY_U)))
                        break;
                }
                insertW(keyForAEO, isCaps);
            }
            break;
        }
    }
    
    if (!isChanged) {
        if (data == KEY_W && vInputType != vSimpleTelex1 && vAllowStandaloneW) {
            checkForStandaloneChar(data, isCaps, KEY_U);
        } else {
            insertKey(data, isCaps);
        }
    }
}

void handleQuickTelex(const Uint16& data, const bool& isCaps) {
    hCode = vWillProcess;
    hBPC = 1;
    hNCC = 2;
    hData[1] = _quickTelex[data][0] | (isCaps ? CAPS_MASK : 0);
    hData[0] = _quickTelex[data][1] | (isCaps ? CAPS_MASK : 0);
    insertKey(_quickTelex[data][1], isCaps, false);
}

bool checkRestoreIfWrongSpelling(const int& handleCode) {
    for (ii = 0; ii < _index; ii++) {
        if (!IS_CONSONANT(CHR(ii)) &&
            (TypingWord[ii] & MARK_MASK || TypingWord[ii] & TONE_MASK || TypingWord[ii] & TONEW_MASK)) {
            
            hCode = handleCode;
            hBPC = _index;
            hNCC = _stateIndex;
            for (i = 0; i < _stateIndex; i++) {
                TypingWord[i] = KeyStates[i];
                hData[_stateIndex - 1 - i] = TypingWord[i];
            }
            _index = _stateIndex;
            return true;
        }
    }
    return false;
}

void vTempOffSpellChecking() {
    if (_useSpellCheckingBefore) {
        vCheckSpelling = vCheckSpelling ? 0 : 1;
    }
}

void vSetCheckSpelling() {
    _useSpellCheckingBefore = vCheckSpelling;
}

void vTempOffEngine(const bool& off) {
    _willTempOffEngine = off;
}

bool checkQuickConsonant() {
    if (_index <= 1) return false;
    l = 0;
    if (_index > 0) {
        if (vQuickStartConsonant && _quickStartConsonant.find(CHR(0)) != _quickStartConsonant.end()) {
            hCode = vRestore;
            hBPC = _index;
            hNCC = _index + 1;
            if (_index < MAX_BUFF-1)
                _index++;
            //right shift
            for (i = _index-1; i >= 2; i--) {
                TypingWord[i] = TypingWord[i-1];
            }
            TypingWord[1] = _quickStartConsonant[CHR(0)][1] | ((TypingWord[0] & CAPS_MASK) && (TypingWord[2] & CAPS_MASK) ? CAPS_MASK : 0);
            TypingWord[0] = _quickStartConsonant[CHR(0)][0] | (TypingWord[0] & CAPS_MASK ? CAPS_MASK : 0);
            l = 1;;
        }
        if (vQuickEndConsonant &&
            (_index-2 >= 0 && !IS_CONSONANT(CHR(_index-2))) &&
            _quickEndConsonant.find(CHR(_index-1)) != _quickEndConsonant.end()) {
            hCode = vRestore;
            if (l == 1) {
                hNCC++;
            } else {
                hBPC = 1;
                hNCC = 2;
            }
            if (_index < MAX_BUFF-1)
                _index++;
            TypingWord[_index-1] = _quickEndConsonant[CHR(_index-2)][1] | (TypingWord[_index-2] & CAPS_MASK ? CAPS_MASK : 0);
            TypingWord[_index-2] = _quickEndConsonant[CHR(_index-2)][0] | (TypingWord[_index-2] & CAPS_MASK ? CAPS_MASK : 0);
            
            l = 1;
        }
        if (l == 1) {
            _hasHandleQuickConsonant = true;
            for (i = _index - 1; i >= 0; i--) {
                hData[_index - 1 - i] = GET(TypingWord[i]);
            }
            return true;
        }
    }
    return false;
}

/*==========================================================================================================*/
/* Auto switch language from typing behavior.
 * Ported from Vikey (https://github.com/tonyminibot/Vikey, GPL v3, © Tony Dinh), which is based on
 * OpenKey too. Rule numbers (D5, D22, ...) refer to Vikey's DECISIONS.md.
 * Everything here runs only at a word break (or on a doubled mark key), never per letter, and
 * uses fixed tables compiled into the binary: no file or system calls.
 *
 * Words are compared as a Telex-normalized ASCII key: circumflex -> doubled vowel (aa ee oo),
 * breve/horn -> letter + w (aw ow uw), đ -> dd, tone letter (s f r x j) at the end.
 * That makes the check independent of old/new tone placement.
 */
#define AS_MISSING (-1000)
#define AS_KEY_LEN 40
//Scores are 10*log10(occurrences per million). When the keys read as a word in both languages,
//leave the current language only if the word in the current language is rare (below its floor)
//and the other reading is at least AS_PREFER points (AS_PREFER/10 orders of magnitude) more common.
#define AS_PREFER 10
#define AS_VN_FLOOR 10      //Vietnamese words used at least ~10 per million are never switched away from
#define AS_EN_FLOOR 99      //no floor: a tie converts when the Vietnamese reading is AS_PREFER points more common (D22)

int vAutoSwitchPaused = 0;
int vAutoSwitchRequest = -1;
Uint32 vAutoSwitchUndoData[MAX_BUFF];
int vAutoSwitchUndoCount = 0;

static char _asLetter[256];
static bool _asLetterReady = false;
static bool _asWordStartOk = true;   //the current word began after a space, Enter, Tab or a fresh start (not inside foo.bar or my_var)

/* Look-back: when a word confidently switches the language, the 1-2 words typed just before it
 * (separated by single spaces) are fixed too if they were ambiguous, e.g. "thí make" -> "this make".
 * An earlier word follows the switch unless its current reading is AS_LOOKBACK_MARGIN points more common. */
#define AS_LOOKBACK_WORDS 2
#define AS_PLAIN_MARGIN 15    //"thí makes": the earlier word must read English ~30x more often than Vietnamese (this vs thí)
#define AS_LOOKBACK_MARGIN 10
struct AsWord {
    Uint32 raw[MAX_BUFF];   //keys as typed
    int rawCount;
    Uint32 shown[MAX_BUFF]; //what is on screen (engine character data)
    int shownCount;
    int en, vn;             //scores of the raw keys as English and of the Vietnamese reading
    bool lookbackOk;        //could follow a switch to English
    bool same;              //looks the same in both modes (nothing to fix)
};
static AsWord _asPrev[AS_LOOKBACK_WORDS];
static int _asPrevCount = 0;

void vAutoSwitchForget() {
    _asPrevCount = 0;
    _asWordStartOk = true;   //a fresh context (language switched, app changed): the next word stands on its own
}

static void asInitLetters() {
    if (_asLetterReady) return;
    memset(_asLetter, 0, sizeof(_asLetter));
    const Uint16 keys[26] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
                             KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
    for (int n = 0; n < 26; n++) _asLetter[keys[n] & 0xFF] = (char)('a' + n);
    _asLetterReady = true;
}

static inline char asLetterOf(const Uint32& data) {
    Uint32 code = data & CHAR_MASK;
    return code < 256 ? _asLetter[code] : 0;
}

static int asLookup(const char* words, const unsigned int* offsets, const signed char* scores, const int& count, const char* word) {
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int c = strcmp(words + offsets[mid], word);
        if (c == 0) return scores[mid];
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return AS_MISSING;
}

static inline int asVietnameseScore(const char* key) {
    return asLookup(kAutoSwitchVnWords, kAutoSwitchVnOffsets, kAutoSwitchVnScores, kAutoSwitchVnCount, key);
}

static inline int asEnglishScore(const char* word) {
    return asLookup(kAutoSwitchEnWords, kAutoSwitchEnOffsets, kAutoSwitchEnScores, kAutoSwitchEnCount, word);
}

/** Normalized key of the word in TypingWord; transformed = it has a tone, ^, w-horn/breve or đ. */
static bool asWordKey(char* out, bool& transformed) {
    transformed = false;
    if (_index == 0 || _longWordHelper.size() > 0) return false;
    int n = 0;
    char tone = 0;
    for (int x = 0; x < _index; x++) {
        Uint32 t = TypingWord[x];
        char c = asLetterOf(t);
        if (c == 0 || n + 3 >= AS_KEY_LEN) return false;
        out[n++] = c;
        if ((t & TONE_MASK) && (c == 'a' || c == 'e' || c == 'o' || c == 'd')) {
            out[n++] = c;
            transformed = true;
        } else if ((t & TONEW_MASK) && (c == 'a' || c == 'o' || c == 'u')) {
            out[n++] = 'w';
            transformed = true;
        }
        switch (t & MARK_MASK) {
            case MARK1_MASK: tone = 's'; break;
            case MARK2_MASK: tone = 'f'; break;
            case MARK3_MASK: tone = 'r'; break;
            case MARK4_MASK: tone = 'x'; break;
            case MARK5_MASK: tone = 'j'; break;
        }
    }
    if (tone) {
        out[n++] = tone;
        transformed = true;
    }
    out[n] = 0;
    return true;
}

/** Lowercase letters of raw key codes; false if any key isn't a letter. */
static bool asRawWord(const Uint32* keys, const int& count, char* out) {
    if (count <= 0 || count >= AS_KEY_LEN) return false;
    for (int x = 0; x < count; x++) {
        out[x] = asLetterOf(keys[x]);
        if (out[x] == 0) return false;
    }
    out[count] = 0;
    return true;
}

//the word lists and the Telex replay are Telex-only; undo and look-back count characters, which only
//holds for the Unicode code table (VNI Windows and Unicode compound use 2 units for some letters)
static inline bool asEnabled() {
    return vAutoSwitchByTyping && !vAutoSwitchPaused && vInputType != vVNI && vCodeTable == 0;
}

bool vAutoSwitchEnabled() {
    return asEnabled();
}

static Uint32 _asDoubleKeys[MAX_BUFF];   //the keys of the word a doubled tone key switched to English, both presses last
static int _asDoubleCount = 0;
static int _asDoubleFirst = -1;      //where the first press is in _asDoubleKeys
static bool _asDoubleBoth = false;   //both presses are on screen
static bool _asApostrophe = false;   //the word ends at an apostrophe: allow contraction stems
static bool asFewCapitals(const Uint32* keys, const int& count);

/** Everything the engine keeps between keys, so a word can be replayed on a scratch session. */
struct AsEngineState {
    Uint32 word[MAX_BUFF], states[MAX_BUFF];
    Byte index, stateIndex, upperStatus;
    bool tempDisable, useSpellingBefore, wordStartOk, afterDigit, macroHandled, quickConsonant, tempOff;
    int dropN; int dropPos[AS_MAX_DROPS]; Uint32 dropKey[AS_MAX_DROPS];
    Uint32 doubleKeys[MAX_BUFF]; int doubleCount, doubleFirst; bool doubleBoth;
    int spaceCount, checkSpelling, language, useMacro, upperFirst, request;
    FixedState longWord, special, statesData;
    size_t typingCount;
    vKeyHookState hook;
};

static void asSaveState(AsEngineState& st) {
    memcpy(st.word, TypingWord, sizeof(TypingWord));
    memcpy(st.states, KeyStates, sizeof(KeyStates));
    st.index = _index; st.stateIndex = _stateIndex; st.upperStatus = _upperCaseStatus;
    st.tempDisable = tempDisableKey; st.useSpellingBefore = _useSpellCheckingBefore;
    st.wordStartOk = _asWordStartOk; st.afterDigit = _afterDigit;
    st.macroHandled = _hasHandledMacro; st.quickConsonant = _hasHandleQuickConsonant; st.tempOff = _willTempOffEngine;
    st.dropN = _asDropN; memcpy(st.dropPos, _asDropPos, sizeof(_asDropPos)); memcpy(st.dropKey, _asDropKey, sizeof(_asDropKey));
    memcpy(st.doubleKeys, _asDoubleKeys, sizeof(_asDoubleKeys));
    st.doubleCount = _asDoubleCount; st.doubleFirst = _asDoubleFirst; st.doubleBoth = _asDoubleBoth;
    st.spaceCount = _spaceCount; st.checkSpelling = vCheckSpelling; st.language = vLanguage;
    st.useMacro = vUseMacro; st.upperFirst = vUpperCaseFirstChar; st.request = vAutoSwitchRequest;
    st.longWord = _longWordHelper;
    st.special = _specialChar;
    st.statesData = _typingStatesData;
    st.typingCount = _typingStates.size();   //a replay types letters only, which never adds or removes typing states
    st.hook = std::move(HookState);
    HookState.macroKey.clear();
    HookState.macroData.clear();
}

static void asRestoreState(AsEngineState& st) {
    memcpy(TypingWord, st.word, sizeof(TypingWord));
    memcpy(KeyStates, st.states, sizeof(KeyStates));
    _index = st.index; _stateIndex = st.stateIndex; _upperCaseStatus = st.upperStatus;
    tempDisableKey = st.tempDisable; _useSpellCheckingBefore = st.useSpellingBefore;
    _asWordStartOk = st.wordStartOk; _afterDigit = st.afterDigit;
    _hasHandledMacro = st.macroHandled; _hasHandleQuickConsonant = st.quickConsonant; _willTempOffEngine = st.tempOff;
    _asDropN = st.dropN; memcpy(_asDropPos, st.dropPos, sizeof(_asDropPos)); memcpy(_asDropKey, st.dropKey, sizeof(_asDropKey));
    memcpy(_asDoubleKeys, st.doubleKeys, sizeof(_asDoubleKeys));
    _asDoubleCount = st.doubleCount; _asDoubleFirst = st.doubleFirst; _asDoubleBoth = st.doubleBoth;
    _spaceCount = st.spaceCount; vCheckSpelling = st.checkSpelling; vLanguage = st.language;
    vUseMacro = st.useMacro; vUpperCaseFirstChar = st.upperFirst; vAutoSwitchRequest = st.request;
    _longWordHelper = st.longWord;
    _specialChar = st.special;
    _typingStatesData = st.statesData;
    while (_typingStates.size() > st.typingCount) _typingStates.pop_back();
    HookState = std::move(st.hook);
}

/** Replays raw keys through the Telex engine on a scratch session. Returns the Vietnamese score of
 *  the result (AS_MISSING if it isn't a real syllable or nothing changed) and the result characters. */
static int asReplay(const Uint32* keys, const int& count, Uint32* result, int& resultCount, bool* changed = NULL) {
    resultCount = 0;
    AsEngineState saved;
    asSaveState(saved);
    vUseMacro = 0;
    vUpperCaseFirstChar = 0;
    _spaceCount = 0;
    _willTempOffEngine = false;
    _afterDigit = false;
    _specialChar.clear();
    startNewSession();
    for (int x = 0; x < count; x++) {
        vKeyHandleEvent(vKeyEvent::Keyboard, vKeyEventState::KeyDown, (Uint16)(keys[x] & CHAR_MASK), (keys[x] & CAPS_MASK) ? 1 : 0, false);
    }
    int vn = AS_MISSING;
    char word[AS_KEY_LEN];
    bool transformed;
    bool keyOk = asWordKey(word, transformed);
    if (changed) *changed = keyOk ? transformed : true;
    if (keyOk && transformed) {
        vn = asVietnameseScore(word);
        if (vn != AS_MISSING) {
            for (int x = 0; x < _index; x++) result[resultCount++] = GET(TypingWord[x]);
        }
    }
    asRestoreState(saved);
    return vn;
}

// ---- Word pairs for ties (D30) -----------------------------------------------------------------------------
// For a word that is a word both ways (bus/bú, been/bên), how often each reading follows the previous word
// in real text ("cho con bú" vs "the bus"), from the built-in table.
#define AS_BIGRAM_W 2.0f   //weight of a pair vote (Vikey's tuning)
#define AS_BIGRAM_MIN 3    //pairs seen fewer times than this say nothing
#define AS_NEXT_PAIR 20    //how sure (20 = 100x) the following word must be of the Vietnamese reading to flip a word kept English (D35)

static Uint16 asKeyOfLetter(char c) {
    static const Uint16 k[26] = {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,
                                 KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
    return (c >= 'a' && c <= 'z') ? k[c - 'a'] : 0;
}

static std::string asUtf8(const Uint16* t, int n) {
    std::string s;
    for (int x = 0; x < n; x++) { unsigned c = t[x];
        if (c < 0x80) s += (char)c; else if (c < 0x800) { s += (char)(0xC0 | c >> 6); s += (char)(0x80 | (c & 63)); }
        else { s += (char)(0xE0 | c >> 12); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); } }
    return s;
}

/** The word as it shows (lowercase UTF-8): the letters, or their Vietnamese reading in Telex. */
static bool asSurface(const char* letters, const bool& vietnamese, std::string& out) {
    if (!vietnamese) { out = letters; return true; }
    int n = (int)strlen(letters);
    if (n == 0 || n >= AS_KEY_LEN - 1) return false;
    asInitLetters();
    Uint32 keys[AS_KEY_LEN], result[MAX_BUFF];
    for (int x = 0; x < n; x++) { keys[x] = asKeyOfLetter(letters[x]); if (!keys[x] && letters[x] != 'a') return false; }
    int count = 0;
    bool changed = false;
    if (asReplay(keys, n, result, count, &changed) != AS_MISSING && count > 0) {
        Uint16 text[MAX_BUFF];
        for (int x = 0; x < count; x++) text[x] = (result[x] & CHAR_CODE_MASK) ? (Uint16)(result[x] & 0xFFFF) : keyCodeToCharacter(result[x]);
        out = asUtf8(text, count);
        return true;
    }
    if (!changed && asVietnameseScore(letters) != AS_MISSING) {   //plain syllables that look the same in Telex: cho, anh, ban
        out = letters;
        return true;
    }
    return false;
}

static int asBuiltInPair(const std::string& key) {
    int lo = 0, hi = kBigramCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(kBigramKeys + kBigramOffsets[mid], key.c_str());
        if (c == 0) return (int)lround(exp2(kBigramLogCounts[mid] / 8.0));
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

/** > 0: the Vietnamese reading follows prev more often, < 0: the English one (10 = 10x); 0: no data. */
static int asBigramVote(const char* prevLetters, const bool& prevVietnamese, const char* letters) {
    std::string p, vn;
    if (!asSurface(prevLetters, prevVietnamese, p) || !asSurface(letters, true, vn)) return 0;
    int cv = asBuiltInPair(p + "\t" + vn), ce = asBuiltInPair(p + "\t" + letters);
    if (cv + ce < AS_BIGRAM_MIN) return 0;
    double v = 10 * log10((cv + 0.5) / (ce + 0.5));
    if (v > 30) v = 30; if (v < -30) v = -30;
    return (int)lround(v * AS_BIGRAM_W);
}

/** Look-forward (D35): the word after a word kept English. > 0 when the Vietnamese reading of the earlier word
 *  ("í", "cả") is followed by this word more often than the English one ("is", "car"); 0 without data. */
static int asNextPairVote(const char* prevLetters, const char* nextLetters) {
    std::string pv, nx;
    if (!asSurface(prevLetters, true, pv) || !asSurface(nextLetters, true, nx)) return 0;
    int cv = asBuiltInPair(pv + "\t" + nx), ce = asBuiltInPair(std::string(prevLetters) + "\t" + nx);
    if (cv + ce < AS_BIGRAM_MIN) return 0;
    double v = 10 * log10((cv + 0.5) / (ce + 0.5));
    if (v > 30) v = 30; if (v < -30) v = -30;
    return (int)lround(v * AS_BIGRAM_W);
}

/** Vietnamese mode: a word was ended by a single space and stays Vietnamese. Keep it for look-back. */
static void asRememberWord() {
    if (!asEnabled()) return;
    asInitLetters();
    AsWord w;
    w.lookbackOk = false;
    w.same = false;
    w.rawCount = _stateIndex;
    w.shownCount = _index;
    if (_stateIndex <= 0 || _index <= 0 || _stateIndex >= MAX_BUFF || _longWordHelper.size() > 0) { _asPrevCount = 0; return; }
    for (int x = 0; x < _stateIndex; x++) w.raw[x] = KeyStates[x];
    for (int x = 0; x < _index; x++) w.shown[x] = GET(TypingWord[x]);
    char word[AS_KEY_LEN], raw[AS_KEY_LEN];
    bool transformed;
    w.en = w.vn = AS_MISSING;
    if (_asWordStartOk && _stateIndex >= _index && asWordKey(word, transformed) && asRawWord(KeyStates, _stateIndex, raw)) {
        w.en = asEnglishScore(raw);
        if (transformed) {        //ambiguous: a real word both ways, and English not much rarer
            w.vn = asVietnameseScore(word);
            w.lookbackOk = w.en != AS_MISSING && w.vn != AS_MISSING && w.en >= w.vn - AS_LOOKBACK_MARGIN;
        } else {                  //looks the same in both modes: look-back passes through English-only words ("makes")
            w.lookbackOk = w.en != AS_MISSING && asVietnameseScore(raw) == AS_MISSING;
            w.same = true;
        }
    }
    for (int x = AS_LOOKBACK_WORDS - 1; x > 0; x--) _asPrev[x] = _asPrev[x - 1];
    _asPrev[0] = w;
    if (_asPrevCount < AS_LOOKBACK_WORDS) _asPrevCount++;
}

static int asStatesWithDrops(Uint32* full, const int& maxN) {
    //newest drop first: an older drop's position never counts a newer dropped key
    int n = _stateIndex;
    if (n > maxN) n = maxN;
    memcpy(full, KeyStates, n * sizeof(Uint32));
    for (int d = _asDropN - 1; d >= 0; d--) {
        int p = _asDropPos[d];
        if (p < 0 || p > n || n + 1 > maxN) continue;
        memmove(full + p + 1, full + p, (n - p) * sizeof(Uint32));
        full[p] = _asDropKey[d]; n++;
    }
    return n;
}

/** Vietnamese mode, at a word break: are the raw keys an English word the user clearly meant? */
static bool asShouldRestoreToEnglishCore() {
    if (!asEnabled() || _hasHandledMacro || !_asWordStartOk) return false;
    asInitLetters();
    if (_stateIndex < 2 || _stateIndex < _index) return false;
    char word[AS_KEY_LEN], raw[AS_KEY_LEN];
    bool transformed;
    bool wordOk = asWordKey(word, transformed);
    //Telex dropped a key on the way ("seemed" -> "semed", "cheese" -> "chees"): with it back, an English word?
    if (_asDropN > 0 && _stateIndex >= 3 && _stateIndex + _asDropN < MAX_BUFF - 1) {   //4+ keys: "aaa" stays Telex "aa"
        Uint32 full[MAX_BUFF];
        int n = asStatesWithDrops(full, MAX_BUFF);
        bool savedTemp = tempDisableKey;
        checkSpelling(true);
        bool invalid = tempDisableKey;   //the shown word is no syllable ("chée")
        tempDisableKey = savedTemp;
        if (asRawWord(full, n, raw) && asEnglishScore(raw) != AS_MISSING && (!wordOk || !transformed || invalid || asVietnameseScore(word) == AS_MISSING)) {
            memcpy(KeyStates, full, n * sizeof(Uint32));   //restore puts these keys back
            _stateIndex = n;
            _asDropN = 0;
            return true;
        }
    }
    if (!wordOk) return false;
    if (!transformed) {
        //no marks left, but Telex ate a key on the way ("seemed" -> "semed", "cheese" -> "chees"): put back an English word.
        //Quick Telex changes keys too (conn -> cong, gg -> gi): a word that came out as a real syllable stays
        if (asRawWord(KeyStates, _stateIndex, raw) && strcmp(raw, word) != 0 && asEnglishScore(raw) != AS_MISSING &&
            asVietnameseScore(word) == AS_MISSING)
            return true;
        //An English-only word that looks the same in both modes ("makes") right after an ambiguous word that
        //stayed Vietnamese ("thí" = this): together they are English. Switch now and fix the earlier word.
        if (_asPrevCount < 1 || !_asPrev[0].lookbackOk || _asPrev[0].same) return false;
        if (_asPrev[0].en < _asPrev[0].vn + AS_PLAIN_MARGIN) return false;   //the earlier word must lean English
        if (!asRawWord(KeyStates, _stateIndex, raw)) return false;
        return asEnglishScore(raw) != AS_MISSING && asVietnameseScore(raw) == AS_MISSING;
    }
    if (!asRawWord(KeyStates, _stateIndex, raw)) return false;
    int en = asEnglishScore(raw);
    int vn = asVietnameseScore(word);

#define AS_ACRONYM_VN 5         //below ~3 per million: tuned on Tinhte (acronyms) vs all-caps Vietnamese news
#define AS_ACRONYM_VN_AMID 15   //right after a lowercase word ("con NAS này"): Caps Lock Vietnamese comes in runs (D27)
    //2+ capitals (NAS, HDD, SATA, GoPro) that Telex turned into a rare or invalid syllable: an acronym or a name (D23)
    //not inside a run of capitals: no earlier word, or a lowercase one
    bool afterLower = _asPrevCount < 1 || _asPrev[0].rawCount < 1;
    if (!afterLower) { afterLower = true; for (int x = 0; x < _asPrev[0].rawCount; x++) if (_asPrev[0].raw[x] & CAPS_MASK) afterLower = false; }
    if (!asFewCapitals(KeyStates, _stateIndex) && (vn == AS_MISSING || vn < (afterLower ? AS_ACRONYM_VN_AMID : AS_ACRONYM_VN))) {
        bool vowelChanged = false;   //Vietnamese acronyms only have đ changed (VĐV, HĐND, BĐS): they stay
        for (int x = 0; x < _index; x++) {
            char c = asLetterOf(TypingWord[x]);
            if ((c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y') && (TypingWord[x] & (MARK_MASK | TONE_MASK | TONEW_MASK)))
                vowelChanged = true;
        }
        if (vowelChanged) return true;
    }
    if (en == AS_MISSING && _asApostrophe) {             //a contraction stem: doesn't, isn't, wasn't -> is it "does" + n?
        size_t n = strlen(raw);
        if (n >= 3 && raw[n - 1] == 'n') { raw[n - 1] = 0; en = asEnglishScore(raw); raw[n - 1] = 'n'; }
    }
#define AS_RESTORE_UNKNOWN 3   //min letters for restoring a word that is neither Vietnamese nor in the English list
    //not a Vietnamese syllable and not a known English word either: a foreign word (TestFlight, analytics)
    if (en == AS_MISSING) {
        if (vn != AS_MISSING || _stateIndex < AS_RESTORE_UNKNOWN) return false;
        //Vietnamese-only modifiers (â ê ô ă ơ ư đ) mean the user was writing Vietnamese: a typo, not a foreign
        //word. Unless the keys have several vowel groups: no single syllable has that (analytics -> "ânlytics").
        int vowelGroups = 0;
        bool inVowel = false;
        for (int x = 0; x < _stateIndex; x++) {
            char c = asLetterOf(KeyStates[x]);
            bool v = c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
            if (v && !inVowel) vowelGroups++;
            inVowel = v;
        }
        if (vowelGroups <= 1)
            for (int x = 0; x < _index; x++) if (TypingWord[x] & (TONE_MASK | TONEW_MASK)) return false;
        //rare but valid syllables (riệt, chịt) are not in the list: require the spelling rules to fail too
        bool savedTemp = tempDisableKey;
        checkSpelling(true);
        bool invalid = tempDisableKey;
        tempDisableKey = savedTemp;
        return invalid;
    }
#define AS_VN_EXTREME 99    //an English word this many points more common wins even over a common Vietnamese word
#define AS_TIE_PREV 5       //a tie leaning English (3x) goes English right after a clearly English word (D22)
    //word pairs (D30): the reading that follows the previous word more often in real text
    if (en != AS_MISSING && vn != AS_MISSING && _asPrevCount >= 1) {
        char praw[AS_KEY_LEN];
        if (asRawWord(_asPrev[0].raw, _asPrev[0].rawCount, praw)) {
            int vote = asBigramVote(praw, !_asPrev[0].same, raw);
            if (vote != 0) return en - vn - vote >= AS_PREFER && (vn < AS_VN_FLOOR || vote <= -AS_PREFER);
        }
    }
    if (en != AS_MISSING && vn != AS_MISSING && en - vn >= AS_TIE_PREV && _asPrevCount >= 1 &&
        _asPrev[0].same && _asPrev[0].en != AS_MISSING && _asPrev[0].vn == AS_MISSING)
        return true;   //"video call thí" -> "this"
    return vn == AS_MISSING || (vn < AS_VN_FLOOR && en - vn >= AS_PREFER) || en - vn >= AS_VN_EXTREME;
}

/** Whether to put the word back to the keys typed. A foreign word restored as typed gets back the keys
 *  Telex undid on the way too ("deepseek", not "deepsek"); a known English word without them stays. */
static bool asShouldRestoreToEnglish() {
    if (!asShouldRestoreToEnglishCore()) return false;
    if (_asDropN > 0 && _stateIndex >= 3 && _stateIndex + _asDropN < MAX_BUFF - 1) {
        char raw[AS_KEY_LEN];
        Uint32 full[MAX_BUFF];
        int n = asStatesWithDrops(full, MAX_BUFF - 1);
        //only for words of 2+ vowel groups (deep-seek): in one syllable "ooo" is the Telex undo ("cuooocf" -> "cuoocf")
        int groups = 0; bool inV = false;
        for (int x = 0; x < n; x++) { char c = asLetterOf(full[x]); bool v = c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; if (v && !inV) groups++; inV = v; }
        if (groups >= 2 && !(asRawWord(KeyStates, _stateIndex, raw) && asEnglishScore(raw) != AS_MISSING)) {
            memcpy(KeyStates, full, n * sizeof(Uint32));
            _stateIndex = n;
            _asDropN = 0;
        }
    }
    return true;
}

/** Before a hyphen: a rare w-word ("ưi") starts an English compound ("Wi-Fi", D23). */
static bool asHyphenRestore() {
    if (!asEnabled() || _hasHandledMacro || !_asWordStartOk || _stateIndex < 2 || _stateIndex < _index) return false;
    asInitLetters();
    char word[AS_KEY_LEN];
    bool transformed;
    bool ok = asWordKey(word, transformed);
    if (!ok || !transformed) return false;
    //only words starting with w (Telex "ư"): "nơ-ron", "rơ-moóc", "táp-lô" are Vietnamese loanwords
    if (asLetterOf(KeyStates[0]) != 'w') return false;
    int vn = asVietnameseScore(word);
    return vn == AS_MISSING || vn < AS_VN_FLOOR;
}

/** Puts the raw keys back (like "restore if wrong spelling") and asks for English.
 *  Ambiguous words just before it (look-back) are put back to their keys too. */
bool vAutoSwitchPrevRestored = false;        //previous[0] of the next English-word check is a word auto switch put back to English
Uint32 vAutoSwitchRestoredKeys[MAX_BUFF];   //the keys of the word just put back to English (D35: a later word can look back on it)
int vAutoSwitchRestoredCount = 0;
static void asRestoreToEnglish(const int& handleCode) {
    vAutoSwitchRestoredCount = 0;
    if (_stateIndex > 0 && _stateIndex < MAX_BUFF) {
        memcpy(vAutoSwitchRestoredKeys, KeyStates, _stateIndex * sizeof(Uint32));
        vAutoSwitchRestoredCount = _stateIndex;
    }
    //how many earlier words follow the switch: consecutive, ambiguous, and fitting in the output buffer.
    //Not after an acronym (2+ capitals): Vietnamese text is full of them, so they say nothing about the
    //words before ("bên ASL", "của USA" stay Vietnamese, D42)
    int back = 0, newCount = _stateIndex, deleteCount = _index;
    bool acronym = !asFewCapitals(KeyStates, _stateIndex);
    while (!acronym && back < _asPrevCount && _asPrev[back].lookbackOk &&
           newCount + _asPrev[back].rawCount + 1 < MAX_BUFF && deleteCount + _asPrev[back].shownCount + 1 < MAX_BUFF) {
        newCount += _asPrev[back].rawCount + 1;
        deleteCount += _asPrev[back].shownCount + 1;
        back++;
    }
    while (back > 0 && _asPrev[back - 1].same) {         //no need to retype unchanged words at the far end
        back--;
        newCount -= _asPrev[back].rawCount + 1;
        deleteCount -= _asPrev[back].shownCount + 1;
    }
    Uint32 out[MAX_BUFF];
    int n = 0;
    vAutoSwitchUndoCount = 0;
    for (int b = back - 1; b >= 0; b--) {
        for (int x = 0; x < _asPrev[b].rawCount; x++) out[n++] = _asPrev[b].raw[x];
        out[n++] = KEY_SPACE;
        for (int x = 0; x < _asPrev[b].shownCount; x++) vAutoSwitchUndoData[vAutoSwitchUndoCount++] = _asPrev[b].shown[x];
        vAutoSwitchUndoData[vAutoSwitchUndoCount++] = KEY_SPACE;
    }
    for (int x = 0; x < _index && vAutoSwitchUndoCount < MAX_BUFF; x++) {
        vAutoSwitchUndoData[vAutoSwitchUndoCount++] = GET(TypingWord[x]);
    }
    for (i = 0; i < _stateIndex; i++) {
        TypingWord[i] = KeyStates[i];
        out[n++] = KeyStates[i];
    }
    hCode = handleCode;
    hBPC = deleteCount;
    hNCC = n;
    for (int x = 0; x < n; x++) hData[n - 1 - x] = out[x];
    _index = _stateIndex;
    _asPrevCount = 0;
    vAutoSwitchRequest = 0;
}

/** Break keys that end a word for auto switch (not arrows, tab, esc, mouse or shortcuts). */
static bool asIsSwitchBreak(const vKeyEvent& event, const Uint16& data, const Uint8& capsStatus, const bool& otherControlKey) {
    if (event != vKeyEvent::Keyboard || otherControlKey) return false;
    return data == KEY_COMMA || data == KEY_DOT || data == KEY_SEMICOLON || data == KEY_ENTER || data == KEY_RETURN ||
           data == KEY_QUOTE ||                                 //closing quote, or an apostrophe: doesn't, we're
           (data == KEY_SLASH && capsStatus == 1) || (data == KEY_1 && capsStatus == 1) || (data == KEY_0 && capsStatus == 1) ||
           (data == KEY_9 && capsStatus == 1);                  //"(" right after a word: web(làm (D27)
}

/** Vietnamese mode, at a break key: should the word go back to its keys and the language to English? */
static bool asShouldSwitchAtBreak(const vKeyEvent& event, const Uint16& data, const Uint8& capsStatus, const bool& otherControlKey) {
    _asApostrophe = data == KEY_QUOTE && capsStatus != 1;
    bool sw = (asIsSwitchBreak(event, data, capsStatus, otherControlKey) && asShouldRestoreToEnglish()) ||
              (data == KEY_MINUS && capsStatus != 1 && !otherControlKey && asHyphenRestore());
    _asApostrophe = false;
    return sw;
}

static bool asFewCapitals(const Uint32* keys, const int& count) {
    int capitals = 0;
    for (int x = 0; x < count; x++) {
        if (!(keys[x] & CAPS_MASK)) continue;
        //DD is one letter, Đ: "DDaf Nawngx" is Đà Nẵng, while DDR, DDoS still have two capitals (D39)
        if (x > 0 && (keys[x] & CHAR_MASK) == KEY_D && (keys[x - 1] & CHAR_MASK) == KEY_D && (keys[x - 1] & CAPS_MASK)) continue;
        capitals++;
    }
    return capitals < 2;   //USA, EST, IRS, camelCase: acronyms and identifiers stay as typed
}

bool vAutoSwitchCheckEnglishWord(const Uint32* keys, const int& count,
                                 const Uint32* const* previous, const int* previousCounts, const int& previousN) {
    if (!asEnabled() || count < 2 || count >= MAX_BUFF - 1) return false;
    asInitLetters();
    char raw[AS_KEY_LEN];
    if (!asRawWord(keys, count, raw)) return false;
    //2+ capitals: acronyms and identifiers stay as typed. All capitals (Caps Lock Vietnamese, "COONG") may still
    //convert, but only to a common syllable and never when the keys are English (USA, EST stay): D23
    bool allCaps = true;
    for (int x = 0; x < count; x++) if (!(keys[x] & CAPS_MASK)) allCaps = false;
    if (!asFewCapitals(keys, count) && !allCaps) return false;
    int en = asEnglishScore(raw);

    Uint32 result[MAX_BUFF];
    int resultCount;
    int vn = asReplay(keys, count, result, resultCount);
    //word pairs (D30): "cho con bus" -> bú, "the bus" stays
    int pairVote = 0;
    if (en != AS_MISSING && vn != AS_MISSING && previousN >= 1 && previousCounts[0] >= 1) {
        char praw[AS_KEY_LEN];
        if (asRawWord(previous[0], previousCounts[0], praw)) pairVote = asBigramVote(praw, false, raw);
    }
    if (vn == AS_MISSING || !(en == AS_MISSING || (en < AS_EN_FLOOR && vn - en + pairVote >= AS_PREFER))) return false;
#define AS_CAPS_VN_FLOOR 15   //all capitals convert only to a common syllable (IRS stays, not "Í"): D23, D27
    if (count > 1 && allCaps && (en != AS_MISSING || vn < AS_CAPS_VN_FLOOR)) return false;
    //a lone all-caps word converts when the Telex marks it as Vietnamese: THOONG, THUOWNGF, VIEETJ (D41).
    //Acronyms rarely double a vowel or put w after a vowel, and the floor above already keeps rare syllables out
    bool telexMarked = false;
    for (int x = 0; raw[x] && raw[x + 1]; x++) {
        char a = raw[x] | 0x20, b = raw[x + 1] | 0x20;
        if ((a == b && (a == 'a' || a == 'e' || a == 'o' || a == 'd')) || (b == 'w' && (a == 'a' || a == 'o' || a == 'u'))) telexMarked = true;
    }
    if (count > 1 && allCaps && !(count >= 4 && telexMarked)) {   //only inside a run of capitals (Caps Lock on): a lone HW, CWS stays
        bool prevCaps = previousN >= 1 && previousCounts[0] >= 2;
        for (int x = 0; prevCaps && x < previousCounts[0]; x++) if (!(previous[0][x] & CAPS_MASK)) prevCaps = false;
        if (!prevCaps) return false;
    }
    //a tie right after a clearly English word stays English ("the theme", "of rust"): D22
#define AS_PAIR_OVERRIDES_D22 10   //word pairs this sure of the Vietnamese reading (10x) beat D22: "team bên US" (D34)
    if (en != AS_MISSING && previousN >= 1 && previousCounts[0] >= 1 && pairVote < AS_PAIR_OVERRIDES_D22 && !vAutoSwitchPrevRestored) {
        char praw[AS_KEY_LEN];
        Uint32 tmp[MAX_BUFF]; int tmpCount;
        if (asRawWord(previous[0], previousCounts[0], praw)) {
            int pen = asEnglishScore(praw);
            int pvn = asReplay(previous[0], previousCounts[0], tmp, tmpCount);
            if (pen != AS_MISSING && (pvn == AS_MISSING || pen - pvn >= AS_PREFER)) return false;
        }
    }

    //look-back: ambiguous words just before it (kept English because they are English words too)
    Uint32 out[MAX_BUFF];
    int n = 0, deleteCount = count, back = 0;
    Uint32 earlier[AS_LOOKBACK_WORDS][MAX_BUFF];
    int earlierCount[AS_LOOKBACK_WORDS];
    bool earlierSame[AS_LOOKBACK_WORDS];
    for (; back < previousN && back < AS_LOOKBACK_WORDS; back++) {
        const Uint32* pk = previous[back];
        int pc = previousCounts[back];
        char praw[AS_KEY_LEN];
        if (pc < 2 || !asRawWord(pk, pc, praw) || !asFewCapitals(pk, pc)) break;
        int pen = asEnglishScore(praw);
        bool changed;
        int pvn = asReplay(pk, pc, earlier[back], earlierCount[back], &changed);
        earlierSame[back] = !changed;
        if (!changed) {           //looks the same in both modes: pass through Vietnamese-only words ("cho")
            if (pen != AS_MISSING || asVietnameseScore(praw) == AS_MISSING) break;
            earlierCount[back] = pc;
            for (int x = 0; x < pc; x++) earlier[back][x] = pk[x];
        } else if (pen == AS_MISSING || pvn == AS_MISSING ||
                   (back == 0 && vAutoSwitchPrevRestored ? asNextPairVote(praw, raw) < AS_NEXT_PAIR :   //switched on purpose: only strong evidence
                    (pvn < pen - AS_LOOKBACK_MARGIN && !(back == 0 && asNextPairVote(praw, raw) >= AS_NEXT_PAIR)))) {
            break;                //only ambiguous words that were kept English follow the switch (or, D35, ones the
                                  //word after them clearly reads as Vietnamese: "em í ạ", "hết cả mood")
        }
        if (resultCount + n + earlierCount[back] + 1 >= MAX_BUFF || deleteCount + pc + 1 >= MAX_BUFF) break;
        n += earlierCount[back] + 1;
        deleteCount += pc + 1;
    }
    while (back > 0 && earlierSame[back - 1]) {          //no need to retype unchanged words at the far end
        back--;
        deleteCount -= previousCounts[back] + 1;
    }
    n = 0;
    vAutoSwitchUndoCount = 0;
    for (int b = back - 1; b >= 0; b--) {
        for (int x = 0; x < earlierCount[b]; x++) out[n++] = earlier[b][x];
        out[n++] = KEY_SPACE;
        for (int x = 0; x < previousCounts[b]; x++) vAutoSwitchUndoData[vAutoSwitchUndoCount++] = previous[b][x];
        vAutoSwitchUndoData[vAutoSwitchUndoCount++] = KEY_SPACE;
    }
    for (int x = 0; x < resultCount; x++) out[n++] = result[x];
    for (int x = 0; x < count; x++) vAutoSwitchUndoData[vAutoSwitchUndoCount++] = keys[x];

    hCode = vRestore;          //platform sends the new text, then the break key itself
    hBPC = (Byte)deleteCount;
    hNCC = (Byte)n;
    hExt = 3;
    for (int x = 0; x < n; x++) hData[n - 1 - x] = out[x];
    return true;
}
/*==========================================================================================================*/

void vEnglishMode(const vKeyEventState& state, const Uint16& data, const bool& isCaps, const bool& otherControlKey) {
    hCode = vDoNothing;
    if (state == vKeyEventState::MouseDown || (otherControlKey && !isCaps)) {
        hMacroKey.clear();
        _willTempOffEngine = false;
    } else if (data == KEY_SPACE) {
        if (!_hasHandledMacro && findMacro(hMacroKey, hMacroData)) {
            hCode = vReplaceMaro;
            hBPC = (Byte)hMacroKey.size();
        }
        hMacroKey.clear();
        _willTempOffEngine = false;
    } else if (data == KEY_DELETE) {
        if (hMacroKey.size() > 0) {
            hMacroKey.pop_back();
        } else {
            _willTempOffEngine = false;
        }
    } else {
        if (isWordBreak(vKeyEvent::Keyboard, state, data) && !isCharKeyCode(data)) {
            hMacroKey.clear();
            _willTempOffEngine = false;
        } else {
            if (!_willTempOffEngine)
                hMacroKey.push_back(data | (isCaps ? CAPS_MASK : 0));
        }
    }
}

void vKeyHandleEvent(const vKeyEvent& event,
                     const vKeyEventState& state,
                     const Uint16& data,
                     const Uint8& capsStatus,
                     const bool& otherControlKey) {
    _isCaps = (capsStatus == 1 || //shift
               capsStatus == 2); //caps lock
    if ((IS_NUMBER_KEY(data) && capsStatus == 1)
        || otherControlKey || isWordBreak(event, state, data) || (IS_NUMBER_KEY(data) && (_index == 0 || _spaceCount > 0))) {   //a digit after a space starts a new word: "mua 1casi" -> "1cái"; glued to letters (abc1def) it stays part of them
        hCode = vDoNothing;
        hBPC = 0;
        hNCC = 0;
        hExt = 1; //word break

        //check macro feature
        if (vUseMacro && isMacroBreakCode(data) && !_hasHandledMacro && findMacro(hMacroKey, hMacroData)) {
            hCode = vReplaceMaro;
            hBPC = (Byte)hMacroKey.size();
            _hasHandledMacro = true;
        } else if (asShouldSwitchAtBreak(event, data, capsStatus, otherControlKey)) {
            asRestoreToEnglish(vRestoreAndStartNewSession); //auto switch: an English word typed in Vietnamese mode
        } else if ((vQuickStartConsonant || vQuickEndConsonant) && !tempDisableKey && isMacroBreakCode(data)) {
            checkQuickConsonant();
        } else if (vRestoreIfWrongSpelling && isWordBreak(event, state, data)) { //restore key if wrong spelling with break-key
            if (!tempDisableKey && vCheckSpelling) {
                checkSpelling(true); //force check spelling
            }
            if (tempDisableKey && !checkRestoreIfWrongSpelling(vRestoreAndStartNewSession)) {
                hCode = vDoNothing;
            }
        }
        
        _isCharKeyCode = state == KeyDown && isCharKeyCode(data);
        if (!_isCharKeyCode) { //clear all line cache
            _specialChar.clear();
            _typingStates.clear();
        } else { //check and save current word
            if (_spaceCount > 0) {
                saveWord(KEY_SPACE, _spaceCount);
                _spaceCount = 0;
            } else {
                saveWord();
            }
            _specialChar.push_back(data | (_isCaps ? CAPS_MASK : 0));
            hExt = 3;//normal word
        }
        
        if (hCode == vDoNothing) {
            startNewSession();
            vCheckSpelling = _useSpellCheckingBefore;
            _willTempOffEngine = false;
        } else if (hCode == vReplaceMaro || _hasHandleQuickConsonant) {
            _index = 0;
        }
        
        //insert key for macro function
        if (vUseMacro) {
            if (_isCharKeyCode) {
                hMacroKey.push_back(data | (_isCaps ? CAPS_MASK : 0));
            } else {
                hMacroKey.clear();
            }
        }
        
        if (vUpperCaseFirstChar) {
            if (data == KEY_DOT)
                _upperCaseStatus = 1;
            else if (data == KEY_ENTER || data == KEY_RETURN)
                _upperCaseStatus = 2;
            else
                _upperCaseStatus = 0;
        }
        _asPrevCount = 0;
        //the next word is a whole word only after whitespace, a click, a shortcut or an opening ( [ { " ',
        //not after . _ - / etc.
        _afterDigit = IS_NUMBER_KEY(data) && capsStatus != 1;
        _asWordStartOk = event == vKeyEvent::Mouse || otherControlKey || data == KEY_ENTER || data == KEY_RETURN || data == KEY_TAB ||
                         (data == KEY_9 && capsStatus == 1) || data == KEY_LEFT_BRACKET || data == KEY_QUOTE;
    } else if (data == KEY_SPACE) {
        _afterDigit = false;
        if (!tempDisableKey && vCheckSpelling) {
            checkSpelling(true); //force check spelling
        }
        if (vUseMacro && !_hasHandledMacro && findMacro(hMacroKey, hMacroData)) { //macro
            hCode = vReplaceMaro;
            hBPC = (Byte)hMacroKey.size();
            _spaceCount++;
            _hasHandledMacro = true;
        } else if (asShouldRestoreToEnglish()) { //auto switch: an English word typed in Vietnamese mode
            asRestoreToEnglish(vRestore);
            _spaceCount++;
        } else if ((vQuickStartConsonant || vQuickEndConsonant) && !tempDisableKey && checkQuickConsonant()) {
            _spaceCount++;
        } else if (vRestoreIfWrongSpelling && tempDisableKey && !_hasHandledMacro) { //restore key if wrong spelling
            if (!checkRestoreIfWrongSpelling(vRestore)) {
                hCode = vDoNothing;
            }
            _spaceCount++;
        } else { //do nothing with SPACE KEY
            hCode = vDoNothing;
            _spaceCount++;
        }
        if (vUseMacro) {
            hMacroKey.clear();
        }
        if (vUpperCaseFirstChar && _upperCaseStatus == 1) {
            _upperCaseStatus = 2;
        }
        //look-back memory: only words separated by a single space count
        if (hCode == vDoNothing && _spaceCount == 1) asRememberWord(); else if (_spaceCount > 1) _asPrevCount = 0;
        //save word
        if (_spaceCount == 1) {
            if (_specialChar.size() > 0) {
                saveSpecialChar();
            } else {
                saveWord();
            }
        }
        vCheckSpelling = _useSpellCheckingBefore;
        _willTempOffEngine = false;
        _asWordStartOk = true;
    } else if (data == KEY_DELETE) {
        _asPrevCount = 0;
        hCode = vDoNothing;
        hExt = 2; //delete
        if (_specialChar.size() > 0) {
            _specialChar.pop_back();
            if (_specialChar.size() == 0) {
                restoreLastTypingState();
            }
        } else if (_spaceCount > 0) { //previous char is space
            _spaceCount--;
            if (_spaceCount == 0) { //restore word
                restoreLastTypingState();
            }
        } else {
            if (_stateIndex > 0) {
                _stateIndex--;
                while (_asDropN > 0 && _stateIndex < _asDropPos[_asDropN - 1]) _asDropN--;
            }
            if (_index > 0){
                _index--;
                if (_longWordHelper.size() > 0) {
                    //right shift
                    for (i = MAX_BUFF - 1; i > 0; i--) {
                        TypingWord[i] = TypingWord[i-1];
                    }
                    TypingWord[0] = _longWordHelper.back();
                    _longWordHelper.pop_back();
                    _index++;
                }
                if (vCheckSpelling)
                    checkSpelling();
            }
            if (vUseMacro && hMacroKey.size() > 0) {
                hMacroKey.pop_back();
            }
            
            hBPC = 0;
            hNCC = 0;
            hExt = 2; //delete key
            if (_index == 0) {
                startNewSession();
                _specialChar.clear();
                restoreLastTypingState();
            } else { //August 23rd continue check grammar
                checkGrammar(1);
            }
        }
    } else { //START AND CHECK KEY
        if (_willTempOffEngine) {
            hCode = vDoNothing;
            hExt = 3;
            return;
        }
        if (_spaceCount > 0) {
            hBPC = 0;
            hNCC = 0;
            hExt = 0;
            startNewSession();
            //continute save space
            saveWord(KEY_SPACE, _spaceCount);
            _spaceCount = 0;
        } else if (_specialChar.size() > 0) {
            saveSpecialChar();
        }

        insertState(data, _isCaps); //save state
        
        if (!IS_SPECIALKEY(data) || tempDisableKey) { //do nothing
            if (vQuickTelex && IS_QUICK_TELEX_KEY(data)) {
                handleQuickTelex(data, _isCaps);
                return;
            } else {
                hCode = vDoNothing;
                hBPC = 0;
                hNCC = 0;
                hExt = 3; //normal key
                insertKey(data, _isCaps);
            }
        } else { //check and update key
            //restore state
            hCode = vDoNothing;
            hExt = 3; //normal key
            handleMainKey(data, _isCaps);
            //auto switch: the same tone key pressed twice undoes the tone, a strong sign of English
            if (hCode == vRestore && asEnabled() && IS_MARK_KEY(data)) {
                vAutoSwitchRequest = 0;
                //Switching to English: put the whole word back to the keys typed, not just the undone tone
                //("tomorr", not "tômrr"). The second press is added after these by the caller (vRestore).
                //Like other Telex input methods the first press goes ("ver"), unless both make an English word
                //already ("err", "boss"); the end of the word decides for good (D19, vAutoSwitchFixDoubledKey).
                if (_stateIndex >= 2 && _stateIndex <= MAX_BUFF && _index <= MAX_BUFF) {
                    int first = -1;                                  //the press that set the tone
                    for (int x = _stateIndex - 2; x >= 0; x--)
                        if ((KeyStates[x] & CHAR_MASK) == data) { first = x; break; }
                    char raw[AS_KEY_LEN];
                    asInitLetters();
                    bool both = first < 0 || (asRawWord(KeyStates, _stateIndex, raw) && asEnglishScore(raw) != AS_MISSING);
                    hBPC = _index;
                    hNCC = 0;
                    for (int x = _stateIndex - 2; x >= 0; x--)
                        if (both || x != first) hData[hNCC++] = KeyStates[x];
                    //remember the word for the end-of-word decision
                    _asDoubleCount = _stateIndex;
                    _asDoubleFirst = first;
                    _asDoubleBoth = both;
                    memcpy(_asDoubleKeys, KeyStates, _stateIndex * sizeof(Uint32));
                }
            }
        }

        if (!vFreeMark && !IS_KEY_D(data)) {
            if (hCode == vDoNothing) {
                checkGrammar(-1);
            } else {
                checkGrammar(0);
            }
        }

        if (hCode == vRestore) {
            insertKey(data, _isCaps);
            if (_stateIndex > 0 && vAutoSwitchRequest != 0) {
                if (_asDropN == AS_MAX_DROPS) { memmove(_asDropPos, _asDropPos + 1, sizeof(int) * (AS_MAX_DROPS - 1)); memmove(_asDropKey, _asDropKey + 1, sizeof(Uint32) * (AS_MAX_DROPS - 1)); _asDropN--; }
                _asDropPos[_asDropN] = _stateIndex - 1; _asDropKey[_asDropN] = KeyStates[_stateIndex - 1]; _asDropN++;
            }
            _stateIndex--;
        }
        
        //insert or replace key for macro feature
        if (vUseMacro) {
            if (hCode == vDoNothing) {
                hMacroKey.push_back(data | (_isCaps ? CAPS_MASK : 0));
            } else if (hCode == vWillProcess || hCode == vRestore) {
                for (i = 0; i < hBPC; i++) {
                    if (hMacroKey.size() > 0) {
                        hMacroKey.pop_back();
                    }
                }
                for (i = _index - hBPC; i < hNCC + (_index - hBPC); i++) {
                    hMacroKey.push_back(TypingWord[i]);
                }
            }
        }
        
        if (vUpperCaseFirstChar) {
            if (_index == 1 && _upperCaseStatus == 2) {
                upperCaseFirstCharacter();
            }
            _upperCaseStatus = 0;
        }
        
        //case [ ]
        if (IS_BRACKET_KEY(data) && (( IS_BRACKET_KEY((Uint16)hData[0])) || vInputType == vSimpleTelex1 || vInputType == vSimpleTelex2)) {
            if (_index - (hCode == vWillProcess ? hBPC : 0) > 0) {
                _index--;
                saveWord();
            }
            _index = 0;
            tempDisableKey = false;
            _stateIndex = 0;
            hExt = 3;
            _specialChar.push_back(data | (_isCaps ? CAPS_MASK : 0));
        }
    }
    
    //Debug
    //cout<<"index "<<(int)_index<< ", stateIndex "<<(int)_stateIndex<<", word "<<_typingStates.size()<<", long word "<<_longWordHelper.size()<< endl;
    //cout<<"backspace "<<(int)hBPC<<endl;
    //cout<<"new char "<<(int)hNCC<<endl<<endl;
}

bool vAutoSwitchFixDoubledKey(const Uint32* keys, const int& count) {
    int n = _asDoubleCount;
    _asDoubleCount = 0;
    if (!asEnabled() || n < 2 || _asDoubleFirst < 0 || count < 0 || n + count >= AS_KEY_LEN || n + count >= MAX_BUFF) return false;
    asInitLetters();
    Uint32 full[MAX_BUFF], single[MAX_BUFF];
    memcpy(full, _asDoubleKeys, n * sizeof(Uint32));
    for (int x = 0; x < count; x++) full[n + x] = keys[x];
    int total = n + count, m = 0;
    for (int x = 0; x < total; x++) if (x != _asDoubleFirst) single[m++] = full[x];
    char rawFull[AS_KEY_LEN], rawSingle[AS_KEY_LEN];
    if (!asRawWord(full, total, rawFull) || !asRawWord(single, m, rawSingle)) return false;
    bool enFull = asEnglishScore(rawFull) != AS_MISSING, enSingle = asEnglishScore(rawSingle) != AS_MISSING;
    const Uint32* shown = _asDoubleBoth ? full : single;
    int shownCount = _asDoubleBoth ? total : m;
    const Uint32* fixed;
    int fixedCount;
    if (_asDoubleBoth && !enFull && enSingle) { fixed = single; fixedCount = m; }            //"mixx" -> "mix"
    else if (!_asDoubleBoth && enFull && !enSingle) { fixed = full; fixedCount = total; }   //"terible" -> "terrible"
    else if (!_asDoubleBoth && !enSingle && !asFewCapitals(full, total)) { fixed = full; fixedCount = total; }   //ASUS, not "AUS"
    else return false;
    if (fixedCount < 1) return false;
    vAutoSwitchUndoCount = 0;   //Backspace right after puts back what was on screen
    for (int x = 0; x < shownCount; x++) vAutoSwitchUndoData[vAutoSwitchUndoCount++] = shown[x];
    hCode = vRestore;          //platform sends the new text, then the break key itself
    hBPC = (Byte)shownCount;
    hNCC = (Byte)fixedCount;
    hExt = 3;
    for (int x = 0; x < fixedCount; x++) hData[fixedCount - 1 - x] = fixed[x];
    return true;
}
