#pragma once

/*
    note(amer): input codes are from: https://github.com/awakecoding/Win32Keyboard/blob/master/vkcodes.h
*/

#define HE_BUTTON_LEFT 0x01
#define HE_BUTTON_RIGHT 0x02
/* Control-break processing */
#define HE_CANCEL_MOUSE_BUTTON 0x03
#define HE_BUTTON_MIDDLE  0x04
#define HE_BUTTON0 0x05
#define HE_BUTTON1 0x06

#define HE_KEY_BACK_SPACE 0x08
#define HE_KEY_TAB 0x09

#define HE_KEY_CLEAR 0x0C
#define HE_KEY_ENTER 0x0D

#define HE_KEY_SHIFT 0x10
#define HE_KEY_CONTROL 0x11
#define HE_KEY_ALT 0x12
#define HE_KEY_PAUSE 0x13
#define HE_KEY_CAPS_LOCK 0x14
/* Input Method Editor (IME) Kana mode */
#define HE_KEY_KANA 0x15
 /* IME Hanguel mode (maintained for compatibility; use #define HE_HANGUL) */
#define HE_KEY_HANGUEL 0x15
/* IME Hangul mode */
#define HE_KEY_HANGUL 0x15

/* IME Junja mode */
#define HE_KEY_JUNJA 0x17
/* IME final mode */
#define HE_KEY_FINAL 0x18
/* IME Hanja mode */
#define HE_KEY_HANJA 0x19
/* IME Kanji mode */
#define HE_KEY_KANJI 0x19

#define HE_KEY_ESCAPE 0x1B
/* IME convert */
#define HE_KEY_CONVERT 0x1C
/* IME nonconvert */
#define HE_KEY_NONCONVERT 0x1D
/* IME accept */
#define HE_KEY_ACCEPT 0x1E
/* IME mode change request */
#define HE_KEY_MODECHANGE 0x1F

#define HE_KEY_SPACE 0x20
#define HE_KEY_PAGE_UP 0x21
#define HE_KEY_PAGE_DOWN 0x22
#define HE_KEY_END 0x23
#define HE_KEY_HOME 0x24
#define HE_KEY_LEFT 0x25
#define HE_KEY_UP 0x26
#define HE_KEY_RIGHT 0x27
#define HE_KEY_DOWN 0x28
#define HE_KEY_SELECT 0x29
#define HE_KEY_PRINT 0x2A
#define HE_KEY_EXECUTE 0x2B
#define HE_KEY_PRINT_SCREEN 0x2C
#define HE_KEY_INSERT 0x2D
#define HE_KEY_DELETE 0x2E
#define HE_KEY_HELP 0x2F

#define HE_KEY_0 0x30
#define HE_KEY_1 0x31
#define HE_KEY_2 0x32
#define HE_KEY_3 0x33
#define HE_KEY_4 0x34
#define HE_KEY_5 0x35
#define HE_KEY_6 0x36
#define HE_KEY_7 0x37
#define HE_KEY_8 0x38
#define HE_KEY_9 0x39

/* The alphabet, the code corresponds to the capitalized letter in the ASCII code */

#define HE_KEY_A 0x41
#define HE_KEY_B 0x42
#define HE_KEY_C 0x43
#define HE_KEY_D 0x44
#define HE_KEY_E 0x45
#define HE_KEY_F 0x46
#define HE_KEY_G 0x47
#define HE_KEY_H 0x48
#define HE_KEY_I 0x49
#define HE_KEY_J 0x4A
#define HE_KEY_K 0x4B
#define HE_KEY_L 0x4C
#define HE_KEY_M 0x4D
#define HE_KEY_N 0x4E
#define HE_KEY_O 0x4F
#define HE_KEY_P 0x50
#define HE_KEY_Q 0x51
#define HE_KEY_R 0x52
#define HE_KEY_S 0x53
#define HE_KEY_T 0x54
#define HE_KEY_U 0x55
#define HE_KEY_V 0x56
#define HE_KEY_W 0x57
#define HE_KEY_X 0x58
#define HE_KEY_Y 0x59
#define HE_KEY_Z 0x5A

#define HE_KEY_LEFT_WINDOWS 0x5B
#define HE_KEY_ROGHT_WINDOWS 0x5C
/* Applications key (Natural keyboard) */
#define HE_KEY_APPS 0x5D

#define HE_KEY_SLEEP 0x5F

#define HE_KEY_NUMPAD0 0x60
#define HE_KEY_NUMPAD1 0x61
#define HE_KEY_NUMPAD2 0x62
#define HE_KEY_NUMPAD3 0x63
#define HE_KEY_NUMPAD4 0x64
#define HE_KEY_NUMPAD5 0x65
#define HE_KEY_NUMPAD6 0x66
#define HE_KEY_NUMPAD7 0x67
#define HE_KEY_NUMPAD8 0x68
#define HE_KEY_NUMPAD9 0x69

#define HE_KEY_MULTIPLY 0x6A
#define HE_KEY_ADD 0x6B
#define HE_KEY_SEPARATOR 0x6C
#define HE_KEY_SUBTRACT 0x6D
#define HE_KEY_DECIMAL 0x6E
#define HE_KEY_DIVIDE 0x6F

