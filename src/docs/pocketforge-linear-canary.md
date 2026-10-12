# PocketForge linear-history canary

This is the administrator runbook for changing this repository's long-term
default from the archived merge history on `pocketforge` to the linear patch
stack on `pocketforge-linear`. It is intentionally scoped to Gamescope. Do not
apply it to another repository until the canary has been reviewed.

All GitHub writes in this runbook use the PocketForge GitHub App token:

```sh
export GH_TOKEN="$(/home/matt/pocketforge-automation/scripts/pf-github-bot-token.sh)"
owner=pocketforge-os
repo=gamescope
linear=pocketforge-linear
archive=pocketforge
```

Never print `GH_TOKEN`. Never move, rename, delete, or force-update
`pocketforge`. Save every response beside the before-state receipt. Stop on an
unexpected response; rollback is not a reason to continue through uncertainty.

## 1. Capture the immutable before state

Create a new file; never append to or overwrite an earlier receipt. Record the
repository, both refs, every repository ruleset and its full JSON, every
organization ruleset and its full JSON, effective rules, legacy protection
responses (including expected 404 responses), open pull requests, and the
workflow filters.

```sh
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
before="/home/matt/recovery/gpu14/linear-rollout/gamescope-before-${stamp}.txt"
test ! -e "$before"
# The evidence collector writes sectioned gh api --include responses to $before.
```

Read-back: check that the file is non-empty, names the current default branch,
contains full JSON for every ID returned by the repository ruleset endpoint
with `includes_parents=true`, and records the protection response for both
named branches. That inventory includes each applicable parent organization
ruleset; a global organization-ruleset listing is neither needed nor used.

Rollback: none. This step is read-only and the receipt is immutable.

## 2. Bootstrap CI on both branch names

This document and `.github/workflows/main.yml` land together through a pull
request based on `pocketforge-linear`. Both `push` and `pull_request` filters
must name `pocketforge` and `pocketforge-linear` before the default changes.

The canary's temporary repository ruleset already exists as ID `24909558`.
Set `linear_ruleset_id=24909558`, fetch that exact ID, and compare its complete
semantics with the JSON below before any later operation. Also enumerate the
applicable rulesets and refuse if another repository ruleset targets
`refs/heads/pocketforge-linear`. The organization pull-request rule targets
`~DEFAULT_BRANCH`, so the temporary local `pull_request` rule closes the
pre-switch interval.

```json
{
  "name": "Protect pocketforge-linear",
  "target": "branch",
  "enforcement": "active",
  "bypass_actors": [],
  "conditions": {"ref_name": {"include": ["refs/heads/pocketforge-linear"], "exclude": []}},
  "rules": [
    {"type": "pull_request", "parameters": {
      "required_approving_review_count": 0,
      "dismiss_stale_reviews_on_push": true,
      "require_code_owner_review": false,
      "require_last_push_approval": false,
      "required_review_thread_resolution": false,
      "require_extra_approval_for_unattributed_changes": true,
      "allowed_merge_methods": ["rebase"]
    }},
    {"type": "deletion"},
    {"type": "non_fast_forward"},
    {"type": "required_status_checks", "parameters": {
      "strict_required_status_checks_policy": true,
      "do_not_enforce_on_create": false,
      "required_status_checks": [
        {"context": "native-tests"},
        {"context": "aarch64-build"}
      ]
    }},
    {"type": "required_linear_history"}
  ]
}
```

Only if `GET /repos/pocketforge-os/gamescope/rulesets/24909558` returns 404 and
the applicable inventory proves that no semantically matching ruleset exists,
stop for owner action to `POST /repos/pocketforge-os/gamescope/rulesets` with
the exact JSON above. Bind `linear_ruleset_id` to the returned ID and perform
the same semantic read-back before continuing. Never POST when 24909558 exists.

Read-back: `GET /repos/pocketforge-os/gamescope/rulesets/$linear_ruleset_id`
must exactly show active enforcement, no bypass actor, the one branch include,
and all five temporary rules. `GET /repos/pocketforge-os/gamescope/rules/branches/pocketforge-linear`
must show the effective rules. Refuse if strict status checks are absent or
false.

Rollback: before the default switch, close the bootstrap PR and call
`DELETE /repos/pocketforge-os/gamescope/rulesets/$linear_ruleset_id`. Read back
the ruleset list and verify that the ID is absent. Delete only the bootstrap
feature branch, never either long-lived branch.

Opening the canary PR started pull-request run 38207861417 from the proposed
workflow at exact head `cf8c5f6218f29fe21d25d5a61a5a774123140fc7`.
`pf-public-isolation-probe`, `native-tests`, and `aarch64-build` all executed and
passed; the two required jobs ran for 3m37s and 4m23s respectively. Therefore a
manual dispatch was not needed. Before requesting any fallback dispatch, first
read the PR check suite and workflow runs for the exact head; never duplicate a
real pull-request run. Record the run ID, event, head SHA, job durations, and
log evidence. After exact-head author review, merge the bootstrap PR with the
deployed `pf-pr-merge.sh`. It must use the required-linear-history rebase path
and prove ordered patch IDs and final-tree equality.

Read-back: verify the PR reports `MERGED`, its merge commit is the returned
rebased head, both workflow filters are present on `pocketforge-linear`,
`git rev-list --merges <old-linear-tip>..<new-linear-tip>` is empty, and the
post-merge proof reports equal commit count, ordered patch IDs, and final tree.

Rollback: do not rewrite the branch. Open and merge a new linear revert PR that
restores the old workflow and removes this document, then verify its exact head.
Only use that rollback before switching the default branch.

