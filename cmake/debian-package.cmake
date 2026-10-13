set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME shairport-2)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "https://github.com/gstn-caruso/shairport-2/issues")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AirPlay 2 audio receiver for PulseAudio")
set(CPACK_DEBIAN_PACKAGE_SECTION sound)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "avahi-daemon, systemd, pulseaudio | pipewire-pulse")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_PACKAGE_CHECKSUM SHA256)

install(FILES COPYING DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-2" RENAME copyright)
install(FILES README.md CHANGELOG.md
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-2")
install(FILES companion/nqptp/PROVENANCE.md
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-2/companion/nqptp")
install(FILES LICENSES DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-2")
install(FILES companion/nqptp/LICENSE companion/nqptp/COPYING companion/nqptp/PROVENANCE.md
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-2/nqptp")
configure_file(scripts/shairport-2-nqptp.service.in shairport-2-nqptp.service @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/shairport-2-nqptp.service"
  DESTINATION "lib/systemd/system")
file(READ "${CMAKE_SOURCE_DIR}/scripts/shairport-2.user.service" packaged_user_service)
string(REPLACE "/usr/local/bin/shairport-2" "${CMAKE_INSTALL_FULL_BINDIR}/shairport-2"
  packaged_user_service "${packaged_user_service}")
file(WRITE "${CMAKE_BINARY_DIR}/shairport-2.service" "${packaged_user_service}")
install(FILES "${CMAKE_BINARY_DIR}/shairport-2.service"
  DESTINATION "lib/systemd/user")

include(CPack)
