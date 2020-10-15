//
// km232 - Keyboard + Mouse Relay tool
//
// Grab Keyboard + Mouse events, and relay them out the serial port
// to a Hagstrom USB-KM232, set especially to feed into a Wombat, for use with
// the Apple IIgs
//
// Written by Jason Andersen  10/14/2020
//

#include <windows.h>
#include <tchar.h>
#include <stdio.h>

#include <vector>

HANDLE hStdin;
HANDLE hStdOut;
DWORD fdwSaveOldMode;

VOID ErrorExit(LPCSTR);
VOID KeyEventProc(KEY_EVENT_RECORD);
VOID MouseEventProc(MOUSE_EVENT_RECORD);
VOID ResizeEventProc(WINDOW_BUFFER_SIZE_RECORD);
void InitScreen(int width, int height);
const char* KeyToString(WORD vkCode);

int main(int argc, char* argv[])
{
    DWORD cNumRead, fdwMode, i;
    INPUT_RECORD irInBuf[128];

    // Get the standard input handle. 

    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (hStdin == INVALID_HANDLE_VALUE)
        ErrorExit("GetStdHandle, Input");

	hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdOut == INVALID_HANDLE_VALUE)
        ErrorExit("GetStdHandle, Output");


    // Save the current input mode, to be restored on exit. 

    if (!GetConsoleMode(hStdin, &fdwSaveOldMode))
        ErrorExit("GetConsoleMode");

    // Enable the window and mouse input events. 

    fdwMode = ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_INSERT_MODE | ENABLE_EXTENDED_FLAGS;
    if (!SetConsoleMode(hStdin, fdwMode))
        ErrorExit("SetConsoleMode");


	// Resize/Clear the screen
	InitScreen(80,24);

    // Loop to read and handle the next 500 input events. 

    while (TRUE)
    {
        // Wait for the events. 

        if (!ReadConsoleInput(
            hStdin,      // input buffer handle 
            irInBuf,     // buffer to read into 
            128,         // size of read buffer 
            &cNumRead)) // number of records read 
            ErrorExit("ReadConsoleInput");

        // Dispatch the events to the appropriate handler. 

        for (i = 0; i < cNumRead; i++)
        {
            switch (irInBuf[i].EventType)
            {
            case KEY_EVENT: // keyboard input 
                KeyEventProc(irInBuf[i].Event.KeyEvent);
                break;

            case MOUSE_EVENT: // mouse input 
                MouseEventProc(irInBuf[i].Event.MouseEvent);
                break;

            case WINDOW_BUFFER_SIZE_EVENT: // scrn buf. resizing 
                ResizeEventProc(irInBuf[i].Event.WindowBufferSizeEvent);
                break;

            case FOCUS_EVENT:  // disregard focus events 

            case MENU_EVENT:   // disregard menu events 
                break;

            default:
                ErrorExit("Unknown event type");
                break;
            }
        }
    }

    // Restore input mode on exit.

    SetConsoleMode(hStdin, fdwSaveOldMode);

    return 0;
}

VOID ErrorExit(LPCSTR lpszMessage)
{
    fprintf(stderr, "%s\n", lpszMessage);

    // Restore input mode on exit.

    SetConsoleMode(hStdin, fdwSaveOldMode);

    ExitProcess(0);
}

VOID KeyEventProc(KEY_EVENT_RECORD ker)
{
// I'm using a vector instead of a map, because I need to know which keys
// are the oldest, to simulator rollover

static std::vector<WORD> keys;	// list of keys that are down

	if (ker.bKeyDown)
	{
		bool bAdd = true;

		// Add to set, if not already in there
		for (int idx = 0; idx < keys.size(); ++idx)
		{
			if (keys[ idx ] == ker.wVirtualKeyCode)
			{
				bAdd = false;
				break;
			}
		}

		if (bAdd)
		{
			keys.push_back( ker.wVirtualKeyCode );
		}
	}
	else
	{
		// Remove from set
		// Add to set, if not already in there
		for (int idx = 0; idx < keys.size(); ++idx)
		{
			if (keys[ idx ] == ker.wVirtualKeyCode)
			{
				keys.erase( keys.begin() + idx );
				break;
			}
		}

	}

//-----------------------------------------------------------------------------
//  Dump the list of keys that are down
//
	COORD dwCursorPosition;
	dwCursorPosition.X = dwCursorPosition.Y = 0;
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);
	printf("                                                                ");
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);

	for (int idx = 0; idx < keys.size(); ++idx)
	{
		//printf(" %s(%04X)", KeyToString(keys[idx]), keys[idx]);
		printf(" %s", KeyToString(keys[idx]));
	}

}

