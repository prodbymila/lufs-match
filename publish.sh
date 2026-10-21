#!/bin/bash
# Publishes this folder as the public GitHub repo "lufs-match", waits for the Windows build,
# and drops the Windows package next to the Mac/Ableton ones in ../Distribucion.
# Run from Terminal:  ./publish.sh
set -euo pipefail
cd "$(dirname "$0")"

REPO=lufs-match
DIST="../Distribucion"
ARTIFACT=LUFS-Match_FL-Studio_Windows

# 1. Log in once (opens the browser). The workflow scope is needed to push .github/workflows.
# Without a terminal on stdin gh asks nothing: it prints the code (also copied to the
# clipboard) and waits while you authorize on the page opened below.
if ! gh auth status >/dev/null 2>&1; then
    echo "Paste the code on the GitHub page that is opening, then click Authorize."
    (sleep 2 && open "https://github.com/login/device") &
    gh auth login --hostname github.com --git-protocol https --web --scopes workflow </dev/null
fi
gh auth setup-git

# 2. Commit as your GitHub user with the private noreply address (your real email stays hidden).
login=$(gh api user --jq .login)
id=$(gh api user --jq .id)
[ -d .git ] || git init -q -b main
git config user.name "$login"
git config user.email "$id+$login@users.noreply.github.com"
git add -A
if ! git rev-parse -q --verify HEAD >/dev/null || ! git diff --cached --quiet; then
    git commit -q -m "LUFS Match VST3: loudness matcher with Tap/Match link and Windows CI

BS.1770-4 loudness matching (momentary, short-term, gated integrated), Tap/Match
instances linked in-process for hosts without a Pre-FX tap, JUCE 8 VST3, and a
GitHub Actions build that tests and packages the Windows VST3.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
fi

# 3. Create the public repo and push (only the first time).
if ! git remote get-url origin >/dev/null 2>&1; then
    gh repo create "$REPO" --public --source . --remote origin --push \
        --description "Loudness matcher VST3 (BS.1770 / LUFS) for FL Studio and other DAWs"
else
    git push -u origin main
fi

# 4. Wait for the Windows build of this commit, then download its package.
sha=$(git rev-parse HEAD)
echo "Waiting for the Windows build to start..."
for _ in $(seq 1 30); do
    run=$(gh run list --repo "$login/$REPO" --commit "$sha" --limit 1 --json databaseId --jq '.[0].databaseId // empty')
    [ -n "$run" ] && break
    sleep 5
done
[ -n "${run:-}" ] || { echo "The build did not start. Check https://github.com/$login/$REPO/actions"; exit 1; }
gh run watch "$run" --repo "$login/$REPO" --exit-status --interval 30

tmp=$(mktemp -d)
gh run download "$run" --repo "$login/$REPO" --name "$ARTIFACT" --dir "$tmp"
mkdir -p "$DIST"
(cd "$tmp" && ditto -c -k --norsrc --noextattr --keepParent "LUFS Match - FL Studio (Windows)" "$OLDPWD/$DIST/$ARTIFACT.zip")
rm -rf "$tmp"
echo
echo "Done: https://github.com/$login/$REPO"
echo "Windows package: $(cd "$DIST" && pwd)/$ARTIFACT.zip"
