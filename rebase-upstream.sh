#!/usr/bin/env bash

CURRENT_BRANCH=$(git branch --show-current)
git branch -D merge-upstream
git remote add upstream https://github.com/KhronosGroup/KTX-Software.git
git pull upstream "$1"
git checkout "upstream/$1"
git checkout -b merge-upstream
git cherry-pick setup
python3 git-filter-repo --paths-from-file wanted-files.txt --force
python3 git-filter-repo --paths-from-file unwanted-files.txt --invert-paths --force
git checkout "$CURRENT_BRANCH"
git rebase merge-upstream
