<!-- Before you open this PR: base branch is `dev` (not `main`), branch name is
     `<member>/<topic>`, and commits read `subsystem: summary` with `git commit -s`.
     See docs/WORKFLOW.md "Branches" and "Commit messages". -->
## Summary
<!-- What changed and why. -->
<!-- Optional issue link: use "Closes #N" if this resolves an existing issue, or
     "Part of #N" if work remains. Omit for small changes without a tracking issue.
     Do not create an issue solely for this PR. -->
<!-- Open ready for review by default. If draft, say what must happen before review.
     List pending platform/GPU checks below; they do not automatically require draft. -->

## Tests
<!-- Tests added or updated -->

- [ ] Docs updated in the last commit of this PR: member file, ROADMAP status, stub register,
      CONTRACT_NOTES (or nothing documented changed)
- [ ] Assigned to this PR and, if applicable, its tracking issue
- [ ] GPU checked (only if render code changed): CUDA build compiles; if a GPU path exists,
      paste the rt_compare summary for a known scene: ______
- [ ] Changes docs/INTERFACES.md: approved by each affected owner, version line bumped
- [ ] Adds, changes, or removes a stub or workaround, or lands code a stub stands in for:
      register in docs/ROADMAP.md updated
      (WORKFLOW.md "Stubs and local workarounds")
- [ ] Developed on a reference environment (WSL Ubuntu 24.04 or Vista); if not, commits carry
      `Developed-on:` / `Needs-check:` trailers (WORKFLOW.md "Development environment")
- [ ] Not a reference environment, but verified on Vista (recommended): node, commit, ctest result: ______
