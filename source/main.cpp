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

#include <libserialport.h>

// Set to 1, otherwise KM232
#define ASC232 1
#define KM232  0

//-----------------------------------------------------------------------------
//
// KM232 USB COMMAND CONSTANTS
//
const unsigned char USB_BufferClear   = 0x38;			// Like a device Reset

const unsigned char USB_MouseLeft     = 0x42;
const unsigned char USB_MouseRight    = 0x43;
const unsigned char USB_MouseUp       = 0x44;
const unsigned char USB_MouseDown     = 0x45;

const unsigned char USB_MouseLeftButton   = 0x49;
const unsigned char USB_MouseRightButton  = 0x4A;
const unsigned char USB_MouseMiddleButton = 0x4D;

const unsigned char USB_ScrollWheelUp   = 0x57;
const unsigned char USB_ScrollWheelDown = 0x58;

const unsigned char USB_MouseSlow     = 0x6D;
const unsigned char USB_MouseFast     = 0x6F;

const unsigned char USB_StatusLEDRead = 0x7F;			// Status of LEDs

// And masks for the StatusLEDRead Command
const unsigned char StatusNumLock    = 0x01;
const unsigned char StatusCapsLock   = 0x02;
const unsigned char StatusScrollLock = 0x04;

const unsigned char USB_BREAK = 128;




//-----------------------------------------------------------------------------
// Global Variables for this Toy Program
HANDLE hStdin;
HANDLE hStdOut;
DWORD fdwSaveOldMode;
static HHOOK keyboardHook = nullptr;
struct sp_port* pSCC = nullptr;

//
// Current List of Keys that are down
//
static std::vector<WORD> keys;	// list of keys that are down


//-----------------------------------------------------------------------------
// Prototypes
VOID ErrorExit(LPCSTR);
VOID KeyEventProc(KEY_EVENT_RECORD);
VOID MouseEventProc(MOUSE_EVENT_RECORD);
VOID ResizeEventProc(WINDOW_BUFFER_SIZE_RECORD);
void FocusEventProc(FOCUS_EVENT_RECORD fer);
void InitScreen(int width, int height);
const char* KeyToString(WORD vkCode);
void InitSerialPort(const char* portName);
int SerialSend(unsigned char);  // Send/Receive Char, if result < 0, then timeout
unsigned char KeyToMakeCode(WORD vkCode);
void RegisterKeyboardHook();

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

	// Setup the Serial Port
	InitSerialPort("COM4");

	// Set we can do ctrl-alt-esc
	//RegisterKeyboardHook();

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
				FocusEventProc(irInBuf[i].Event.FocusEvent);
				break;

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

//-----------------------------------------------------------------------------

void FocusEventProc(FOCUS_EVENT_RECORD fer)
{
	COORD dwCursorPosition;
	dwCursorPosition.X = 0;
	dwCursorPosition.Y = 1;
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);

	printf("FOCUS EVENT: %s ", fer.bSetFocus ? "true" : "false");

	if (!fer.bSetFocus)
	{
		#if 0
		// When we lose focus, we better release all the keys
		while (keys.size())
		{
			WORD key = keys[ keys.size() - 1 ];
			keys.pop_back();

			unsigned char km_code = KeyToMakeCode( key );
			if (km_code)
			{
				// Send Break Code
				SerialSend(km_code + USB_BREAK);
			}
		}
		#else
		// Clear the Keys more efficiently
		if (0 != keys.size())
		{
			keys.clear();
			SerialSend(USB_BufferClear); // USB Buffer Clear
		}
		#endif

		// Erase the Key Status
		dwCursorPosition.Y = 4;
		SetConsoleCursorPosition(hStdOut, dwCursorPosition);
		printf("                                                                ");


	}
}

//-----------------------------------------------------------------------------

VOID KeyEventProc(KEY_EVENT_RECORD ker)
{
// I'm using a vector instead of a map, because I need to know which keys
// are the oldest, to simulator rollover

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

			// Send Make
			unsigned char km_code = KeyToMakeCode( ker.wVirtualKeyCode );
			if (km_code)
			{
				SerialSend(km_code);
			}
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

				unsigned char km_code = KeyToMakeCode( ker.wVirtualKeyCode );
				if (km_code)
				{
					// Send Break Code
					SerialSend(km_code + USB_BREAK);
				}


				break;
			}
		}

	}

//-----------------------------------------------------------------------------
//  Dump the list of keys that are down
//
	COORD dwCursorPosition;
	dwCursorPosition.X = 0;
	dwCursorPosition.Y = 4;
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);
	printf("                                                                ");
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);

	for (int idx = 0; idx < keys.size(); ++idx)
	{
		printf(" %s(%02X)", KeyToString(keys[idx]), keys[idx]);
		//printf(" %s", KeyToString(keys[idx]));
	}

}

//-----------------------------------------------------------------------------

VOID MouseEventProc(MOUSE_EVENT_RECORD mer)
{
static POINT currentMouse;
static bool mouseTrack;
static bool button0 = false;

	COORD dwCursorPosition;
	dwCursorPosition.X = 0;
	dwCursorPosition.Y = 8;
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);
	printf("                                                                ");
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);

