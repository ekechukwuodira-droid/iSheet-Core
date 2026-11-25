# Adds the SPDX licence line to every C++ source and header in iSheet Core that lacks one.
# Run from the sheetcore folder:  .\tools\add-spdx.ps1            (dry run: lists the files)
#                                 .\tools\add-spdx.ps1 -Apply     (writes the changes)
param([switch]$Apply)

$line = "// SPDX-License-Identifier: LGPL-3.0-or-later`r`n"
$utf8 = New-Object System.Text.UTF8Encoding($false)
$roots = @("modules", "shared", "tools") | Where-Object { Test-Path $_ }
$changed = 0

Get-ChildItem -Path $roots -Recurse -File -Include *.hpp, *.cpp, *.h, *.c |
    Where-Object { $_.FullName -notmatch '\\(build|\.git)\\' } |
    ForEach-Object {
        $text = [IO.File]::ReadAllText($_.FullName)
        if ($text -match 'SPDX-License-Identifier') { return }
        $changed++
        if ($Apply) {
            [IO.File]::WriteAllText($_.FullName, $line + $text, $utf8)
            Write-Host "updated  $($_.FullName)"
        } else {
            Write-Host "would update  $($_.FullName)"
        }
    }

if ($Apply) { Write-Host "$changed file(s) updated." } else { Write-Host "$changed file(s) would be updated. Re-run with -Apply to write them." }
