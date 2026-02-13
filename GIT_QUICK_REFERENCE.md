# Git Quick Reference for JesFs

## Quick Answer: Merging Codex/Copilot Branch to Main

**The command to apply the current committed branch from Codex to your main branch:**

```bash
git checkout main
git merge codex/your-branch-name
git push origin main
```

---

## Common Git Commands for JesFs Development

### Checking Your Current Status
```bash
# See which branch you're on and what's changed
git status

# View recent commits
git log --oneline -10

# See all branches
git branch -a
```

### Switching Branches
```bash
# Switch to main branch
git checkout main

# Switch to a specific branch
git checkout branch-name

# Create and switch to a new branch
git checkout -b new-branch-name
```

### Merging Branches
```bash
# Merge a feature branch into main
git checkout main
git merge feature-branch-name

# Merge with a commit message
git merge feature-branch-name -m "Merge feature-branch-name"

# Abort a merge if conflicts are too complex
git merge --abort
```

### Updating Your Branch
```bash
# Pull latest changes from remote
git pull origin main

# Pull from upstream (if configured)
git pull upstream main

# Fetch changes without merging
git fetch origin
```

### Committing Changes
```bash
# Stage all changes
git add .

# Stage specific files
git add file1.c file2.h

# Commit with message
git commit -m "Your commit message"

# Amend last commit
git commit --amend
```

### Pushing Changes
```bash
# Push to your fork
git push origin branch-name

# Push current branch
git push

# Force push (use with caution!)
git push -f origin branch-name
```

### Working with Remotes
```bash
# View remotes
git remote -v

# Add upstream remote
git remote add upstream https://github.com/joembedded/JesFs.git

# Fetch from upstream
git fetch upstream
```

### Undoing Changes
```bash
# Discard changes in a file
git checkout -- filename

# Unstage a file
git reset HEAD filename

# Revert last commit (creates new commit)
git revert HEAD

# Reset to a specific commit (careful!)
git reset --hard commit-hash
```

### Viewing Differences
```bash
# See uncommitted changes
git diff

# See changes in staged files
git diff --staged

# Compare two branches
git diff main..feature-branch
```

### Cherry-Picking Commits
```bash
# Apply a specific commit to current branch
git cherry-pick commit-hash

# Apply multiple commits
git cherry-pick commit1 commit2 commit3
```

---

## Specific Workflows for JesFs

### Workflow 1: Merge Copilot/Codex Branch to Main
```bash
# Step 1: Make sure you're on the codex branch with all changes committed
git status

# Step 2: Switch to main
git checkout main

# Step 3: Pull latest changes
git pull origin main

# Step 4: Merge the codex branch
git merge copilot/your-branch-name

# Step 5: Resolve conflicts if any, then push
git push origin main
```

### Workflow 2: Create Feature Branch, Make Changes, Merge Back
```bash
# Create feature branch
git checkout -b feature/new-feature

# Make your changes, then commit
git add .
git commit -m "Add new feature"

# Switch to main and merge
git checkout main
git merge feature/new-feature

# Push to remote
git push origin main
```

### Workflow 3: Sync Fork with Upstream
```bash
# Fetch upstream changes
git fetch upstream

# Switch to main
git checkout main

# Merge upstream changes
git merge upstream/main

# Push to your fork
git push origin main
```

### Workflow 4: Create Pull Request from Feature Branch
```bash
# Push your feature branch to GitHub
git push origin feature/your-feature

# Then go to GitHub and create a Pull Request
# Navigate to: https://github.com/joembedded/JesFs
# Click "Pull requests" > "New pull request"
# Select your fork and branch, then submit
```

---

## Troubleshooting

### Merge Conflicts
```bash
# When you see merge conflicts:
# 1. Open the conflicting files
# 2. Look for conflict markers: <<<<<<<, =======, >>>>>>>
# 3. Edit the files to resolve conflicts
# 4. Stage the resolved files
git add resolved-file.c

# 5. Complete the merge
git commit -m "Resolved merge conflicts"
```

### Accidentally Committed to Wrong Branch
```bash
# If you committed to main instead of a feature branch:
# 1. Create a new branch from current state
git branch feature/new-branch

# 2. Reset main to previous state
git reset --hard HEAD~1

# 3. Switch to the new branch
git checkout feature/new-branch
```

### Need to Undo Last Commit (Not Pushed Yet)
```bash
# Undo commit but keep changes
git reset --soft HEAD~1

# Undo commit and discard changes
git reset --hard HEAD~1
```

---

## Additional Resources

- **Full Contributing Guide:** See [CONTRIBUTING.md](CONTRIBUTING.md)
- **GitHub Help:** https://docs.github.com/en/get-started
- **Git Documentation:** https://git-scm.com/doc

---

**For more detailed information about contributing to JesFs, please see [CONTRIBUTING.md](CONTRIBUTING.md)**
