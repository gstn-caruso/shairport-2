plist_sources = custom_target(
    'get-info-response',
    input: 'get_info_response.xml',
    output: ['get_info_response.cpp', 'get_info_response.h'],
    command: [
        find_program('sh'),
        files('../xml_plist_codegen.sh'),
        '@INPUT@',
        meson.current_build_dir(),
    ],
)
