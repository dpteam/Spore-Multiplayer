param(
    [int]$X = 400,
    [int]$Y = 269
)

$src = @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class RealClick {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, IntPtr extra);

    public struct RECT { public int L, T, R, B; }

    const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
    const uint MOUSEEVENTF_LEFTUP = 0x0004;

    public static string Click(int relX, int relY) {
        IntPtr target = IntPtr.Zero;
        EnumWindows((h, l) => {
            var c = new StringBuilder(128);
            GetClassNameW(h, c, 128);
            if (c.ToString() == "Canvas") { target = h; return false; }
            return true;
        }, IntPtr.Zero);

        if (target == IntPtr.Zero) return "Canvas not found";

        SetForegroundWindow(target);
        System.Threading.Thread.Sleep(400);

        RECT r;
        GetWindowRect(target, out r);

        int absX = r.L + relX;
        int absY = r.T + relY;

        SetCursorPos(absX, absY);
        System.Threading.Thread.Sleep(250);

        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, IntPtr.Zero);
        System.Threading.Thread.Sleep(80);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, IntPtr.Zero);

        return "clicked at screen " + absX + "," + absY;
    }
}
"@
Add-Type -TypeDefinition $src -Language CSharp

[RealClick]::Click($X, $Y)
