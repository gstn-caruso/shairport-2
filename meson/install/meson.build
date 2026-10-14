install_man('../../man/shairport-2.1')
if get_option('install_config_files')
    install_data(
        '../../scripts/shairport-2.conf',
        install_dir: get_option('sysconfdir'),
        rename: 'shairport-2.conf.sample',
    )
endif
docdir = get_option('datadir') / 'doc/shairport-2'
install_data('../../COPYING', install_dir: docdir, rename: 'copyright')
install_data(
    '../../README.md',
    '../../CHANGELOG.md',
    '../../LICENSES',
    install_dir: docdir,
)
install_data(
    '../../companion/nqptp/PROVENANCE.md',
    install_dir: docdir / 'companion/nqptp',
)
install_data(
    '../../companion/nqptp/LICENSE',
    '../../companion/nqptp/COPYING',
    '../../companion/nqptp/PROVENANCE.md',
    install_dir: docdir / 'nqptp',
)
unit_config = configuration_data()
unit_config.set('bindir', bindir)
companion_unit = configure_file(
    input: '../../scripts/shairport-2-nqptp.service.in',
    output: 'shairport-2-nqptp.service',
    configuration: unit_config,
)
install_data(companion_unit, install_dir: 'lib/systemd/system')
receiver_unit = configure_file(
    input: '../../scripts/shairport-2.user.service',
    output: 'shairport-2.service',
    command: [
        find_program('sed'),
        's|/usr/local/bin/shairport-2|' + bindir / 'shairport-2' + '|g',
        '@INPUT@',
    ],
    capture: true,
)
install_data(receiver_unit, install_dir: 'lib/systemd/user')

ruby = find_program('ruby', required: false)
dpkg_deb = find_program('dpkg-deb', required: false)
dpkg_shlibdeps = find_program('dpkg-shlibdeps', required: false)
dpkg = find_program('dpkg', required: false)
if ruby.found() and dpkg_deb.found() and dpkg_shlibdeps.found() and dpkg.found()
    architecture = run_command(dpkg, '--print-architecture', check: true).stdout().strip()
    run_target(
        'deb',
        command: [
            ruby,
            files('../debian-package.rb'),
            meson.project_build_root(),
            find_program('meson'),
            meson.project_version(),
            architecture,
            dpkg_deb,
            dpkg_shlibdeps,
        ],
        depends: [receiver_binary, companion_binary],
    )
endif
