param([Parameter(Mandatory)][string]$Path, [long]$Max = 3000000)
# Exit 1 when the EXE is larger than $Max bytes (Spec 2.3 / 12.6). Used as a Release post-build step.
if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    Write-Host "check-size: file not found: $Path"
    exit 1
}
$size = (Get-Item -LiteralPath $Path).Length
if ($size -gt $Max) {
    Write-Host ("FAIL: {0} is {1:N0} bytes, over the limit of {2:N0} by {3:N0} bytes." -f $Path, $size, $Max, ($size - $Max))
    exit 1
}
Write-Host ("OK: {0} is {1:N0} bytes (limit {2:N0}, {3:N0} bytes to spare)." -f $Path, $size, $Max, ($Max - $size))
exit 0
