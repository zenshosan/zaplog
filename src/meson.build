

conf_data = configuration_data()
conf_data.set('version', meson.project_version())
config_h = configure_file(
  input : 'zaplog_config.h.in',
  output : 'zaplog_config.h',
  configuration : conf_data
)

git_hash_h = vcs_tag(
  command : ['git', 'show', '--format=%h'],
  input : 'git_hash.h.in',
  output :'git_hash.h',
  fallback : ' ',
  replace_string : '@git_hash@'
)

git_date_h = vcs_tag(
  command : ['git', 'log', '-1', '--format=%cI'],
  input : 'git_date.h.in',
  output :'git_date.h',
  fallback : ' ',
  replace_string : '@git_date@'
)

message('**meson compiler id**:', meson.get_compiler('cpp').get_id())

if meson.get_compiler('cpp').get_id() == 'msvc'
  extra_args = ['/utf-8']
elif meson.get_compiler('cpp').get_id() == 'gcc'
  extra_args = ['-Wno-unused-variable', '-Wno-interference-size']
else
  extra_args = ['-Wno-unused-variable']
endif

lib_args = ['-DZAPLOG_BUILDING', '-DZAPLOG_BUILDING_SLIB'] + extra_args
slib_sources = [
  config_h,
  git_hash_h,
  git_date_h,
  'zaplog.cpp',
]

slib = static_library(
  'zaplog',
  sources : slib_sources,
  install : true,
  cpp_args : lib_args,
)

gtest_dep = dependency('gtest', required : true)
gtest_main_dep = dependency('gtest_main', required : true)


unit_test_args = ['-DZAPLOG_BUILDING', '-DZAPLOG_BUILDING_SLIB', '-DLIBPT_ENABLE_BUILD_TESTS'] + extra_args
unit_test_sources = [
  'pt/pf_bounded_spsc_zero_copy_test.cpp',
  'pt/pf_mpsc_ringbuffer_test.cpp'
]
unit_test_exe = executable(
  'unit_test',
  sources : unit_test_sources,
  dependencies : [ gtest_dep, gtest_main_dep, ],
  cpp_args : unit_test_args
)
test('unittest', unit_test_exe)


api_test_args = ['-DZAPLOG_BUILDING', '-DZAPLOG_BUILDING_SLIB' ] + extra_args
api_test_sources = [
  'zaplog_test.cpp',
]
api_test_exe = executable(
  'api_test',
  sources : api_test_sources,
  dependencies : [ gtest_dep, gtest_main_dep, ],
  link_with : slib,
  cpp_args : api_test_args)
test('apitest', api_test_exe)


# Make this library usable as a Meson subproject.
zaplog_dep = declare_dependency(
  include_directories: include_directories('.'),
  link_with : slib)

# Make this library usable from the system's
# package manager.
install_headers('zaplog.hpp', subdir : 'zaplog')

pkg_mod = import('pkgconfig')
pkg_mod.generate(
  name : 'zaplog',
  filebase : 'zaplog',
  description : 'Zaplog: A C++ logging library.',
  subdirs : 'zaplog',
  libraries : slib,
  version : meson.project_version(),
)
