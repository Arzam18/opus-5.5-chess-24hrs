# Verifies the deliverable in final/: UCI id, compliance, standalone DLLs, perft.
$root = "C:\Users\steve\Dropbox\Chess\Development\AI Benchmarks\Opus 5.5"
$exe = "$root\final\Opus55chess24hrs.exe"
Write-Output "== id =="
"uci`nquit" | & $exe | Select-String "id name|id author|option name Hash"
Write-Output "== DLLs =="
& objdump -p $exe | Select-String "DLL Name"
Write-Output "== perft suite (depth 5) =="
& $exe perftsuite "$root\resources\perft\perft.epd" 5
Write-Output "== compliance =="
& "$root\resources\fastchess\fastchess.exe" --compliance $exe 2>&1 | Select-Object -Last 2
Write-Output "== hash 256 + short search =="
"uci`nsetoption name Hash value 256`nisready`nposition startpos moves e2e4`ngo wtime 10000 btime 10000 winc 100 binc 100`n" | & $exe | Select-String "readyok|bestmove"