#ifndef MOUSE_HWHEELED
#define MOUSE_HWHEELED 0x0008
#endif
    printf("Mouse:");

    switch (mer.dwEventFlags)
    {
    case DOUBLE_CLICK:
        printf(" 2click");
    case 0:

        if (mer.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)
        {
            printf(" left");
			SerialSend(USB_MouseLeftButton); 	// Make Code
			button0 = true;
        }
		else if (button0)
		{
			button0 = false;
			SerialSend(USB_MouseLeftButton + USB_BREAK);  // Break Code
		}

        if (mer.dwButtonState & RIGHTMOST_BUTTON_PRESSED)
        {
            printf(" right");
			GetCursorPos(&currentMouse);
			mouseTrack = true;
        }
		else
		{
			mouseTrack = false;
		}

		if (mer.dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED)
		{
			printf(" middle");
		}
        break;
    case MOUSE_HWHEELED:
        printf("h wheel");
        break;
	case MOUSE_MOVED:
		{
			POINT p;
			if (GetCursorPos(&p))
			{
				//cursor position now in p.x and p.y
				printf(" %d,%d %d,%d", p.x, p.y, mer.dwMousePosition.X, mer.dwMousePosition.Y );
			}

			if (mouseTrack)
			{
				int result;

				while ((p.x != currentMouse.x) || (p.y != currentMouse.y))
				{
					if (p.x > currentMouse.x)
					{
						currentMouse.x++;
						result = SerialSend(USB_MouseRight);
					}
					else if (p.x < currentMouse.x)
					{
						currentMouse.x--;
						result = SerialSend(USB_MouseLeft);
					}

					// Timeout on the port
					if (result < 0) break;

					if (p.y > currentMouse.y)
					{
						currentMouse.y++;
						result = SerialSend(USB_MouseDown);
					}
					else if (p.y < currentMouse.y)
					{
						currentMouse.y--;
						result = SerialSend(USB_MouseUp);
					}

					// Timeout on the port
					if (result < 0) break;
				}
			}

		}
        break;
    case MOUSE_WHEELED:
        printf(" wheel");
        break;
    default:
        printf(" unknown");
        break;
    }
}

VOID ResizeEventProc(WINDOW_BUFFER_SIZE_RECORD wbsr)
{
	COORD dwCursorPosition;

	dwCursorPosition.Y = 23;
	dwCursorPosition.X = 0;

// Hide Cursor
	CONSOLE_CURSOR_INFO cursorInfo;
	cursorInfo.dwSize = 100;
	cursorInfo.bVisible = FALSE;
	SetConsoleCursorInfo(hStdOut, &cursorInfo);

//  Erase Previous Position
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);
	printf("                                                                ");
