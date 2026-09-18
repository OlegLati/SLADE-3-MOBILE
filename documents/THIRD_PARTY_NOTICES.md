# Third-Party Notices

This project includes third-party software and source code. Each component remains subject to its own license and copyright notices.

> **Important:** This file is an index of the third-party components identified in the bundled source tree. It does not replace the license files distributed with those components. The original license/notice files should be preserved in their respective directories.

## 1. SLADE

**Component:** SLADE  
**License:** GNU General Public License, version 2 (GPL-2.0)  
**License file:** `app/src/main/cpp/third_party/SLADE/LICENSE`

The bundled SLADE source tree contains the upstream GPLv2 license text. SLADE is integrated into this project as a substantial third-party codebase.

Preserve the original `LICENSE` file and applicable copyright notices when redistributing the project.

## 2. fmt

**Component:** fmt  
**Version identified in bundled source:** 11.1.3  
**License:** MIT  
**License file:** `app/src/main/cpp/third_party/SLADE/thirdparty/fmt/LICENSE`

The bundled fmt headers identify version 11.1.3. The upstream project is distributed under the MIT license.

The original license and copyright notices must remain with the component.

## 3. LunaSVG

**Component:** LunaSVG  
**License:** MIT  
**License file:** `app/src/main/cpp/third_party/SLADE/thirdparty/lunasvg/LICENSE`

LunaSVG is distributed under the MIT license.

The original license and copyright notices must remain with the component.

## 4. sigslot

**Component:** sigslot  
**License:** MIT  
**License file:** `app/src/main/cpp/third_party/SLADE/thirdparty/sigslot/LICENSE`

sigslot is distributed under the MIT license.

The original license and copyright notices must remain with the component.

## 5. sol2

**Component:** sol2  
**Version identified in bundled source:** 3.5.0  
**License:** MIT  
**License notices:** License/header notices included in `app/src/main/cpp/third_party/SLADE/thirdparty/sol/`

The bundled sol2 headers identify version 3.5.0 and contain MIT license notices.

The original license and copyright notices must remain with the component.

## 6. DUMB

**Component:** DUMB (DUMB 0.9.3)  
**License:** DUMB custom permissive license  
**License file:** `app/src/main/cpp/third_party/SLADE/thirdparty/dumb/licence.txt`

The bundled DUMB source contains its own license text (`licence.txt`). This is not an MIT, BSD, or GPL license; it is the project's historical custom permissive license.

Redistribution and modification must follow the conditions stated in the original `licence.txt`. In particular, do not remove or alter the license notice and clearly mark modified source versions.

## 7. LZMA SDK

**Component:** LZMA SDK  
**License:** Public domain (as stated by the bundled SDK notice)  
**License/notice file:** `app/src/main/cpp/third_party/SLADE/thirdparty/lzma/lzma.txt`

The bundled LZMA SDK notice states that the SDK code is placed in the public domain by Igor Pavlov. The notice also identifies portions based on public-domain PPMd and SHA-256 code.

Keep the original `lzma.txt` notice with the bundled source.

## 8. GLAD / Khronos Platform Headers

**Component:** GLAD generated OpenGL loader and Khronos platform headers  
**GLAD version/generator identified in bundled source:** glad 0.1.35  
**Location:** `app/src/main/cpp/third_party/SLADE/thirdparty/glad/`

The bundled `glad.c` and `glad.h` identify generated code from glad 0.1.35. The tree also contains `KHR/khrplatform.h`, which carries Khronos copyright and license/permission notices.

Because generated GLAD code and Khronos platform headers have distinct notices, all existing headers and license/notice text in this directory should be preserved.

Do not remove the copyright or permission notices from the GLAD/Khronos files.

## License Preservation Policy

For redistribution of this repository:

1. Keep every third-party license and notice file in its original component directory.
2. Do not replace a component's license with the project's root license.
3. Do not remove copyright headers from third-party source files.
4. If third-party source is modified, retain the original notices and comply with the component's modification/redistribution terms.
5. Treat `app/src/main/cpp/third_party/SLADE/` as third-party source unless a file has been explicitly verified as project-owned code.
6. This document is an inventory/index, not a substitute for the actual license texts.

## Scope and Audit Note

This notice was prepared from the bundled `SLADE_3_MOBILE` source tree and its included license/notice files. It documents the components actually present in this project, rather than every dependency used by current upstream SLADE releases.

This is a software license inventory, not legal advice. Before publishing a public distribution with a new overall licensing scheme, especially where project-owned code is combined with GPL-covered SLADE code, review the final distribution and licensing terms with a qualified legal professional if necessary.
