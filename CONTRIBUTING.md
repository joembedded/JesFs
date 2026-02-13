# Contributing to JesFs

Thank you for your interest in contributing to JesFs! This document provides guidelines for contributing to the project and explains the git workflow used for managing changes.

## Table of Contents
- [Getting Started](#getting-started)
- [Git Workflow](#git-workflow)
- [Merging Feature Branches to Main](#merging-feature-branches-to-main)
- [Code Style](#code-style)
- [Testing](#testing)
- [Submitting Changes](#submitting-changes)

## Getting Started

1. Fork the repository on GitHub
2. Clone your fork locally:
   ```bash
   git clone https://github.com/YOUR_USERNAME/JesFs.git
   cd JesFs
   ```
3. Add the upstream repository:
   ```bash
   git remote add upstream https://github.com/joembedded/JesFs.git
   ```

## Git Workflow

JesFs uses a standard git workflow with feature branches. Here's how to work with branches:

### Creating a Feature Branch

```bash
# Make sure you're on the main branch
git checkout main

# Pull the latest changes
git pull upstream main

# Create a new feature branch
git checkout -b feature/your-feature-name
```

### Making Changes

1. Make your changes in your feature branch
2. Commit your changes with descriptive commit messages:
   ```bash
   git add .
   git commit -m "Add feature: brief description of what you changed"
   ```
3. Push your branch to your fork:
   ```bash
   git push origin feature/your-feature-name
   ```

## Merging Feature Branches to Main

### If You're Working on Your Own Fork

Once your feature is complete and tested, you'll want to merge it to the main branch. Here's the process:

#### Option 1: Using Git Merge (Recommended for Local Repositories)

```bash
# 1. Make sure all changes are committed in your feature branch
git add .
git commit -m "Final changes for feature"

# 2. Switch to the main branch
git checkout main

# 3. Pull the latest changes from upstream (if applicable)
git pull upstream main

# 4. Merge your feature branch into main
git merge feature/your-feature-name

# 5. Resolve any conflicts if they occur
# Edit conflicting files, then:
git add .
git commit -m "Merge feature/your-feature-name into main"

# 6. Push the updated main branch
git push origin main
```

#### Option 2: Using Pull Request (Recommended for Contributions)

For contributing to the main JesFs repository:

```bash
# 1. Push your feature branch to GitHub
git push origin feature/your-feature-name

# 2. Go to GitHub and create a Pull Request
# - Navigate to https://github.com/joembedded/JesFs
# - Click "Pull requests" > "New pull request"
# - Click "compare across forks"
# - Select your fork and feature branch
# - Click "Create pull request"
# - Fill in the description and submit
```

### Applying Changes from a Codex/Copilot Branch to Main

**To apply the current committed branch from Codex to your main branch, use one of these commands:**

#### Quick Answer:
```bash
# If you're currently on a codex/copilot branch and want to merge it to main:
git checkout main
git merge codex/your-branch-name
```

#### Detailed Steps:

1. **Verify you're on the codex branch and all changes are committed:**
   ```bash
   git status
   git log --oneline -5
   ```

2. **Switch to the main branch:**
   ```bash
   git checkout main
   ```

3. **Make sure your main branch is up to date:**
   ```bash
   git pull origin main
   # Or if you have upstream configured:
   git pull upstream main
   ```

4. **Merge the codex branch into main:**
   ```bash
   git merge codex/your-branch-name
   ```
   
   Or if you prefer a cleaner history without merge commits:
   ```bash
   git rebase codex/your-branch-name
   ```

5. **If there are conflicts, resolve them:**
   ```bash
   # Edit the conflicting files
   git add <resolved-files>
   git commit -m "Resolved merge conflicts"
   ```

6. **Push the updated main branch:**
   ```bash
   git push origin main
   ```

#### Alternative: Cherry-Pick Specific Commits

If you only want specific commits from the codex branch:

```bash
# View commits in the codex branch
git log codex/your-branch-name --oneline

# Cherry-pick specific commits
git checkout main
git cherry-pick <commit-hash>
git cherry-pick <another-commit-hash>

# Push the changes
git push origin main
```

### Common Scenarios

#### Scenario 1: Merge Copilot/Codex Branch to Main (Current Branch is Copilot/Codex)
```bash
git checkout main
git merge copilot/your-branch-name
# Or for codex branches: git merge codex/your-branch-name
git push origin main
```

#### Scenario 2: Apply Changes Without Switching Branches
```bash
# If you're on the codex branch and want to update main without switching
git fetch origin main:main
git checkout main
git merge codex/your-branch-name
git push origin main
```

#### Scenario 3: Squash All Commits into One
```bash
git checkout main
git merge --squash codex/your-branch-name
git commit -m "Merge all changes from codex branch"
git push origin main
```

## Code Style

JesFs follows these coding conventions:

- **C Code Style**: 
  - Use K&R style bracing
  - Indent with spaces (consistent with existing code)
  - Keep functions focused and concise
  - Comment complex algorithms

- **Naming Conventions**:
  - Functions: `fs_function_name()` (lowercase with underscores)
  - Structs: `STRUCT_NAME` (uppercase with underscores)
  - Variables: `variable_name` (lowercase with underscores)

## Testing

Before submitting changes:

1. **Build and test** on your target platform (nRF52, CC13xx/CC26xx, Windows, etc.)
2. **Verify** that existing functionality still works
3. **Test** new features thoroughly
4. **Check** for memory leaks and buffer overflows

### Platform-Specific Testing

- **Nordic nRF52**: Build with SES and SDK 17.1.0
- **TI CC13xx/CC26xx**: Build with CCS and TI-RTOS
- **Windows**: Compile and run the PC demo

## Submitting Changes

### Pull Request Checklist

Before submitting a pull request:

- [ ] Code builds without errors
- [ ] All tests pass
- [ ] Code follows the project's style guidelines
- [ ] Commit messages are clear and descriptive
- [ ] Documentation is updated if needed
- [ ] No unnecessary files are included (build artifacts, etc.)

### Pull Request Description

Include in your PR description:
- What changes were made
- Why these changes were necessary
- How to test the changes
- Any platform-specific considerations

## Questions?

If you have questions about contributing, please:
- Open an issue on GitHub
- Visit [joembedded.de](https://joembedded.de/)
- Check the [README.md](README.md) for more information

## License

By contributing to JesFs, you agree that your contributions will be licensed under the MIT License.
