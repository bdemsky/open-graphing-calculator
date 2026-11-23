# Known issues

The documentation passes (2026-10) produced a list of ~200 bugs across the app,
core, platform, tools, hardware generators and CAS. All of them were fixed with
regression tests, or confirmed not to be bugs, in the bug-fix pass merged on
2026-10-20 (see `git log --grep "bug-fix pass"` and the merge messages after it).
What remains:

## Open
- CAS: `tan(0.5_rev)` stays `tan(pi)` instead of simplifying to 0.
- CAS: in DEG mode, `sin(30_deg+30)` treats the bare 30 as radians (a sum mixing
  angle-unit and plain terms; the plain terms should be degrees).
- CAS: `odesolve(x,[x,y],[1e6,0],1e6+1)` = 1000000.49997 (relative error ~3e-11).

- GRAPH TRACE does not visit stat-plot points (the stat overlay has no
  `nsets`/`point` callbacks); geometry points are traced.
- A line being typed in a spreadsheet cell or the list editor is dropped by a
  power cycle (home, RPN and 3D z= keep theirs).

## Deferred on purpose
- `hardware/gen/pcb_build.py` `read_fills`: the fix would change the DRC repair
  pass and therefore routing on any regeneration of the ordered board.

## Fixed but not directly testable
- Platform (SDRAM self-refresh status, cached SDRAM check, PMON alert retry,
  I2C reset, RTC wakeup clamp, QSPI memory-mapped restore): checked against
  RM0433 and built; verify during bring-up.
- Stat CSV read error → FS_EIO (simulated SD has no read-fault injection).
- Help paragraphs > 1 KB now wrap (no such page exists).
- Graph trace restart across a traceable overlay (no overlay is traceable yet).
- `ode.c` basis-array guard and `integ.c` `inverse_subs`/`integrate_def`
  (no input found that reaches them).
