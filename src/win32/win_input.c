/*
===========================================================================

Return to Castle Wolfenstein multiplayer GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein multiplayer GPL Source Code (RTCW MP Source Code).  

RTCW MP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW MP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW MP Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the RTCW MP Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the RTCW MP Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

// win_input.c -- win32 mouse and joystick code
// 02/21/97 JCB Added extended DirectInput code to support external controllers.

#include "../client/client.h"
#include "win_local.h"

// sswolf - begin rinput - source: quake
#include "in_raw.h"

#ifndef DEDICATED
// defines
#define MAX_RI_DEVICE_SIZE 128
#define INIT_RIBUFFER_SIZE (sizeof(RAWINPUTHEADER)+sizeof(RAWMOUSE))

#define RI_RAWBUTTON_MASK 0x000003E0
#define RI_INVALID_POS    0x80000000

// raw input dynamic functions
typedef int 	(WINAPI* pGetRawInputDeviceList)	(OUT PRAWINPUTDEVICELIST pRawInputDeviceList, IN OUT PINT puiNumDevices, IN UINT cbSize);
typedef int 	(WINAPI* pGetRawInputData)			(IN HRAWINPUT hRawInput, IN UINT uiCommand, OUT LPVOID pData, IN OUT PINT pcbSize, IN UINT cbSizeHeader);
typedef int 	(WINAPI* pGetRawInputDeviceInfoA)	(IN HANDLE hDevice, IN UINT uiCommand, OUT LPVOID pData, IN OUT PINT pcbSize);
typedef BOOL(WINAPI* pRegisterRawInputDevices)	(IN PCRAWINPUTDEVICE pRawInputDevices, IN UINT uiNumDevices, IN UINT cbSize);
typedef int		(WINAPI* pGetLastError)				(void);


pGetRawInputDeviceList		_GRIDL;
pGetRawInputData			_GRID;
pGetRawInputDeviceInfoA		_GRIDIA;
pRegisterRawInputDevices	_RRID;
pGetLastError				_GLE;

typedef struct
{

	HANDLE			rawinputhandle; // raw input, identify particular mice

	int				numbuttons;
	volatile int	buttons;

	volatile int	delta[2];
	int				pos[2];

} rawmouse_t;

rawmouse_t* rawmice;
RAWINPUT* raw;
int			ribuffersize;

static int	rawmicecount;

void		IN_DeRegisterRawMouse(void);
int			IN_RegisterRawMouse(void);

#endif // ~!DEDICATED


typedef struct {
	int oldButtonState;

	qboolean mouseActive;
	qboolean mouseInitialized;
} WinMouseVars_t;

static WinMouseVars_t s_wmv;

static int window_center_x, window_center_y;

//
// MIDI definitions
//
static void IN_StartupMIDI( void );
static void IN_ShutdownMIDI( void );

#define MAX_MIDIIN_DEVICES  8

typedef struct {
	int numDevices;
	MIDIINCAPS caps[MAX_MIDIIN_DEVICES];

	HMIDIIN hMidiIn;
} MidiInfo_t;

static MidiInfo_t s_midiInfo;

//
// Joystick definitions
//
#define JOY_MAX_AXES        6               // X, Y, Z, R, U, V

typedef struct {
	qboolean avail;
	int id;                 // joystick number
	JOYCAPS jc;

	int oldbuttonstate;
	int oldpovstate;

	JOYINFOEX ji;
} joystickInfo_t;

static joystickInfo_t joy;



cvar_t  *in_midi;
cvar_t  *in_midiport;
cvar_t  *in_midichannel;
cvar_t  *in_mididevice;

cvar_t  *in_mouse;
cvar_t  *in_joystick;
cvar_t  *in_joyBallScale;
cvar_t  *in_debugJoystick;
cvar_t  *joy_threshold;

qboolean in_appactive;

// forward-referenced functions
void IN_StartupJoystick( void );
void IN_JoyMove( void );

static void MidiInfo_f( void );

#ifndef DEDICATED
/*
=========================================================================

Raw Mouse Input

=========================================================================
*/

void IN_ShutdownRawMouse(void) {
	if (rawmicecount < 1)
		return;

	IN_DeRegisterRawMouse();

	Z_Free(rawmice);
	Z_Free(raw);

	rawmice = NULL;
	raw = NULL;

	rawmicecount = 0;
}


void IN_DeRegisterRawMouse(void)
{
	RAWINPUTDEVICE Rid;

	if (rawmicecount < 1)
		return;

	// deregister raw input
	Rid.usUsagePage = 0x01;
	Rid.usUsage = 0x02;
	Rid.dwFlags = RIDEV_REMOVE;
	Rid.hwndTarget = NULL;

	(*_RRID)(&Rid, 1, sizeof(Rid));
}

