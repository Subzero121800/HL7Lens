# Changelog

## Unreleased

### Added
- Installer: on PCs without admin rights (or when the Windows prompt is declined) it now saves the checked plugin
  and a `README-for-IT.txt` request note to `Downloads\HL7Lens-for-IT` instead of stopping with an error.
- README: install from the Windows Run box (Win+R), and a section for managed State, Federal and hospital PCs.

### Fixed
- Installer: folder names containing an apostrophe no longer break the elevated copy, and the installed DLL's
  signature is checked again after the copy.

## 1.1.0 - 2026-10-16

### Added
- **Compare two messages**: with another message in the same file, with the clipboard, or with the message
  in the other view. Segments are aligned by ID, values compared down to repetition and component;
  validation findings present in only one message are listed; MSH-7 and MSH-10 are reported apart as
  expected differences.
- **Version-aware definitions** for HL7 v2.1 to v2.7.1, chosen from MSH-12: field and component names,
  data types and repetition rules. Validation flags fields the declared version does not define and
  fields that repeat where the version allows one value.
- **Custom segments** defined in `settings.ini` (`[ZCT]`, `n=Name|TYPE|repeats`), applied as soon as the
  file is saved. *Define custom segments...* opens the file with an example.
- **Repeating fields**: repetition count in the tree, *Repetition n of m* in the readout, and next/previous
  field stop at every repetition.
- One-command PowerShell installer (`install.ps1`) with checksum and signature verification.
- *Support development* menu item and About button.

### Fixed
- After clicking a node in the tree, the arrow keys stayed with the tree, so the caret seemed to snap back
  a line. Focus now returns to the editor.
- The panel status line could read *No HL7 message* while the panel showed the message.

### Changed
- Licensed under the GNU GPL v3 or later.

## 1.0.1 - 2026-10-16

### Changed
- About box rebuilt as a dialog with author, version, copyright and the full notices.
- Labelled *Expand all* / *Collapse all* buttons; both also in the menu.

### Fixed
- About text was cut off at a fixed 2048-character buffer and ended in "Copy...".

## 1.0.0 - 2026-10-16

- First release: decoder panel, message tree, live readout, validation, version and ELR checks, navigation,
  reports, ACK, de-identification, repair, escape tools, syntax colors.
