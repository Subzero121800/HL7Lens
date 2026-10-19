# Security and privacy

HL7Lens runs entirely offline: it reads the text open in Notepad++, sends nothing over any network and
stores no message content.

## Supported versions

Security fixes go into the latest release. Update with the one-command installer or the newest zip from
[Releases](https://github.com/Subzero121800/HL7Lens/releases).

## Reporting a problem

Please report security issues privately through GitHub's **Report a vulnerability** (Security tab) rather
than in a public issue.

## Patient data

**Never put real patient data in an issue, pull request or attachment.** Use made-up values or run the
message through *Plugins > HL7Lens > De-identified copy* and review the result before sharing.

## Verifying a download

Release binaries are Authenticode-signed by `CN=Joseph C McGinty Jr.`
(SHA-1 thumbprint `52C94A9DFEE33D25429354D2A7837EC227B665F4`) and every release zip has a `.sha256` file.
`install.ps1` checks both before it copies anything.