int IN_RawInput_IsRDPMouse(char* cDeviceString)
{
	char cRDPString[] = "\\??\\Root#RDP_MOU#";
	int i;

	if (strlen(cDeviceString) < strlen(cRDPString)) {
		return 0;
	}

	for (i = strlen(cRDPString) - 1; i >= 0; i--)
	{
		if (cDeviceString[i] != cRDPString[i])
			return 0;
	}

	return 1; // is RDP mouse
}

int IN_RegisterRawMouse(void) {
	// This function registers to receive the WM_INPUT messages
	RAWINPUTDEVICE Rid; // Register only for mouse messages from wm_input.  

	//register to get wm_input messages
	Rid.usUsagePage = 0x01;
	Rid.usUsage = 0x02;
	Rid.dwFlags = RIDEV_NOLEGACY; // adds HID mouse and also ignores legacy mouse messages
	Rid.hwndTarget = NULL;

	// Register to receive the WM_INPUT message for any change in mouse (buttons, wheel, and movement will all generate the same message)
	return (*_RRID)(&Rid, 1, sizeof(Rid));
}

void IN_DeactivateRawMouse(void)
{
	IN_DeRegisterRawMouse();
}

void IN_RawMouse(int* mx, int* my) {
	int x;

	*mx = *my = 0;

	for (x = 0; x < rawmicecount; x++)
	{
		*mx += rawmice[x].delta[0];
		*my += rawmice[x].delta[1];

		rawmice[x].delta[0] = rawmice[x].delta[1] = 0;
	}
}


void IN_ActivateRawMouse(void) {
	HMODULE kernel32;
	if (!IN_RegisterRawMouse())
	{
		Com_Printf("Raw input: unable to register raw input\n");
		kernel32 = LoadLibrary("kernel32.dll");
		if (kernel32)
		{
			_GLE = (pGetLastError)GetProcAddress(kernel32, "GetLastError");
			if (_GLE)
			{
				int errorCode = (*_GLE)();
				Com_Printf("Raw input: error code is %i\n", errorCode);
			}
			else {
				Com_Printf("Raw input: function GetLastError could not be registered\n");
				return;
			}
		}
		else {
			Com_Printf("Raw input: unable to load kernel32.dll\n");
			return;
		}
		IN_ShutdownRawMouse();
		Com_Printf("Falling back to Win32 mouse support...\n");
		Cvar_Set("in_mouse", "1");
	}
}

qboolean IN_InitRawMouse(void)
{
	// "0" to exclude, "1" to include
	PRAWINPUTDEVICELIST pRawInputDeviceList;
	int inputdevices, i, j, mtemp;
	char dname[MAX_RI_DEVICE_SIZE];

	// Return 0 if rawinput is not available
	HMODULE user32 = LoadLibrary("user32.dll");
	if (!user32)
	{
		Com_Printf("Raw input: unable to load user32.dll\n");
		return qfalse;
	}
	_RRID = (pRegisterRawInputDevices)GetProcAddress(user32, "RegisterRawInputDevices");
	if (!_RRID)
	{
		Com_Printf("Raw input: function RegisterRawInputDevices could not be registered\n");
		return qfalse;
	}
	_GRIDL = (pGetRawInputDeviceList)GetProcAddress(user32, "GetRawInputDeviceList");
	if (!_GRIDL)
	{
		Com_Printf("Raw input: function GetRawInputDeviceList could not be registered\n");
		return qfalse;
	}
	_GRIDIA = (pGetRawInputDeviceInfoA)GetProcAddress(user32, "GetRawInputDeviceInfoA");
	if (!_GRIDIA)
	{
		Com_Printf("Raw input: function GetRawInputDeviceInfoA could not be registered\n");
		return qfalse;
	}
	_GRID = (pGetRawInputData)GetProcAddress(user32, "GetRawInputData");
	if (!_GRID)
	{
		Com_Printf("Raw input: function GetRawInputData could not be registered\n");
		return qfalse;
	}

	// 1st call to GetRawInputDeviceList: Pass NULL to get the number of devices.
	if ((*_GRIDL)(NULL, &inputdevices, sizeof(RAWINPUTDEVICELIST)) != 0)
	{
		Com_Printf("Raw input: unable to count raw input devices\n");
		return qfalse;
	}

	// Allocate the array to hold the DeviceList
	pRawInputDeviceList = Z_Malloc(sizeof(RAWINPUTDEVICELIST) * inputdevices);

	// 2nd call to GetRawInputDeviceList: Pass the pointer to our DeviceList and GetRawInputDeviceList() will fill the array
	if ((*_GRIDL)(pRawInputDeviceList, &inputdevices, sizeof(RAWINPUTDEVICELIST)) == -1)
	{
		Com_Printf("Raw input: unable to get raw input device list\n");
		return qfalse;
	}

	// Loop through all devices and count the mice
	for (i = 0, mtemp = 0; i < inputdevices; i++)
	{
		if (pRawInputDeviceList[i].dwType == RIM_TYPEMOUSE)
		{
			j = MAX_RI_DEVICE_SIZE;

			// Get the device name and use it to determine if it's the RDP Terminal Services virtual device.
			if ((*_GRIDIA)(pRawInputDeviceList[i].hDevice, RIDI_DEVICENAME, dname, &j) < 0)
				dname[0] = 0;

			if (IN_RawInput_IsRDPMouse(dname)) // ignore rdp mouse
				continue;

			// advance temp device count
			mtemp++;
		}
	}

	// exit out if no devices found
	if (!mtemp)
	{
		Com_Printf("Raw input: no usable device found\n");
		return qfalse;
	}

	// Loop again and bind devices
	rawmice = Z_Malloc(sizeof(rawmouse_t) * mtemp);
	for (i = 0; i < inputdevices; i++)
	{
		if (pRawInputDeviceList[i].dwType == RIM_TYPEMOUSE)
		{
			j = MAX_RI_DEVICE_SIZE;

			// Get the device name and use it to determine if it's the RDP Terminal Services virtual device.
			if ((*_GRIDIA)(pRawInputDeviceList[i].hDevice, RIDI_DEVICENAME, dname, &j) < 0)
				dname[0] = 0;

			if (IN_RawInput_IsRDPMouse(dname)) // ignore rdp mouse
				continue;

			// print pretty message about the mouse
			dname[MAX_RI_DEVICE_SIZE - 1] = 0;
			for (mtemp = strlen(dname); mtemp >= 0; mtemp--)
			{
				if (dname[mtemp] == '#')
				{
					dname[mtemp + 1] = 0;
					break;
				}
			}
			Com_Printf("Raw input: [%i] %s\n", i, dname);

			// set handle
			rawmice[rawmicecount].rawinputhandle = pRawInputDeviceList[i].hDevice;
			rawmice[rawmicecount].numbuttons = 10;
			rawmice[rawmicecount].pos[0] = RI_INVALID_POS;
			rawmicecount++;
		}
	}


	// free the RAWINPUTDEVICELIST
	Z_Free(pRawInputDeviceList);

	// alloc raw input buffer
	raw = Z_Malloc(INIT_RIBUFFER_SIZE);
	ribuffersize = INIT_RIBUFFER_SIZE;

	Com_Printf("Raw input: initialized with %i mice\n", rawmicecount);

	return qtrue; // success
}