VOID MouseEventProc(MOUSE_EVENT_RECORD mer)
{
	COORD dwCursorPosition;
	dwCursorPosition.X = dwCursorPosition.Y = 1;
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);

#ifndef MOUSE_HWHEELED
#define MOUSE_HWHEELED 0x0008
#endif
    printf("Mouse event: ");

    switch (mer.dwEventFlags)
    {
    case 0:

        if (mer.dwButtonState == FROM_LEFT_1ST_BUTTON_PRESSED)
        {
            printf("left button press \n");
        }
        else if (mer.dwButtonState == RIGHTMOST_BUTTON_PRESSED)
        {
            printf("right button press \n");
        }
        else
        {
            printf("button press\n");
        }
        break;
    case DOUBLE_CLICK:
        printf("double click\n");
        break;
    case MOUSE_HWHEELED:
        printf("horizontal mouse wheel\n");
        break;
    case MOUSE_MOVED:
        printf("mouse moved\n");
        break;
    case MOUSE_WHEELED:
        printf("vertical mouse wheel\n");
        break;
    default:
        printf("unknown\n");
        break;
    }
}

VOID ResizeEventProc(WINDOW_BUFFER_SIZE_RECORD wbsr)
{
	COORD dwCursorPosition;
	dwCursorPosition.X = dwCursorPosition.Y = 2;
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);

    printf("Resize event\n");
    printf("Console screen buffer is %d columns by %d rows.\n", wbsr.dwSize.X, wbsr.dwSize.Y);
}


//-----------------------------------------------------------------------------
// Resize the Window, and the Screen
// Clear Screen
// Initialize Title
// Hide Cursor
// Etc
void InitScreen(int width, int height)
{
	// Set the text colors
	WORD wTextAttrib = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY | BACKGROUND_BLUE;
	SetConsoleTextAttribute(hStdOut, wTextAttrib);

	// Set The Console Title
	SetConsoleTitle(L"KM232 Terminal - Version 0");

	// Shrink the window - (due to how these functions work)
	SMALL_RECT winRect;
	winRect.Left = 0; winRect.Top = 0;
	winRect.Right = 1; winRect.Bottom = 1;
	SetConsoleWindowInfo(hStdOut, TRUE, &winRect);

	// Set Screen Dimensions
	COORD dwSize;
	dwSize.X = width; dwSize.Y=height;
	SetConsoleScreenBufferSize(hStdOut, dwSize);

	// Resize Window for our new buffer
	CONSOLE_FONT_INFO fontInfo;
    GetCurrentConsoleFont(hStdOut, TRUE, &fontInfo);

	winRect.Right = width-1;
	winRect.Bottom = height-1;
	SetConsoleWindowInfo(hStdOut, TRUE, &winRect);

	// Clear the Screen
	// Then fill Attribs for each line
	// Then fill spaces for each line
	COORD dwCursorPosition;
	dwCursorPosition.X = dwCursorPosition.Y = 0;
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);

	DWORD numOutput;
	FillConsoleOutputCharacter(hStdOut, L' ', width * height, dwCursorPosition,
							   &numOutput);

	// Hide Cursor
	CONSOLE_CURSOR_INFO cursorInfo;
	cursorInfo.dwSize = 100;
	cursorInfo.bVisible = FALSE;
	SetConsoleCursorInfo(hStdOut, &cursorInfo);

}


//-----------------------------------------------------------------------------

