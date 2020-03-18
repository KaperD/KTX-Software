<img src="https://www.khronos.org/assets/images/api_logos/khronos.svg" width="300"/>

The Official Khronos KTX Software Repository
---

| GNU/Linux, iOS & OSX |  Windows | Documentation | 
|----------------------| :------: | :-----------: |
| [![Build Status](https://travis-ci.org/KhronosGroup/KTX-Software.svg?branch=master)](https://travis-ci.org/KhronosGroup/KTX-Software) | [![Build status](https://ci.appveyor.com/api/projects/status/rj9bg8g2jphg3rc0/branch/master?svg=true)](https://ci.appveyor.com/project/msc-/ktx/branch/master) | [![Build status](https://codedocs.xyz/KhronosGroup/KTX-Software.svg)](https://codedocs.xyz/KhronosGroup/KTX-Software/) |

This is the official home of the source code
for the Khronos KTX library and tools.

KTX (Khronos Texture) is a lightweight file format textures for OpenGL<sup>®</sup>, Vulkan<sup>®</sup> and other GPU APIs. KTX files contain all the parameters needed for texture loading. A single file can contain anything from a simple base-level 2D texture through to a cubemap array texture with mipmaps. Textures can be stored in Basis Universal or any of the block-compressed formats supported by OpenGL family and Vulkan APIs and extensions or can be stored uncompressed. Basis Universal is a supercompressed block-compressed format that can be quickly transcoded to any GPU-supported format.

The software consists of:

- *libktx* - a small library of functions for writing and reading KTX
files, and instantiating OpenGL®, OpenGL ES™️ and Vulkan® textures
from them.
- *libktx.{js,wasm}* - Web assembly version of libktx and
Javascript wrapper.
- *msc\_basis\_transcoder.{js,wasm}* - Web assembly transcoder and
Javascript wrapper for Basis Universal format images. For use with KTX parsers written in Javascript.
- *ktx2check* - a tool for validating KTX Version 2 format files
- *ktx2ktx2* - a tool for converting a KTX Version 1 file to a KTX
Version 2 file.
- *ktxinfo* - a tool to display information about a KTX file in
human readable form.
- *ktxsc* - a tool to supercompress a KTX Version 2 file that
contains uncompressed images.
- *toktx* - a tool to create KTX files from PNG or Netpbm format images. It supports mipmap generation and encoding & supercompression to
Basis Universal format.

See the Doxygen generated [live documentation](https://github.khronos.org/KTX-Software/)
for API and tool usage information.

See [CONTRIBUTING](CONTRIBUTING.md) for information about contributing.

See [LICENSE](LICENSE.md) for information about licensing.

See [BUILDING](BUILDING.md) for information about building the code.

<!--
More information about KTX and links to tools that support it can be
found on the
[KTX page](http://www.khronos.org/opengles/sdk/tools/KTX/) of
the [OpenGL ES SDK](http://www.khronos.org/opengles/sdk) on
[khronos.org](http://www.khronos.org).
-->

If you need help with using the KTX library or KTX tools, please use the
[KTX forum](https://community.khronos.org/c/other-standards/ktx/).
To report problems use GitHub [issues](https://github.com/KhronosGroup/KTX/issues).

**IMPORTANT:** you **must** install the [Git LFS](https://github.com/github/git-lfs)
command line extension in order to fully checkout this repository after cloning. You
need at least version 1.1.

A few files have `$Date$` keywords. If you care about having the proper
dates shown or will be generating the documentation or preparing
distribution archives, you **must** follow the instructions below.

#### <a id="kwexpansion"></a>$Date$ keyword expansion

$Date$ keywords are expanded via a smudge & clean filter. To install
the filter, issue the following commands in the root of your clone.

On Unix (Linux, Mac OS X, etc.) platforms and Windows using Git for Windows'
Git Bash or Cygwin's bash terminal:

```bash
./install-gitconfig.sh
rm TODO.md include/ktx.h tools/toktx/toktx.cpp
git checkout TODO.md include/ktx.h tools/toktx/toktx.cpp
```

On Windows with the Command Prompt (requires `git.exe` in a directory
on your %PATH%):

```cmd
install-gitconfig.bat
del TODO.md include/ktx.h tools/toktx/toktx.cpp
git checkout TODO.md include/ktx.h tools/toktx/toktx.cpp 
```

The first command adds an [include] of the repo's `.gitconfig` to the
local git config file `.git/config`, i.e. the one in your clone of the repo.
`.gitconfig` contains the config of the "keyworder" filter. The remaining
commands force a new checkout of the affected files to smudge them with the
date. These two are unnecessary if you plan to edit these files.