//================================
// raw input read functions
//================================

void IN_RawInput_MouseRead(HANDLE in_device_handle) {
	int i = 0, tbuttons, j;
	int dwSize;

	if (!raw || !rawmice || rawmicecount < 1)
		return; // no thx

	// get raw input
	if ((*_GRID)((HRAWINPUT)in_device_handle, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER)) == -1)
	{
		Com_Printf("Raw input: unable to add to get size of raw input header.\n");
		return;
	}

	if (dwSize > ribuffersize)
	{
		ribuffersize = dwSize;
		Z_Free(raw);
		raw = Z_Malloc(dwSize);
	}

	if ((*_GRID)((HRAWINPUT)in_device_handle, RID_INPUT, raw, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
		Com_Printf("Raw input: unable to add to get raw input header.\n");
		return;
	}

	// find mouse in our mouse list
	for (; i < rawmicecount; i++)
	{
		if (rawmice[i].rawinputhandle == raw->header.hDevice)
			break;
	}
	if (i == rawmicecount) // we're not tracking this mouse
		return;

	// movement
	if (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
	{
		if (rawmice[i].pos[0] != RI_INVALID_POS)
		{
			rawmice[i].delta[0] += raw->data.mouse.lLastX - rawmice[i].pos[0];
			rawmice[i].delta[1] += raw->data.mouse.lLastY - rawmice[i].pos[1];
		}
		rawmice[i].pos[0] = raw->data.mouse.lLastX;
		rawmice[i].pos[1] = raw->data.mouse.lLastY;
	}
	else // RELATIVE
	{
		rawmice[i].delta[0] += raw->data.mouse.lLastX;
		rawmice[i].delta[1] += raw->data.mouse.lLastY;
		rawmice[i].pos[0] = RI_INVALID_POS;
	}

	// sswolf - notice extra buttons
	// buttons
	if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN)
		Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MOUSE1, qtrue, 0, NULL);
	if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_UP)
		Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MOUSE1, qfalse, 0, NULL);
	if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_DOWN)
		Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MOUSE2, qtrue, 0, NULL);
	if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_UP)
		Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MOUSE2, qfalse, 0, NULL);
	if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_DOWN)
		Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MOUSE3, qtrue, 0, NULL);
	if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_UP)
		Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MOUSE3, qfalse, 0, NULL);
	if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN)
		Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MOUSE4, qtrue, 0, NULL);
	if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP)
		Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MOUSE4, qfalse, 0, NULL);
	if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN)
		Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MOUSE5, qtrue, 0, NULL);
	if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP)
		Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MOUSE5, qfalse, 0, NULL);


	// mouse wheel
	if (raw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
	{      // If the current message has a mouse_wheel message
		if ((SHORT)raw->data.mouse.usButtonData > 0)
		{
			Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL);
			Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL);
		}
		if ((SHORT)raw->data.mouse.usButtonData < 0)
		{
			Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL);
			Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL);
		}
	}

	// extra buttons
	tbuttons = raw->data.mouse.ulRawButtons & RI_RAWBUTTON_MASK;
	for (j = 6; j < rawmice[i].numbuttons; j++)
	{
		if ((tbuttons & (1 << j)) && !(rawmice[i].buttons & (1 << j)))
		{
			Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MOUSE1 + j, qtrue, 0, NULL);
		}
		if (!(tbuttons & (1 << j)) && (rawmice[i].buttons & (1 << j)))
		{
			Sys_QueEvent(g_wv.sysMsgTime, SE_KEY, K_MOUSE1 + j, qfalse, 0, NULL);
		}

	}

	rawmice[i].buttons &= ~RI_RAWBUTTON_MASK;
	rawmice[i].buttons |= tbuttons;
}
#endif // ~!DEDICATED

