# Steam v5.25 editor follow-up — 2026-10-20

The user confirmed that the light and gizmo move during the requested manual live-edit test in the IGCS-paused session. The production log records creation of Spot1 and entry into its Edit panel. This does not establish exhaustive coverage of Point lights, intensity or each axis.

The passive trace was installed successfully (record size 680, state size 288, dirty offset 92, state offset 100). Its two hooks disabled automatically after 120 seconds. There were no UI_EDIT or ENGINE_IN records: the manual edits occurred outside the capture window. No UI-to-engine diagnosis can be inferred from that empty trace.

The symbol map was reconstructed from the original v5.25 object files. All PE sections matched the original DLL byte for byte, with identical RVAs and sizes. The production source and DLL were not changed for this trace.

Separate native tests, documented in ../light-refresh-probe/RESULTS.md, showed that SetLocation alone updates Point/Spot actor positions and light matrices while the world reports paused. A missing explicit ForceUpdate is therefore not a demonstrated cause of the earlier reported failure. No additional production patch was justified by these results.

The trace hooks are inactive. The user-created Spot1 was left intact. Epic runtime testing remains pending.