#define HE_KEY_F1 0x70
#define HE_KEY_F2 0x71
#define HE_KEY_F3 0x72
#define HE_KEY_F4 0x73
#define HE_KEY_F5 0x74
#define HE_KEY_F6 0x75
#define HE_KEY_F7 0x76
#define HE_KEY_F8 0x77
#define HE_KEY_F9 0x78
#define HE_KEY_F10 0x79
#define HE_KEY_F11 0x7A
#define HE_KEY_F12 0x7B
#define HE_KEY_F13 0x7C
#define HE_KEY_F14 0x7D
#define HE_KEY_F15 0x7E
#define HE_KEY_F16 0x7F
#define HE_KEY_F17 0x80
#define HE_KEY_F18 0x81
#define HE_KEY_F19 0x82
#define HE_KEY_F20 0x83
#define HE_KEY_F21 0x84
#define HE_KEY_F22 0x85
#define HE_KEY_F23 0x86
#define HE_KEY_F24 0x87

#define HE_KEY_NUMLOCK 0x90
#define HE_KEY_SCROLL 0x91

#define HE_KEY_LEFT_SHIFT 0xA0
#define HE_KEY_RIGHT_SHIFT 0xA1
#define HE_KEY_LEFT_CONTROL 0xA2
#define HE_KEY_RIGHT_CONTROL 0xA3
#define HE_KEY_LEFT_ALT 0xA4
#define HE_KEY_RIGHT_ALT 0xA5

/* Browser related keys */

/* Windows 2000/XP: Browser Back key */
#define HE_KEY_BROWSER_BACK 0xA6
/* Windows 2000/XP: Browser Forward key */
#define HE_KEY_BROWSER_FORWARD 0xA7
/* Windows 2000/XP: Browser Refresh key */
#define HE_KEY_BROWSER_REFRESH 0xA8
/* Windows 2000/XP: Browser Stop key */
#define HE_KEY_BROWSER_STOP 0xA9
/* Windows 2000/XP: Browser Search key */
#define HE_KEY_BROWSER_SEARCH 0xAA
/* Windows 2000/XP: Browser Favorites key */
#define HE_KEY_BROWSER_FAVORITES 0xAB
/* Windows 2000/XP: Browser Start and Home key */
#define HE_KEY_BROWSER_HOME 0xAC

/* Volume related keys */

/* Windows 2000/XP: Volume Mute key */
#define HE_KEY_VOLUME_MUTE 0xAD
/* Windows 2000/XP: Volume Down key */
#define HE_KEY_VOLUME_DOWN 0xAE
/* Windows 2000/XP: Volume Up key */
#define HE_KEY_VOLUME_UP 0xAF

/* Media player related keys */

/* Windows 2000/XP: Next Track key */
#define HE_KEY_MEDIA_NEXT_TRACK 0xB0
/* Windows 2000/XP: Previous Track key */
#define HE_KEY_MEDIA_PREV_TRACK 0xB1
/* Windows 2000/XP: Stop Media key */
#define HE_KEY_MEDIA_STOP 0xB2
/* Windows 2000/XP: Play/Pause Media key */
#define HE_KEY_MEDIA_PLAY_PAUSE 0xB3

/* Application launcher keys */

#define HE_KEY_LAUNCH_MAIL 0xB4
#define HE_KEY_MEDIA_SELECT 0xB5
#define HE_KEY_LAUNCH_APP1 0xB6
#define HE_KEY_LAUNCH_APP2 0xB7

/* 0xB8 and 0xB9 are reserved */

/* OEM keys */

/* Windows 2000/XP: For the US standard keyboard, the ';:' key */
#define HE_KEY_SEMI_COLON 0xBA

/* Windows 2000/XP: For any country/region, the '+' key */
#define HE_KEY_PLUS 0xBB

/* Windows 2000/XP: For any country/region, the ',' key */
#define HE_KEY_COMMA 0xBC

/* Windows 2000/XP: For any country/region, the '-' key */
#define HE_KEY_MINUS 0xBD

/* Windows 2000/XP: For any country/region, the '.' key */
#define HE_KEY_PERIOD 0xBE

/* Windows 2000/XP: For the US standard keyboard, the '/?' key */
#define HE_KEY_FORWARD_SLASH_QUESTION 0xBF

/* Windows 2000/XP: For the US standard keyboard, the '`~' key */
#define HE_KEY_ACUTE 0xC0

#define HE_KEY_ABNT_C1 0xC1 /* Brazilian (ABNT) Keyboard */
#define HE_KEY_ABNT_C2 0xC2 /* Brazilian (ABNT) Keyboard */

/* Windows 2000/XP: For the US standard keyboard, the '[{' key */
#define HE_KEY_OPEN_BRACKET 0xDB

/* Windows 2000/XP: For the US standard keyboard, the '\|' key */
#define HE_KEY_BACK_SLASH 0xDC

/* Windows 2000/XP: For the US standard keyboard, the ']}' key */
#define HE_KEY_CLOSE_BRACKEY 0xDD

/* Windows 2000/XP: For the US standard keyboard, the 'single-quote/double-quote' key */
#define HE_KEY_QUOTE 0xDE

/* Windows 95/98/Me, Windows NT 4.0, Windows 2000/XP: IME PROCESS key */
#define HE_KEY_PROCESSKEY 0xE5

/* 0xE6 is OEM specific */

/* The #define HE_PACKET key is the low word of a 32-bit Virtual Key value used */
                /* for non-keyboard input methods. For more information, */
                /* see Remark in KEYBDINPUT, SendInput, WM_KEYDOWN, and WM_KEYUP */
/* Windows 2000/XP: Used to pass Unicode characters as if they were keystrokes. */
#define HE_KEY_PACKET 0xE7
#define HE_KEY_PLAY 0xFA
#define HE_KEY_ZOOM 0xFB