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

HANDLE hStdin;
HANDLE hStdOut;
DWORD fdwSaveOldMode;

VOID ErrorExit(LPCSTR);
VOID KeyEventProc(KEY_EVENT_RECORD);
VOID MouseEventProc(MOUSE_EVENT_RECORD);
VOID ResizeEventProc(WINDOW_BUFFER_SIZE_RECORD);
void InitScreen(int width, int height);

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
	COORD dwCursorPosition;
	dwCursorPosition.X = dwCursorPosition.Y = 0;
	SetConsoleCursorPosition(hStdOut, dwCursorPosition);

    printf("Key event: ");

    if (ker.bKeyDown)
        printf("key pressed\n");
    else printf("key released\n");
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

