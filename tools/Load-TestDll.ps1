param(
    [Parameter(Mandatory=$true)][ValidateSet('Steam','Epic')][string]$Edition,
    [Parameter(Mandatory=$true)][string]$DllPath,
    [Parameter(Mandatory=$true)][string]$GameExePath
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$resolvedDll = (Resolve-Path -LiteralPath $DllPath).Path
$moduleName = [IO.Path]::GetFileName($resolvedDll)
$isRefreshProbe = $moduleName -in @('BAKLightRefreshProbe.dll','BAKLightRefreshCleanup.dll') -and
    $resolvedDll.StartsWith($projectRoot + '\experiments\light-refresh-probe\', [StringComparison]::OrdinalIgnoreCase)
$isEditorTrace = $moduleName -eq 'BAKEditorTrace.dll' -and
    $resolvedDll.StartsWith($projectRoot + '\experiments\editor-trace\', [StringComparison]::OrdinalIgnoreCase)
$isPoseProbe = $moduleName -eq 'BAKPoseProbeLauncher.dll' -and
    $resolvedDll.StartsWith($projectRoot + '\experiments\pose-probe\', [StringComparison]::OrdinalIgnoreCase)
if (-not $resolvedDll.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
    ($moduleName -ne 'BAKTweaks.dll' -and -not $isRefreshProbe -and -not $isEditorTrace -and -not $isPoseProbe)) {
    throw 'Only this project BAKTweaks.dll or an explicitly isolated project probe can be loaded.'
}
$gamePath = (Resolve-Path -LiteralPath $GameExePath).Path
$games = @(Get-Process BatmanAK -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $gamePath })
if ($games.Count -ne 1) { throw "Expected one running $Edition BatmanAK.exe, found $($games.Count)." }
$game = $games[0]
$loaded = @($game.Modules | Where-Object { $_.ModuleName -ieq $moduleName })
if ($loaded.Count) { throw "$moduleName is already loaded from $($loaded[0].FileName). Restart the game before testing another DLL." }
if (-not ('BakTestLoader' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BakTestLoader {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint allocation, uint protection);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] bytes, UIntPtr size, out UIntPtr written);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr attributes, UIntPtr stackSize, IntPtr start, IntPtr argument, uint flags, out uint tid);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetModuleHandle(string name);
    [DllImport("kernel32.dll", CharSet=CharSet.Ansi, ExactSpelling=true)] public static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool VirtualFreeEx(IntPtr process, IntPtr address, UIntPtr size, uint freeType);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
}
$handle = [IntPtr]::Zero; $remote = [IntPtr]::Zero; $thread = [IntPtr]::Zero; $threadFinished = $true
try {
    $handle = [BakTestLoader]::OpenProcess(0x43A, $false, $game.Id)
    if ($handle -eq [IntPtr]::Zero) { throw "OpenProcess failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
    $bytes = [Text.Encoding]::Unicode.GetBytes($resolvedDll + [char]0)
    $byteCount = [UIntPtr]::new([uint64]$bytes.Length)
    $remote = [BakTestLoader]::VirtualAllocEx($handle, [IntPtr]::Zero, $byteCount, 0x3000, 4)
    if ($remote -eq [IntPtr]::Zero) { throw 'VirtualAllocEx failed.' }
    $written = [UIntPtr]::Zero
    if (-not [BakTestLoader]::WriteProcessMemory($handle, $remote, $bytes, $byteCount, [ref]$written) -or $written.ToUInt64() -ne $bytes.Length) {
        throw 'WriteProcessMemory failed.'
    }
    $load = [BakTestLoader]::GetProcAddress([BakTestLoader]::GetModuleHandle('kernel32.dll'), 'LoadLibraryW')
    if ($load -eq [IntPtr]::Zero) { throw 'LoadLibraryW unavailable.' }
    # Resolve the owning system module in both processes, including forwarded exports.
    $owner = [Diagnostics.Process]::GetCurrentProcess().Modules | Where-Object {
        $load.ToInt64() -ge $_.BaseAddress.ToInt64() -and $load.ToInt64() -lt $_.BaseAddress.ToInt64() + $_.ModuleMemorySize
    } | Select-Object -First 1
    $remoteOwner = $game.Modules | Where-Object { $_.ModuleName -ieq $owner.ModuleName } | Select-Object -First 1
    if (-not $owner -or -not $remoteOwner) { throw 'Unable to resolve target system module.' }
    $remoteLoad = [IntPtr]($remoteOwner.BaseAddress.ToInt64() + $load.ToInt64() - $owner.BaseAddress.ToInt64())
    [uint32]$tid = 0
    $thread = [BakTestLoader]::CreateRemoteThread($handle, [IntPtr]::Zero, [UIntPtr]::Zero, $remoteLoad, $remote, 0, [ref]$tid)
    if ($thread -eq [IntPtr]::Zero) { throw 'CreateRemoteThread failed.' }
    $threadFinished = $false
    $threadFinished = [BakTestLoader]::WaitForSingleObject($thread, 10000) -eq 0
    if (-not $threadFinished) { throw 'Loader thread still pending; no second injection attempted.' }
    $game.Refresh()
    $result = @($game.Modules | Where-Object { $_.FileName -ieq $resolvedDll })
    if ($isPoseProbe) {
        # The launcher is designed to unload itself as soon as it queues work.
        # Its own log records whether the request reached the v5.32 export.
        [pscustomobject]@{ Edition=$Edition; ProcessId=$game.Id; Dll=$resolvedDll; SelfUnloading=$true; StillLoaded=($result.Count -eq 1); ResultLog=(Join-Path (Split-Path -Parent $resolvedDll) 'BAKPoseProbeLauncher.log') }
    } else {
        if ($result.Count -ne 1) { throw 'The DLL was not found in the game module list.' }
        [pscustomobject]@{ Edition=$Edition; ProcessId=$game.Id; Dll=$resolvedDll; BaseAddress=('0x{0:X}' -f $result[0].BaseAddress.ToInt64()) }
    }
} finally {
    if ($thread -ne [IntPtr]::Zero) { [void][BakTestLoader]::CloseHandle($thread) }
    if ($remote -ne [IntPtr]::Zero -and $threadFinished) { [void][BakTestLoader]::VirtualFreeEx($handle, $remote, [UIntPtr]::Zero, 0x8000) }
    if ($handle -ne [IntPtr]::Zero) { [void][BakTestLoader]::CloseHandle($handle) }
}

