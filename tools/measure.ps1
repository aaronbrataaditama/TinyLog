param([string]$ProcessName = 'TinyLog', [string]$Label = '')
# Prints memory/handle metrics for a running TinyLog (Spec 9.3 / 12.6). Read-only.
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class TLGui {
  [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr hProcess, uint flags);
}
'@
$p = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Error "$ProcessName is not running"; exit 1 }
$gdi  = [TLGui]::GetGuiResources($p.Handle, 0)
$user = [TLGui]::GetGuiResources($p.Handle, 1)
$pws = (Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$($p.Id)" -ErrorAction SilentlyContinue).WorkingSetPrivate
'{0,-28} PrivWS={1,7:N0} KB  WS={2,7:N0} KB  Private={3,7:N0} KB  PeakWS={4,7:N0} KB  GDI={5,4}  USER={6,4}  Threads={7,3}  Handles={8,4}' -f `
  $Label, ($pws/1KB), ($p.WorkingSet64/1KB), ($p.PrivateMemorySize64/1KB), ($p.PeakWorkingSet64/1KB), $gdi, $user, $p.Threads.Count, $p.HandleCount
