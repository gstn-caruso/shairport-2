if(NOT DEFINED ROOT OR NOT DEFINED PREFIX OR NOT DEFINED VERSION)
  message(FATAL_ERROR "ROOT, PREFIX and VERSION are required")
endif()
foreach(path
    "${PREFIX}/bin/shairport-2" "${PREFIX}/bin/shairport-2-nqptp"
    "${PREFIX}/lib/systemd/user/shairport-2.service"
    "${PREFIX}/lib/systemd/system/shairport-2-nqptp.service"
    "${PREFIX}/share/doc/shairport-2/nqptp/PROVENANCE.md"
    "${PREFIX}/share/doc/shairport-2/nqptp/LICENSE"
    "${PREFIX}/share/doc/shairport-2/nqptp/COPYING")
  if(NOT EXISTS "${ROOT}${path}")
    message(FATAL_ERROR "Missing installed companion contract file: ${path}")
  endif()
endforeach()
execute_process(COMMAND "${ROOT}${PREFIX}/bin/shairport-2-nqptp" --version
  RESULT_VARIABLE status OUTPUT_VARIABLE identity ERROR_VARIABLE error)
if(NOT status EQUAL 0 OR NOT identity MATCHES "${VERSION}; NQPTP 1.2.8;.*smi10")
  message(FATAL_ERROR "Unexpected companion identity: ${identity} ${error}")
endif()
execute_process(COMMAND "${ROOT}${PREFIX}/bin/shairport-2" --version
  RESULT_VARIABLE status OUTPUT_VARIABLE identity ERROR_VARIABLE error)
if(NOT status EQUAL 0 OR NOT identity MATCHES "${VERSION}-AirPlay2-")
  message(FATAL_ERROR "Unexpected receiver identity: ${identity} ${error}")
endif()
file(READ "${ROOT}${PREFIX}/lib/systemd/system/shairport-2-nqptp.service" system_unit)
foreach(required "ExecStart=${PREFIX}/bin/shairport-2-nqptp" "DynamicUser=yes"
    "AmbientCapabilities=CAP_NET_BIND_SERVICE" "LimitRTPRIO=6")
  string(FIND "${system_unit}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Companion service is missing ${required}")
  endif()
endforeach()
file(READ "${ROOT}${PREFIX}/lib/systemd/user/shairport-2.service" user_unit)
if(system_unit MATCHES "(Before|After|Requires|Wants)=.*shairport-2.service" OR
    user_unit MATCHES "(Before|After|Requires|Wants)=.*nqptp")
  message(FATAL_ERROR "Cross-manager systemd dependency is forbidden")
endif()
file(GLOB_RECURSE installed_entries LIST_DIRECTORIES true "${ROOT}/*")
foreach(entry IN LISTS installed_entries)
  if(entry MATCHES "\\.(wants|requires)/")
    message(FATAL_ERROR "Installation activates a service: ${entry}")
  endif()
endforeach()
if(DEFINED PACKAGE)
  execute_process(COMMAND dpkg-deb -f "${PACKAGE}" Version
    RESULT_VARIABLE status OUTPUT_VARIABLE package_version OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT status EQUAL 0 OR NOT package_version STREQUAL VERSION)
    message(FATAL_ERROR "Package version does not match binaries")
  endif()
  execute_process(COMMAND dpkg-deb -f "${PACKAGE}" Depends
    RESULT_VARIABLE status OUTPUT_VARIABLE dependencies)
  foreach(required "avahi-daemon" "systemd" "pulseaudio | pipewire-pulse" "libstdc++6")
    string(FIND "${dependencies}" "${required}" position)
    if(NOT status EQUAL 0 OR position EQUAL -1)
      message(FATAL_ERROR "Missing package dependency: ${required}: ${dependencies}")
    endif()
  endforeach()
  execute_process(COMMAND dpkg-deb --control "${PACKAGE}" "${ROOT}/package-control"
    RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "Cannot inspect Debian control metadata")
  endif()
  foreach(script preinst postinst prerm postrm)
    if(EXISTS "${ROOT}/package-control/${script}")
      message(FATAL_ERROR "Unexpected maintainer script; services must require explicit activation")
    endif()
  endforeach()
endif()