/*
============================================================

WIN32 MOUSE CONTROL

============================================================
*/

/*
================
IN_InitWin32Mouse
================
*/
void IN_InitWin32Mouse( void ) {
}

/*
================
IN_ShutdownWin32Mouse
================
*/
void IN_ShutdownWin32Mouse( void ) {
}

/*
================
IN_ActivateWin32Mouse
================
*/
void IN_ActivateWin32Mouse( void ) {
	int width, height;
	RECT window_rect;

	width = GetSystemMetrics( SM_CXSCREEN );
	height = GetSystemMetrics( SM_CYSCREEN );

	GetWindowRect( g_wv.hWnd, &window_rect );
	if ( window_rect.left < 0 ) {
		window_rect.left = 0;
	}
	if ( window_rect.top < 0 ) {
		window_rect.top = 0;
	}
	if ( window_rect.right >= width ) {
		window_rect.right = width - 1;
	}
	if ( window_rect.bottom >= height - 1 ) {
		window_rect.bottom = height - 1;
	}
	window_center_x = ( window_rect.right + window_rect.left ) / 2;
	window_center_y = ( window_rect.top + window_rect.bottom ) / 2;

	SetCursorPos( window_center_x, window_center_y );

	SetCapture( g_wv.hWnd );
	// NERVE - SMF - dont do this in developer mode
	if ( !com_developer->integer ) {
		ClipCursor( &window_rect );
	}
	while ( ShowCursor( FALSE ) >= 0 )
		;
}

/*
================
IN_DeactivateWin32Mouse
================
*/
void IN_DeactivateWin32Mouse( void ) {
	// NERVE - SMF - dont do this in developer mode
	if ( !com_developer->integer ) {
		ClipCursor( NULL );
	}
	ReleaseCapture();
	while ( ShowCursor( TRUE ) < 0 )
		;
}

/*
================
IN_Win32Mouse
================
*/
void IN_Win32Mouse( int *mx, int *my ) {
	POINT current_pos;

	// find mouse movement
	GetCursorPos( &current_pos );

	// force the mouse to the center, so there's room to move
	SetCursorPos( window_center_x, window_center_y );

	*mx = current_pos.x - window_center_x;
	*my = current_pos.y - window_center_y;
}

/*
============================================================

DIRECT INPUT MOUSE CONTROL

============================================================
*/
#ifndef DOOMSOUND   ///// (SA) DOOMSOUND
#undef DEFINE_GUID

