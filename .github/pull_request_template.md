<!-- Before you open this PR: base branch is `dev` (not `main`), branch name is
     `<member>/<topic>`, and commits read `subsystem: summary` with `git commit -s`.
     See docs/WORKFLOW.md "Branches" and "Commit messages". -->
## Summary
<!-- What changed and why. -->
Closes #
<!-- Replace with the issue number this PR finishes, e.g. "Closes #5". Merging then closes the issue
     automatically. Use one "Closes #N" per issue. If the issue needs more PRs, write "Part of #N"
     instead and delete the line above. -->

## Tests
<!-- Tests added or updated -->

- [ ] GPU checked (only if render code changed): CUDA build compiles; if a GPU path exists,
      paste the rt_compare summary for a known scene: ______
- [ ] Changes docs/INTERFACES.md: approved by each affected owner, version line bumped
- [ ] Adds, changes, or removes a stub or workaround, or lands code a stub stands in for:
      register in docs/ROADMAP.md updated
      (WORKFLOW.md "Stubs and local workarounds")
- [ ] Developed on a reference environment (WSL Ubuntu 24.04 or Vista); if not, commits carry
      `Developed-on:` / `Needs-check:` trailers (WORKFLOW.md "Development environment")
- [ ] Not a reference environment, but verified on Vista (recommended): node, commit, ctest result: ______
