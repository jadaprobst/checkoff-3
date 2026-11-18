# Uninitialized Memory in C: Compiler-Dependent Security Outcomes
 
A minimal reproduction showing that an uninitialized local variable in an authorization check produces different security decisions depending on the compiler and optimization level — and that in the failing case the compiler removes the authorization check from the binary entirely.
 
## TL;DR
 
The same six-line function grants or denies access depending on flags you pass to the compiler:
 
| Compiler | `-O0` | `-O2` |
|---|---|---|
| gcc 13.3.0 | `ACCESS GRANTED` | `ACCESS GRANTED` |
| clang 18.1.3 | `access denied` | `ACCESS GRANTED` |
 
At `-O2`, neither compiler emits the `strcmp` call at all. The check is not merely evaluated against a junk value — it is deleted as dead code, because reading an uninitialized variable is undefined behavior and the optimizer is permitted to assume undefined behavior never occurs.
 
`gcc -Wall -Wextra` reports no warning on this program.
 
## Research question
 
C does not zero automatic (stack) variables or `malloc`'d memory. The common mental model is that reading such a variable yields "whatever stale bytes were there" — i.e. an unpredictable but ordinary integer.
 
The C standard instead classifies this as reading an *indeterminate value*, whose use is undefined behavior (C17 §6.2.4, §3.19.2; see also committee defect reports DR 260 and DR 451 on so-called "wobbly" values).
 
**Question:** does that distinction have observable consequences, or is it a specification technicality? Specifically — can an optimizing compiler produce behavior that the "stale bytes" model does not predict?
 
**Answer:** yes. See Finding 2.
 
## Environment
 
Results below were produced on:
 
- **OS / arch:** Linux, x86-64
- **gcc:** 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1)
- **clang:** 18.1.3 (Ubuntu clang version 18.1.3-1ubuntu1)
Record your own versions before reproducing:
 
```sh
uname -m && gcc --version | head -1 && clang --version | head -1
```
 