#define DEFINE_GUID( name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8 ) \
	EXTERN_C const GUID name \
	= { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

DEFINE_GUID( GUID_SysMouse,   0x6F1D2B60,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00 );
DEFINE_GUID( GUID_XAxis,   0xA36D02E0,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00 );
DEFINE_GUID( GUID_YAxis,   0xA36D02E1,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00 );
DEFINE_GUID( GUID_ZAxis,   0xA36D02E2,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00 );


#define DINPUT_BUFFERSIZE           16
#define iDirectInputCreate( a,b,c,d ) pDirectInputCreate( a,b,c,d )

HRESULT ( WINAPI * pDirectInputCreate )( HINSTANCE hinst, DWORD dwVersion,
										 LPDIRECTINPUT * lplpDirectInput, LPUNKNOWN punkOuter );

#endif ///// (SA) DOOMSOUND

static HINSTANCE hInstDI;

typedef struct MYDATA {
	LONG lX;                    // X axis goes here
	LONG lY;                    // Y axis goes here
	LONG lZ;                    // Z axis goes here
	BYTE bButtonA;              // One button goes here
	BYTE bButtonB;              // Another button goes here
	BYTE bButtonC;              // Another button goes here
	BYTE bButtonD;              // Another button goes here
} MYDATA;

static DIOBJECTDATAFORMAT rgodf[] = {
	{ &GUID_XAxis,    FIELD_OFFSET( MYDATA, lX ),       DIDFT_AXIS | DIDFT_ANYINSTANCE,   0,},
	{ &GUID_YAxis,    FIELD_OFFSET( MYDATA, lY ),       DIDFT_AXIS | DIDFT_ANYINSTANCE,   0,},
	{ &GUID_ZAxis,    FIELD_OFFSET( MYDATA, lZ ),       0x80000000 | DIDFT_AXIS | DIDFT_ANYINSTANCE,   0,},
	{ 0,              FIELD_OFFSET( MYDATA, bButtonA ), DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
	{ 0,              FIELD_OFFSET( MYDATA, bButtonB ), DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
	{ 0,              FIELD_OFFSET( MYDATA, bButtonC ), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
	{ 0,              FIELD_OFFSET( MYDATA, bButtonD ), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
};

#define NUM_OBJECTS ( sizeof( rgodf ) / sizeof( rgodf[0] ) )

static DIDATAFORMAT df = {
	sizeof( DIDATAFORMAT ),       // this structure
	sizeof( DIOBJECTDATAFORMAT ), // size of object data format
	DIDF_RELAXIS,               // absolute axis coordinates
	sizeof( MYDATA ),             // device data size
	NUM_OBJECTS,                // number of objects
	rgodf,                      // and here they are
};

static LPDIRECTINPUT g_pdi;
static LPDIRECTINPUTDEVICE g_pMouse;

/*
============================================================

  MOUSE CONTROL

============================================================
*/

/*
===========
IN_ActivateMouse

Called when the window gains focus or changes in some way
===========
*/
void IN_ActivateMouse( void ) {
	if ( !s_wmv.mouseInitialized ) {
		return;
	}
	if ( !in_mouse->integer ) {
		s_wmv.mouseActive = qfalse;
		return;
	}
	if ( s_wmv.mouseActive ) {
		return;
	}

	s_wmv.mouseActive = qtrue;

#ifndef DEDICATED
	if (in_mouse->integer > 1) {
		IN_ActivateRawMouse();
	}
#endif	

	IN_ActivateWin32Mouse();
}


/*
===========
IN_DeactivateMouse

Called when the window loses focus
===========
*/
void IN_DeactivateMouse( void ) {
	if ( !s_wmv.mouseInitialized ) {
		return;
	}
	if ( !s_wmv.mouseActive ) {
		return;
	}
	s_wmv.mouseActive = qfalse;

#ifndef DEDICATED
	IN_DeactivateRawMouse();
#endif
	IN_DeactivateWin32Mouse();
}

/*
===========
IN_StartupMouse
===========
*/
void IN_StartupMouse( void ) {
	s_wmv.mouseInitialized = qfalse;

	if ( in_mouse->integer == 0 ) {
		Com_Printf( "Mouse control not active.\n" );
		return;
	}

	s_wmv.mouseInitialized = qtrue;
#ifndef  DEDICATED
	if (in_mouse->integer == 1) {
		IN_InitWin32Mouse();
}
	else if (in_mouse->integer > 1) {
		IN_InitRawMouse();
	}
#else
	IN_InitWin32Mouse();
#endif // ! DEDICATED
}

/*
===========
IN_MouseEvent
===========
*/
void IN_MouseEvent( int mstate ) {
	int i;

	if ( !s_wmv.mouseInitialized ) {
		return;
	}

// perform button actions
	for  ( i = 0 ; i < 3 ; i++ )
	{
		if ( ( mstate & ( 1 << i ) ) &&
			 !( s_wmv.oldButtonState & ( 1 << i ) ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE1 + i, qtrue, 0, NULL );
		}

		if ( !( mstate & ( 1 << i ) ) &&
			 ( s_wmv.oldButtonState & ( 1 << i ) ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE1 + i, qfalse, 0, NULL );
		}
	}

	s_wmv.oldButtonState = mstate;
}

/*
===========
IN_MouseMove
===========
*/
void IN_MouseMove( void ) {
	int mx, my;

#ifndef DEDICATED
	if (rawmicecount > 0) {
		IN_RawMouse(&mx, &my);
	}
	else {
		IN_Win32Mouse(&mx, &my);
	}
#else
	IN_Win32Mouse(&mx, &my);
#endif

	if ( !mx && !my ) {
		return;
	}

	Sys_QueEvent( 0, SE_MOUSE, mx, my, 0, NULL );
}


/*
=========================================================================

=========================================================================
*/

/*
===========
IN_Startup
===========
*/
void IN_Startup( void ) {

	// TODO: no joystick support yet...


//	Com_Printf ("\n------- Input Initialization -------\n");
	IN_StartupMouse();
	IN_StartupJoystick();
	IN_StartupMIDI();
//	Com_Printf ("------------------------------------\n");

	in_mouse->modified = qfalse;
	in_joystick->modified = qfalse;
}

/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown( void ) {
	IN_DeactivateMouse();
#ifndef DEDICATED
	IN_ShutdownRawMouse(); 
#endif
	IN_ShutdownMIDI();
	Cmd_RemoveCommand( "midiinfo" );
}


/*
===========
IN_Init
===========
*/
void IN_Init( void ) {
	// MIDI input controler variables
	in_midi                 = Cvar_Get( "in_midi",                   "0",     CVAR_ARCHIVE );
	in_midiport             = Cvar_Get( "in_midiport",               "1",     CVAR_ARCHIVE );
	in_midichannel          = Cvar_Get( "in_midichannel",            "1",     CVAR_ARCHIVE );
	in_mididevice           = Cvar_Get( "in_mididevice",         "0",     CVAR_ARCHIVE );

	Cmd_AddCommand( "midiinfo", MidiInfo_f );

	// mouse variables
	in_mouse                = Cvar_Get( "in_mouse",                  "1",     CVAR_ARCHIVE | CVAR_LATCH );

	// joystick variables
	in_joystick             = Cvar_Get( "in_joystick",               "0",     CVAR_ARCHIVE | CVAR_LATCH );
	in_joyBallScale         = Cvar_Get( "in_joyBallScale",           "0.02",      CVAR_ARCHIVE );
	in_debugJoystick        = Cvar_Get( "in_debugjoystick",          "0",     CVAR_TEMP );

	joy_threshold           = Cvar_Get( "joy_threshold",         "0.15",      CVAR_ARCHIVE );

	IN_Startup();
}


/*
===========
IN_Activate

Called when the main window gains or loses focus.
The window may have been destroyed and recreated
between a deactivate and an activate.
===========
*/
void IN_Activate( qboolean active ) {
	in_appactive = active;

	if ( !active ) {
		IN_DeactivateMouse();
	}
}


/*
==================
IN_Frame

Called every frame, even if not generating commands
==================
*/
void IN_Frame( void ) {
	// post joystick events
	IN_JoyMove();

	if ( !s_wmv.mouseInitialized ) {
		return;
	}

	if ( cls.keyCatchers & KEYCATCH_CONSOLE ) {
		// temporarily deactivate if not in the game and
		// running on the desktop
		// voodoo always counts as full screen
		if ( Cvar_VariableValue( "r_fullscreen" ) == 0
			 && strcmp( Cvar_VariableString( "r_glDriver" ), _3DFX_DRIVER_NAME ) ) {
			IN_DeactivateMouse();
			return;
		}
	}

	if ( !in_appactive ) {
		IN_DeactivateMouse();
		return;
	}

	IN_ActivateMouse();

	// post events to the system que
	IN_MouseMove();

}


/*
===================
IN_ClearStates
===================
*/
void IN_ClearStates( void ) {
	s_wmv.oldButtonState = 0;
}


/*
=========================================================================

JOYSTICK

=========================================================================
*/

/*
===============
IN_StartupJoystick
===============
*/
void IN_StartupJoystick( void ) {
	int numdevs;
	MMRESULT mmr;

	// assume no joystick
	joy.avail = qfalse;

	if ( !in_joystick->integer ) {
		Com_DPrintf( "Joystick is not active.\n" );
		return;
	}

	// verify joystick driver is present
	if ( ( numdevs = joyGetNumDevs() ) == 0 ) {
		Com_DPrintf( "joystick not found -- driver not present\n" );
		return;
	}

	// cycle through the joystick ids for the first valid one
	mmr = 0;
	for ( joy.id = 0 ; joy.id < numdevs ; joy.id++ )
	{
		memset( &joy.ji, 0, sizeof( joy.ji ) );
		joy.ji.dwSize = sizeof( joy.ji );
		joy.ji.dwFlags = JOY_RETURNCENTERED;

		if ( ( mmr = joyGetPosEx( joy.id, &joy.ji ) ) == JOYERR_NOERROR ) {
			break;
		}
	}

	// abort startup if we didn't find a valid joystick
	if ( mmr != JOYERR_NOERROR ) {
		Com_Printf( "joystick not found -- no valid joysticks (%x)\n", mmr );
		return;
	}

	// get the capabilities of the selected joystick
	// abort startup if command fails
	memset( &joy.jc, 0, sizeof( joy.jc ) );
	if ( ( mmr = joyGetDevCaps( joy.id, &joy.jc, sizeof( joy.jc ) ) ) != JOYERR_NOERROR ) {
		Com_Printf( "joystick not found -- invalid joystick capabilities (%x)\n", mmr );
		return;
	}

	Com_Printf( "Joystick found.\n" );
	Com_Printf( "Pname: %s\n", joy.jc.szPname );
	Com_Printf( "OemVxD: %s\n", joy.jc.szOEMVxD );
	Com_Printf( "RegKey: %s\n", joy.jc.szRegKey );

	Com_Printf( "Numbuttons: %i / %i\n", joy.jc.wNumButtons, joy.jc.wMaxButtons );
	Com_Printf( "Axis: %i / %i\n", joy.jc.wNumAxes, joy.jc.wMaxAxes );
	Com_Printf( "Caps: 0x%x\n", joy.jc.wCaps );
	if ( joy.jc.wCaps & JOYCAPS_HASPOV ) {
		Com_Printf( "HASPOV\n" );
	} else {
		Com_Printf( "no POV\n" );
	}

	// old button and POV states default to no buttons pressed
	joy.oldbuttonstate = 0;
	joy.oldpovstate = 0;

	// mark the joystick as available
	joy.avail = qtrue;
}

/*
===========
JoyToF
===========
*/
float JoyToF( int value ) {
	float fValue;

	// move centerpoint to zero
	value -= 32768;

	// convert range from -32768..32767 to -1..1
	fValue = (float)value / 32768.0;

	if ( fValue < -1 ) {
		fValue = -1;
	}
	if ( fValue > 1 ) {
		fValue = 1;
	}
	return fValue;
}

int JoyToI( int value ) {
	// move centerpoint to zero
	value -= 32768;

	return value;
}

int joyDirectionKeys[16] = {
	K_LEFTARROW, K_RIGHTARROW,
	K_UPARROW, K_DOWNARROW,
	K_JOY16, K_JOY17,
	K_JOY18, K_JOY19,
	K_JOY20, K_JOY21,
	K_JOY22, K_JOY23,

	K_JOY24, K_JOY25,
	K_JOY26, K_JOY27
};

/*
===========
IN_JoyMove
===========
*/
void IN_JoyMove( void ) {
	float fAxisValue;
	int i;
	DWORD buttonstate, povstate;
	int x, y;

	// verify joystick is available and that the user wants to use it
	if ( !joy.avail ) {
		return;
	}

	// collect the joystick data, if possible
	memset( &joy.ji, 0, sizeof( joy.ji ) );
	joy.ji.dwSize = sizeof( joy.ji );
	joy.ji.dwFlags = JOY_RETURNALL;

	if ( joyGetPosEx( joy.id, &joy.ji ) != JOYERR_NOERROR ) {
		// read error occurred
		// turning off the joystick seems too harsh for 1 read error,\
		//      // but what should be done?
		// Com_Printf ("IN_ReadJoystick: no response\n");
		// joy.avail = false;
		return;
	}

	if ( in_debugJoystick->integer ) {
		Com_Printf( "%8x %5i %5.2f %5.2f %5.2f %5.2f %6i %6i\n",
					joy.ji.dwButtons,
					joy.ji.dwPOV,
					JoyToF( joy.ji.dwXpos ), JoyToF( joy.ji.dwYpos ),
					JoyToF( joy.ji.dwZpos ), JoyToF( joy.ji.dwRpos ),
					JoyToI( joy.ji.dwUpos ), JoyToI( joy.ji.dwVpos ) );
	}

	// loop through the joystick buttons
	// key a joystick event or auxillary event for higher number buttons for each state change
	buttonstate = joy.ji.dwButtons;
	for ( i = 0 ; i < joy.jc.wNumButtons ; i++ ) {
		if ( ( buttonstate & ( 1 << i ) ) && !( joy.oldbuttonstate & ( 1 << i ) ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_JOY1 + i, qtrue, 0, NULL );
		}
		if ( !( buttonstate & ( 1 << i ) ) && ( joy.oldbuttonstate & ( 1 << i ) ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_JOY1 + i, qfalse, 0, NULL );
		}
	}
	joy.oldbuttonstate = buttonstate;

	povstate = 0;

	// convert main joystick motion into 6 direction button bits
	for ( i = 0; i < joy.jc.wNumAxes && i < 4 ; i++ ) {
		// get the floating point zero-centered, potentially-inverted data for the current axis
		fAxisValue = JoyToF( ( &joy.ji.dwXpos )[i] );

		if ( fAxisValue < -joy_threshold->value ) {
			povstate |= ( 1 << ( i * 2 ) );
		} else if ( fAxisValue > joy_threshold->value ) {
			povstate |= ( 1 << ( i * 2 + 1 ) );
		}
	}

	// convert POV information from a direction into 4 button bits
	if ( joy.jc.wCaps & JOYCAPS_HASPOV ) {
		if ( joy.ji.dwPOV != JOY_POVCENTERED ) {
			if ( joy.ji.dwPOV == JOY_POVFORWARD ) {
				povstate |= 1 << 12;
			}
			if ( joy.ji.dwPOV == JOY_POVBACKWARD ) {
				povstate |= 1 << 13;
			}
			if ( joy.ji.dwPOV == JOY_POVRIGHT ) {
				povstate |= 1 << 14;
			}
			if ( joy.ji.dwPOV == JOY_POVLEFT ) {
				povstate |= 1 << 15;
			}
		}
	}

	// determine which bits have changed and key an auxillary event for each change
	for ( i = 0 ; i < 16 ; i++ ) {
		if ( ( povstate & ( 1 << i ) ) && !( joy.oldpovstate & ( 1 << i ) ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, joyDirectionKeys[i], qtrue, 0, NULL );
		}

		if ( !( povstate & ( 1 << i ) ) && ( joy.oldpovstate & ( 1 << i ) ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, joyDirectionKeys[i], qfalse, 0, NULL );
		}
	}
	joy.oldpovstate = povstate;

	// if there is a trackball like interface, simulate mouse moves
	if ( joy.jc.wNumAxes >= 6 ) {
		x = JoyToI( joy.ji.dwUpos ) * in_joyBallScale->value;
		y = JoyToI( joy.ji.dwVpos ) * in_joyBallScale->value;
		if ( x || y ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE, x, y, 0, NULL );
		}
	}
}

/*
=========================================================================

MIDI

=========================================================================
*/

static void MIDI_NoteOff( int note ) {
	int qkey;

	qkey = note - 60 + K_AUX1;

	if ( qkey > 255 || qkey < K_AUX1 ) {
		return;
	}

	Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, qkey, qfalse, 0, NULL );
}

static void MIDI_NoteOn( int note, int velocity ) {
	int qkey;

	if ( velocity == 0 ) {
		MIDI_NoteOff( note );
	}

	qkey = note - 60 + K_AUX1;

	if ( qkey > 255 || qkey < K_AUX1 ) {
		return;
	}

	Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, qkey, qtrue, 0, NULL );
}

static void CALLBACK MidiInProc( HMIDIIN hMidiIn, UINT uMsg, DWORD dwInstance,
								 DWORD dwParam1, DWORD dwParam2 ) {
	int message;

	switch ( uMsg )
	{
	case MIM_OPEN:
		break;
	case MIM_CLOSE:
		break;
	case MIM_DATA:
		message = dwParam1 & 0xff;

		// note on
		if ( ( message & 0xf0 ) == 0x90 ) {
			if ( ( ( message & 0x0f ) + 1 ) == in_midichannel->integer ) {
				MIDI_NoteOn( ( dwParam1 & 0xff00 ) >> 8, ( dwParam1 & 0xff0000 ) >> 16 );
			}
		} else if ( ( message & 0xf0 ) == 0x80 )   {
			if ( ( ( message & 0x0f ) + 1 ) == in_midichannel->integer ) {
				MIDI_NoteOff( ( dwParam1 & 0xff00 ) >> 8 );
			}
		}
		break;
	case MIM_LONGDATA:
		break;
	case MIM_ERROR:
		break;
	case MIM_LONGERROR:
		break;
	}

//	Sys_QueEvent( sys_msg_time, SE_KEY, wMsg, qtrue, 0, NULL );
}

static void MidiInfo_f( void ) {
	int i;

	const char *enableStrings[] = { "disabled", "enabled" };

	Com_Printf( "\nMIDI control:       %s\n", enableStrings[in_midi->integer != 0] );
	Com_Printf( "port:               %d\n", in_midiport->integer );
	Com_Printf( "channel:            %d\n", in_midichannel->integer );
	Com_Printf( "current device:     %d\n", in_mididevice->integer );
	Com_Printf( "number of devices:  %d\n", s_midiInfo.numDevices );
	for ( i = 0; i < s_midiInfo.numDevices; i++ )
	{
		if ( i == Cvar_VariableValue( "in_mididevice" ) ) {
			Com_Printf( "***" );
		} else {
			Com_Printf( "..." );
		}
		Com_Printf(    "device %2d:       %s\n", i, s_midiInfo.caps[i].szPname );
		Com_Printf( "...manufacturer ID: 0x%hx\n", s_midiInfo.caps[i].wMid );
		Com_Printf( "...product ID:      0x%hx\n", s_midiInfo.caps[i].wPid );

		Com_Printf( "\n" );
	}
}

static void IN_StartupMIDI( void ) {
	int i;

	if ( !Cvar_VariableValue( "in_midi" ) ) {
		return;
	}

	//
	// enumerate MIDI IN devices
	//
	s_midiInfo.numDevices = midiInGetNumDevs();

	for ( i = 0; i < s_midiInfo.numDevices; i++ )
	{
		midiInGetDevCaps( i, &s_midiInfo.caps[i], sizeof( s_midiInfo.caps[i] ) );
	}

	//
	// open the MIDI IN port
	//
	if ( midiInOpen( &s_midiInfo.hMidiIn,
					 in_mididevice->integer,
					 ( unsigned long ) MidiInProc,
					 ( unsigned long ) NULL,
					 CALLBACK_FUNCTION ) != MMSYSERR_NOERROR ) {
		Com_Printf( "WARNING: could not open MIDI device %d: '%s'\n", in_mididevice->integer, s_midiInfo.caps[( int ) in_mididevice->value] );
		return;
	}

	midiInStart( s_midiInfo.hMidiIn );
}

static void IN_ShutdownMIDI( void ) {
	if ( s_midiInfo.hMidiIn ) {
		midiInClose( s_midiInfo.hMidiIn );
	}
	memset( &s_midiInfo, 0, sizeof( s_midiInfo ) );
}
