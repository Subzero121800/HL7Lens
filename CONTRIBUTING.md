# Contributing

Bug reports are welcome in [Issues](https://github.com/Subzero121800/HL7Lens/issues). Questions, ideas and
"how do I" go in [Discussions](https://github.com/Subzero121800/HL7Lens/discussions). Please follow the
[code of conduct](CODE_OF_CONDUCT.md).

- Include the Notepad++ version and bitness (? > About Notepad++) and the HL7Lens version (Plugins > HL7Lens > About).
- Attach a **synthetic** message that shows the problem. Never real patient data.
- For pull requests: run `make test` (the native suite must pass) and build both `make x64` and `make x86`.
  Keep the core (`src/hl7.cpp`, `src/dict.cpp`) free of Windows dependencies so it stays testable on Linux.

By contributing you agree that your contribution is licensed under the GNU GPL v3 or later.