## 3. Switch the default and finalize rules

Immediately before mutation, re-read the repository, applicable rulesets with
`GET /repos/pocketforge-os/gamescope/rulesets?includes_parents=true`, full
ruleset 18506023 (`pf-require-pr`) through the repository-scoped ruleset
endpoint, both repository rulesets, and effective rules for
`pocketforge-linear`. Refuse unless:

- the workflow on `pocketforge-linear` targets both branch names;
- repository rules enforce deletion, non-fast-forward, strict required checks
  `native-tests` and `aarch64-build`, and required linear history;
- organization ruleset 18506023 is active, targets `~DEFAULT_BRANCH`, contains
  `pull_request`, and has no bypass actors.

Switch with:

```sh
gh api -X PATCH repos/$owner/$repo -f default_branch="$linear"
```

Read-back: `GET /repos/$owner/$repo` must report
`default_branch=pocketforge-linear`. Effective rules for `pocketforge-linear`
must now include the organization pull-request rule and the four repository
rules. Refuse any later mutation until both checks pass.

Rollback: only before a pull request or default-branch-derived automation has
used the new default, call
`gh api -X PATCH repos/$owner/$repo -f default_branch="$archive"`, then read
back the repository and effective rules. If the new default has been used,
stop and design a forward repair instead of hiding history by switching back.

Remove only the now-duplicate local `pull_request` rule by replacing the full
ruleset through the update endpoint:

```sh
gh api -X PUT "repos/$owner/$repo/rulesets/$linear_ruleset_id" --input -
```

The input must preserve active enforcement, no bypass actors, the exact branch
condition, deletion, non-fast-forward, strict required status checks, and
required linear history. Repository ruleset updates use `PUT`; `PATCH` returns
HTTP 404 and must not be interpreted as proof that the ruleset is absent.

Read-back: the full ruleset JSON must contain exactly those four rule types;
effective rules must still contain `pull_request` from organization ruleset
18506023. Confirm strict=true and both check contexts again.

Rollback: restore `linear_ruleset_id` with the complete temporary JSON from
step 2 using
`gh api -X PUT "repos/$owner/$repo/rulesets/$linear_ruleset_id" --input -`,
then read back both the full and effective rules.

Create a second repository ruleset:

```json
{
  "name": "Lock pocketforge archive",
  "target": "branch",
  "enforcement": "active",
  "bypass_actors": [],
  "conditions": {"ref_name": {"include": ["refs/heads/pocketforge"], "exclude": []}},
  "rules": [{"type": "update"}, {"type": "deletion"}]
}
```

Record its returned ID as `archive_ruleset_id`.

Read-back: fetch the full ruleset and effective rules for `pocketforge`; require
active update and deletion rules with no bypass. Verify the archived ref is
still exactly its before-state SHA.

Rollback: while old ruleset 24422440 remains active, delete only
`archive_ruleset_id`, read back its absence, and verify `pocketforge` is still
at the captured SHA.

After the archive lock is proven active, retire repository ruleset 24422440.
Preserve its full before-state JSON in the receipt.

Read-back: the repository ruleset list must omit 24422440; effective rules for
`pocketforge` must still show update and deletion from `archive_ruleset_id`.

Rollback: recreate ruleset 24422440 from its captured semantic fields (name,
target, enforcement, bypass actors, conditions, and rules), record the new ID,
and read it back. IDs are server-assigned and need not match.

## 4. Replay pull request 7

PR 7's existing head contains merge ancestry and cannot be fixed by retargeting.
Its owning lane must save the old head SHA and create a temporary backup ref,
then replay the PR's non-merge commits in original order onto the current
`pocketforge-linear`. To keep PR 7 itself, the owner must update its feature ref
with an explicit lease against the captured old head; no other actor may mutate
that lane.

Before update, prove the new chain has no merge commits, its ordered stable
patch IDs equal the intended PR series, and its final tree equals a clean replay.
Use `--force-with-lease=<head-ref>:<captured-old-head>` only on PR 7's feature
ref. This exception never applies to `pocketforge`, `pocketforge-linear`, or any
default branch.

Read-back: `GET /repos/pocketforge-os/gamescope/pulls/7` must retain PR number 7,
report base `pocketforge-linear`, and report the exact replayed head. The compare
API must show `behind_by=0`; `git rev-list --merges base..head` must be empty.
Run CI at that exact head, perform a new exact-head review, and merge only with:

```sh
pf-automation-tool pf-pr-merge.sh pocketforge-os/gamescope#7
```

The gate must print the old-to-new SHA map. Read back PR state `MERGED` and its
merge commit. Verify target history has no merge in the admitted range, the
post-merge ordered patch IDs equal the reviewed series, and the final tree equals
the reviewed head tree.

Rollback before merge: restore the saved feature head with an explicit lease
against the replayed head, retarget PR 7 to `pocketforge`, and delete the backup
ref only after both read-backs pass. Rollback after merge: never rewrite the
default branch; use a new reviewed linear revert PR and retain all receipts.

## 5. After-state receipt

Create a new `gamescope-after-<UTC timestamp>.txt` beside the before receipt.
Capture the same surfaces plus PR 7, its merge result, the merge tool's SHA map,
and local ancestry/tree/patch-ID proofs.

Read-back: compare before and after. Only the documented workflow commit,
default branch, named rulesets, and PR 7 replay/merge may differ.

Rollback: each mutation uses its step-specific rollback above. Never use a
broad restoration script or a force update of either long-lived branch.
