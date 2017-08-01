/* ========================================================================
$File: $
$Date: $
$Revision: $
$Creator: Casey Muratori $
$Notice: (C) Copyright 2014 by Molly Rocket, Inc. All Rights Reserved. $
======================================================================== */

#include <windows.h>
#include <stdint.h>
#include <xinput.h>
#include <dsound.h>

#define internal static 
#define local_persist static 
#define global_variable static

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef int32 bool32;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

struct win32_offscreen_buffer
{
    // NOTE(casey): Pixels are alwasy 32-bits wide, Memory Order BB GG RR XX
    BITMAPINFO Info;
    void *Memory;
    int Width;
    int Height;
    int Pitch;
};

struct win32_window_dimension
{
    int Width;
    int Height;
};

// TODO(casey): This is a global for now.
global_variable bool GlobalRunning;
global_variable win32_offscreen_buffer GlobalBackbuffer;
global_variable LPDIRECTSOUNDBUFFER GlobalSecondaryBuffer;

// NOTE(casey): XInputGetState

//DEF: This is whole thing is some way of setting up a way to call XInputGetState on an aliased method or something.  I really dont understand
//but its some way to protect against having different methods to track based on the XINPUT library that is loaded I think?  Need to look into this more.
#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub)
{
    return(ERROR_DEVICE_NOT_CONNECTED);
}
global_variable x_input_get_state *XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_

// NOTE(casey): XInputSetState
#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub)
{
    return(ERROR_DEVICE_NOT_CONNECTED);
}
global_variable x_input_set_state *XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_

#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

internal void
Win32LoadXInput(void)
{
    // TODO(casey): Test this on Windows 8
    HMODULE XInputLibrary = LoadLibraryA("xinput1_4.dll");
    if (!XInputLibrary)
    {
        // TODO(casey): Diagnostic
        XInputLibrary = LoadLibraryA("xinput1_3.dll");
    }

    if (XInputLibrary)
    {
        XInputGetState = (x_input_get_state *)GetProcAddress(XInputLibrary, "XInputGetState");
        if (!XInputGetState) { XInputGetState = XInputGetStateStub; }

        XInputSetState = (x_input_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
        if (!XInputSetState) { XInputSetState = XInputSetStateStub; }

        // TODO(casey): Diagnostic

    }
    else
    {
        // TODO(casey): Diagnostic
    }
}

internal void
Win32InitDSound(HWND Window, int32 SamplesPerSecond, int32 BufferSize)
{
    // NOTE(casey): Load the library
    HMODULE DSoundLibrary = LoadLibraryA("dsound.dll");
    if (DSoundLibrary)
    {
        // NOTE(casey): Get a DirectSound object! - cooperative
        direct_sound_create *DirectSoundCreate = (direct_sound_create *)
            GetProcAddress(DSoundLibrary, "DirectSoundCreate");

        // TODO(casey): Double-check that this works on XP - DirectSound8 or 7??
        LPDIRECTSOUND DirectSound;
        if (DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &DirectSound, 0)))
        {
            WAVEFORMATEX WaveFormat = {};
            WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
            WaveFormat.nChannels = 2;
            WaveFormat.nSamplesPerSec = SamplesPerSecond;
            WaveFormat.wBitsPerSample = 16;
            WaveFormat.nBlockAlign = (WaveFormat.nChannels*WaveFormat.wBitsPerSample) / 8;
            WaveFormat.nAvgBytesPerSec = WaveFormat.nSamplesPerSec*WaveFormat.nBlockAlign;
            WaveFormat.cbSize = 0;

            if (SUCCEEDED(DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY)))
            {
                //DEF: All of this stuff with the primary buffer is about setting the buffer format.  To do that we have to jump through hoops to get
                //The buffer back by calling CreateSoundBuffer and if that works we set the format and then we dont care about it anymore.
                //Still confused by this but I guess its just one of those things you have to know.
                DSBUFFERDESC BufferDescription = {};
                BufferDescription.dwSize = sizeof(BufferDescription);
                BufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;

                // NOTE(casey): "Create" a primary buffer
                // TODO(casey): DSBCAPS_GLOBALFOCUS?
                LPDIRECTSOUNDBUFFER PrimaryBuffer;
                if (SUCCEEDED(DirectSound->CreateSoundBuffer(&BufferDescription, &PrimaryBuffer, 0)))
                {
                    HRESULT Error = PrimaryBuffer->SetFormat(&WaveFormat);
                    if (SUCCEEDED(Error))
                    {
                        // NOTE(casey): We have finally set the format!
                        OutputDebugStringA("Primary buffer format was set.\n");
                    }
                    else
                    {
                        // TODO(casey): Diagnostic
                    }
                }
                else
                {
                    // TODO(casey): Diagnostic
                }
            }
            else
            {
                // TODO(casey): Diagnostic
            }

            // TODO(casey): DSBCAPS_GETCURRENTPOSITION2
            DSBUFFERDESC BufferDescription = {};
            BufferDescription.dwSize = sizeof(BufferDescription);
            BufferDescription.dwFlags = 0;
            BufferDescription.dwBufferBytes = BufferSize;
            BufferDescription.lpwfxFormat = &WaveFormat;
            //DEF: Secondary buffer is the one we care about and will actually be using and writing to.  We apparently cant write to the primary buffer
            //at all.
            HRESULT Error = DirectSound->CreateSoundBuffer(&BufferDescription, &GlobalSecondaryBuffer, 0);
            if (SUCCEEDED(Error))
            {
                OutputDebugStringA("Secondary buffer created successfully.\n");
            }
        }
        else
        {
            // TODO(casey): Diagnostic
        }
    }
    else
    {
        // TODO(casey): Diagnostic
    }
}

