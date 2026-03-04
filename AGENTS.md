# AGENTS.md

## Project Overview

LinBox — a Linux sandbox for deterministic simulation. Runs real native services (PostgreSQL, Redis, Nginx, Node.js) inside controlled containers where every outbound call (time, network, filesystem, random) is interceptable and controllable.

## Before Starting Any Task

**MANDATORY: Before writing any code, read the project documentation.**

This ensures consistency with existing patterns. Skipping this step leads to code that doesn't match project conventions.

## Voice Input Recognition

The user often uses voice recognition. Text may contain transcription errors.

- Auto-correct obvious errors from context
- If unclear or ambiguous - ask for clarification
- Try to fix independently when confident, but verify when uncertain

## When to Apply These Guidelines

**CRITICAL: File operation rules apply ONLY when working with repository files.**

✅ **Apply rules when:** Creating, editing, or deleting files; organizing directory structure; writing documentation to disk

❌ **DO NOT apply when:** Having regular conversation in chat; providing analysis or advice without creating files

**Default behavior:** If user asks for analysis/discussion without mentioning "create", "write", "document" - respond in chat only.

## Development Guidelines

### Documentation Standards

**Note:** These rules do NOT apply to AGENTS.md and CLAUDE.md config files.

**Language:**
- All documentation files (*.md) MUST be written in English, regardless of conversation language
- Translate content to English before writing to disk

**README consistency (docs/ only):**
- Every directory in `docs/` MUST have a README.md with links to all files and subdirectories
- When adding/removing files in docs/, update the parent README.md
- Source code directories (`src/`) do NOT require README.md files

**README is navigation only:**
- Any README.md file is for **navigation only** — never put actual content in README files
- Actual content goes in separate named files (e.g., `summary.md`, `research.md`, `overview.md`)

**Linking rules:**
- ALL links to directories MUST point to README.md explicitly
  - ✅ Good: `[base/](base/README.md)`
  - ❌ Bad: `[base/](base/)`
- Link text should be readable, not technical filenames
  - ✅ Good: `[base environment](base/README.md)`
  - ❌ Bad: `[README.md](base/README.md)`

**No absolute or external local paths:**
- NEVER write absolute filesystem paths in documentation files (e.g., `/Users/...`, `/home/...`, `/tmp/...`)
- NEVER write local paths that point outside this repository (e.g., `~/other-project/...`, `../other-repo/...`)
- This includes paths from tool output — always convert them before writing to files
- For files within the repository, use repository-relative paths (e.g., `src/engine/parser.ts`, `docs/adr/01-wasm.md`)
- For external projects, use GitHub URLs instead (e.g., `https://github.com/electric-sql/pglite/blob/main/packages/pglite/src/pglite.ts`)
- **Verify all external URLs** before writing them to files — fetch the page to confirm it exists and contains expected content. LLM-generated URLs are frequently hallucinated

**Navigation:**
- Every .md file MUST have a back link to parent README
- Place navigation at the bottom after `---` separator
  - Example: `[← Back to Main](README.md)` or `[← Back](../README.md)`

### Localized File Synchronization
Translations (`.<lang>.md`, e.g., `.de.md`, `.fr.md`) are optional and created only on explicit user request. When a localized version exists, it MUST be kept in sync with the primary English file.

Before completing ANY file edit task:
- Check if localized versions exist (e.g., `overview.md` → check for `overview.*.md`)
- If localized versions exist: Apply identical changes to ALL versions
- If only the primary version exists: Edit that version only

**Link consistency in localized files:** Internal links must point to the same locale:
- ✅ Correct: `[components](components/README.de.md)` (in a `.de.md` file)
- ❌ Wrong: `[components](components/README.md)` (mixing locales)

### File and Directory Reorganization
When moving or renaming files/folders:
1. Use `Grep` to search for all mentions of the file/folder path in `.md` files
2. Document all files that reference the path
3. Update all found references to point to the new location
4. Verify navigation links in moved files point to correct parent directories

### File Naming Conventions
- Use lowercase, hyphens for spaces (e.g., `canvas-renderer.ts`)

### Commit Message Format
First line: imperative mood, lowercase start, no trailing period:
- `add canvas rendering component`
- `fix websocket reconnection bug`
- `update content type system`

For larger commits, add brief description after a blank line (standard git convention):
```
add websocket reconnection logic

handle network interruptions with exponential backoff
```

Do NOT add "Generated with Claude Code" or "Co-Authored-By: Claude" to commit messages

### Git Push Policy
- NEVER run `git push` without explicit user request
- Only push when user explicitly says "push" or "git push"
- After commit, wait for user instruction before pushing

### PR Merge Policy
- Use `--squash` by default when merging PRs (unless explicitly specified otherwise)
- Always write a custom commit message for the squash commit (summarizing PR changes)
- Delete the source branch after merge with `--delete-branch`

### Rolling Back Changes
When discarding or reverting local changes:
- **ALWAYS stash first** before any destructive git operations (`git checkout .`, `git restore .`, `git reset --hard`, etc.)
- Use `git stash -u` to include untracked files
- This ensures work is never permanently lost — it can always be recovered via `git stash pop`

### Pre-Commit Self-Analysis
Before each commit, briefly consider:
1. **Friction points** — Were there misunderstandings, repeated corrections, or user frustration?
2. **Pattern recognition** — Did the same issue occur multiple times in this session?
3. **Process gaps** — Is there a missing guideline that would have prevented the problem?

If improvements are identified:
- Suggest specific AGENTS.md changes to the user
- Keep suggestions actionable and concise
- Don't suggest changes for one-off issues — only recurring patterns

**Anti-loop safeguard:** Don't suggest meta-rules about suggesting rules. Focus on concrete workflow improvements.

### Branching Strategy
- Feature branches: `feature/<name>` (e.g., `feature/schema`)
- Small fixes and refactoring can go directly to main
- Large features and breaking changes go through feature branches

### After Switching Branches or Pulling
After `git checkout`, `git switch`, or `git pull` that includes changes to dependency files:
1. Run dependency install to sync
2. Clear caches if needed

### Research
Conduct all research and exploration in the `rnd/` folder. Create a new subfolder for each research topic.

### Product Ideas
Product ideas and concepts go to `docs/ideas/`. Do NOT create ideas in `rnd/` — that folder is for research only.

### Work Structure

Location: `work/`

- **Epics** (E01, E02...) — `E##-name/epic.md`
- **Stories** (S01, S02...) — `E##-name/S##-name/story.md`
- **Research** (R01, R02...) — `R##-name/research.md`
- **Tasks** (T01, T02...) — `E##-name/S##-name/T##-name/task.md` or `R##-name/T##-name/task.md`

Creating new:
- Epic number — sequential (E01, E02, E03...)
- Story number — sequential within stories (S01, S02, S03...)
- Research number — sequential within research (R01, R02, R03...)
- Task number — within story/research (T01, T02)

### Board

Location: `work/board.md`

Workflow:
- Start task → add to "In Progress"
- Finish task → remove from board (no "Completed" section)

### Running Project Scripts
Prefer project scripts over running files directly.

Before running commands:
1. Check `package.json` for available scripts
2. Use `npm run <script>`

Scripts handle working directory, arguments, and pre/post steps. Direct file execution may skip important setup.
