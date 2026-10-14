require 'find'
require 'open3'
require 'tmpdir'

root, prefix, version, package = ARGV
abort 'ROOT, PREFIX and VERSION are required' unless root && prefix && version
root = File.expand_path(root)
installed = File.join(root, prefix)
paths = %w[bin/shairport-2 bin/shairport-2-nqptp share/man/man1/shairport-2.1
  share/doc/shairport-2/README.md lib/systemd/user/shairport-2.service
  lib/systemd/system/shairport-2-nqptp.service share/doc/shairport-2/nqptp/PROVENANCE.md
  share/doc/shairport-2/nqptp/LICENSE share/doc/shairport-2/nqptp/COPYING]
paths.each { |path| abort "Missing installed artifact: #{path}" unless File.file?(File.join(installed, path)) }
abort 'Missing renamed sample configuration' unless File.file?(File.join(root, 'etc/shairport-2.conf.sample'))
Find.find(root) do |path|
  abort "Legacy program identity installed: #{path}" if File.basename(path).start_with?('shairport-sync')
  abort "Installation activates a service: #{path}" if path.match?(/\.(wants|requires)\//)
end
def output(*command)
  stdout, stderr, status = Open3.capture3(*command)
  abort "Command failed: #{command.first}: #{stderr}" unless status.success?
  stdout
end
receiver = output(File.join(installed, 'bin/shairport-2'), '--version')
abort "Unexpected receiver identity: #{receiver}" unless receiver.include?("#{version}-AirPlay2-")
companion = output(File.join(installed, 'bin/shairport-2-nqptp'), '--version')
abort "Unexpected companion identity: #{companion}" unless companion.include?("#{version}; NQPTP 1.2.8;") && companion.include?('smi10')
system_unit = File.read(File.join(installed, 'lib/systemd/system/shairport-2-nqptp.service'))
user_unit = File.read(File.join(installed, 'lib/systemd/user/shairport-2.service'))
["ExecStart=#{prefix}/bin/shairport-2-nqptp", 'DynamicUser=yes', 'AmbientCapabilities=CAP_NET_BIND_SERVICE', 'LimitRTPRIO=6'].each do |required|
  abort "Companion service is missing #{required}" unless system_unit.include?(required)
end
abort 'Receiver service does not launch the renamed binary' unless user_unit.include?("ExecStart=#{prefix}/bin/shairport-2\n")
abort 'Cross-manager systemd dependency is forbidden' if
  system_unit.match?(/(?:Before|After|Requires|Wants)=.*shairport-2.service/) || user_unit.match?(/(?:Before|After|Requires|Wants)=.*nqptp/)
Dir.mktmpdir('shairport-unit-check') do |dir|
  unit = File.join(dir, 'shairport-2-nqptp.service')
  File.write(unit, system_unit.sub("ExecStart=#{prefix}/", "ExecStart=#{installed}/"))
  output('systemd-analyze', 'verify', '--man=no', unit)
end
if package
  abort 'Unexpected Debian package identity' unless output('dpkg-deb', '-f', package, 'Package').strip == 'shairport-2'
  abort 'Package version does not match binaries' unless output('dpkg-deb', '-f', package, 'Version').strip == version
  architecture = output('dpkg', '--print-architecture').strip
  abort 'Unexpected package architecture' unless output('dpkg-deb', '-f', package, 'Architecture').strip == architecture
  dependencies = output('dpkg-deb', '-f', package, 'Depends')
  ['avahi-daemon', 'systemd', 'pulseaudio | pipewire-pulse', 'libstdc++6'].each do |required|
    abort "Missing package dependency: #{required}: #{dependencies}" unless dependencies.include?(required)
  end
  Dir.mktmpdir('shairport-package-control') do |dir|
    output('dpkg-deb', '--control', package, dir)
    %w[preinst postinst prerm postrm].each do |script|
      abort 'Unexpected maintainer script; services require explicit activation' if File.exist?(File.join(dir, script))
    end
  end
end
puts 'Installation and package identity contracts passed.'
