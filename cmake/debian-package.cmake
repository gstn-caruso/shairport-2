set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME shairport-sync)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Shairport Sync maintainers <shairport-sync@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AirPlay 2 audio receiver for PulseAudio")
set(CPACK_DEBIAN_PACKAGE_SECTION sound)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "avahi-daemon, systemd, pulseaudio | pipewire-pulse")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_PACKAGE_CHECKSUM SHA256)

install(FILES COPYING DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-sync" RENAME copyright)
install(FILES README.md BUILD.md CONFIGURATION.md AIRPLAY2.md ADDINGTOHOME.md
  TROUBLESHOOTING.md VALIDATION.md RELEASENOTES.md CHANGELOG.md
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-sync")
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-sync/docs"
  FILES_MATCHING PATTERN "*.md")
install(FILES pair_ap/README.md DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-sync/pair_ap")
install(FILES companion/nqptp/PROVENANCE.md companion/nqptp/VERIFICATION.md
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-sync/companion/nqptp")
install(FILES LICENSES DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-sync")
install(FILES companion/nqptp/LICENSE companion/nqptp/COPYING companion/nqptp/PROVENANCE.md
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-sync/nqptp")
configure_file(scripts/shairport-sync-nqptp.service.in shairport-sync-nqptp.service @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/shairport-sync-nqptp.service"
  DESTINATION "lib/systemd/system")
file(READ "${CMAKE_SOURCE_DIR}/scripts/shairport-sync.user.service" packaged_user_service)
string(REPLACE "/usr/local/bin/shairport-sync" "${CMAKE_INSTALL_FULL_BINDIR}/shairport-sync"
  packaged_user_service "${packaged_user_service}")
file(WRITE "${CMAKE_BINARY_DIR}/shairport-sync.service" "${packaged_user_service}")
install(FILES "${CMAKE_BINARY_DIR}/shairport-sync.service"
  DESTINATION "lib/systemd/user")

include(CPack)
