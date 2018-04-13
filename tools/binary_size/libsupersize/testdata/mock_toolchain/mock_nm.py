# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys


_SHRINK_TO_FIT_CLONE = ('blink::ContiguousContainerBase::shrinkToFit() '
                        '[clone .part.1234] [clone .isra.2]')
_ELF_OUTPUT = """002b6e20 t $t
00000010 N
002b6bb8 t $t
002a0010 t {}
0028d900 t startup._GLOBAL__sub_I_page_allocator.cc
002a0010 t FooAlias()
002b6bb8 t $t.23
002a0010 t BarAlias()
002a0000 t blink::ContiguousContainerBase::shrinkToFit()
002a0000 t BazAlias(bool)
002b6bb8 t $t.22
""".format(_SHRINK_TO_FIT_CLONE)

_SHRINK_TO_FIT = ('blink::ContiguousContainerBase::shrinkToFit() '
                  '[clone .part.1234] [clone .isra.2]')
_OBJECT_OUTPUTS = {
    'obj/third_party/icu/icuuc/ucnv_ext.o': [
        '01010101 t ' + _SHRINK_TO_FIT,
        '01010101 t _GLOBAL__sub_I_SkDeviceProfile.cpp',
        '01010101 t foo_bar',
        '002a0000 t BazAlias(bool)',
        '00000000 r .L.str',
        '00000005 r .L.str.1',
        '01010101 r vtable for ChromeMainDelegate',
        '01010101 r vtable for ChromeMainDelegate',
        '01010101 r vtable for chrome::mojom::FieldTrialRecorder',
        ('01010101 t ucnv_extMatchFromU(int const*, int, unsigned short const*,'
         ' int, unsigned short const*, int, unsigned int*, signed char, signed '
         'char)'),
    ],
    'obj/third_party/WebKit.a': [
        '',
        'PaintChunker.o:',
        '01010101 t ' + _SHRINK_TO_FIT,
        '010101 t (anonymous namespace)::kAnimationFrameTimeHistogramClassPath',
        '010101 r vtable for ChromeMainDelegateAndroid',
        ('01010101 r blink::(anonymous namespace)::CSSValueKeywordsHash::findVa'
         'lueImpl(char const*, unsigned int)::value_word_list'),
        '',
        'ContiguousContainer.o:',
        '01010101 d chrome::mojom::FilePatcher::Name_',
        '01010101 r vtable for chrome::mojom::FieldTrialRecorderProxy',
        '01010101 r google::protobuf::internal::pLinuxKernelMemoryBarrier',
        '01010101 r base::android::kBaseRegisteredMethods',
        ('01010101 r base::android::(anonymous namespace)::g_renderer_histogram'
         '_code'),
        ('01010101 r base::android::(anonymous namespace)::g_library_version_nu'
         'mber'),
        ('01010101 t blink::ContiguousContainerBase::ContiguousContainerBase(bl'
         'ink::ContiguousContainerBase&&)'),
        ('01010101 t (anonymous namespace)::blink::PaintChunker::releasePaintCh'
         'unks() [clone .part.1]'),
    ],
    'obj/base/base/page_allocator.o': [
        '01010101 t _GLOBAL__sub_I_page_allocator.cc',
        '01010101 t _GLOBAL__sub_I_bbr_sender.cc',
        '01010101 t _GLOBAL__sub_I_pacing_sender.cc',
        '00000000 r .L.str',
        '01010101 t extFromUUseMapping(aj, int)',
        '01010101 t extFromUUseMapping(signed char, unsigned int, int)',
        '01010101 t Name',
        '01010101 v vtable for mojo::MessageReceiver',
        '01010101 r kMethodsAnimationFrameTimeHistogram',
        '01010101 r google::protobuf::internal::pLinuxKernelCmpxchg',
    ],
    'obj/third_party/ffmpeg/libffmpeg_internal.a': [
        '',
        'fft_float.o:',
        '01010101 b ff_cos_65536',
        '01010101 b ff_cos_131072',
        '002a0010 t FooAlias()',
        '002b6bb8 t $t',
        '002a0010 t BarAlias()',
        ''
        'fft_fixed.o:'
        '01010101 b ff_cos_131072_fixed',
    ],
    '../../third_party/gvr-android-sdk/libgvr_shim_static_arm.a': [
        '',
        'libcontroller_api_impl.a_controller_api_impl.o:'
        '01010101 d .Lswitch.table.45',
        '',
        'libport_android_jni.a_jni_utils.o:',
        '01010101 t (anonymous namespace)::kSystemClassPrefixes',
    ],
}

def _PrintHeader(path):
  sys.stdout.write('\n')
  sys.stdout.write(path + ':\n')


def _PrintOutput(path):
  if path.endswith(os.path.join('mock_output_directory', 'elf')):
    sys.stdout.write(_ELF_OUTPUT)
  else:
    lines = _OBJECT_OUTPUTS.get(os.path.normpath(path))
    assert lines, 'No mock_nm.py entry for: ' + path
    sys.stdout.write('\n'.join(lines))
    sys.stdout.write('\n')


def main():
  paths = [p for p in sys.argv[1:] if not p.startswith('-')]
  if len(paths) == 1:
    _PrintOutput(paths[0])
  else:
    for path in paths:
      _PrintHeader(path)
      _PrintOutput(path)


if __name__ == '__main__':
  main()
