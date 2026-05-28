# How to upload VitalScope to GitHub

A beginner-friendly guide to publishing this project on your own GitHub account.

## Prerequisites

- A GitHub account (free at https://github.com/signup)
- Git installed on your computer (https://git-scm.com/downloads)
- The `VitalScope_GitHub` folder downloaded and extracted

## Option A — Using GitHub Web Interface (easiest)

### Step 1 — Create a new repository

1. Go to https://github.com
2. Click the **+** icon in the top-right → **New repository**
3. Fill in:
   - **Repository name:** `vitalscope`
   - **Description:** "Portable ESP32 health monitor with HR, SpO₂, temperature, and ECG"
   - **Visibility:** Public
   - ⚠️ Do NOT check "Initialize this repository with a README" (we already have one)
4. Click **Create repository**

### Step 2 — Upload your files

1. On the empty repository page, click **uploading an existing file**
2. Drag the entire contents of `VitalScope_GitHub` folder into the upload area
   - You should see: `README.md`, `LICENSE`, `CONTRIBUTING.md`, `docs/`, `hardware/`, `src/`
3. Scroll down, add a commit message: "Initial commit"
4. Click **Commit changes**

✅ Done! Your project is now on GitHub.

## Option B — Using Git command line (recommended for future updates)

### Step 1 — Create the repository (same as Option A, step 1)

### Step 2 — Open a terminal and navigate to your project

```bash
cd path/to/VitalScope_GitHub
```

### Step 3 — Initialize Git and push

```bash
# Initialize git
git init

# Set your identity (only first time on this machine)
git config --global user.name "Your Name"
git config --global user.email "you@email.com"

# Add all files
git add .

# Commit
git commit -m "Initial commit: VitalScope ESP32 health monitor"

# Set main branch
git branch -M main

# Connect to your GitHub repo (replace YOUR_USERNAME)
git remote add origin https://github.com/YOUR_USERNAME/vitalscope.git

# Push
git push -u origin main
```

You'll be asked for GitHub credentials. Use a **Personal Access Token** instead of your password — generate one at https://github.com/settings/tokens (scope: `repo`).

## Step 3 — Verify and customize

1. Visit your repository: `https://github.com/YOUR_USERNAME/vitalscope`
2. Verify the README renders properly (the SVG images should display inline)
3. Add a description and topics:
   - Click the gear icon next to "About" on the right side
   - Add description: "ESP32 portable health monitor"
   - Add topics: `esp32`, `arduino`, `health-monitor`, `iot`, `ecg`, `pulse-oximeter`
4. Save changes

## Step 4 — Make it shine

### Add a project image to social previews

1. Go to repository **Settings** → scroll to **Social preview**
2. Upload `docs/images/hero-banner.svg` (convert to PNG first via any online converter)
3. This image shows up when people share the link on Twitter, Discord, etc.

### Add badges (optional)

The README already has some badges. To customize them, edit the top of `README.md`:

```markdown
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
```

You can generate more badges at https://shields.io

### Pin the repository to your profile

1. Go to your GitHub profile page
2. Click **Customize your pins**
3. Add VitalScope to feature it on your profile

## Future updates

When you make changes:

```bash
git add .
git commit -m "Description of what changed"
git push
```

The README and images update automatically on GitHub.

## Sharing your project

Once published, share the link:

- **LinkedIn:** Great for visibility to employers — write a post about what you built
- **Reddit:** r/arduino, r/esp32, r/electronics — communities love DIY health tech
- **Twitter/X:** Tag #esp32 #arduino #DIY
- **Hackaday.io:** Submit it as a project — they feature good DIY hardware

Good luck!
