param(
    [string]$Command = 'mp_status',
    [int]$ProcessId = 0
)

$src = @"
using System;
using System.Runtime.InteropServices;

public class ConInject {
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool AttachConsole(uint pid);
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool FreeConsole();
    [DllImport("kernel32.dll", SetLastError = true)] public static extern IntPtr CreateFileA(
        string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr templ);
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool WriteConsoleInputA(
        IntPtr h, INPUT_RECORD[] recs, uint count, out uint written);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);

    [StructLayout(LayoutKind.Explicit)]
    public struct INPUT_RECORD {
        [FieldOffset(0)] public ushort EventType;
        [FieldOffset(4)] public KEY_EVENT_RECORD KeyEvent;
    }

    [StructLayout(LayoutKind.Explicit, CharSet = CharSet.Unicode)]
    public struct KEY_EVENT_RECORD {
        [FieldOffset(0)] public int bKeyDown;
        [FieldOffset(4)] public ushort wRepeatCount;
        [FieldOffset(6)] public ushort wVirtualKeyCode;
        [FieldOffset(8)] public ushort wVirtualScanCode;
        [FieldOffset(10)] public char UnicodeChar;
        [FieldOffset(12)] public uint dwControlKeyState;
    }

    const uint GENERIC_WRITE = 0x40000000;
    const uint GENERIC_READ = 0x80000000;
    const uint FILE_SHARE_READ = 1;
    const uint FILE_SHARE_WRITE = 2;
    const uint OPEN_EXISTING = 3;

    public static string Send(uint pid, string text) {
        FreeConsole();
        if (!AttachConsole(pid)) {
            return "AttachConsole failed: " + Marshal.GetLastWin32Error();
        }

        IntPtr h = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);

        if (h == IntPtr.Zero || h.ToInt64() == -1) {
            FreeConsole();
            return "CreateFile CONIN$ failed: " + Marshal.GetLastWin32Error();
        }

        var records = new System.Collections.Generic.List<INPUT_RECORD>();

        foreach (char ch in text) {
            records.Add(MakeKey(ch, true));
            records.Add(MakeKey(ch, false));
        }

        // Enter
        records.Add(MakeKey('\r', true));
        records.Add(MakeKey('\r', false));

        uint written = 0;
        bool ok = WriteConsoleInputA(h, records.ToArray(), (uint)records.Count, out written);

        CloseHandle(h);
        FreeConsole();

        return ok ? "sent " + written + " events" : "WriteConsoleInput failed: " + Marshal.GetLastWin32Error();
    }

    static INPUT_RECORD MakeKey(char ch, bool down) {
        var rec = new INPUT_RECORD();
        rec.EventType = 0x0001; // KEY_EVENT
        rec.KeyEvent.bKeyDown = down ? 1 : 0;
        rec.KeyEvent.wRepeatCount = 1;
        rec.KeyEvent.wVirtualKeyCode = 0;
        rec.KeyEvent.wVirtualScanCode = 0;
        rec.KeyEvent.UnicodeChar = ch;
        rec.KeyEvent.dwControlKeyState = 0;
        return rec;
    }
}
"@

Add-Type -TypeDefinition $src -Language CSharp

if ($ProcessId -eq 0) {
    $ProcessId = (Get-Process -Name SporeApp -ErrorAction SilentlyContinue | Select-Object -First 1).Id
}

if (-not $ProcessId) {
    Write-Host 'SporeApp is not running'
    exit 1
}

Write-Host ([ConInject]::Send([uint32]$ProcessId, $Command))
