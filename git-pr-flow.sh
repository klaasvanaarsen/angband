#!/usr/bin/env bash
set -euo pipefail

###############################################################################
# Configuration
###############################################################################

# Allowed semantic prefixes (cross-project + systems/native)
ALLOWED_TYPES=(
  feat fix refactor perf docs test build chore style ci
  windows posix core ui input fs net
)

# Allowed scopes (extend as needed)
ALLOWED_SCOPES=(
  windows core ui input fs net build docs
)

DEFAULT_BASE_BRANCH=""
UPSTREAM_REMOTE="upstream"   # angband/angband
FORK_REMOTE="origin"         # your fork

###############################################################################
# Helpers
###############################################################################

die() { echo "error: $*" >&2; exit 1; }

join_by() { local IFS="$1"; shift; echo "$*"; }

detect_base_branch() {
  if git show-ref --verify --quiet refs/heads/main; then
    echo "main"
  elif git show-ref --verify --quiet refs/heads/master; then
    echo "master"
  else
    die "Cannot detect base branch (main/master)"
  fi
}

current_branch() {
  git branch --show-current
}

stored_title() {
  git config branch."$(current_branch)".pr-title || true
}

validate_pr_title() {
  local title="$1"
  local types scopes type scope

  types=$(join_by "|" "${ALLOWED_TYPES[@]}")
  scopes=$(join_by "|" "${ALLOWED_SCOPES[@]}")

  if ! echo "$title" | grep -Eq \
    "^(${types})(\(((${scopes}))\))?: [^[:space:]].+"; then
    die "Invalid PR title.
Expected: type(scope): subject
Allowed types: ${types}
Allowed scopes: ${scopes}"
  fi
}

slugify() {
  echo "$1" \
    | tr '[:upper:]' '[:lower:]' \
    | sed -E 's/[^a-z0-9]+/-/g' \
    | sed -E 's/^-+|-+$//g'
}

branch_from_title() {
  local title="$1"
  local type scope subject slug

  type=$(echo "$title" | sed -E 's/^([a-zA-Z]+).*/\1/')
  scope=$(echo "$title" | sed -nE 's/^[^(]+\(([^)]+)\):.*/\1/p')
  subject=$(echo "$title" | sed -E 's/^[^:]+:\s*//')

  slug=$(slugify "$subject")

  if [ -n "$scope" ]; then
    echo "${type}/${scope}-${slug}"
  else
    echo "${type}/${slug}"
  fi
}

require_staged_changes() {
  if git diff --cached --quiet; then
    die "Nothing staged"
  fi
}

local_branch_exists() {
  git show-ref --verify --quiet "refs/heads/$1"
}

remote_branch_exists() {
  git show-ref --verify --quiet "refs/remotes/$1/$2"
}

list_associated_branches() {
  local branch="$1"

  echo "Associated branches:"
  echo "  local:  $branch"

  if remote_branch_exists "$FORK_REMOTE" "$branch"; then
    echo "  remote: $FORK_REMOTE/$branch"
  else
    echo "  remote: <none>"
  fi
}

branch_sync_status() {
  local base="$UPSTREAM_REMOTE/$DEFAULT_BASE_BRANCH"
  local branch
  branch=$(current_branch)

  # Ensure we have up-to-date refs
  git fetch "$UPSTREAM_REMOTE" "$DEFAULT_BASE_BRANCH" >/dev/null 2>&1 || true

  # Output: "<behind> <ahead>"
  git rev-list --left-right --count "$base...$branch" 2>/dev/null \
    || echo "<error> <error>"
}

###############################################################################
# Commands
###############################################################################

cmd_new() {
  local title="$1"
  validate_pr_title "$title"

  local branch
  branch=$(branch_from_title "$title")

  git check-ref-format --branch "$branch"
  git checkout -b "$branch"

  git config branch."$branch".pr-title "$title"

  echo "Created and switched to branch: $branch"
}

cmd_commit() {
  local title
  title=$(stored_title)
  [ -n "$title" ] || die "No stored PR title on this branch"

  require_staged_changes
  git commit -m "$title"
}

cmd_squash() {
  local base title
  title=$(stored_title)
  [ -n "$title" ] || die "No stored PR title on this branch"

  base=$(git merge-base "${DEFAULT_BASE_BRANCH}" HEAD)

  git reset --soft "$base"
  git commit -m "$title"
}

