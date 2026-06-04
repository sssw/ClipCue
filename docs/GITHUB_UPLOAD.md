# Uploading this project to GitHub

```powershell
git init
git add .
git commit -m "Initial PathCue engineering preview"
git branch -M main
git remote add origin https://github.com/<OWNER>/<REPO>.git
git push -u origin main
```

GitHub Actions will start automatically after push. Open the repository's **Actions** tab and download artifacts from the `windows-build` workflow.

## Optional release

After validating the artifact:

```powershell
git tag v0.1.0
git push origin v0.1.0
```

You can then attach the ZIP artifact to a GitHub Release manually or extend the workflow to publish releases.
