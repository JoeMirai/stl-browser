# Local validation

Tested on Ubuntu 26.04.1 / GNOME with Qt 5.15.18.

- ASCII and binary STL, binary header starting with `solid`, empty models,
  malformed/truncated files, numeric parse errors, and invalid binary counts.
- Natural filename sorting, uppercase `.STL`, and nonrecursive filtering.
- Arrow boundaries, rapid selection, error recovery, and window-close notification.
- Appearance/file cache invalidation, persistent settings, hover tooltips.
- A 1,000-file folder: only visible thumbnail images load; old icons release
  their images after scrolling away.
- Real Sonic models, including a 674,034-triangle head, render in all four shading modes.

With that head loaded and thumbnails complete, the installed app used about
171 MiB resident memory and 0% CPU over a five-second idle sample. The original
fstl used about 228 MiB resident memory on the same model during this session.
These are observations on this machine, not cross-platform resource guarantees.
The OpenGL driver and shared library pages are included in resident memory.

Transient parsing buffers are returned to the OS after model upload. Images
have a 32 MiB memory-cache limit and 200 MiB disk-cache limit; total application
RAM is not limited to those sizes.

The GUI suite runs on the desktop display and also with software OpenGL.
GitHub Actions builds on Ubuntu 24.04 and runs the same suite under Xvfb.
The optional local Sonic-fixture test skips on CI because model files are not
stored in this repository.
