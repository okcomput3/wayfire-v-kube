filters = shared_module('vertical_kube', 
    [
        'cube.cpp',
        'popout-compositor.cpp',
        'simple-background.cpp',
        'shader-background.cpp',
        'image-background.cpp',
        'skydome.cpp',
        'cubemap.cpp'
    ],
    dependencies: [wayfire, blur_base, dependency('threads')],
    install: true,
    install_dir: wayfire.get_variable(pkgconfig: 'plugindir'))