internal win32_window_dimension
Win32GetWindowDimension(HWND Window)
{
    win32_window_dimension Result;

    RECT ClientRect;
    GetClientRect(Window, &ClientRect);
    Result.Width = ClientRect.right - ClientRect.left;
    Result.Height = ClientRect.bottom - ClientRect.top;

    return(Result);
}

internal void
RenderWeirdGradient(win32_offscreen_buffer *Buffer, int BlueOffset, int GreenOffset)
{
    // TODO(casey): Let's see what the optimizer does

    uint8 *Row = (uint8 *)Buffer->Memory;
    for (int Y = 0;
        Y < Buffer->Height;
        ++Y)
    {
        uint32 *Pixel = (uint32 *)Row;
        for (int X = 0;
            X < Buffer->Width;
            ++X)
        {
            uint8 Blue = (X + BlueOffset);
            uint8 Green = (Y + GreenOffset);
            //DEF: Still need to understand this.  Something about bit shifting left 8 and oring with the Blue bit to get the color.
            *Pixel++ = ((Green << 8) | Blue);
        }

        Row += Buffer->Pitch;
    }
}

internal void
Win32ResizeDIBSection(win32_offscreen_buffer *Buffer, int Width, int Height)
{
    // TODO(casey): Bulletproof this.
    // Maybe don't free first, free after, then free first if that fails.

    if (Buffer->Memory)
    {
        VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }

    Buffer->Width = Width;
    Buffer->Height = Height;

    int BytesPerPixel = 4;

    // NOTE(casey): When the biHeight field is negative, this is the clue to
    // Windows to treat this bitmap as top-down, not bottom-up, meaning that
    // the first three bytes of the image are the color for the top left pixel
    // in the bitmap, not the bottom left!
    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height;
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    // NOTE(casey): Thank you to Chris Hecker of Spy Party fame
    // for clarifying the deal with StretchDIBits and BitBlt!
    // No more DC for us.
    int BitmapMemorySize = (Buffer->Width*Buffer->Height)*BytesPerPixel;
    Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Buffer->Pitch = Width*BytesPerPixel;

    // TODO(casey): Probably clear this to black
}

internal void
Win32DisplayBufferInWindow(win32_offscreen_buffer *Buffer,
    HDC DeviceContext, int WindowWidth, int WindowHeight)
{
    // TODO(casey): Aspect ratio correction
    // TODO(casey): Play with stretch modes
    StretchDIBits(DeviceContext,
        /*
        X, Y, Width, Height,
        X, Y, Width, Height,
        */
        0, 0, WindowWidth, WindowHeight,
        0, 0, Buffer->Width, Buffer->Height,
        Buffer->Memory,
        &Buffer->Info,
        DIB_RGB_COLORS, SRCCOPY);
}

