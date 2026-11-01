# SPDX-License-Identifier: Apache-2.0
$ErrorActionPreference = 'Stop'
try {
    $compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    if (-not (Test-Path $compiler)) {
        $compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
    }
    if (-not (Test-Path $compiler)) {
        throw 'The Windows .NET Framework 4.x C# compiler was not found.'
    }
    $binaryDir = Join-Path $env:LOCALAPPDATA 'VelaPokaReceiver\bin'
    New-Item -ItemType Directory -Force -Path $binaryDir | Out-Null
    $binary = Join-Path $binaryDir 'VelaPokaReceiver.exe'
    $sources = @((Join-Path $PSScriptRoot 'ReceiverCore.cs'), (Join-Path $PSScriptRoot 'ReceiverWindow.cs'))
    $rebuild = -not (Test-Path $binary)
    if (-not $rebuild) {
        foreach ($source in $sources) {
            if ((Get-Item $source).LastWriteTimeUtc -gt (Get-Item $binary).LastWriteTimeUtc) { $rebuild = $true }
        }
    }
    if ($rebuild) {
        $compileArgs = @('/nologo', '/target:winexe', '/optimize+', "/out:$binary",
                         '/r:System.Windows.Forms.dll', '/r:System.Drawing.dll', '/r:System.Core.dll') + $sources
        & $compiler @compileArgs
        if ($LASTEXITCODE -ne 0) { throw 'Receiver compilation failed. See the compiler messages above.' }
    }
    Start-Process -FilePath $binary
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
