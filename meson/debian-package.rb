require 'digest'
require 'fileutils'
require 'open3'

build_dir, meson, version, architecture, dpkg_deb, dpkg_shlibdeps = ARGV
build_dir = File.expand_path(build_dir)
work_dir = File.join(build_dir, 'debian-package')
stage = File.join(work_dir, 'debian/shairport-2')
packages = File.join(build_dir, 'packages')
FileUtils.rm_rf(work_dir)
FileUtils.mkdir_p([stage, packages], mode: 0o755)
abort 'Staged installation failed' unless system(meson, 'install', '-C', build_dir, '--no-rebuild', '--destdir', stage)
receiver = File.join(stage, 'usr/bin/shairport-2')
companion = File.join(stage, 'usr/bin/shairport-2-nqptp')
abort 'Debian packaging requires --prefix=/usr --sysconfdir=/etc' unless
  File.file?(receiver) && File.file?(companion) && File.file?(File.join(stage, 'etc/shairport-2.conf.sample'))
control = <<~CONTROL
  Package: shairport-2
  Version: #{version}
  Architecture: #{architecture}
  Section: sound
  Priority: optional
  Maintainer: https://github.com/gstn-caruso/shairport-2/issues
  Description: AirPlay 2 audio receiver for PulseAudio
CONTROL
FileUtils.mkdir_p(File.join(work_dir, 'debian'))
File.write(File.join(work_dir, 'debian/control'), "Source: shairport-2\n\n#{control}")
output, error, status = Open3.capture3(dpkg_shlibdeps, '-O', "-e#{receiver}", "-e#{companion}", chdir: work_dir)
abort error unless status.success?
warn error unless error.empty?
dependencies = output.lines.find { |line| line.start_with?('shlibs:Depends=') }&.delete_prefix('shlibs:Depends=')&.strip
abort 'dpkg-shlibdeps did not report runtime dependencies' if dependencies.nil? || dependencies.empty?
control += "Depends: avahi-daemon, systemd, pulseaudio | pipewire-pulse, #{dependencies}\n"
FileUtils.mkdir_p(File.join(stage, 'DEBIAN'), mode: 0o755)
File.write(File.join(stage, 'DEBIAN/control'), control, mode: 'w', perm: 0o644)
package_name = "shairport-2_#{version}_#{architecture}.deb"
package = File.join(packages, package_name)
abort 'dpkg-deb failed' unless system(dpkg_deb, '--root-owner-group', '--build', stage, package)
File.write(package + '.sha256', "#{Digest::SHA256.file(package).hexdigest}  #{package_name}\n")
puts package
