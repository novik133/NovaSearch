project('novasearch', 'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'warning_level=2',
    'c_std=gnu11',
  ]
)

# Build the Rust daemon using Cargo
if get_option('daemon')
  cargo = find_program('cargo', required: true)
  
  daemon_target = custom_target('novasearch-daemon',
    output: 'novasearch-daemon',
    console: true,
    install: true,
    install_dir: get_option('bindir'),
    build_by_default: true,
    command: [
      cargo, 'build',
      '--manifest-path', meson.project_source_root() / 'daemon' / 'Cargo.toml',
      '--release',
      '&&',
      'cp',
      meson.project_source_root() / 'daemon' / 'target' / 'release' / 'novasearch-daemon',
      '@OUTPUT@'
    ]
  )
endif

# Build the panel plugin
if get_option('panel')
  subdir('panel')
endif

# Summary
summary({
  'prefix': get_option('prefix'),
  'bindir': get_option('bindir'),
  'libdir': get_option('libdir'),
}, section: 'Directories')

summary({
  'Rust daemon': 'Cargo',
  'Panel plugin': 'Meson',
}, section: 'Build system')
