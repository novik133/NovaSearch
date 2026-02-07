panel_sources = files(
  'src/main.c',
  'src/database.c',
)

# Dependencies
gtk3_dep = dependency('gtk+-3.0', version: '>= 3.22', required: get_option('panel'))
xfce4panel_dep = dependency('libxfce4panel-2.0', version: '>= 4.12', required: get_option('panel'))
sqlite3_dep = dependency('sqlite3', version: '>= 3.0', required: get_option('panel'))
keybinder_dep = dependency('keybinder-3.0', required: get_option('panel'))

if not gtk3_dep.found() or not xfce4panel_dep.found() or not sqlite3_dep.found() or not keybinder_dep.found()
  if get_option('panel')
    error('Panel dependencies not found. Install libgtk-3-dev, libxfce4panel-2.0-dev, libsqlite3-dev, and libkeybinder-3.0-dev')
  else
    warning('Panel dependencies not found, skipping panel build')
  endif
  subdir_done()
endif

panel_deps = [
  gtk3_dep,
  xfce4panel_dep,
  sqlite3_dep,
  keybinder_dep,
]

# Build the panel plugin as a shared library
panel_plugin = shared_library('novasearch-panel',
  panel_sources,
  dependencies: panel_deps,
  install: true,
  install_dir: get_option('libdir') / 'xfce4' / 'panel' / 'plugins',
  name_prefix: 'lib',
)

# Install the desktop file
install_data('novasearch-panel.desktop',
  install_dir: get_option('datadir') / 'xfce4' / 'panel' / 'plugins',
)

# Tests
if get_option('tests')
  theft_dep = dependency('theft', required: false)
  
  test_sources = files(
    'tests/test_main.c',
  )
  
  if theft_dep.found()
    test_exe = executable('test_runner',
      test_sources,
      dependencies: panel_deps + [theft_dep],
      link_with: panel_plugin,
    )
    
    test('panel tests', test_exe)
  else
    warning('theft library not found, property-based tests will be skipped')
  endif
endif
