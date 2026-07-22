#!/usr/bin/env bash
# Dispatch all Release Candidate build workflows via the GitHub Actions API.
# Requires: gh CLI, authenticated with repo access.
set -euo pipefail

BRANCH="${1:-master}"

WORKFLOWS=(
  ccpp_mac_rc.yml
  ccpp_mac_arm_rc.yml
  ccpp_ubuntu_gtk2_rc.yml
  ccpp_ubuntu_gtk3_rc.yml
  ccpp_ubuntu_gtk3_arm_rc.yml
  ccpp_win_rc.yml
)

echo "Dispatching RC builds on branch '$BRANCH'..."
for wf in "${WORKFLOWS[@]}"; do
  echo "-> $wf"
  gh workflow run "$wf" --ref "$BRANCH"
done

echo
echo "Triggered. Recent runs:"
sleep 5
for wf in "${WORKFLOWS[@]}"; do
  gh run list --workflow "$wf" --branch "$BRANCH" --limit 1 \
    --json workflowName,status,url \
    --template '{{range .}}{{.workflowName}}: {{.status}} {{.url}}{{"\n"}}{{end}}'
done
