# Usage: match.ps1 -e1 build\v2.exe -n1 v2 -e2 build\v1.exe -n2 v1 -tc 5+0.05 -rounds 150 -conc 3 -tag v2_vs_v1 [-hash 32] [-extra "..."]
param(
    [string]$e1, [string]$n1, [string]$e2, [string]$n2,
    [string]$tc = "10+0.1", [int]$rounds = 100, [int]$conc = 10, [string]$tag = "match", [int]$hash = 64,
    [string]$sprt = ""
)
$root = "C:\Users\steve\Dropbox\Chess\Development\AI Benchmarks\Opus 5.5"
$src = "$root\source"
function Resolve-Eng([string]$e) { if ($e -like "*resources*") { return "$root\$e" } else { return "$src\$e" } }
$a = @("-engine", "cmd=$(Resolve-Eng $e1)", "name=$n1", "-engine", "cmd=$(Resolve-Eng $e2)", "name=$n2",
    "-each", "tc=$tc", "option.Hash=$hash",
    "-openings", "file=$root\resources\fastchess\UHO.pgn", "format=pgn", "order=random", "plies=16",
    "-rounds", "$rounds", "-repeat", "-concurrency", "$conc", "-ratinginterval", "20", "-recover",
    "-pgnout", "file=$src\tests\$tag.pgn")
if ($sprt -ne "") { $a += @("-sprt") + $sprt.Split(" ") }
& "$root\resources\fastchess\fastchess.exe" @a 2>&1 | Out-File -Encoding utf8 "$src\tests\$tag.log"