// Set New Position
//	dwCursorPosition.Y = wbsr.dwSize.Y - 2;
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);
    printf("Console screen buffer is %d columns by %d rows.", wbsr.dwSize.X, wbsr.dwSize.Y);
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
	SetConsoleTitle(L"KM232 Terminal - Version 0.1");

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
//
// Convert VK/ Windows Virtual Key into a BreakCode for the KM232
// 
// A result of 0, means no translation available 
//
unsigned char KeyToMakeCode(WORD vkCode)
{
static const char table[ 256 ] 
{
	0, //"0x00",				// 0x00
	0, //"VK_LBUTTON",		// 0x01
	0, //"VK_RBUTTON",		// 0x02
	0, //"VK_CANCEL",		// 0x03
	0, //"VK_MBUTTON",		// 0x04 /* NOT contiguous with L & RBUTTON */
	0, //"VK_XBUTTON1",		// 0x05 /* NOT contiguous with L & RBUTTON */
	0, //"VK_XBUTTON2",		// 0x06 /* NOT contiguous with L & RBUTTON */
	0, //"0x07",				// 0x07
	15, // VK_BACK, 0x08
	16, // VK_TAB,  0x09
	0, //"0x0A",				// 0x0A
	0, //"0x0B",				// 0x0B
	0, //"VK_CLEAR",			// 0x0C
	43, // VK_RETURN, 0x0D
	0, //"0x0E",				// 0x0E
	0, //"0x0F",				// 0x0F
	44, // VK_SHIFT, 0x10 - This is the code for Left Shift
	58, // VK_CONTROL, 0x11 - Left Control
	60, // VK_MENU, 0x12 - Left Alt
	0, //"VK_PAUSE",			// 0x13
	30, // VK_CAPITAL, 0x14
	0, //"VK_KANA",			// 0x15 VK_HANGEUL,VK_HANGUL
	0, //"0x16",				// 0x16 unassigned
	0, //"VK_JUNJA",			// 0x17
	0, //"VK_FINAL",			// 0x18
	0, //"VK_HANJA",			// 0x19 VK_KANJI
	0, //"0x1A",				// 0x1A unassigned
	110, // VK_ESCAPE, 0x1B
	0, //"VK_CONVERT",		// 0x1C
	0, //"VK_NONCONVERT",	// 0x1D
	0, //"VK_ACCEPT",		// 0x1E
	0, //"VK_MODECHANGE",	// 0x1F
	61, // VK_SPACE, 0x20
	85, // VK_PRIOR, 0x21 - Page Up
	86, // VK_NEXT", 0x22 - Page Down
	81, // VK_END, 0x23
	80, // VK_HOME, 0x24
	79, // VK_LEFT, 0x25
	83, // VK_UP, 0x26
	89, // VK_RIGHT, 0x27
	84, // VK_DOWN, 0x28
	0, //"VK_SELECT",		// 0x29
	0, //"VK_PRINT",			// 0x2A
	0, //"VK_EXECUTE",		// 0x2B
	0, //"VK_SNAPSHOT",		// 0x2C
	75, // VK_INSERT, 0x2D
	76, // VK_DELETE, 0x2E
	0, //"VK_HELP",			// 0x2F
	11, // VK_0,0x30
	2,  // VK_1,0x31
	3,  // VK_2,0x32
	4,  // VK_3,0x33
	5,  // VK_4,0x34
	6,  // VK_5,0x35
	7,  // VK_6,0x36
	8,  // VK_7,0x37
	9,  // VK_8,0x38
	10, // VK_9,0x39
	0,  // 0x3A
	0,  // 0x3B
	0,  // 0x3C
	0,  // 0x3D
	0,  // 0x3E
	0,  // 0x3F
	0,  // 0x40
	31, // VK_A, 0x41
	50, // VK_B, 0x42
	48, // VK_C, 0x43
	33, // VK_D, 0x44
	19, // VK_E, 0x45
	34, // VK_F, 0x46
	35, // VK_G, 0x47
	36, // VK_H, 0x48
	24, // VK_I, 0x49
	37, // VK_J, 0x4A
	38, // VK_K, 0x4B
	39, // VK_L, 0x4C
	52, // VK_M, 0x4D
	51, // VK_N, 0x4E
	25, // VK_O, 0x4F
	26, // VK_P, 0x50
	17, // VK_Q, 0x51
	20, // VK_R, 0x52
	32, // VK_S, 0x53
	21, // VK_T, 0x54
	23, // VK_U, 0x55
	49, // VK_V, 0x56
	18, // VK_W, 0x57
	47, // VK_X, 0x58
	22, // VK_Y, 0x59
	46, // VK_Z, 0x5A
	70, // VK_LWIN, 0x5B
	71, // VK_RWIN, 0x5C
	0, //"VK_APPS",          // 0x5D
	0, //"0x5E",             // 0x5E reserved
	0, //"VK_SLEEP",         // 0x5F
	99, // VK_NUMPAD0, 0x60
	93, // VK_NUMPAD1, 0x61
	98, // VK_NUMPAD2, 0x62
	103,// VK_NUMPAD3, 0x63
	92, // VK_NUMPAD4, 0x64
	97, // VK_NUMPAD5, 0x65
	102,// VK_NUMPAD6, 0x66
	91, // VK_NUMPAD7, 0x67
	96, // VK_NUMPAD8, 0x68
	101, //VK_NUMPAD9, 0x69
	100, //"VK_MULTIPLY", 0x6A
	106, //VK_ADD, 0x6B
	0, //"VK_SEPARATOR",		// 0x6C
	105,//VK_SUBTRACT,0x6D
	104,//VK_DECIMAL, 0x6E
	95, // VK_DIVIDE, 0x6F
	112, // VK_F1, 0x70
	113, // VK_F2, 0x71
	114, // VK_F3, 0x72
	115, // VK_F4, 0x73
	116, // VK_F5, 0x74
	117, // VK_F6, 0x75
	118, // VK_F7, 0x76
	119, // VK_F8, 0x77
	120, // VK_F9, 0x78
	121, // VK_F10, 0x79
	122, // VK_F11, 0x7A
	124, //123, // VK_F12, 0x7B
	0, //"VK_F13",			// 0x7C
	0, //"VK_F14",           // 0x7D
	0, //"VK_F15",           // 0x7E
	0, //"VK_F16",           // 0x7F
	0, //"VK_F17",           // 0x80
	0, //"VK_F18",           // 0x81
	0, //"VK_F19",           // 0x82
	0, //"VK_F20",           // 0x83
	0, //"VK_F21",           // 0x84
	0, //"VK_F22",           // 0x85
	0, //"VK_F23",           // 0x86
	0, //"VK_F24",           // 0x87
	0, //"VK_NAVIGATION_VIEW",	// 0x88 reserved
	0, //"VK_NAVIGATION_MENU",	// 0x89 reserved
	0, //"VK_NAVIGATION_UP",		// 0x8A reserved
	0, //"VK_NAVIGATION_DOWN",	// 0x8B reserved
	0, //"VK_NAVIGATION_LEFT",	// 0x8C reserved
	0, //"VK_NAVIGATION_RIGHT",	// 0x8D reserved
	0, //"VK_NAVIGATION_ACCEPT",	// 0x8E reserved
	0, //"VK_NAVIGATION_CANCEL",	// 0x8F reserved
	90, // VK_NUMLOCK, 0x90
	125,// VK_SCROLL,  0x91   Scroll Lock
	0, //"VK_OEM_NEC_EQUAL",		// 0x92 '=' key on numpad, VK_OEM_FJ_JISHO 'Dictionary' key
	0, //"VK_OEM_FJ_MASSHOU",	// 0x93 'Unregister word' key
	0, //"VK_OEM_FJ_TOUROKU",	// 0x94 'Register word' key
	0, //"VK_OEM_FJ_LOYA",		// 0x95 'Left OYAYUBI' key
	0, //"VK_OEM_FJ_ROYA",		// 0x96 'Right OYAYUBI' key
	0, //"0x97",					// 0x97
	0, //"0x98",                 // 0x98
	0, //"0x99",                 // 0x99
	0, //"0x9A",                 // 0x9A
	0, //"0x9B",                 // 0x9B
	0, //"0x9C",                 // 0x9C
	0, //"0x9D",                 // 0x9D
	0, //"0x9E",                 // 0x9E
	0, //"0x9F",                 // 0x9F
	44, //VK_LSHIFT, 0xA0
	57, //VK_RSHIFT, 0xA1
	58, //VK_LCONTROL, 0xA2
	64, //VK_RCONTROL, 0xA3
	60, //VK_LMENU, 0xA4, L-Alt
	62, //VK_RMENU, 0xA5, R-Alt
	0, //"VK_BROWSER_BACK",		// 0xA6
	0, //"VK_BROWSER_FORWARD",	// 0xA7
	0, //"VK_BROWSER_REFRESH",	// 0xA8
	0, //"VK_BROWSER_STOP",		// 0xA9
	0, //"VK_BROWSER_SEARCH",	// 0xAA
	0, //"VK_BROWSER_FAVORITES", // 0xAB
	0, //"VK_BROWSER_HOME",		// 0xAC
	0, //"VK_VOLUME_MUTE",		// 0xAD
	0, //"VK_VOLUME_DOWN",		// 0xAE
	0, //"VK_VOLUME_UP",			// 0xAF
	0, //"VK_MEDIA_NEXT_TRACK",	// 0xB0
	0, //"VK_MEDIA_PREV_TRACK",	// 0xB1
	0, //"VK_MEDIA_STOP",		// 0xB2
	0, //"VK_MEDIA_PLAY_PAUSE",	// 0xB3
	0, //"VK_LAUNCH_MAIL",		// 0xB4
	0, //"VK_LAUNCH_MEDIA_SELECT", // 0xB5
	0, //"VK_LAUNCH_APP1",   	// 0xB6
	0, //"VK_LAUNCH_APP2",		// 0xB7
	0, //"0xB8",					// 0xB8
	0, //"0xB9",					// 0xB9
	40, // VK_OEM_1, 0xBA, ';:' for US
	13, // VK_OEM_PLUS, 0xBB, '=+' any country
	53, // VK_OEM_COMMA, 0xBC ',<' any country
	12, // VK_OEM_MINUS, 0xBD, '-_' any country
	54, // VK_OEM_PERIOD, 0xBE '.>' any country
	55, // VK_OEM_2, 0xBF, '/?' for US
	1, //VK_OEM_3,0xC0 '`~' for US
	0, //"0xC1",					// 0xC1
	0, //"0xC2",					// 0xC2
	0, //"VK_GAMEPAD_A",							// 0xC3 reserved
	0, //"VK_GAMEPAD_B",							// 0xC4 reserved
	0, //"VK_GAMEPAD_X",							// 0xC5 reserved
	0, //"VK_GAMEPAD_Y",							// 0xC6 reserved
	0, //"VK_GAMEPAD_RIGHT_SHOULDER",			// 0xC7 reserved
	0, //"VK_GAMEPAD_LEFT_SHOULDER",				// 0xC8 reserved
	0, //"VK_GAMEPAD_LEFT_TRIGGER",				// 0xC9 reserved
	0, //"VK_GAMEPAD_RIGHT_TRIGGER",				// 0xCA reserved
	0, //"VK_GAMEPAD_DPAD_UP",					// 0xCB reserved
	0, //"VK_GAMEPAD_DPAD_DOWN",					// 0xCC reserved
	0, //"VK_GAMEPAD_DPAD_LEFT",					// 0xCD reserved
	0, //"VK_GAMEPAD_DPAD_RIGHT",				// 0xCE reserved
	0, //"VK_GAMEPAD_MENU",						// 0xCF reserved
	0, //"VK_GAMEPAD_VIEW",						// 0xD0 reserved
	0, //"VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON",	// 0xD1 reserved
	0, //"VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON",   // 0xD2 reserved
	0, //"VK_GAMEPAD_LEFT_THUMBSTICK_UP",		// 0xD3 reserved
	0, //"VK_GAMEPAD_LEFT_THUMBSTICK_DOWN",		// 0xD4 reserved
	0, //"VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT",		// 0xD5 reserved
	0, //"VK_GAMEPAD_LEFT_THUMBSTICK_LEFT", 		// 0xD6 reserved
	0, //"VK_GAMEPAD_RIGHT_THUMBSTICK_UP",		// 0xD7 reserved
	0, //"VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN",		// 0xD8 reserved
	0, //"VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT",	// 0xD9 reserved
	0, //"VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT",		// 0xDA reserved
	27, // VK_OEM_4, 0xDB, '[{' for US
	29, // VK_OEM_5, 0xDC, '\|' for US
	28, // VK_OEM_6, 0xDD, ']}' for US
	41, // VK_OEM_7, 0xDE, ''"' for US
	0, //"VK_OEM_8",			    // 0xDF
	0, //"0xE0",				    // 0xE0
	0, //"VK_OEM_AX",		    // 0xE1 'AX' key on Japanese AX kbd
	0, //"VK_OEM_102",		    // 0xE2 "<>" or "\|" on RT 102-key kbd.
	0, //"VK_ICO_HELP",		    // 0xE3 Help key on ICO
	0, //"VK_ICO_00",		    // 0xE4 00 key on ICO
	0, //"VK_PROCESSKEY",	    // 0xE5
	0, //"VK_ICO_CLEAR",		    // 0xE6
	0, //"VK_PACKET",		    // 0xE7
	0, //"0xE8",				    // 0xE8
	0, //"VK_OEM_RESET",		    // 0xE9
	0, //"VK_OEM_JUMP",		    // 0xEA
	0, //"VK_OEM_PA1",		    // 0xEB
	0, //"VK_OEM_PA2",		    // 0xEC
	0, //"VK_OEM_PA3",		    // 0xED
	0, //"VK_OEM_WSCTRL",	    // 0xEE
	0, //"VK_OEM_CUSEL",		    // 0xEF
	0, //"VK_OEM_ATTN",		    // 0xF0
	0, //"VK_OEM_FINISH",	    // 0xF1
	0, //"VK_OEM_COPY",		    // 0xF2
	0, //"VK_OEM_AUTO",		    // 0xF3
	0, //"VK_OEM_ENLW",		    // 0xF4
	0, //"VK_OEM_BACKTAB",	    // 0xF5
	0, //"VK_ATTN",			    // 0xF6
	0, //"VK_CRSEL",			    // 0xF7
	0, //"VK_EXSEL",			    // 0xF8
	0, //"VK_EREOF",			    // 0xF9
	0, //"VK_PLAY",			    // 0xFA
	0, //"VK_ZOOM",			    // 0xFB
	0, //"VK_NONAME",		    // 0xFC
	0, //"VK_PA1",			    // 0xFD
	0, //"VK_OEM_CLEAR",		    // 0xFE
	0  //"0xFF"				    // 0xFF
};
	return table[ vkCode & 0xFF ];
}

