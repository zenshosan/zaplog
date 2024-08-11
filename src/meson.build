

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

lib_args = ['-DZAPLOG_BUILDING', '-DZAPLOG_BUILDING_SLIB']

slib_sources = [
  config_h,
  git_hash_h,
  git_date_h,
  'zaplog.cpp'
]

slib = static_library(
  'zaplog',
  sources : slib_sources,
  install : true,
  cpp_args : lib_args,
)

gtest_dep = dependency('gtest', required : true)
gtest_main_dep = dependency('gtest_main', required : true)

test_args = ['-DZAPLOG_BUILDING', '-DZAPLOG_BUILDING_SLIB']
test_exe = executable(
  'zaplog_test',
  'zaplog_test.cpp',
  dependencies : [ gtest_dep, gtest_main_dep, ],
  link_with : slib,
  cpp_args : test_args)
test('zaplog', test_exe)

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
  description : 'Meson sample project.',
  subdirs : 'zaplog',
  libraries : slib,
  version : '0.1',
)