Divergence from these results is expected on other versions and is itself a finding — see [Threats to validity](#threats-to-validity).
 
## Files
 
| File | Purpose |
|---|---|
| `auth.c` | Primary experiment. Uninitialized flag in an authorization check. |
| `selfcmp.c` | Supporting: `x == x` evaluates to false. |
| `ghost.c` | Supporting: a returned function's stack frame is reused, and its leftover bytes appear in a later function's uninitialized local. |
| `fixes/fix1_return.c` | Fix A — eliminate the variable; return the expression. |
| `fixes/fix2_init.c` | Fix B — initialize at declaration to the fail-closed value. |
| `fixes/fix3_else.c` | Fix C — add the missing `else`. |
| `fixes/fix3_else_broken.c` | Counter-example to Fix C: a non-exhaustive `else if` chain reopens the defect. |
 
## Reproducing
 
### Finding 1 — the security decision depends on the toolchain
 
```sh
for cc in gcc clang; do
  for opt in -O0 -O2; do
    $cc $opt -w -o auth_tmp auth.c
    printf "%-6s %-4s -> %s\n" "$cc" "$opt" "$(./auth_tmp)"
  done
done
```
 
Expected output:
 
```
gcc    -O0  -> ACCESS GRANTED
gcc    -O2  -> ACCESS GRANTED
clang  -O0  -> access denied
clang  -O2  -> ACCESS GRANTED
```
 
Note that clang flips between `-O0` and `-O2`: same source, same compiler, same machine, opposite security outcome.
 
### Finding 2 — the check is deleted, not merely mis-evaluated
 
```sh
clang -O2 -w -S -masm=intel -o - auth.c | sed -n '/^main:/,/^\s*\.size\s*main/p'
```
 
Observed: `main` reduces to a single `puts` of the string `"ACCESS GRANTED"`. The `strcmp` call is absent and the string `"access denied"` is not referenced.
 
This is the result the "stale bytes" model cannot explain. The compiler did not pick a nonzero junk value — it reasoned that the only branch giving the program defined behavior sets `allowed = 1`, concluded `allowed` is therefore 1, and deleted the now-redundant comparison.
 
On Apple Silicon, drop `-masm=intel` (it is x86-only); the result is the same in arm64 assembly.
 
### Finding 3 — gcc issues no diagnostic
 
```sh
gcc   -O2 -Wall -Wextra -c -o /dev/null auth.c   # silent
clang -O2 -Wall -Wextra -c -o /dev/null auth.c   # -Wsometimes-uninitialized
```
 
Consequence for build policy:
 
```sh
gcc   -O2 -Wall -Wextra -Werror -o /dev/null auth.c   # exit 0 — bug ships
clang -O2 -Wall -Wextra -Werror -o /dev/null auth.c   # exit 1 — bug caught
```
 
`-Werror` is only as strong as the underlying analysis. On this program gcc's analysis is empty, so the policy provides no protection.
 
### Finding 4 — `x == x` is false
 
```sh
clang -O2 -w -o selfcmp selfcmp.c && ./selfcmp
```
 
Observed: `x == x --> 0`. The assembly shows `xor esi, esi` — the tautology was constant-folded to false at compile time. No integer-valued model of `x` can produce this; only a model in which `x` has no value at all.
 
### Finding 5 — all three source fixes resolve it
 
```sh
for f in fix1_return fix2_init fix3_else; do
  for cc in gcc clang; do
    for opt in -O0 -O2; do
      $cc $opt -o fix_tmp "fixes/$f.c"
      printf "%-12s %-6s %-4s -> %s\n" "$f" "$cc" "$opt" "$(./fix_tmp)"
    done
  done
done
```
 
Expected: `access denied` in all twelve configurations. The behavior stops depending on the toolchain, which is the actual goal.
 
### Finding 6 — compiler hardening is partial
 
Applying `-ftrivial-auto-var-init=zero` to the unfixed program:
 
| Compiler | `-O0` | `-O2` |
|---|---|---|
| gcc 13.3.0 | `access denied` | `ACCESS GRANTED` |
| clang 18.1.3 | `access denied` | `access denied` |
 
```sh
for cc in gcc clang; do
  for opt in -O0 -O2; do
    $cc $opt -ftrivial-auto-var-init=zero -w -o ti_tmp auth.c
    printf "%-6s %-4s -> %s\n" "$cc" "$opt" "$(./ti_tmp)"
  done
done
```
 
The mitigation rescues clang but does not rescue gcc 13.3 at `-O2` on this program. The mechanism behind the gcc result was not investigated and is an open question (see [Open questions](#open-questions)). The practical reading is that automatic variable initialization is defense-in-depth, not a substitute for correct source.
 
## Interpretation
 
1. **Undefined behavior is not a technicality.** The "stale bytes" model predicts an unpredictable value. It does not predict a deleted check (Finding 2) or a false tautology (Finding 4). Both follow from the standard's actual rule and from nothing weaker.
2. **The failure mode is backward-propagating.** Because the read is UB, the optimizer may delete code that *precedes* it. The security check vanished even though it appears earlier in the source than the faulty read.
3. **Fail-closed matters more than "initialize your variables."** `int allowed = 0;` is secure; `int allowed = 1;` is initialized, warning-free, and catastrophic. The defaults must be the restrictive outcome so that unwritten paths fall into denial.
4. **Exhaustive branching is the fragile fix.** `fixes/fix3_else.c` is correct today, but adding an `else if` without a terminal `else` reopens the defect — `fixes/fix3_else_broken.c` is that edit.
5. **Defense in depth, ordered by reliability:** correct source → build with a compiler that actually warns, plus `-Werror` → MemorySanitizer in CI → `-ftrivial-auto-var-init`. Each later layer is weaker and more toolchain-contingent than the one before it.
## Threats to validity
 
- **Version specificity.** All results are tied to the exact compiler versions above. Different versions may behave differently. This is a limitation of the measurement but supports the thesis: behavior that varies by toolchain version is behavior no code reviewer can audit from source.
- **`strcmp` absence is not a UB detector.** Once the program is fixed, clang may still eliminate the `strcmp` call — legitimately, by constant-folding `strcmp("guest", "admin")` since both arguments are literals. A missing call therefore has two possible causes. Program output, not call count, is the measurement. Finding 2's inspection is valid only on the unfixed program.
- **Junk values are not reproducible.** The specific integer printed by `ghost.c` depends on process environment, stack contents, and ASLR, and will differ between runs and machines. Only the categorical outcome (whether the earlier frame's bytes are visible at all) is the claim.
- **Single platform.** x86-64 Linux only. Behavior on arm64 / macOS is untested.
- **Toy program.** The reproduction is deliberately minimal. It demonstrates the mechanism; it does not measure how often this pattern occurs in real codebases.
## Open questions
 
- Why does `-ftrivial-auto-var-init=zero` fail to change gcc 13.3's `-O2` outcome? Candidate explanation: pass ordering, with UB-based inference running before the initialization is inserted. Unverified.
- Does `fixes/fix3_else_broken.c` still trigger clang's warning? Expected yes for clang, no for gcc — untested.
- Does MemorySanitizer (`clang -fsanitize=memory`) flag `auth.c`, and does the instrumented binary still grant access?
## Presentation
 
Slides for the in-class presentation: `slides/uninitialized-memory.pdf`
 
## References
 
- ISO/IEC 9899:2018 (C17), §3.19.2 (indeterminate value), §6.2.4 (storage durations), §6.7.9 (initialization).
- C committee defect reports DR 260 and DR 451, on indeterminate values that may change between reads.
- CVE-2008-0166 — Debian OpenSSL predictable PRNG, caused by removing a line that seeded from uninitialized memory while silencing a Valgrind warning. A cautionary case on both uninitialized memory as an entropy source and on suppressing memory-safety diagnostics.
 