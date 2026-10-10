project(
    'vertical-cube',
    'c',
    'cpp',
    version: '0.2.0',
    license: 'MIT',
    meson_version: '>=0.51.0',
    default_options: [
        'buildtype=release',
        'cpp_std=c++17',
        'c_std=c11',
    ],
)

add_project_arguments(['-DWLR_USE_UNSTABLE'], language: ['cpp', 'c'])
add_project_arguments(['-DWAYFIRE_PLUGIN'], language: ['cpp', 'c'])
add_project_link_arguments(['-rdynamic','-fPIC'], language:'cpp')

wayfire = dependency('wayfire', version: '>=0.11.0')

if meson.get_compiler('cpp').has_header_symbol('wayfire/opengl.hpp',
    'wf::gles::for_each_scissor_rect', dependencies: wayfire)
    add_project_arguments('-DVERTICAL_KUBE_SCISSOR_REGIONS', language: 'cpp')
endif

# Match upstream cube's optional tessellation path, retaining GLES 2 fallback.
gles32 = get_option('enable_gles32')
if not gles32.disabled()
    if meson.get_compiler('cpp').has_header('GLES3/gl32.h', dependencies: wayfire)
        add_project_arguments('-DVERTICAL_KUBE_USE_GLES32', language: 'cpp')
    elif gles32.enabled()
        error('GLES 3.2 support requires GLES3/gl32.h')
    endif
endif

blur_base = meson.get_compiler('cpp').find_library('wayfire-blur-base',
    dirs: wayfire.get_variable(pkgconfig: 'libdir'), required: true)

subdir('src')
subdir('metadata')