const char* KeyToString(WORD vkCode)
{
static const char* table[ 256 ]
{
	"0x00",					// 0x00
	"VK_LBUTTON",			// 0x01
	"VK_RBUTTON",   		// 0x02
	"VK_CANCEL",    		// 0x03
	"VK_MBUTTON",   		// 0x04    /* NOT contiguous with L & RBUTTON */
	"VK_XBUTTON1",   		// 0x05    /* NOT contiguous with L & RBUTTON */
	"VK_XBUTTON2",   		// 0x06    /* NOT contiguous with L & RBUTTON */
	"0x07",   				// 0x07
	"VK_BACK",   			// 0x08
	"VK_TAB",   //            0x09
	"0x0A",   //			  0x0A
	"0x0B",   //			  0x0B
	"VK_CLEAR",   //          0x0C
	"VK_RETURN",   //         0x0D
	"0x0E",   //			  0x0E
	"0x0F",   //			  0x0F
	"VK_SHIFT",   //          0x10
	"VK_CONTROL",   //        0x11
	"VK_MENU",   //           0x12
	"VK_PAUSE",   //          0x13
	"VK_CAPITAL",   //        0x14
	"VK_KANA",   //           0x15
//#define VK_HANGEUL        0x15  /* old name - should be here for compatibility */
//#define VK_HANGUL         0x15
	"0x16",   // 			  0x16   //: unassigned
	"VK_JUNJA",   //          0x17
	"VK_FINAL",   //          0x18
	"VK_HANJA",   //          0x19
//#define VK_KANJI          0x19
	"0x1A",   //			  0x1A		// : unassigned
	"VK_ESCAPE",   //         0x1B
	"VK_CONVERT",   //        0x1C
	"VK_NONCONVERT",   //     0x1D
	"VK_ACCEPT",   //         0x1E
	"VK_MODECHANGE",   //     0x1F
	"VK_SPACE",   //          0x20
	"VK_PRIOR",   //          0x21
	"VK_NEXT",   //           0x22
	"VK_END",   //            0x23
	"VK_HOME",   //           0x24
	"VK_LEFT",   //           0x25
	"VK_UP",   //             0x26
	"VK_RIGHT",   //          0x27
	"VK_DOWN",   //           0x28
	"VK_SELECT",   //         0x29
	"VK_PRINT",   //          0x2A
	"VK_EXECUTE",   //        0x2B
	"VK_SNAPSHOT",   //       0x2C
	"VK_INSERT",   //         0x2D
	"VK_DELETE",   //         0x2E
	"VK_HELP",   //           0x2F
	"VK_0",   //			  0x30
	"VK_1",   //			  0x31
	"VK_2",   //			  0x32
	"VK_3",   //			  0x33
	"VK_4",   //			  0x34
	"VK_5",   //			  0x35
	"VK_6",   //			  0x36
	"VK_7",   //			  0x37
	"VK_8",   //			  0x38
	"VK_9",   //			  0x39
	"0x3A",   //              0x3A
	"0x3B",   //              0x3B
	"0x3C",   //              0x3C
	"0x3D",   //              0x3D
	"0x3E",   //              0x3E
	"0x3F",   //              0x3F
	"0x40",   //			  0x40  // : unassigned
	"VK_A",   //			  0x41
	"VK_B",   //			  0x42
	"VK_C",   //			  0x43
	"VK_D",   //			  0x44
	"VK_E",   //			  0x45
	"VK_F",   //			  0x46
	"VK_G",   //			  0x47
	"VK_H",   //			  0x48
	"VK_I",   //			  0x49
	"VK_J",   //			  0x4A
	"VK_K",   //			  0x4B
	"VK_L",   //			  0x4C
	"VK_M",   //			  0x4D
	"VK_N",   //			  0x4E
	"VK_O",   //			  0x4F
	"VK_P",   //			  0x50
	"VK_Q",   //			  0x51
	"VK_R",   //			  0x52
	"VK_S",   //			  0x53
	"VK_T",   //			  0x54
	"VK_U",   //			  0x55
	"VK_V",   //			  0x56
	"VK_W",   //			  0x57
	"VK_X",   //			  0x58
	"VK_Y",   //			  0x59
	"VK_Z",   //			  0x5A
	"VK_LWIN",   //           0x5B
	"VK_RWIN",   //           0x5C
	"VK_APPS",   //           0x5D
	"0x5E",   //              0x5E // : reserved
	"VK_SLEEP",   //          0x5F
	"VK_NUMPAD0",   //        0x60
	"VK_NUMPAD1",   //        0x61
	"VK_NUMPAD2",   //        0x62
	"VK_NUMPAD3",   //        0x63
	"VK_NUMPAD4",   //        0x64
	"VK_NUMPAD5",   //        0x65
	"VK_NUMPAD6",   //        0x66
	"VK_NUMPAD7",   //        0x67
	"VK_NUMPAD8",   //        0x68
	"VK_NUMPAD9",   //        0x69
	"VK_MULTIPLY",   //       0x6A
	"VK_ADD",   //            0x6B
	"VK_SEPARATOR",   //      0x6C
	"VK_SUBTRACT",   //       0x6D
	"VK_DECIMAL",   //        0x6E
	"VK_DIVIDE",   //         0x6F
	"VK_F1",   //             0x70
	"VK_F2",   //             0x71
	"VK_F3",   //             0x72
	"VK_F4",   //             0x73
	"VK_F5",   //             0x74
	"VK_F6",   //             0x75
	"VK_F7",   //             0x76
	"VK_F8",   //             0x77
	"VK_F9",   //             0x78
	"VK_F10",   //            0x79
	"VK_F11",   //            0x7A
	"VK_F12",   //            0x7B
	"VK_F13",   //            0x7C
	"VK_F14",   //            0x7D
	"VK_F15",   //            0x7E
	"VK_F16",   //            0x7F
	"VK_F17",   //            0x80
	"VK_F18",   //            0x81
	"VK_F19",   //            0x82
	"VK_F20",   //            0x83
	"VK_F21",   //            0x84
	"VK_F22",   //            0x85
	"VK_F23",   //            0x86
	"VK_F24",   //            0x87
	"VK_NAVIGATION_VIEW",   //     0x88 // reserved
	"VK_NAVIGATION_MENU",   //     0x89 // reserved
	"VK_NAVIGATION_UP",   //       0x8A // reserved
	"VK_NAVIGATION_DOWN",   //     0x8B // reserved
	"VK_NAVIGATION_LEFT",   //     0x8C // reserved
	"VK_NAVIGATION_RIGHT",   //    0x8D // reserved
	"VK_NAVIGATION_ACCEPT",   //   0x8E // reserved
	"VK_NAVIGATION_CANCEL",   //   0x8F // reserved
	"VK_NUMLOCK",   //        0x90
	"VK_SCROLL",   //         0x91
	"VK_OEM_NEC_EQUAL",   //  0x92   // '=' key on numpad
//#define VK_OEM_FJ_JISHO   0x92   // 'Dictionary' key
	"VK_OEM_FJ_MASSHOU",   // 0x93   // 'Unregister word' key
	"VK_OEM_FJ_TOUROKU",   // 0x94   // 'Register word' key
	"VK_OEM_FJ_LOYA",   //    0x95   // 'Left OYAYUBI' key
	"VK_OEM_FJ_ROYA",   //    0x96   // 'Right OYAYUBI' key
	"0x97",   //              0x97
	"0x98",   //              0x98
	"0x99",   //              0x99
	"0x9A",   //              0x9A
	"0x9B",   //              0x9B
	"0x9C",   //              0x9C
	"0x9D",   //              0x9D
	"0x9E",   //              0x9E
	"0x9F",   //              0x9F
	"VK_LSHIFT",   //         0xA0
	"VK_RSHIFT",   //         0xA1
	"VK_LCONTROL",   //       0xA2
	"VK_RCONTROL",   //       0xA3
	"VK_LMENU",   //          0xA4
	"VK_RMENU",   //          0xA5
	"VK_BROWSER_BACK",   //        0xA6
	"VK_BROWSER_FORWARD",   //     0xA7
	"VK_BROWSER_REFRESH",   //     0xA8
	"VK_BROWSER_STOP",   //        0xA9
	"VK_BROWSER_SEARCH",   //      0xAA
	"VK_BROWSER_FAVORITES",   //   0xAB
	"VK_BROWSER_HOME",   //        0xAC
	"VK_VOLUME_MUTE",   //         0xAD
	"VK_VOLUME_DOWN",   //         0xAE
	"VK_VOLUME_UP",   //           0xAF
	"VK_MEDIA_NEXT_TRACK",   //    0xB0
	"VK_MEDIA_PREV_TRACK",   //    0xB1
	"VK_MEDIA_STOP",   //          0xB2
	"VK_MEDIA_PLAY_PAUSE",   //    0xB3
	"VK_LAUNCH_MAIL",   //         0xB4
	"VK_LAUNCH_MEDIA_SELECT",   // 0xB5
	"VK_LAUNCH_APP1",   //         0xB6
	"VK_LAUNCH_APP2",   //         0xB7
	"0xB8",   //                   0xB8
	"0xB9",   //                   0xB9
	"VK_OEM_1",   //          0xBA   // ';:' for US
	"VK_OEM_PLUS",   //       0xBB   // '+' any country
	"VK_OEM_COMMA",   //      0xBC   // ',' any country
	"VK_OEM_MINUS",   //      0xBD   // '-' any country
	"VK_OEM_PERIOD",   //     0xBE   // '.' any country
	"VK_OEM_2",   //          0xBF   // '/?' for US
	"VK_OEM_3",   //          0xC0   // '`~' for US
	"0xC1",   //              0xC1
	"0xC2",   //              0xC2
	"VK_GAMEPAD_A",   //                         0xC3 // reserved
	"VK_GAMEPAD_B",   //                         0xC4 // reserved
	"VK_GAMEPAD_X",   //                         0xC5 // reserved
	"VK_GAMEPAD_Y",   //                         0xC6 // reserved
	"VK_GAMEPAD_RIGHT_SHOULDER",   //            0xC7 // reserved
	"VK_GAMEPAD_LEFT_SHOULDER",   //             0xC8 // reserved
	"VK_GAMEPAD_LEFT_TRIGGER",   //              0xC9 // reserved
	"VK_GAMEPAD_RIGHT_TRIGGER",   //             0xCA // reserved
	"VK_GAMEPAD_DPAD_UP",   //                   0xCB // reserved
	"VK_GAMEPAD_DPAD_DOWN",   //                 0xCC // reserved
	"VK_GAMEPAD_DPAD_LEFT",   //                 0xCD // reserved
	"VK_GAMEPAD_DPAD_RIGHT",   //                0xCE // reserved
	"VK_GAMEPAD_MENU",   //                      0xCF // reserved
	"VK_GAMEPAD_VIEW",   //                      0xD0 // reserved
	"VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON",   //    0xD1 // reserved
	"VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON",   //   0xD2 // reserved
	"VK_GAMEPAD_LEFT_THUMBSTICK_UP",   //        0xD3 // reserved
	"VK_GAMEPAD_LEFT_THUMBSTICK_DOWN",   //      0xD4 // reserved
	"VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT",   //     0xD5 // reserved
	"VK_GAMEPAD_LEFT_THUMBSTICK_LEFT",   //      0xD6 // reserved
	"VK_GAMEPAD_RIGHT_THUMBSTICK_UP",   //       0xD7 // reserved
	"VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN",   //     0xD8 // reserved
	"VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT",   //    0xD9 // reserved
	"VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT",   //     0xDA // reserved
	"VK_OEM_4",   //          0xDB  //  '[{' for US
	"VK_OEM_5",   //          0xDC  //  '\|' for US
	"VK_OEM_6",   //          0xDD  //  ']}' for US
	"VK_OEM_7",   //          0xDE  //  ''"' for US
	"VK_OEM_8",   //          0xDF
	"0xE0",   //              0xE0
	"VK_OEM_AX",   //         0xE1  //  'AX' key on Japanese AX kbd
	"VK_OEM_102",   //        0xE2  //  "<>" or "\|" on RT 102-key kbd.
	"VK_ICO_HELP",   //       0xE3  //  Help key on ICO
	"VK_ICO_00",   //         0xE4  //  00 key on ICO
	"VK_PROCESSKEY",   //     0xE5
	"VK_ICO_CLEAR",   //      0xE6
	"VK_PACKET",   //         0xE7
	"0xE8",   //              0xE8
	"VK_OEM_RESET",   //      0xE9
	"VK_OEM_JUMP",   //       0xEA
	"VK_OEM_PA1",   //        0xEB
	"VK_OEM_PA2",   //        0xEC
	"VK_OEM_PA3",   //        0xED
	"VK_OEM_WSCTRL",   //     0xEE
	"VK_OEM_CUSEL",   //      0xEF
	"VK_OEM_ATTN",   //       0xF0
	"VK_OEM_FINISH",   //     0xF1
	"VK_OEM_COPY",   //       0xF2
	"VK_OEM_AUTO",   //       0xF3
	"VK_OEM_ENLW",   //       0xF4
	"VK_OEM_BACKTAB",   //    0xF5
	"VK_ATTN",   //           0xF6
	"VK_CRSEL",   //          0xF7
	"VK_EXSEL",   //          0xF8
	"VK_EREOF",   //          0xF9
	"VK_PLAY",   //           0xFA
	"VK_ZOOM",   //           0xFB
	"VK_NONAME",   //         0xFC
	"VK_PA1",   //            0xFD
	"VK_OEM_CLEAR",   //      0xFE
	"0xFF"		//              0xFF
};
	return table[ vkCode & 0xFF ];
}


//-----------------------------------------------------------------------------

