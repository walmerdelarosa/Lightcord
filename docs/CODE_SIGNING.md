# Setting up release signing (maintainers)

Windows releases are signed through [SignPath](https://signpath.io) with a free certificate from
[SignPath Foundation](https://signpath.org). The public policy is in [CODE_SIGNING_POLICY.md](../CODE_SIGNING_POLICY.md).

## How it works

1. A push to `main` builds `Lightcord-windows-x64` (unsigned) as usual.
2. If that version has release notes and no GitHub release yet (`plan` job), and SignPath is configured,
   the `sign` job sends the artifact to SignPath and **waits up to 2 hours** for an approver to approve the request.
3. SignPath signs `Lightcord.exe`, the job checks the signature and the product name/version, and the
   `release` job publishes the signed package.
4. Without SignPath configured, the release is published unsigned (with a warning in the run).
   If signing is configured but fails or is rejected, nothing is published.

## One-time setup

1. **Apply** to the SignPath Foundation program (<https://signpath.org/apply>) with this repository.
   Requirements are listed at <https://signpath.org/terms>.
2. **Turn on two-factor authentication** on GitHub and on SignPath (required by the program).
3. In SignPath, once the project is created:
   - Connect the project to GitHub as a trusted build system.
   - Create or update the **artifact configuration** with the contents of
     [`.signpath/artifact-configuration.xml`](../.signpath/artifact-configuration.xml) and make it the default.
   - Check the slugs of the project and of the signing policy (usually `release-signing`).
   - Create an API token for a CI user that is allowed to submit requests to that policy.
4. In GitHub → *Settings → Secrets and variables → Actions*:

   | Type | Name | Value |
   | --- | --- | --- |
   | Secret | `SIGNPATH_API_TOKEN` | The SignPath API token. |
   | Variable | `SIGNPATH_ORGANIZATION_ID` | Your SignPath organization ID (from the dashboard URL). Signing is enabled when this exists. |
   | Variable (optional) | `SIGNPATH_PROJECT_SLUG` | Only if it is not `Lightcord`. |
   | Variable (optional) | `SIGNPATH_SIGNING_POLICY_SLUG` | Only if it is not `release-signing`. |
   | Variable (optional) | `SIGNPATH_ARTIFACT_CONFIGURATION_SLUG` | Only to use a configuration other than the project's default. |

5. When the application is approved, remove the "application pending" notice from `CODE_SIGNING_POLICY.md`.

## Releasing

Bump `project(VERSION)` in `CMakeLists.txt` (the Windows version resource follows it automatically), add
`docs/release-notes/v<version>.md` and push to `main`. Then approve the signing request in SignPath.

Signing does not remove SmartScreen warnings at once: SmartScreen builds reputation for the publisher and the
file from download history, so the first downloads of a new release can still show a warning.