cmd_pr_create() {
  local title branch
  title=$(stored_title)
  [ -n "$title" ] || die "No stored PR title on this branch"

  branch=$(current_branch)

  git push -u "$FORK_REMOTE" "$branch"

  gh pr create \
    --head "$FORK_REMOTE:$branch" \
    --base "$UPSTREAM_REMOTE:$DEFAULT_BASE_BRANCH" \
    --title "$title" \
    --body ""
}

cmd_cleanup() {
  local branch pr_state
  branch=$(current_branch)

  pr_state=$(gh pr view --json state -q .state 2>&1) || pr_state="<error $?: ${pr_state}>"
    
  [ "$pr_state" = "MERGED" ] || die "Cannot clean up branch. PR not merged or failed to get state. Details: $pr_state"

  git checkout "$DEFAULT_BASE_BRANCH"
  git pull "$UPSTREAM_REMOTE" "$DEFAULT_BASE_BRANCH"

  git branch -D "$branch"
  git push "$FORK_REMOTE" --delete "$branch" || true

  echo "Cleaned up branch and remote ref"
}

cmd_abandon() {
  local branch
  branch=$(current_branch)

  [ "$branch" != "$DEFAULT_BASE_BRANCH" ] \
    || die "Refusing to abandon base branch '$DEFAULT_BASE_BRANCH'"

  echo "Abandoning branch '$branch'"

  git checkout "$DEFAULT_BASE_BRANCH"
  git pull "$UPSTREAM_REMOTE" "$DEFAULT_BASE_BRANCH"

  if local_branch_exists "$branch"; then
    git branch -D "$branch"
  fi

  if remote_branch_exists "$FORK_REMOTE" "$branch"; then
    git push "$FORK_REMOTE" --delete "$branch" || true
  fi

  echo "Branch abandoned and cleaned up"
}

cmd_status() {
  local branch title pr_url pr_state
  local behind ahead sync_msg

  branch=$(current_branch)
  title=$(stored_title)
  echo "Branch:   $branch"
  echo "Title:    ${title:-<none>}"

  pr_url=$(gh pr view --json url -q .url 2>&1) || pr_url="<error $?: ${pr_url}>"
  pr_state=$(gh pr view --json state -q .state 2>&1) || pr_state="<error $?: ${pr_state}>"
  echo "PR URL:   ${pr_url:-<none>}"
  echo "PR State: ${pr_state:-<none>}"

  read -r behind ahead < <(branch_sync_status)
  if [[ "$behind" =~ ^[0-9]+$ && "$ahead" =~ ^[0-9]+$ ]]; then
    if (( behind == 0 && ahead > 0 )); then
      sync_msg="ahead by $ahead commit(s), ready to PR"
    elif (( behind > 0 && ahead == 0 )); then
      sync_msg="behind by $behind commit(s), rebase required"
    elif (( behind > 0 && ahead > 0 )); then
      sync_msg="diverged (ahead $ahead, behind $behind), rebase recommended"
    else
      sync_msg="no changes relative to base"
    fi
  else
    sync_msg="unable to determine (git error)"
  fi
  echo "Sync:     $sync_msg"

  list_associated_branches "$branch"

  if [ "$pr_state" = "MERGED" ]; then
    echo "Cleanup:  branch can be deleted safely"
  else
    echo "Cleanup:  use 'git-pr-flow abandon' to discard"
  fi
}

cmd_help() {
  cat <<EOF
Usage: git-pr-flow <command> [args]

Commands:
  new "<type(scope): subject>"   Create branch from PR title
  commit                         Commit staged changes using PR title
  squash                         Squash branch commits using PR title
  pr-create                      Push and create GitHub PR
  cleanup                        Delete branch after PR merged
  abandon                        Delete branch even if PR not merged
  status                         Show PR/branch status
  help                           Show this help

Examples:
  git-pr-flow new "feat(windows): open HTML documentation from Help menu"
  git add .
  git-pr-flow commit
  git-pr-flow squash
  git-pr-flow pr-create
  git-pr-flow status
  git-pr-flow cleanup
EOF
}

###############################################################################
# Entry point
###############################################################################

DEFAULT_BASE_BRANCH=$(detect_base_branch)

cmd="${1:-help}"
shift || true

case "$cmd" in
  new)        cmd_new "$*" ;;
  commit)     cmd_commit ;;
  squash)     cmd_squash ;;
  pr-create)  cmd_pr_create ;;
  cleanup)    cmd_cleanup ;;
  abandon)    cmd_abandon ;;
  status)     cmd_status ;;
  help|*)     cmd_help ;;
esac
