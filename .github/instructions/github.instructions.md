---
name: 'GitHub bookkeeping'
description: 'Use when opening, editing or labelling a pull request, when creating, closing or linking an issue, when creating or closing a milestone, when assigning labels, or when touching docs/roadmap.md: how a change reaches GitHub in this repository, how a pull request is tied to the roadmap point it delivers, what each label means, and how the roadmap, the issues and the milestones stay in step across iterations.'
---

# GitHub bookkeeping

How a change reaches this repository on GitHub, and how the roadmap, the issues and the
milestones stay in step. What is written along the way - commit messages, branch names, pull
request titles and bodies, issues and comments - is English and plain ASCII, without
typographic characters and without emoji.

## Commits and branches

- Conventional Commits: `type(scope): description`, the description lowercase and imperative,
  with the reason for the change in the body. `type` is one of `feat`, `fix`, `docs`,
  `refactor`, `performance`, `test`, `build`, `ci`, `style`, `chore` or `revert`.
- Branches are `type/purpose`, two or three lowercase words joined by hyphens:
  `feature/rest-provider`, `fix/env-trailing-space`, `docs/development-guide`. `main` is the
  only long-lived branch, and a branch is deleted once it is merged.
- A pull request title uses the header of a commit. The repository merges with squash only, so
  that title is the message that lands on `main`.

## The three axes, and why they do not overlap

| Axis | What it is | Named |
| --- | --- | --- |
| Milestone | one roadmap stage | exactly the `## v0.x.y - ...` heading of `docs/roadmap.md` |
| Issue | one point of a stage | the bullet, without its trailing period |
| Label | one area of the project | the list below |

A milestone is never named after a feature and a label is never named after a stage. Keeping
them apart is what makes both of them worth filtering by.

`docs/roadmap.md` is the source of truth. An issue is a copy of one point of it, and its body
is the note under that bullet: what exists, what is missing, and the open questions. When the
two disagree, the roadmap wins and the issue is edited.

## Labels

| Label | Colour | For |
| --- | --- | --- |
| `core` | `0b5cad` | The NPC, the conversation and the data behind them |
| `provider` | `8250df` | Inference backends and how a call is made |
| `http` | `006b75` | The HTTP boundary and its libcurl implementation |
| `architecture` | `5319e7` | Package layout, API surface and conventions |
| `build` | `8b949e` | CMake, dependencies, toolchain and packaging |
| `ci` | `fbca04` | Workflows and repository automation |
| `tests` | `b60205` | The suite and what verifies a change |
| `examples` | `f97316` | Runnable clients under `examples/` and `apps/` |

- `documentation` is the label GitHub ships and means what a `docs` label would: use it
  instead of adding a second one.
- `dependencies` and `github-actions` belong to the bots. Leave them alone.
- An issue or a pull request carries the areas it *works on*, which is not every directory it
  touches: two or three, and only when the change really belongs to both.
- The workflow labels of GitHub (`bug`, `enhancement`, `good first issue`, `help wanted`,
  `question`) are for the maintainer to place by hand. Do not reach for them on your own.

## A pull request

1. Title in Conventional Commits. The repository merges with squash only, so the title is the
   commit that lands on the default branch.
2. Body: `## What` (what changed and why it is worth merging), `## How to check` (the commands
   to run and what a green result looks like) and `## Notes` (only what the reviewer needs: an
   open question, a decision taken, what the change deliberately leaves out). The body starts
   at `## What`: the title is already the header, and repeating it above the first section is a
   line the reader has just read.
3. Labels of the areas it works on.
4. **A pull request does not carry a milestone.** Milestones count roadmap points, so a stage
   shows the state of its stage and not how many pull requests it took.
5. `Closes #N` for every point the pull request delivers, at the end of the body. That keyword
   is the one the Development sidebar reads, and it is the only place that works: a closing
   keyword in a comment does nothing.
6. The same pull request marks the point in `docs/roadmap.md`, so the document and the issue
   agree the moment it is merged. A point that is half done becomes `[~]` with the note saying
   what is missing, and the issue stays open; a point that is done becomes `[x]`.
7. `Part of #N` when the change is a step of a point that is still open. It leaves a
   cross-reference in the timeline without claiming to finish anything, which is what a
   closing keyword there would do on merge.

## An issue

- One issue per point of the roadmap, in the milestone of its stage, with the roadmap note as
  the body. An empty body is fine for a point the roadmap does not explain.
- A point that is already done at the time the issue is created is created and closed at once:
  the milestone then reads as what the stage delivered. `[x]` in the roadmap, closed issue,
  and the two say the same thing.
- `[~]` is an open issue: the note says what is already there and what is missing.
- **Comments carry what the roadmap does not.** The body is a copy of a roadmap point, so it
  holds the plan and never the findings: a trap that cost a day, a decision and what follows
  from it, a measurement worth remembering, or a number that has moved since. Write one when
  the issue will be read again by someone who was not there, and only then; a comment per step
  of the work buries the one that matters.

## Keeping them in step

| When | What happens |
| --- | --- |
| A point lands | The pull request marks it in `docs/roadmap.md` and carries `Closes #N`, so the merge closes the issue |
| A stage is fully closed | Close the milestone by hand, and move the version in `CMakeLists.txt` if the stage is one of the ones that carries a number (v0.2.0 did) |
| A new point appears in the roadmap | A new issue in that stage, with the note as the body |
| Something is learned that the roadmap does not say | A comment on the issue that owns it |
| The roadmap and an issue disagree | Edit the issue: the roadmap is the source of truth |

## Commands

```bash
# gh talks to GitHub over SSH, and its API calls are not git transit.
gh config get git_protocol

# There is no `gh milestone`: the API is the way.
gh api repos/{owner}/{repo}/milestones -f title='v0.9.0 - Polish, profiling and examples'
gh api "repos/{owner}/{repo}/milestones?state=all" --paginate \
    --jq '.[] | "\(.title): \(.open_issues) open / \(.closed_issues) closed"'

# One issue per point, in its stage. The body is the roadmap note, so it goes in a file.
gh issue create --title 'Terminal client under apps/' --body-file /tmp/body.md \
    --milestone 'v0.9.0 - Polish, profiling and examples'
gh issue close <url>

# Labels add, never replace: the ones already there stay.
gh pr edit 14 --add-label core,provider,http
gh issue edit 53 --add-label architecture

# Linking a pull request that was merged before the issue existed.
gh pr view 27 --json body --jq .body > /tmp/body.md   # read it before rewriting it
gh pr edit 27 --body-file /tmp/body.md
```

Written down because each of these cost a round of confusion:

- `gh api` on a list takes `.[].title`, not `.title`. The latter fails with
  `expected an object but got: array`.
- A run of tens of `gh` calls takes minutes. Send it to a file (`> /tmp/log 2>&1`) and read the
  file: piping it to `tail` prints nothing until the end and looks like a hang.
- A script that creates milestones or issues has to check what already exists first, or the
  second run duplicates everything.
- Multi-line text never goes in a `-f` flag: `--body-file`.
- Retrospective linking only points at issues that are **already closed**. A closing keyword on
  an open issue closes it when the pull request is merged, and a past pull request was not
  waiting for permission to do that.

## What is not ours to publish

Pushing, opening or editing a pull request, and now also creating or editing an issue, a label
or a milestone, are writes to the repository: they happen when the user has asked for them, one
approval for one action. Preparing the batch first and showing it works better than doing it
and describing it afterwards.

The checks a change passes before any of this live in `code-generation.instructions.md`.
