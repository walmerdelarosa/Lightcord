# Code signing policy

> **Status: application pending.** Lightcord has applied to the SignPath Foundation open-source
> program. Until it is approved, Windows releases are published unsigned.

Once approved, Windows releases of Lightcord use free code signing provided by
[SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

## What is signed

- Only `Lightcord.exe`, which is built from this repository by the
  [Build workflow](https://github.com/walmerdelarosa/Lightcord/actions/workflows/build.yml) on GitHub-hosted runners.
- Third-party libraries shipped next to it (Qt and the Microsoft Visual C++ runtime) are distributed as published
  by their authors and are not signed with Lightcord's certificate.
- Every signed file carries the product name `Lightcord` and the version of the release.
- Releases are signed only after a manual approval in SignPath. Development builds (workflow artifacts) are never signed.

## Team roles

| Role | Members |
| --- | --- |
| Committers and reviewers | [@walmerdelarosa](https://github.com/walmerdelarosa) |
| Approvers | [@walmerdelarosa](https://github.com/walmerdelarosa) |

Changes from people outside the team (pull requests) are reviewed by a team member before they are merged.
All team members use multi-factor authentication on GitHub and SignPath.

## Privacy policy

Lightcord does not collect telemetry and has no servers of its own. It connects only to Discord
(`discord.com`, Discord's gateway and Discord's voice servers), sending the data needed to use your account:
your token, the messages you write and, during calls, your encrypted voice.
Your token is stored on your computer, in the Windows Credential Manager. Lightcord does not transfer any
information to other networked systems unless specifically requested by the user.

## Verifying a download

Each release includes a `.sha256` file. Once signed releases start, you can also check the signature:
right-click `Lightcord.exe` → *Properties* → *Digital Signatures*. The signer must be **SignPath Foundation**.
