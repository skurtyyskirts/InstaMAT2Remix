# Third-party components

## texconv.exe (DirectXTex)

Microsoft DirectXTex texture processing library — `texconv.exe`.
<https://github.com/microsoft/DirectXTex>

MIT License. Copyright (c) Microsoft Corporation.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Qt 6

This distribution includes the Qt 6 runtime libraries (Qt6Core, Qt6Gui,
Qt6Widgets, Qt6Network and platform plugins), used under the terms of the
GNU Lesser General Public License v3 (LGPL-3.0).
<https://www.qt.io/licensing/>

The Qt libraries are dynamically linked and unmodified. You may replace them
with your own builds of the same Qt version. Qt source code is available at
<https://code.qt.io/>.

## Microsoft Visual C++ Runtime

This distribution redistributes the Microsoft Visual C++ runtime DLLs
`vcruntime140.dll`, `vcruntime140_1.dll`, and `msvcp140.dll` (Visual C++
2015–2022 redistributable, x64), placed app-local next to
`InstaMAT2RemixExport.exe`. © Microsoft Corporation. Redistributed under the
redistribution rights granted to applications built with Microsoft Visual
Studio. <https://visualstudio.microsoft.com/license-terms/>

## xxHash

`InstaMAT2Remix/vendor/xxhash.h` — used by `InstaMAT2Duplicate` to generate
each duplicated material's fresh 64-bit identity (`XXH3_64bits`), included
inline via `XXH_INLINE_ALL` (no separate binary/link step).
<https://github.com/Cyan4973/xxHash>

BSD 2-Clause License. Copyright (C) 2012-2023 Yann Collet.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

## InstaMAT Plugin SDK

`InstaMATAPI.h` / `InstaMATPluginAPI.h` © InstaMaterial GmbH. Used for
building InstaMAT Studio plugins per the SDK's terms. Not redistributed in
this binary package.