//-----------------------------------------------------------------------------

const char* KeyToString(WORD vkCode)
{
static const char* table[ 256 ]
{
		"0x00",				// 0x00
		"VK_LBUTTON",		// 0x01
		"VK_RBUTTON",		// 0x02
		"VK_CANCEL",		// 0x03
		"VK_MBUTTON",		// 0x04 /* NOT contiguous with L & RBUTTON */
		"VK_XBUTTON1",		// 0x05 /* NOT contiguous with L & RBUTTON */
		"VK_XBUTTON2",		// 0x06 /* NOT contiguous with L & RBUTTON */
		"0x07",				// 0x07
		"VK_BACK",			// 0x08
		"VK_TAB",			// 0x09
		"0x0A",				// 0x0A
		"0x0B",				// 0x0B
		"VK_CLEAR",			// 0x0C
		"VK_RETURN",		// 0x0D
		"0x0E",				// 0x0E
		"0x0F",				// 0x0F
		"VK_SHIFT",			// 0x10
		"VK_CONTROL",		// 0x11
		"VK_MENU",			// 0x12
		"VK_PAUSE",			// 0x13
		"VK_CAPITAL",		// 0x14
		"VK_KANA",			// 0x15 VK_HANGEUL,VK_HANGUL
		"0x16",				// 0x16 unassigned
		"VK_JUNJA",			// 0x17
		"VK_FINAL",			// 0x18
		"VK_HANJA",			// 0x19 VK_KANJI
		"0x1A",				// 0x1A unassigned
		"VK_ESCAPE",		// 0x1B
		"VK_CONVERT",		// 0x1C
		"VK_NONCONVERT",	// 0x1D
		"VK_ACCEPT",		// 0x1E
		"VK_MODECHANGE",	// 0x1F
		"VK_SPACE",			// 0x20
		"VK_PRIOR",			// 0x21
		"VK_NEXT",			// 0x22
		"VK_END",			// 0x23
		"VK_HOME",			// 0x24
		"VK_LEFT",			// 0x25
		"VK_UP",			// 0x26
		"VK_RIGHT",			// 0x27
		"VK_DOWN",			// 0x28
		"VK_SELECT",		// 0x29
		"VK_PRINT",			// 0x2A
		"VK_EXECUTE",		// 0x2B
		"VK_SNAPSHOT",		// 0x2C
		"VK_INSERT",		// 0x2D
		"VK_DELETE",		// 0x2E
		"VK_HELP",			// 0x2F
		"VK_0",				// 0x30
		"VK_1",				// 0x31
		"VK_2",				// 0x32
		"VK_3",             // 0x33
		"VK_4",             // 0x34
		"VK_5",             // 0x35
		"VK_6",             // 0x36
		"VK_7",             // 0x37
		"VK_8",             // 0x38
		"VK_9",             // 0x39
		"0x3A",             // 0x3A
		"0x3B",             // 0x3B
		"0x3C",             // 0x3C
		"0x3D",             // 0x3D
		"0x3E",             // 0x3E
		"0x3F",             // 0x3F
		"0x40",             // 0x40 unassigned
		"VK_A",             // 0x41
		"VK_B",             // 0x42
		"VK_C",             // 0x43
		"VK_D",             // 0x44
		"VK_E",             // 0x45
		"VK_F",             // 0x46
		"VK_G",             // 0x47
		"VK_H",             // 0x48
		"VK_I",             // 0x49
		"VK_J",             // 0x4A
		"VK_K",             // 0x4B
		"VK_L",             // 0x4C
		"VK_M",             // 0x4D
		"VK_N",             // 0x4E
		"VK_O",             // 0x4F
		"VK_P",             // 0x50
		"VK_Q",             // 0x51
		"VK_R",             // 0x52
		"VK_S",             // 0x53
		"VK_T",             // 0x54
		"VK_U",             // 0x55
		"VK_V",             // 0x56
		"VK_W",             // 0x57
		"VK_X",             // 0x58
		"VK_Y",             // 0x59
		"VK_Z",             // 0x5A
		"VK_LWIN",          // 0x5B
		"VK_RWIN",          // 0x5C
		"VK_APPS",          // 0x5D
		"0x5E",             // 0x5E reserved
		"VK_SLEEP",         // 0x5F
		"VK_NUMPAD0",       // 0x60
		"VK_NUMPAD1",       // 0x61
		"VK_NUMPAD2",       // 0x62
		"VK_NUMPAD3",       // 0x63
		"VK_NUMPAD4",       // 0x64
		"VK_NUMPAD5",       // 0x65
		"VK_NUMPAD6",       // 0x66
		"VK_NUMPAD7",       // 0x67
		"VK_NUMPAD8",       // 0x68
		"VK_NUMPAD9",       // 0x69
		"VK_MULTIPLY",      // 0x6A
		"VK_ADD",           // 0x6B
		"VK_SEPARATOR",		// 0x6C
		"VK_SUBTRACT",		// 0x6D
		"VK_DECIMAL", 		// 0x6E
		"VK_DIVIDE",		// 0x6F
		"VK_F1",			// 0x70
		"VK_F2",			// 0x71
		"VK_F3",            // 0x72
		"VK_F4",            // 0x73
		"VK_F5",            // 0x74
		"VK_F6",            // 0x75
		"VK_F7",            // 0x76
		"VK_F8",            // 0x77
		"VK_F9",            // 0x78
		"VK_F10",			// 0x79
		"VK_F11",			// 0x7A
		"VK_F12",			// 0x7B
		"VK_F13",			// 0x7C
		"VK_F14",           // 0x7D
		"VK_F15",           // 0x7E
		"VK_F16",           // 0x7F
		"VK_F17",           // 0x80
		"VK_F18",           // 0x81
		"VK_F19",           // 0x82
		"VK_F20",           // 0x83
		"VK_F21",           // 0x84
		"VK_F22",           // 0x85
		"VK_F23",           // 0x86
		"VK_F24",           // 0x87
		"VK_NAVIGATION_VIEW",	// 0x88 reserved
		"VK_NAVIGATION_MENU",	// 0x89 reserved
		"VK_NAVIGATION_UP",		// 0x8A reserved
		"VK_NAVIGATION_DOWN",	// 0x8B reserved
		"VK_NAVIGATION_LEFT",	// 0x8C reserved
		"VK_NAVIGATION_RIGHT",	// 0x8D reserved
		"VK_NAVIGATION_ACCEPT",	// 0x8E reserved
		"VK_NAVIGATION_CANCEL",	// 0x8F reserved
		"VK_NUMLOCK",			// 0x90
		"VK_SCROLL",			// 0x91
		"VK_OEM_NEC_EQUAL",		// 0x92 '=' key on numpad, VK_OEM_FJ_JISHO 'Dictionary' key
		"VK_OEM_FJ_MASSHOU",	// 0x93 'Unregister word' key
		"VK_OEM_FJ_TOUROKU",	// 0x94 'Register word' key
		"VK_OEM_FJ_LOYA",		// 0x95 'Left OYAYUBI' key
		"VK_OEM_FJ_ROYA",		// 0x96 'Right OYAYUBI' key
		"0x97",					// 0x97
		"0x98",                 // 0x98
		"0x99",                 // 0x99
		"0x9A",                 // 0x9A
		"0x9B",                 // 0x9B
		"0x9C",                 // 0x9C
		"0x9D",                 // 0x9D
		"0x9E",                 // 0x9E
		"0x9F",                 // 0x9F
		"VK_LSHIFT",			// 0xA0
		"VK_RSHIFT",			// 0xA1
		"VK_LCONTROL",			// 0xA2
		"VK_RCONTROL",			// 0xA3
		"VK_LMENU",				// 0xA4
		"VK_RMENU",				// 0xA5
		"VK_BROWSER_BACK",		// 0xA6
		"VK_BROWSER_FORWARD",	// 0xA7
		"VK_BROWSER_REFRESH",	// 0xA8
		"VK_BROWSER_STOP",		// 0xA9
		"VK_BROWSER_SEARCH",	// 0xAA
		"VK_BROWSER_FAVORITES", // 0xAB
		"VK_BROWSER_HOME",		// 0xAC
		"VK_VOLUME_MUTE",		// 0xAD
		"VK_VOLUME_DOWN",		// 0xAE
		"VK_VOLUME_UP",			// 0xAF
		"VK_MEDIA_NEXT_TRACK",	// 0xB0
		"VK_MEDIA_PREV_TRACK",	// 0xB1
		"VK_MEDIA_STOP",		// 0xB2
		"VK_MEDIA_PLAY_PAUSE",	// 0xB3
		"VK_LAUNCH_MAIL",		// 0xB4
		"VK_LAUNCH_MEDIA_SELECT", // 0xB5
		"VK_LAUNCH_APP1",   	// 0xB6
		"VK_LAUNCH_APP2",		// 0xB7
		"0xB8",					// 0xB8
		"0xB9",					// 0xB9
		"VK_OEM_1",				// 0xBA ';:' for US
		"VK_OEM_PLUS",			// 0xBB '+' any country
		"VK_OEM_COMMA",			// 0xBC ',' any country
		"VK_OEM_MINUS",			// 0xBD '-' any country
		"VK_OEM_PERIOD",		// 0xBE '.' any country
		"VK_OEM_2",				// 0xBF '/?' for US
		"VK_OEM_3",				// 0xC0 '`~' for US
		"0xC1",					// 0xC1
		"0xC2",					// 0xC2
		"VK_GAMEPAD_A",							// 0xC3 reserved
		"VK_GAMEPAD_B",							// 0xC4 reserved
		"VK_GAMEPAD_X",							// 0xC5 reserved
		"VK_GAMEPAD_Y",							// 0xC6 reserved
		"VK_GAMEPAD_RIGHT_SHOULDER",			// 0xC7 reserved
		"VK_GAMEPAD_LEFT_SHOULDER",				// 0xC8 reserved
		"VK_GAMEPAD_LEFT_TRIGGER",				// 0xC9 reserved
		"VK_GAMEPAD_RIGHT_TRIGGER",				// 0xCA reserved
		"VK_GAMEPAD_DPAD_UP",					// 0xCB reserved
		"VK_GAMEPAD_DPAD_DOWN",					// 0xCC reserved
		"VK_GAMEPAD_DPAD_LEFT",					// 0xCD reserved
		"VK_GAMEPAD_DPAD_RIGHT",				// 0xCE reserved
		"VK_GAMEPAD_MENU",						// 0xCF reserved
		"VK_GAMEPAD_VIEW",						// 0xD0 reserved
		"VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON",	// 0xD1 reserved
		"VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON",   // 0xD2 reserved
		"VK_GAMEPAD_LEFT_THUMBSTICK_UP",		// 0xD3 reserved
		"VK_GAMEPAD_LEFT_THUMBSTICK_DOWN",		// 0xD4 reserved
		"VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT",		// 0xD5 reserved
		"VK_GAMEPAD_LEFT_THUMBSTICK_LEFT", 		// 0xD6 reserved
		"VK_GAMEPAD_RIGHT_THUMBSTICK_UP",		// 0xD7 reserved
		"VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN",		// 0xD8 reserved
		"VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT",	// 0xD9 reserved
		"VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT",		// 0xDA reserved
		"VK_OEM_4",				// 0xDB '[{' for US
		"VK_OEM_5",			    // 0xDC '\|' for US
		"VK_OEM_6",			    // 0xDD ']}' for US
		"VK_OEM_7",			    // 0xDE ''"' for US
		"VK_OEM_8",			    // 0xDF
		"0xE0",				    // 0xE0
		"VK_OEM_AX",		    // 0xE1 'AX' key on Japanese AX kbd
		"VK_OEM_102",		    // 0xE2 "<>" or "\|" on RT 102-key kbd.
		"VK_ICO_HELP",		    // 0xE3 Help key on ICO
		"VK_ICO_00",		    // 0xE4 00 key on ICO
		"VK_PROCESSKEY",	    // 0xE5
		"VK_ICO_CLEAR",		    // 0xE6
		"VK_PACKET",		    // 0xE7
		"0xE8",				    // 0xE8
		"VK_OEM_RESET",		    // 0xE9
		"VK_OEM_JUMP",		    // 0xEA
		"VK_OEM_PA1",		    // 0xEB
		"VK_OEM_PA2",		    // 0xEC
		"VK_OEM_PA3",		    // 0xED
		"VK_OEM_WSCTRL",	    // 0xEE
		"VK_OEM_CUSEL",		    // 0xEF
		"VK_OEM_ATTN",		    // 0xF0
		"VK_OEM_FINISH",	    // 0xF1
		"VK_OEM_COPY",		    // 0xF2
		"VK_OEM_AUTO",		    // 0xF3
		"VK_OEM_ENLW",		    // 0xF4
		"VK_OEM_BACKTAB",	    // 0xF5
		"VK_ATTN",			    // 0xF6
		"VK_CRSEL",			    // 0xF7
		"VK_EXSEL",			    // 0xF8
		"VK_EREOF",			    // 0xF9
		"VK_PLAY",			    // 0xFA
		"VK_ZOOM",			    // 0xFB
		"VK_NONAME",		    // 0xFC
		"VK_PA1",			    // 0xFD
		"VK_OEM_CLEAR",		    // 0xFE
		"0xFF"				    // 0xFF
};
	return table[ vkCode & 0xFF ];
}


