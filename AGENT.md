# AGENT.md

## Purpose

This repository may use goal-oriented planning, task lists, handoff notes, and session logs, but those tools exist to preserve context, not to justify frequent commits.

Default to a work-safe commit discipline even though this is a personal project.

## Commit Discipline

### Hard rule

Do not create frequent "progress marker" commits.

Specifically, do not commit just because:

- a task checkbox was completed
- a plan item moved forward
- `.agent-context/handoff.md` or `.agent-context/session-log.md` was updated
- a design/spec/plan document was touched only to reflect status
- there is a desire to leave breadcrumbs for the next session
- a small intermediate refactor compiles but is not yet a clean reviewable slice

Goal mode can drive planning. It must not drive commit granularity.

### What to do instead

Use local context updates without committing when you only need continuity:

- update `.agent-context/handoff.md`
- update `.agent-context/session-log.md`
- add or refine plan/spec documents
- leave the workspace dirty across sessions if the work is still one unfinished slice

Recording context is encouraged. Frequent checkpoint commits are not.

### When a commit is allowed

Create a commit only when the current work is a cohesive, reviewable unit that would make sense in a normal work repository.

Usually that means all of the following are true:

- the change delivers one clear behavior change, bug fix, refactor slice, or documentation artifact with durable standalone value
- the relevant code and tests for that slice are included together
- status-only doc updates are folded into the same commit instead of committed separately
- the commit message describes the actual engineering change, not task bookkeeping

### Default cadence

- Prefer one commit per completed slice, not one commit per task update.
- If several small tasks belong to one accepted goal, accumulate them and commit once at the end of the slice.
- If work spans multiple sessions, keep using handoff/session notes and defer the commit until the slice is actually ready.

### Narrow exceptions

Extra commits are allowed only when at least one of these is true:

- the user explicitly asks for a checkpoint commit
- two changes are genuinely independent and each is mergeable on its own
- a standalone design/spec document is the explicit deliverable for the session

If none of those conditions hold, do not commit yet.

## Anti-Patterns

Avoid commit patterns like:

- `docs: mark Task X complete`
- `docs: update session log`
- `checkpoint`
- `wip`
- multiple commits in the same goal that only move bookkeeping forward

Task completion is a planning artifact, not a commit boundary.

## Preferred Behavior

When finishing a session:

1. update local context if needed
2. decide whether the current work is actually a cohesive slice
3. commit only if the slice is reviewable and durable
4. otherwise leave clear notes and stop without committing

When in doubt, prefer no commit.
