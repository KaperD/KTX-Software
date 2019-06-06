##
# @internal
# @copyright © 2015, Mark Callow. For license see LICENSE.md.
#
# @brief Generate project to build KTX library for OpenGL.
#
{
  'variables': {
    'sources': [
      # .h files are included so they will appear in IDEs' file lists.
      '../include/ktx.h',
      'checkheader.c',
      'dfdutils/createdfd.c',
      'dfdutils/dfd.h',
      'dfdutils/dfd4vkformat.c',
      'dfdutils/printdfd.c',
      'dfdutils/vkdfdswitchbody.inl',
      'errstr.c',
      'etcdec.cxx',
      'etcunpack.cxx',
      'filestream.c',
      'filestream.h',
      'gl_format.h',
      'gl_funcptrs.h',
      'gles1_funcptrs.h',
      'gles2_funcptrs.h',
      'gles3_funcptrs.h',
      'glloader.c',
      'hashlist.c',
      'hashtable.c',
      'ktxgl.h',
      'ktxint.h',
      'memstream.c',
      'memstream.h',
      'stream.h',
      'swap.c',
      'texture.c',
      'uthash.h',
      'vkformat_enum.h',
      'writer.c',
      'writer_apiv1.c',
      'writer_v2.c',
    ],
    # Use _files to get the names relativized
    'vksource_files': [
      '../include/ktxvulkan.h',
      'vk_format.h',
      'vkloader.c',
      'vk_funclist.inl',
      'vk_funcs.c',
      'vk_funcs.h'
    ],
    'include_dirs': [
      '../include',
      '../other_include',
    ],
  }, # variables

  'includes': [
      '../gyp_include/libgl.gypi',
      '../gyp_include/libvulkan.gypi',
  ],
  'targets': [
    {
      'target_name': 'libktx.gl',
      'type': '<(library)',
      'cflags': [ '-std=c99' ],
      'defines': [ 'KTX_OPENGL=1' ],
      'direct_dependent_settings': {
         'include_dirs': [ '<@(include_dirs)' ],
      },
      'include_dirs': [ '<@(include_dirs)' ],
      'mac_bundle': 0,
      'dependencies': [ 'vulkan_headers' ],
      'sources': [
        '<@(sources)',
        '<@(vksource_files)',
      ],
      'conditions': [
        ['_type == "shared_library"', {
          'dependencies': [ 'libgl', 'libvulkan.lazy' ],
          'conditions': [
            ['OS == "mac" or OS == "ios"', {
              'direct_dependent_settings': {
                'target_conditions': [
                  ['_mac_bundle == 1', {
#                    'actions': [{
#                      # This could potentially break non-bundle apps built as
#                      # part of the same project. At present those are in
#                      # separate projects. However using @rpath as the install
#                      # name of a dylib installed in /usr/local/lib does work
#                      # - currently. The reason for doing this instead of
#                      # always setting INSTALL_PATH for the library to @rpath
#                      # is so library installation via xcodebuild install will
#                      # put it in the right place.
#                      'action_name': 'Change libktx.dylib "install name".',
#                      'inputs': [ '<(PRODUCT_DIR)/<(_target_name)<(SHARED_LIB_SUFFIX)' ],
#                      # Input & output are the same file. If set "outputs", the
#                      # build fails with "Invalid task with mutable output but
#                      # no other virtual output node". So use just a space.
#                      'outputs': [ ' ' ],
#                      'action': [
#                        'install_name_tool', '-change',
#                        '/usr/local/lib', '@rpath',
#                        '<@(_inputs)',
#                      ],
#                    }], # actions
                    'copies': [{
                      'xcode_code_sign': 1,
                      'destination': '<(PRODUCT_DIR)/$(FRAMEWORKS_FOLDER_PATH)',
                      'files': [ '<(PRODUCT_DIR)/<(_target_name)<(SHARED_LIB_SUFFIX)' ],
                    }], # copies
                    'xcode_settings': {
                      # Tell DYLD where to search for this dylib.
                      # "man ld" for more information. Look for -rpath.
                      'LD_RUNPATH_SEARCH_PATHS': [ '@executable_path/../Frameworks' ],
                    },
                  }, {
                    'xcode_settings': {
                      'LD_RUNPATH_SEARCH_PATHS': [ '@executable_path' ],
                    },
                  }], # _mac_bundle == 1
                ], # target_conditions
              }, # direct_dependent_settings
              'sources!': [
                'vk_funclist.inl',
                'vk_funcs.c',
                'vk_funcs.h',
              ],
              'xcode_settings': {
                # Set the "install name" so dyld will not refuse to load a
                # bundle's dylib when it finds it along the path set above.
                'INSTALL_PATH': '@rpath',
              }
            }, 'OS == "linux"', {
              'defines': [ 'KTX_USE_FUNCPTRS_FOR_VULKAN' ],
              'dependencies!': [ 'libvulkan.lazy' ],
            }] # OS == "mac or OS == "ios"
          ], # conditions
        }] # _type == "shared_library"
      ], # conditions
    }, # libktx.gl target
    {
      'target_name': 'libktx.es1',
      'type': 'static_library',
      'cflags': [ '-std=c99' ],
      'defines': [ 'KTX_OPENGL_ES1=1' ],
      'direct_dependent_settings': {
        'include_dirs': [ '<@(include_dirs)' ],
      },
      'sources': [ '<@(sources)' ],
      'include_dirs': [ '<@(include_dirs)' ],
    }, # libktx.es1
    {
      'target_name': 'libktx.es3',
      'type': 'static_library',
      'cflags': [ '-std=c99' ],
      'defines': [ 'KTX_OPENGL_ES3=1' ],
      'dependencies': [ 'vulkan_headers' ],
      'direct_dependent_settings': {
         'include_dirs': [ '<@(include_dirs)' ],
      },
      'sources': [
        '<@(sources)',
        '<@(vksource_files)',
      ],
      'include_dirs': [ '<@(include_dirs)' ],
    }, # libktx.es3
  ], # targets
  'conditions': [
    ['OS == "linux" or OS == "mac" or OS == "win"', {
      # Can only build doc and only need to generate source files on desktops
      'targets': [
        {
          'target_name': 'libktx.doc',
          'type': 'none',
          'variables': {
            'variables': { # level 2
              'output_dir': '../build/docs',
            },
            'output_dir': '<(output_dir)',
            'doxyConfig': 'libktx.doxy',
            'timestamp': '<(output_dir)/.libktx_gentimestamp',
          },
          'actions': [
            {
              'action_name': 'buildLibktxDoc',
              'message': 'Generating libktx documentation with Doxygen',
              'inputs': [
                '../<(doxyConfig)',
                '../runDoxygen',
                '../lib/mainpage.md',
                '../LICENSE.md',
                '../TODO.md',
                '<@(sources)',
                '<@(vksource_files)',
              ],
              # If other partial Doxygen outputs are included, e.g.
              # (<(output_dir)/html/libktx), CMake's make generator
              # on Linux (at least), makes timestamp dependent on
              # those other outputs. If those outputs exist, then
              # neither timestamp nor the document is updated.
              'outputs': [ '<(timestamp)' ],
              # doxygen must be run in the top-level project directory
              # so that ancestors of that directory will be removed
              # from paths displayed in the documentation. That is
              # the directory where the .doxy and .gyp files are stored.
              #
              # With Xcode, the current directory during project
              # build is one we need so we're good to go. However
              # we need to spawn another shell with -l so the
              # startup (.bashrc, etc) files will be read.
              #
              # With MSVS the working directory will be the
              # location of the vcxproj file. However when the
              # action is using bash ('msvs_cygwin_shell': '1',
              # the default, is set) no path relativization is
              # performed on any command arguments. If forced, by
              # using variable names such as '*_dir', paths will be
              # made relative to the location of the .gyp file.
              #
              # A setup_env.bat file is run before the command.
              # Apparently that .bat file is expected to be in the
              # same location as the .gyp and to cd to
              # its directory. That makes things work.
              #
              # Note that the same setup_env.bat is run by
              # rules but rules relativize paths to the vcxproj
              # location so cd to the .gyp home breaks rules.
              # Therefore in rules set 'msvs_cygwin_shell': '0.
              #
              # If using cmd.exe ('msvs_cygwin_shell': '0')
              # the MSVS generator will relativize to the vcxproj
              # location *all* command arguments, that do not look
              # like options.
              #
              # With `make`, cmake, etc, like Xcode,  the current
              # directory during project build is the one we need.
              'msvs_cygwin_shell': 1,
              'action': [
                './runDoxygen',
                '-t', '<(timestamp)',
                '-o', '<(output_dir)/html',
                '<(doxyConfig)',
              ],
            }, # buildLibktxDoc action
          ], # actions
        }, # libktx.doc
# Comment out temporarily because cmake clean and removes
# vkformat_enum.h but doesn't build it, if the other 2 outputs
# are not present. Reinstate when those files are in the tree.
#        {
#          'target_name': 'mkvkformatfiles',
#          'type': 'none',
#          'variables': {
#            'vkformatfiles_dir': '.',
#            'conditions': [ 
#              ['GENERATOR == "cmake"', {
#                # FIXME Need to find a way to use $VULKAN_SDK *if* set.
#                'vkinclude_dir': '/usr/include',
#              }, {
#                'vkinclude_dir': '$(VULKAN_SDK)/include',
#              }],
#            ], # conditions
#          },
#          'actions': [
#            {
#              'action_name': 'run_mkvkformatfiles',
#              'message': 'Generating VkFormat-related source files',
#              'inputs': [
#                '<(vkinclude_dir)/vulkan/vulkan_core.h',
#                'mkvkformatfiles',
#              ],
#              'outputs': [
#                'vkformat_enum.h',
#                'vkformat_prohibited.c',
#                'vkformat_str.c',
#              ],
#              # The current directory during project is that of
#              # the .gyp file. See above. Hence the annoying "lib/"
#              'msvs_cygwin_shell': 1,
#              'action': [
#                'lib/mkvkformatfiles', '<(vkformatfiles_dir)',
#              ],
#            }, # run mkvkformatfiles action
#            {
#              'action_name': 'run_makevkswitch',
#              'message': 'Generating VkFormat/DFD switch body',
#              'inputs': [
#                'vkformat_enum.h',
#                'dfdutils/makevkswitch.pl',
#              ],
#              'outputs': [
#                'dfdutils/vkdfdswitchbody.inl',
#              ],
#              # The current directory during this action is that of
#              # the .gyp file. See above. Hence the annoying "lib/"
#              'msvs_cygwin_shell': 1,
#              'action': [
#                'lib/dfdutils/makevkswitch.pl',
#                '<@(_inputs)',
#                '<@(_outputs)',
#              ],
#            }, # run makevkswitch action
#          ], # actions
#        }, # mkvkformatfiles
      ], # targets
    }], # 'OS == "linux" or OS == "mac" or OS == "win"'
  ], # conditions
}

# vim:ai:ts=4:sts=4:sw=2:expandtab:textwidth=70
