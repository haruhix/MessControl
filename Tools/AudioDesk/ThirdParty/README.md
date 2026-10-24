# Bundled runtime notices

Audio Desk runs a packaged Python interpreter and invokes FFmpeg/ffprobe as separate replaceable programs. The application does not require these tools to be installed system-wide.

- Python 3.14.2, CPython: the interpreter's complete license and third-party notices are retained in `Python-LICENSE.txt`. Source: https://www.python.org/downloads/release/python-3142/ .
- PyInstaller 6.19.0: its bootloader is distributed under the terms and application-distribution exception in `PyInstaller-COPYING.txt`. Source: https://github.com/pyinstaller/pyinstaller/tree/v6.19.0 .
- FFmpeg/ffprobe `n9.0.2-35-g9a9b70bb8c-20261024`, Windows x86-64 LGPL shared build by BtbN. This builder is linked by the FFmpeg project's Windows download page: https://ffmpeg.org/download.html . Upstream build: https://github.com/BtbN/FFmpeg-Builds/releases/tag/latest .

The downloaded FFmpeg archive was `ffmpeg-n9.0-latest-win64-lgpl-shared-9.0.zip`, SHA-256 `5a204635723d3c28365aec847b79649f340873d8cc4bde632c619187c17992fd`. The archive was verified before packaging. Its LGPL v3 notice is retained in `FFmpeg-LGPL.txt`, with the referenced GPL v3 terms in `FFmpeg-GPLv3.txt`.

FFmpeg corresponding source at commit `9a9b70bb8c` is retained in `ffmpeg-corresponding-source.tar.gz`. The build recipes and patches are retained in `ffmpeg-build-recipes.tar.gz`; their exact source revision and archive hashes are recorded in `ffmpeg-source-manifest.json`. The recipes identify the dependencies and their source locations. Original source endpoints:

- https://github.com/FFmpeg/FFmpeg/archive/9a9b70bb8c.tar.gz
- https://github.com/BtbN/FFmpeg-Builds

The media executables and shared libraries remain in the application's `media` directory after extraction and can be replaced with compatible builds. Audio Desk does not modify them. Build metadata and checksums for the distributed runtime are recorded in `runtime-manifest.json` beside the launcher.

Audio licenses and creator credits are documented separately in `../THIRD_PARTY_AUDIO.md` and inside `library.zip`.