//-----------------------------------------------------------------------------

void InitSerialPort(const char* portName)
{
//-------------------------------------------------------
// Cursor Position ($$TODO), make this easier
	COORD dwCursorPosition;
	dwCursorPosition.X = 0;
	dwCursorPosition.Y = 0;
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);
//-------------------------------------------------------

	sp_get_port_by_name(portName, &pSCC);

	if (pSCC)
	{
		if (SP_OK == sp_open(pSCC, SP_MODE_READ_WRITE))
		{
			#if !ASC232
			sp_set_baudrate(pSCC, 9600);
			sp_set_bits(pSCC, 8);
			sp_set_parity(pSCC, SP_PARITY_NONE);
			sp_set_stopbits(pSCC, 1);
			sp_set_flowcontrol(pSCC, SP_FLOWCONTROL_NONE);
			#else
			sp_set_baudrate(pSCC, 38400);
			sp_set_bits(pSCC, 8);
			sp_set_parity(pSCC, SP_PARITY_NONE);
			sp_set_stopbits(pSCC, 1);
			sp_set_flowcontrol(pSCC, SP_FLOWCONTROL_RTSCTS);
			#endif

#if ASC232
//			Sleep(1000);
#endif


			// Probably a good idea to reset the keyboard if it's out first connect
			int result = SerialSend( USB_BufferClear );
			//printf("result = %02x", result);

#if KM232
			if (result >= 0)
				result = SerialSend( USB_MouseFast );
#endif

			if (result >= 0)
				result = SerialSend( USB_StatusLEDRead );

//			printf(" result = %02x", result);

			if (result >= 0)
			{
				if ((result >= 0x30) && (result <= 0x37))
				{
					#if ASC232
					printf("ASC232 live on %s", portName);
					#else
					printf("KM232 live on %s", portName);
					#endif
				}
			}
			else
			{
				printf("No Response on %s", portName);
			}
		}
		else
		{
			printf("FAILED TO OPEN - %s", portName);
		}
	}
	else
	{
		printf("FAILED TO FIND PORT - %s", portName);
	}
}