internal LRESULT CALLBACK
Win32MainWindowCallback(HWND Window,
    UINT Message,
    WPARAM WParam,
    LPARAM LParam)
{
    LRESULT Result = 0;

    switch (Message)
    {
    case WM_CLOSE:
    {
        // TODO(casey): Handle this with a message to the user?
        GlobalRunning = false;
    } break;

    case WM_ACTIVATEAPP:
    {
        OutputDebugStringA("WM_ACTIVATEAPP\n");
    } break;

    case WM_DESTROY:
    {
        // TODO(casey): Handle this as an error - recreate window?
        GlobalRunning = false;
    } break;

    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    {
        uint32 VKCode = WParam;
        //DEF: This is about checking the bit that is specified in the docs for LParam around if its indicating WasDown or IsDown and Casey likes to 
        //explicitly turn these into 0/1 for the bool so he checks if the specific bit is set or not I think. https://msdn.microsoft.com/en-us/library/windows/desktop/ms646281(v=vs.85).aspx
        bool WasDown = ((LParam & (1 << 30)) != 0);
        bool IsDown = ((LParam & (1 << 31)) == 0);
        if (WasDown != IsDown)
        {
            if (VKCode == 'W')
            {
            }
            else if (VKCode == 'A')
            {
            }
            else if (VKCode == 'S')
            {
            }
            else if (VKCode == 'D')
            {
            }
            else if (VKCode == 'Q')
            {
            }
            else if (VKCode == 'E')
            {
            }
            else if (VKCode == VK_UP)
            {
            }
            else if (VKCode == VK_LEFT)
            {
            }
            else if (VKCode == VK_DOWN)
            {
            }
            else if (VKCode == VK_RIGHT)
            {
            }
            else if (VKCode == VK_ESCAPE)
            {
                OutputDebugStringA("ESCAPE: ");
                if (IsDown)
                {
                    OutputDebugStringA("IsDown ");
                }
                if (WasDown)
                {
                    OutputDebugStringA("WasDown");
                }
                OutputDebugStringA("\n");
            }
            else if (VKCode == VK_SPACE)
            {
            }
        }

        bool32 AltKeyWasDown = (LParam & (1 << 29));
        if ((VKCode == VK_F4) && AltKeyWasDown)
        {
            GlobalRunning = false;
        }
    } break;

    case WM_PAINT:
    {
        PAINTSTRUCT Paint;
        HDC DeviceContext = BeginPaint(Window, &Paint);
        win32_window_dimension Dimension = Win32GetWindowDimension(Window);
        Win32DisplayBufferInWindow(&GlobalBackbuffer, DeviceContext,
            Dimension.Width, Dimension.Height);
        EndPaint(Window, &Paint);
    } break;

    default:
    {
        //            OutputDebugStringA("default\n");
        Result = DefWindowProc(Window, Message, WParam, LParam);
    } break;
    }

    return(Result);
}