//-----------------------------------------------------------------------------
//
// if result < 0, then timeout, or other error
//
int SerialSend(unsigned char command)
{
	int result = -1;

	if (pSCC)
	{
		int num_bytes = sp_blocking_write(pSCC, &command, 1, 50);

		if (1 == num_bytes)
		{
			// Value Sent, get the result, and return it back
			unsigned char byte = 0;
			num_bytes = sp_blocking_read(pSCC, &byte, 1, 50);

			if (1 == num_bytes)
			{
				// We got a result
				result = (int)byte;
			}
		}
	}

	return result;
}
//-----------------------------------------------------------------------------
void RemoveKeyboardHook()
{
  if (keyboardHook != nullptr)
  {
    UnhookWindowsHookEx(keyboardHook);
    keyboardHook = nullptr;
  }
}

LRESULT CALLBACK LowLevelKeyboardHook(int code, WPARAM wParam, LPARAM lParam)
{
#if 0
  static bool deskManagerKeysDown = false;
  if ((wParam == WM_KEYDOWN) || (wParam == WM_KEYUP))
  {
    KBDLLHOOKSTRUCT* hookStruct = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    if ((hookStruct->vkCode == VK_ESCAPE) && (GetAsyncKeyState(VK_CONTROL) < 0) && (GetAsyncKeyState(VK_MENU) < 0))
    {
      //Direct2DWindow* mainWindow = Direct2DWindow::GetMainWindow();
      //if ((mainWindow != nullptr) && (GetActiveWindow() == mainWindow->GetHwnd()))
      {
		#if 1
        if (wParam == WM_KEYDOWN)
        {
          // make sure we only add Ctrl+OA+Esc to the queue once
          if (!deskManagerKeysDown)
          {
            deskManagerKeysDown = true;
            //keyboard->keycodeQueue.push(KEYCODE_ESCAPE);
          }
        }
        else
        {
          //keyboard->keycodeQueue.push(KEYCODE_ESCAPE | '\x80');
          deskManagerKeysDown = false;
        }
		#endif
        return reinterpret_cast<LPARAM>(&LowLevelKeyboardHook);
      }
    }
  }
#endif
  return CallNextHookEx(nullptr, code, wParam, lParam);
}
void RegisterKeyboardHook()
{
  if (keyboardHook == nullptr)
  {
    //keyboard = this;
    keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardHook, GetModuleHandle(nullptr), 0);
  }
}
//-----------------------------------------------------------------------------