int CALLBACK
WinMain(HINSTANCE Instance,
    HINSTANCE PrevInstance,
    LPSTR CommandLine,
    int ShowCode)
{
    Win32LoadXInput();

    //DEF: Initialize our WindowClass with everything at 0;
    WNDCLASSA WindowClass = {};

    Win32ResizeDIBSection(&GlobalBackbuffer, 1280, 720);

    WindowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    WindowClass.lpfnWndProc = Win32MainWindowCallback;
    WindowClass.hInstance = Instance;
    //    WindowClass.hIcon;
    WindowClass.lpszClassName = "HandmadeHeroWindowClass";

    if (RegisterClassA(&WindowClass))
    {
        HWND Window =
            CreateWindowExA(
                0,
                WindowClass.lpszClassName,
                "Handmade Hero",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                0,
                0,
                Instance,
                0);
        if (Window)
        {
            // NOTE(casey): Since we specified CS_OWNDC, we can just
            // get one device context and use it forever because we
            // are not sharing it with anyone.
            HDC DeviceContext = GetDC(Window);

            // NOTE(casey): Graphics test
            int XOffset = 0;
            int YOffset = 0;

            // NOTE(casey): Sound test
            int SamplesPerSecond = 48000;
            //DEF: This was chosen because it is the closest multiple of 2 to the middle c freq which is something like 261.xxx
            int ToneHz = 256;
            //DEF: How loud.  16000 is really loud.
            int16 ToneVolume = 3000;
            //DEF: Where are we currently playing within our cycle.  0 to 4800 in this case?
            uint32 RunningSampleIndex = 0;
            //DEF: We are doign a square wave which is ___---___---___--- as opposed to \_/^\_/.  So this is how long each single cycle of ___--- is.
            //Which says for each full 48000 sample cycle how many ToneHz cycles can we fit in.
            int SquareWavePeriod = SamplesPerSecond / ToneHz;
            //DEF: we are computing here how long each part of the square wave is.  So how long is the ___ and how long is the --- which is the samples
            //full square wave period ___--- divided by 2 giving us the size of each ___ and --- individually so we know how long to play high and 
            //how long to play low.
            int HalfSquareWavePeriod = SquareWavePeriod / 2;
            //DEF: Bytes per sample is 16 * 2 because you write samples in pairs of LEFT RIGHT LEFT RIGHT each of which (left or right) is 16
            int BytesPerSample = sizeof(int16) * 2;
            //DEF: This is the full size of the buffer because we are saying that 48hz (not sure why this was chosen, apparently its a standard)
            //is 48000 cycles per second and we are going to play a total of 4 bytes (2 int16s, one for right and one for left channel).
            int SecondaryBufferSize = SamplesPerSecond*BytesPerSample;

            Win32InitDSound(Window, SamplesPerSecond, SecondaryBufferSize);
            bool32 SoundIsPlaying = false;

            GlobalRunning = true;
            while (GlobalRunning)
            {
                MSG Message;

                while (PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
                {
                    if (Message.message == WM_QUIT)
                    {
                        GlobalRunning = false;
                    }

                    TranslateMessage(&Message);
                    DispatchMessageA(&Message);
                }

                // TODO(casey): Should we poll this more frequently
                for (DWORD ControllerIndex = 0;
                    ControllerIndex < XUSER_MAX_COUNT;
                    ++ControllerIndex)
                {
                    XINPUT_STATE ControllerState;
                    if (XInputGetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS)
                    {
                        // NOTE(casey): This controller is plugged in
                        // TODO(casey): See if ControllerState.dwPacketNumber increments too rapidly
                        XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;

                        bool Up = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
                        bool Down = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
                        bool Left = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
                        bool Right = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
                        bool Start = (Pad->wButtons & XINPUT_GAMEPAD_START);
                        bool Back = (Pad->wButtons & XINPUT_GAMEPAD_BACK);
                        bool LeftShoulder = (Pad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
                        bool RightShoulder = (Pad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
                        bool AButton = (Pad->wButtons & XINPUT_GAMEPAD_A);
                        bool BButton = (Pad->wButtons & XINPUT_GAMEPAD_B);
                        bool XButton = (Pad->wButtons & XINPUT_GAMEPAD_X);
                        bool YButton = (Pad->wButtons & XINPUT_GAMEPAD_Y);

                        int16 StickX = Pad->sThumbLX;
                        int16 StickY = Pad->sThumbLY;

                        XOffset += StickX >> 12;
                        YOffset += StickY >> 12;
                    }
                    else
                    {
                        // NOTE(casey): The controller is not available
                    }
                }

                RenderWeirdGradient(&GlobalBackbuffer, XOffset, YOffset);

                // NOTE(casey): DirectSound output test
                //DEF: The point where the sound is playing.
                DWORD PlayCursor;
                //TDEF: he point where we can start writing sound.
                DWORD WriteCursor;
                //DEF: Try and fill the cursors from our buffer.
                if (SUCCEEDED(GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor)))
                {
                    //DEF: Keep in mind as you read this part that what we are trying to accomplish is the playing of something long 
                    //(like 10 seconds or whatever) inside a shorter buffer (like 2 seconds).  So the point is to try and start it playing
                    //and then as it plays we are going to write the freed up space for it to keep playing as it goes.  That will help to 
                    //understand what some of the code and notes I have written are doing.  Check notes for a diagram.


                    //DEF: Find the byte we want to start locking at.  I think we are going to try and align it with our samples per second size?
                    //So this is index * 4 in our current case (sizeof int16 is 4 bytes or 32 bits) and then we check for the remainder and
                    //that is our byte to lock?  Still not totally sure how this works but eventually the math tells us that we need to have two
                    //regions we are writing to.  The region from the ByteToLock to the end of the buffer and the region from the beginning of
                    //the buffer to the end of the bytes we want to write.
                    DWORD ByteToLock = RunningSampleIndex*BytesPerSample % SecondaryBufferSize;
                    //DEF: How many bytes we are goign to write.
                    DWORD BytesToWrite;
                    //If the byte to lock is the same as the cursor then we are writing the entire buffer I think.
                    if (ByteToLock == PlayCursor)
                    {
                        BytesToWrite = SecondaryBufferSize;
                    }
                    //DEF: Once we are going to start locking behind the play cursor then we will have two regions to deal with.  So we first take the 
                    //buffer size and subtract the byte where we are going to start locking and then we add the bytes from the beginning to the play
                    //cursor.
                    else if (ByteToLock > PlayCursor)
                    {
                        BytesToWrite = (SecondaryBufferSize - ByteToLock);
                        BytesToWrite += PlayCursor;
                    }
                    //DEF: Otherwise we are just going to go from the PlayCursor to the byte to lock.
                    //Might need to re look at this section.
                    else
                    {
                        BytesToWrite = PlayCursor - ByteToLock;
                    }

                    // TODO(casey): More strenuous test!
                    // TODO(casey): Switch to a sine wave
                    VOID *Region1;
                    DWORD Region1Size;
                    VOID *Region2;
                    DWORD Region2Size;
                    //DEF: We ask to lock at the given byte for the specified size of write and the lock function will give us back the regions and the
                    //regions sizes we need to write to.
                    if (SUCCEEDED(GlobalSecondaryBuffer->Lock(ByteToLock, BytesToWrite,
                        &Region1, &Region1Size,
                        &Region2, &Region2Size,
                        0)))
                    {
                        // TODO(casey): assert that Region1Size/Region2Size is valid

                        // TODO(casey): Collapse these two loops
                        //DEF: How many samples can we get into region 1?
                        DWORD Region1SampleCount = Region1Size / BytesPerSample;
                        //DEF: We are dereferencing Region1 as an int16 (casting a pointer?).
                        int16 *SampleOut = (int16 *)Region1;
                        for (DWORD SampleIndex = 0;
                            SampleIndex < Region1SampleCount;
                            ++SampleIndex)
                        {
                            //DEF: Are we in the upper half of the square wave or are we in the lower half?  Play whichever tone (high, or low).
                            //We find this out by taking the running sample index and dividing it by half of our wave and seeing if its 0 or 1.
                            int16 SampleValue = ((RunningSampleIndex++ / HalfSquareWavePeriod) % 2) ? ToneVolume : -ToneVolume;
                            //I am confused by this part.  Not sure what this is doing or why we would do it twice.
                            *SampleOut++ = SampleValue;
                            *SampleOut++ = SampleValue;
                        }

                        //DEF: All the same stuff if we have a second region.
                        DWORD Region2SampleCount = Region2Size / BytesPerSample;
                        SampleOut = (int16 *)Region2;
                        for (DWORD SampleIndex = 0;
                            SampleIndex < Region2SampleCount;
                            ++SampleIndex)
                        {
                            int16 SampleValue = ((RunningSampleIndex++ / HalfSquareWavePeriod) % 2) ? ToneVolume : -ToneVolume;
                            *SampleOut++ = SampleValue;
                            *SampleOut++ = SampleValue;
                        }

                        //DEF: Unlock the buffer so it can play it when it gets there.
                        GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
                    }
                }

                if (!SoundIsPlaying)
                {
                    GlobalSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
                    SoundIsPlaying = true;
                }

                win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                Win32DisplayBufferInWindow(&GlobalBackbuffer, DeviceContext,
                    Dimension.Width, Dimension.Height);
            }
        }
        else
        {
            // TODO(casey): Logging
        }
    }
    else
    {
        // TODO(casey): Logging
    }

    return(0);
}
